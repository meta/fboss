/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/interface/sflow/CmdDeleteInterfaceSflow.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

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
constexpr std::string_view kAttrSampleDest = "sample-dest";
constexpr std::string_view kAttrIngressRate = "ingress-rate";
constexpr std::string_view kAttrEgressRate = "egress-rate";
constexpr auto kValidSflowAttrs = "sample-dest, ingress-rate, egress-rate";
} // namespace

SflowDeleteAttrArgs::SflowDeleteAttrArgs(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No sflow attribute provided. Valid attributes are: {}",
            kValidSflowAttrs));
  }
  for (const auto& raw : v) {
    std::string attr = raw;
    std::transform(attr.begin(), attr.end(), attr.begin(), [](unsigned char c) {
      return std::tolower(c);
    });
    if (attr != kAttrSampleDest && attr != kAttrIngressRate &&
        attr != kAttrEgressRate) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown sflow attribute '{}'. Valid attributes are: {}",
              attr,
              kValidSflowAttrs));
    }
    attributes_.push_back(std::move(attr));
  }
  data_ = std::move(v);
}

CmdDeleteInterfaceSflowTraits::RetType CmdDeleteInterfaceSflow::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::InterfaceList& interfaces,
    const ObjectArgType& sflowAttrs) {
  if (interfaces.empty()) {
    throw std::invalid_argument("No interface name provided");
  }

  auto& session = ConfigSession::getInstance();

  std::vector<std::string> updatedNames;
  std::vector<std::string> skippedNames;
  for (const utils::Intf& intf : interfaces) {
    cfg::Port* port = intf.getPort();
    if (!port) {
      // Resolved as an L3 interface only (e.g. an SVI): these sflow
      // attributes are all Port attributes, so there is nothing to clear --
      // report it rather than silently succeeding.
      skippedNames.push_back(intf.name());
      continue;
    }
    for (const auto& attr : sflowAttrs.getAttributes()) {
      if (attr == kAttrSampleDest) {
        port->sampleDest().reset();
      } else if (attr == kAttrIngressRate) {
        port->sFlowIngressRate() = 0;
      } else {
        port->sFlowEgressRate() = 0;
      }
    }
    updatedNames.push_back(intf.name());
  }
  if (updatedNames.empty()) {
    throw std::invalid_argument("No port found for the specified interface(s)");
  }

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  std::string message = fmt::format(
      "Reset sFlow {} for interface(s) {}",
      folly::join(", ", sflowAttrs.getAttributes()),
      folly::join(", ", updatedNames));
  if (!skippedNames.empty()) {
    message +=
        fmt::format("; skipped (no port): {}", folly::join(", ", skippedNames));
  }
  return message;
}

void CmdDeleteInterfaceSflow::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteInterfaceSflow, CmdDeleteInterfaceSflowTraits>::run();

std::vector<std::string> CmdDeleteInterfaceSflowTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completeAttrList(
      typed,
      {std::string(kAttrSampleDest),
       std::string(kAttrIngressRate),
       std::string(kAttrEgressRate)});
}

} // namespace facebook::fboss
