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

#include <memory>
#include <optional>
#include <string>

#include <folly/MacAddress.h>

#include "fboss/platform/platform_checks/CommandLog.h"
#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

// Value of the "<label>: <value>" line of `weutil --all` output, if present.
std::optional<std::string> findWeutilField(
    const std::string& weutilOutput,
    const std::string& label);

/**
 * Validates that the BMC's eth0 MAC address matches the BMC MAC in its EEPROM.
 */
class BmcMacAddressCheck : public PlatformCheck {
 public:
  using PlatformCheck::PlatformCheck;

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::BMC_MAC_ADDRESS_CHECK;
  }

  std::string getName() const override {
    return "BMC MAC Address";
  }

  std::string getDescription() const override {
    return "Validates that the BMC eth0 MAC address matches the EEPROM BMC MAC";
  }
};

/**
 * Validates that the BMC can parse every EEPROM it knows about.
 */
class BmcEepromCheck : public PlatformCheck {
 public:
  using PlatformCheck::PlatformCheck;

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::BMC_EEPROM_CHECK;
  }

  std::string getName() const override {
    return "BMC EEPROMs";
  }

  std::string getDescription() const override {
    return "Validates that weutil on the BMC can read every EEPROM it lists";
  }
};

/**
 * Validates that the x86 MAC address agrees across the BMC's view
 * (wedge_us_mac.sh), the EEPROM, and the x86's own eth0.
 */
class X86MacConsistencyCheck : public PlatformCheck {
 public:
  X86MacConsistencyCheck(CheckTarget x86, CheckTarget bmc)
      : PlatformCheck(std::move(x86)), bmc_(std::move(bmc)) {}

  CheckResult run() override;

  std::optional<std::string> getSkipReason() const override;

  CheckType getType() const override {
    return CheckType::X86_MAC_CONSISTENCY_CHECK;
  }

  std::string getName() const override {
    return "x86 MAC Consistency";
  }

  std::string getDescription() const override {
    return "Validates that the x86 MAC matches between wedge_us_mac.sh, the EEPROM and x86 eth0";
  }

 private:
  CheckTarget bmc_;
};

} // namespace facebook::fboss::platform::platform_checks
