// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/interface/mirror/CmdConfigInterfaceMirror.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <set>
#include <unordered_set>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/interface/MirrorSamplingUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kAttrIngress = "ingress";
constexpr std::string_view kAttrEgress = "egress";
constexpr auto kValidMirrorAttrs = "ingress, egress";

std::string toLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return std::tolower(c);
  });
  return s;
}
} // namespace

MirrorAttrArgs::MirrorAttrArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No mirror direction provided. Valid directions are: {}",
            kValidMirrorAttrs));
  }
  if (v.size() % 2 != 0) {
    throw std::invalid_argument(
        "Expected <direction> <mirror-name> pairs; got an odd number of tokens");
  }
  for (size_t i = 0; i < v.size(); i += 2) {
    std::string attr = toLower(v[i]);
    if (attr != kAttrIngress && attr != kAttrEgress) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown mirror direction '{}'. Valid directions are: {}",
              attr,
              kValidMirrorAttrs));
    }
    if (v[i + 1].empty()) {
      throw std::invalid_argument(
          fmt::format("Mirror name for {} must not be empty", attr));
    }
    attributes_.emplace_back(std::move(attr), v[i + 1]);
  }
  data_ = std::move(v);
}

CmdConfigInterfaceMirrorTraits::RetType CmdConfigInterfaceMirror::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& mirrorAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  // Last occurrence of a repeated direction wins.
  std::optional<std::string> newIngress;
  std::optional<std::string> newEgress;
  for (const auto& [attr, name] : mirrorAttrs.getAttributes()) {
    if (attr == kAttrIngress) {
      newIngress = name;
    } else {
      newEgress = name;
    }
  }

  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  // The agent rejects a port bound to a mirror that sw.mirrors doesn't define.
  std::set<std::string> configuredMirrors;
  for (const auto& mirror : *swConfig.mirrors()) {
    configuredMirrors.insert(*mirror.name());
  }
  for (const auto& name : {newIngress, newEgress}) {
    if (name.has_value() && !configuredMirrors.contains(*name)) {
      throw std::invalid_argument(
          fmt::format("Mirror '{}' is not configured", *name));
    }
  }

  std::vector<cfg::Port*> ports;
  std::vector<std::string> updatedNames;
  std::vector<std::string> skippedNames;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (!port) {
      // Resolved as an L3 interface only (e.g. an SVI): mirror bindings are
      // Port attributes, so report it rather than silently succeeding.
      skippedNames.push_back(intf.name());
      continue;
    }
    ports.push_back(port);
    updatedNames.push_back(intf.name());
  }
  if (ports.empty()) {
    throw std::invalid_argument("No port found for the specified interface(s)");
  }

  // A mirror's egress port is where the mirrored copies are sent. The agent
  // refuses a mirror whose egress port is also bound to that same mirror, as
  // ingress or egress (ThriftConfigApplier::createMirror throws "MirrorConfig
  // <name> doesn't match ingress ... or egress mirror ..."), so that config
  // would only fail later, at commit time.
  //
  // There are two ways to get there:
  //   1. bind the port first, then `config mirror <name> egress-port <port>`;
  //      CmdConfigMirror already rejects that.
  //   2. set the egress port first, then `config interface <port> mirror`;
  //      this loop rejects that, before anything is staged.
  //
  // Only the mirrors named in this call matter. The egress port can be given
  // by port name or by logicalID, so match either form. If any port in the
  // call is the egress port, the whole call fails and no port is changed.
  for (const auto& mirror : *swConfig.mirrors()) {
    const auto& egressPort = mirror.destination()->egressPort();
    if (!egressPort.has_value() ||
        (*mirror.name() != newIngress && *mirror.name() != newEgress)) {
      continue;
    }
    for (size_t i = 0; i < ports.size(); ++i) {
      bool isDestination = false;
      switch (egressPort->getType()) {
        case cfg::MirrorEgressPort::Type::name:
          isDestination = ports[i]->name().has_value() &&
              *ports[i]->name() == egressPort->get_name();
          break;
        case cfg::MirrorEgressPort::Type::logicalID:
          isDestination = *ports[i]->logicalID() == egressPort->get_logicalID();
          break;
        case cfg::MirrorEgressPort::Type::__EMPTY__:
          break;
      }
      if (isDestination) {
        throw std::invalid_argument(
            fmt::format(
                "Interface {} is the egress port of mirror '{}'; it cannot be "
                "bound to that mirror",
                updatedNames[i],
                *mirror.name()));
      }
    }
  }

  // A new ingress binding must not put sampling ports (sample-dest mirror) on
  // a second ingress mirror; see checkSingleSamplingMirror(). The check uses
  // the bindings this call would leave, not the current ones, so moving every
  // sampling port to another mirror in a single call is allowed. Egress
  // bindings do not affect sampling, so they are not checked.
  if (newIngress.has_value()) {
    checkSingleSamplingMirror(
        swConfig,
        std::unordered_set<const cfg::Port*>(ports.begin(), ports.end()),
        /*targetsSample=*/false,
        newIngress);
  }

  for (cfg::Port* port : ports) {
    if (newIngress.has_value()) {
      port->ingressMirror() = *newIngress;
    }
    if (newEgress.has_value()) {
      port->egressMirror() = *newEgress;
    }
  }

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  std::vector<std::string> setParts;
  if (newIngress.has_value()) {
    setParts.push_back(fmt::format("ingress={}", *newIngress));
  }
  if (newEgress.has_value()) {
    setParts.push_back(fmt::format("egress={}", *newEgress));
  }
  std::string message = fmt::format(
      "Successfully set mirror {} for interface(s) {}",
      folly::join(", ", setParts),
      folly::join(", ", updatedNames));
  if (!skippedNames.empty()) {
    message +=
        fmt::format("; skipped (no port): {}", folly::join(", ", skippedNames));
  }
  return message;
}

void CmdConfigInterfaceMirror::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigInterfaceMirror, CmdConfigInterfaceMirrorTraits>::run();

} // namespace facebook::fboss
