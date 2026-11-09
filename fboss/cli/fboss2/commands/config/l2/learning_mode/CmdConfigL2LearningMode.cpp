/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/l2/learning_mode/CmdConfigL2LearningMode.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/lang/Assume.h>
#include <iostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

L2LearningModeArg::L2LearningModeArg(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "L2 learning mode is required (hardware, software, or disabled)");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one L2 learning mode (hardware, software, or disabled)");
  }

  std::string mode = v[0];
  folly::toLowerAscii(mode);
  if (mode == "hardware") {
    l2LearningMode_ = cfg::L2LearningMode::HARDWARE;
  } else if (mode == "software") {
    l2LearningMode_ = cfg::L2LearningMode::SOFTWARE;
  } else if (mode == "disabled") {
    l2LearningMode_ = cfg::L2LearningMode::DISABLED;
  } else {
    throw std::invalid_argument(
        "Invalid L2 learning mode '" + v[0] +
        "'. Expected 'hardware', 'software', or 'disabled'");
  }
  data_.push_back(v[0]);
}

namespace {
std::string l2LearningModeToString(cfg::L2LearningMode mode) {
  switch (mode) {
    case cfg::L2LearningMode::HARDWARE:
      return "hardware";
    case cfg::L2LearningMode::SOFTWARE:
      return "software";
    case cfg::L2LearningMode::DISABLED:
      return "disabled";
  }
  folly::assume_unreachable();
}
} // namespace

CmdConfigL2LearningModeTraits::RetType CmdConfigL2LearningMode::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& learningMode) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();

  cfg::L2LearningMode newMode = learningMode.getL2LearningMode();

  // Get current mode for informational purposes
  cfg::L2LearningMode currentMode =
      *swConfig.switchSettings()->l2LearningMode();

  if (currentMode == newMode) {
    return fmt::format(
        "L2 learning mode is already set to '{}'",
        l2LearningModeToString(newMode));
  }

  // Update the l2LearningMode in switchSettings
  swConfig.switchSettings()->l2LearningMode() = newMode;

  // Save the updated config - L2 learning mode changes require agent cold boot
  // to clear ASIC state
  session.saveConfig(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  return fmt::format(
      "Successfully set L2 learning mode to '{}'",
      l2LearningModeToString(newMode));
}

void CmdConfigL2LearningMode::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigL2LearningMode, CmdConfigL2LearningModeTraits>::run();

} // namespace facebook::fboss
