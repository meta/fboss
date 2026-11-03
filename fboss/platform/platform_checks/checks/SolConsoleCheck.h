/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <chrono>
#include <string>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform {
class ExpectSession;
} // namespace facebook::fboss::platform

namespace facebook::fboss::platform::platform_checks {

/**
 * Validates that the x86 serial console is reachable from the BMC: sol.sh
 * must show the x86 login prompt.
 */
class SolConsoleCheck : public PlatformCheck {
 public:
  explicit SolConsoleCheck(
      CheckTarget bmc,
      std::chrono::seconds promptTimeout = std::chrono::seconds(10))
      : PlatformCheck(std::move(bmc)), promptTimeout_(promptTimeout) {}

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::BMC_SOL_CONSOLE_CHECK;
  }

  std::string getName() const override {
    return "SOL Console";
  }

  std::string getDescription() const override {
    return "Validates that sol.sh on the BMC shows the x86 login prompt";
  }

 private:
  // The console interaction; may throw if the session breaks. Appends what
  // the terminal showed to `transcript`.
  CheckResult runConsole(ExpectSession& terminal, std::string& transcript);

  std::chrono::seconds promptTimeout_;
};

} // namespace facebook::fboss::platform::platform_checks
