/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/ptp/transparent_clock/CmdConfigPtpTransparentClock.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <iostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {
constexpr std::string_view kEnable = "enable";
constexpr std::string_view kDisable = "disable";
} // namespace

PtpTransparentClockArg::PtpTransparentClockArg(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "PTP transparent clock state is required (enable or disable)");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one argument (enable or disable)");
  }

  std::string state = v[0];
  folly::toLowerAscii(state);
  if (state == kEnable) {
    enable_ = true;
  } else if (state == kDisable) {
    enable_ = false;
  } else {
    throw std::invalid_argument(
        "Invalid PTP transparent clock state '" + v[0] +
        "'. Expected 'enable' or 'disable'");
  }
  data_.push_back(v[0]);
}

CmdConfigPtpTransparentClockTraits::RetType
CmdConfigPtpTransparentClock::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& state) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();

  bool newValue = state.getEnable();
  bool currentValue = *swConfig.switchSettings()->ptpTcEnable();

  if (currentValue == newValue) {
    return fmt::format(
        "PTP transparent clock is already {}",
        newValue ? "enabled" : "disabled");
  }

  swConfig.switchSettings()->ptpTcEnable() = newValue;
  try {
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  } catch (...) {
    swConfig.switchSettings()->ptpTcEnable() = currentValue;
    throw;
  }

  return fmt::format(
      "Successfully {} PTP transparent clock",
      newValue ? "enabled" : "disabled");
}

void CmdConfigPtpTransparentClock::printOutput(const RetType& output) {
  std::cout << output << "\n";
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigPtpTransparentClock,
    CmdConfigPtpTransparentClockTraits>::run();

std::vector<std::string> CmdConfigPtpTransparentClockTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(
      typed, {{std::string(kEnable), std::string(kDisable)}});
}

} // namespace facebook::fboss
