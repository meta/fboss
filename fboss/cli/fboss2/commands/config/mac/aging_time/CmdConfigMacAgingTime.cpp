/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/mac/aging_time/CmdConfigMacAgingTime.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

MacAgingTimeArg::MacAgingTimeArg(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "MAC aging time in seconds is required (e.g. 300)");
  }
  if (v.size() != 1) {
    throw std::invalid_argument("Expected exactly one argument: seconds");
  }

  try {
    seconds_ = folly::to<int32_t>(v[0]);
  } catch (const folly::ConversionError&) {
    throw std::invalid_argument(
        "Invalid MAC aging time '" + v[0] + "': must be an integer");
  }

  if (seconds_ <= 0) {
    throw std::invalid_argument(
        "MAC aging time must be a positive integer, got " + v[0]);
  }

  data_.push_back(v[0]);
}

CmdConfigMacAgingTimeTraits::RetType CmdConfigMacAgingTime::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& agingTime) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();

  int32_t newSeconds = agingTime.getSeconds();
  int32_t currentSeconds = *swConfig.switchSettings()->l2AgeTimerSeconds();

  if (currentSeconds == newSeconds) {
    return fmt::format(
        "MAC aging time is already set to {} seconds", newSeconds);
  }

  swConfig.switchSettings()->l2AgeTimerSeconds() = newSeconds;

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully set MAC aging time to {} seconds", newSeconds);
}

void CmdConfigMacAgingTime::printOutput(const RetType& output) {
  std::cout << output << "\n";
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigMacAgingTime, CmdConfigMacAgingTimeTraits>::run();

} // namespace facebook::fboss
