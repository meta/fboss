/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/arp/CmdDeleteArp.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

using namespace arp_attrs;

ArpDeleteAttrs::ArpDeleteAttrs(const std::vector<std::string>& v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "No ARP attribute provided. Valid attrs: {}",
            folly::join(", ", kValidAttrs)));
  }

  std::unordered_set<std::string> seen;
  for (const auto& attr : v) {
    if (std::find(kValidAttrs.begin(), kValidAttrs.end(), attr) ==
        kValidAttrs.end()) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown ARP attribute '{}'. Valid attrs: {}",
              attr,
              folly::join(", ", kValidAttrs)));
    }
    if (!seen.insert(attr).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate ARP attribute '{}'", attr));
    }
    attributes_.push_back(attr);
  }
}

CmdDeleteArpTraits::RetType CmdDeleteArp::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();

  // Default-constructed SwitchConfig carries the switch_config.thrift
  // per-field default values, so resets can never drift from the IDL.
  const cfg::SwitchConfig kDefaults;

  std::vector<std::string> resets;
  for (const auto& attr : args.getAttributes()) {
    int32_t defaultValue = 0;
    if (attr == kTimeout) {
      defaultValue = *kDefaults.arpTimeoutSeconds();
      swConfig.arpTimeoutSeconds() = defaultValue;
    } else if (attr == kAgeInterval) {
      defaultValue = *kDefaults.arpAgerInterval();
      swConfig.arpAgerInterval() = defaultValue;
    } else if (attr == kMaxProbes) {
      defaultValue = *kDefaults.maxNeighborProbes();
      swConfig.maxNeighborProbes() = defaultValue;
    } else if (attr == kStaleInterval) {
      defaultValue = *kDefaults.staleEntryInterval();
      swConfig.staleEntryInterval() = defaultValue;
    } else {
      // ArpDeleteAttrs validates this; defensive guard in case kValidAttrs
      // drifts from the dispatch here.
      throw std::runtime_error(
          fmt::format("Unhandled ARP attribute '{}'", attr));
    }
    resets.push_back(fmt::format("{} to {}", attr, defaultValue));
  }
  std::string result = fmt::format("Reset arp {}", folly::join(", ", resets));

  // The ARP/NDP timer attributes are applied hitlessly by
  // ThriftConfigApplier::updateSwitchSettings (no SAI ChangeProhibited guard).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return result;
}

void CmdDeleteArp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdDeleteArp, CmdDeleteArpTraits>::run();

} // namespace facebook::fboss
