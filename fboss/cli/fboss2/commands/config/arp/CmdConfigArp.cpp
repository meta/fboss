/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/arp/CmdConfigArp.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

using namespace arp_attrs;

ArpConfigArgs::ArpConfigArgs(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        fmt::format(
            "Expected <attr> <value>, got {} argument(s). Valid attrs: {}",
            v.size(),
            folly::join(", ", kValidAttrs)));
  }

  if (std::find(kValidAttrs.begin(), kValidAttrs.end(), v[0]) ==
      kValidAttrs.end()) {
    throw std::invalid_argument(
        fmt::format(
            "Unknown ARP attribute '{}'. Valid attrs: {}",
            v[0],
            folly::join(", ", kValidAttrs)));
  }

  int32_t parsed = 0;
  try {
    parsed = folly::to<int32_t>(v[1]);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        fmt::format("Value for '{}' must be an integer, got '{}'", v[0], v[1]));
  }

  if (parsed < 0) {
    throw std::invalid_argument(
        fmt::format(
            "Value for '{}' must be non-negative, got {}", v[0], parsed));
  }

  attribute_ = v[0];
  value_ = parsed;
  data_ = std::move(v);
}

CmdConfigArpTraits::RetType CmdConfigArp::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();

  const auto& attr = args.getAttribute();
  int32_t value = args.getValue();

  if (attr == kTimeout) {
    swConfig.arpTimeoutSeconds() = value;
  } else if (attr == kAgeInterval) {
    swConfig.arpAgerInterval() = value;
  } else if (attr == kMaxProbes) {
    swConfig.maxNeighborProbes() = value;
  } else if (attr == kStaleInterval) {
    swConfig.staleEntryInterval() = value;
  } else {
    // ArpConfigArgs validates this; defensive guard in case kValidAttrs
    // drifts from the dispatch switch here.
    throw std::runtime_error(fmt::format("Unhandled ARP attribute '{}'", attr));
  }

  // All ARP/NDP timer attributes are applied hitlessly by
  // ThriftConfigApplier::updateSwitchSettings (no SAI ChangeProhibited guard).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format("Set arp {} to {}", attr, value);
}

void CmdConfigArp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigArp, CmdConfigArpTraits>::run();

} // namespace facebook::fboss
