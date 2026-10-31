// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/interface/mirror/CmdDeleteInterfaceMirror.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kAttrIngress = "ingress";
constexpr std::string_view kAttrEgress = "egress";
constexpr auto kValidMirrorAttrs = "ingress, egress";
} // namespace

MirrorDeleteAttrArgs::MirrorDeleteAttrArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No mirror direction provided. Valid directions are: {}",
            kValidMirrorAttrs));
  }
  for (const auto& raw : v) {
    std::string attr = raw;
    std::transform(attr.begin(), attr.end(), attr.begin(), [](unsigned char c) {
      return std::tolower(c);
    });
    if (attr != kAttrIngress && attr != kAttrEgress) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown mirror direction '{}'. Valid directions are: {}",
              attr,
              kValidMirrorAttrs));
    }
    attributes_.push_back(std::move(attr));
  }
  data_ = std::move(v);
}

CmdDeleteInterfaceMirrorTraits::RetType CmdDeleteInterfaceMirror::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& mirrorAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  auto& session = ConfigSession::getInstance();

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
    for (const auto& attr : mirrorAttrs.getAttributes()) {
      if (attr == kAttrIngress) {
        port->ingressMirror().reset();
      } else {
        port->egressMirror().reset();
      }
    }
    updatedNames.push_back(intf.name());
  }
  if (updatedNames.empty()) {
    throw std::invalid_argument("No port found for the specified interface(s)");
  }

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  std::string message = fmt::format(
      "Removed mirror {} for interface(s) {}",
      folly::join(", ", mirrorAttrs.getAttributes()),
      folly::join(", ", updatedNames));
  if (!skippedNames.empty()) {
    message +=
        fmt::format("; skipped (no port): {}", folly::join(", ", skippedNames));
  }
  return message;
}

void CmdDeleteInterfaceMirror::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteInterfaceMirror, CmdDeleteInterfaceMirrorTraits>::run();

} // namespace facebook::fboss
