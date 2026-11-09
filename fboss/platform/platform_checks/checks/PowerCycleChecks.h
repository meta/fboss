/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "fboss/platform/platform_checks/CommandLog.h"
#include "fboss/platform/platform_checks/PlatformCheck.h"

// Checks that reboot or power cycle parts of the switch. They are disruptive:
// fixmyfboss only runs them when explicitly asked to.
namespace facebook::fboss::platform::platform_checks {

struct RebootTiming {
  // Time for a reset to take effect before polling for the host.
  std::chrono::seconds settle{10};
  std::chrono::seconds pollInterval{5};
  std::chrono::seconds timeout{300};
};

// Waits `timing.settle`, then polls until `host` answers or `timing.timeout`.
bool waitForHost(const Host& host, const RebootTiming& timing, CommandLog& log);

/**
 * Validates that the BMC boots from both its master and slave flash, ending on
 * master.
 */
class SecondaryBmcBootCheck : public PlatformCheck {
 public:
  explicit SecondaryBmcBootCheck(CheckTarget bmc, RebootTiming timing = {})
      : PlatformCheck(std::move(bmc)), timing_(timing) {}

  CheckResult run() override;

  bool isDisruptive() const override {
    return true;
  }

  CheckType getType() const override {
    return CheckType::BMC_SECONDARY_FLASH_BOOT_CHECK;
  }

  std::string getName() const override {
    return "BMC Secondary Flash Boot";
  }

  std::string getDescription() const override {
    return "Reboots the BMC from its slave flash and back to master (disruptive)";
  }

 private:
  std::optional<CheckResult> expectBootSource(
      CommandLog& log,
      const std::string& flash);
  std::optional<CheckResult> rebootFrom(
      CommandLog& log,
      const std::string& flash);

  RebootTiming timing_;
};

/**
 * Validates that wedge_power.sh on the BMC can power the x86 off and on, and
 * that a full reset brings both BMC and x86 back.
 */
class WedgePowerCheck : public PlatformCheck {
 public:
  // `self` is the machine running fixmyfboss, which this check must not power
  // cycle.
  WedgePowerCheck(
      CheckTarget x86,
      CheckTarget bmc,
      std::shared_ptr<const Host> self,
      RebootTiming timing = {})
      : PlatformCheck(std::move(x86)),
        bmc_(std::move(bmc)),
        self_(std::move(self)),
        timing_(timing) {}

  CheckResult run() override;

  std::optional<std::string> getSkipReason() const override;

  bool isDisruptive() const override {
    return true;
  }

  CheckType getType() const override {
    return CheckType::WEDGE_POWER_CHECK;
  }

  std::string getName() const override {
    return "wedge_power";
  }

  std::string getDescription() const override {
    return "Power cycles the x86 and resets the whole switch with wedge_power.sh (disruptive)";
  }

 private:
  std::optional<CheckResult> setPower(
      CommandLog& log,
      const std::string& state);
  // Expects `host` to come back with less uptime than the time since
  // `poweredAt`, i.e. to have booted after it.
  std::optional<CheckResult> expectFreshBoot(
      CommandLog& log,
      const Host& host,
      std::chrono::steady_clock::time_point poweredAt);

  CheckTarget bmc_;
  std::shared_ptr<const Host> self_;
  RebootTiming timing_;
};

} // namespace facebook::fboss::platform::platform_checks
