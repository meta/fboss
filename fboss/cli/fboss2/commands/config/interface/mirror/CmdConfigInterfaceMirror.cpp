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

  // The agent samples every port with sampleDest MIRROR through a single
  // ingress mirror (ApplyThriftConfig: "Only one mirror can be configured
  // across all ports, to sample traffic"). Check the bindings this call would
  // leave, so rebinding the only sampling port to another mirror still works.
  if (newIngress.has_value()) {
    std::unordered_set<const cfg::Port*> targets(ports.begin(), ports.end());
    std::set<std::string> samplingMirrors;
    for (const auto& port : *swConfig.ports()) {
      if (!port.sampleDest().has_value() ||
          *port.sampleDest() != cfg::SampleDestination::MIRROR) {
        continue;
      }
      if (targets.contains(&port)) {
        samplingMirrors.insert(*newIngress);
      } else if (port.ingressMirror().has_value()) {
        samplingMirrors.insert(*port.ingressMirror());
      }
    }
    if (samplingMirrors.size() > 1) {
      throw std::invalid_argument(
          fmt::format(
              "Ports with sample-dest mirror must all use the same ingress "
              "mirror; this would leave them on: {}",
              folly::join(", ", samplingMirrors)));
    }
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
