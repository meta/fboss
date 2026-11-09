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

#include <string_view>
#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

// Constants for i801_smbus timeout check
inline constexpr std::string_view kI801Driver = "i801_smbus";
inline constexpr std::string_view kI801PciId = "0000:00:1f.4";
inline constexpr std::string_view kSysPciDrivers = "/sys/bus/pci/drivers";
inline constexpr std::string_view kMcbEepromPath =
    "/run/devmap/eeproms/MCB_EEPROM";

/**
 * Validates that the i801_smbus driver does not have timeout issues
 */
class i801SmbusTimeoutCheck : public PlatformCheck {
 public:
  explicit i801SmbusTimeoutCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::I801_SMBUS_TIMEOUT_CHECK;
  }

  std::string getName() const override {
    return "i801 SMBUS Timeout Check";
  }

  std::string getDescription() const override {
    return "Checks if if i801_smbus driver has timeout issue which can cause MCB EEPROM read failures.";
  }
};

} // namespace facebook::fboss::platform::platform_checks
