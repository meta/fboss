/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/i801SmbusTimeoutCheck.h"

#include <filesystem>

#include <folly/logging/xlog.h>
#include "fboss/platform/platform_checks/CommandLog.h"
#include "fboss/platform/platform_checks/HostEeprom.h"

namespace facebook::fboss::platform::platform_checks {

CheckResult i801SmbusTimeoutCheck::run() {
  CommandLog log;
  // Check if MCB EEPROM exists on this platform
  if (!host().exists(kMcbEepromPath)) {
    XLOG(INFO) << "MCB EEPROM not found on this platform, check not applicable";
    log.note(
        std::string(kMcbEepromPath) + " does not exist; check not applicable");
    return log.attachTo(makeOK());
  }

  // Check if MCB_EEPROM can be read
  try {
    readEepromByPath(host(), kMcbEepromPath);
    XLOG(INFO) << "i801SmbusTimeoutCheck passed: MCB EEPROM is reachable";
    log.note("weutil read MCB EEPROM " + std::string(kMcbEepromPath));
    return log.attachTo(makeOK());
  } catch (const std::exception& eepromEx) {
    XLOG(DBG2) << "Failed to read MCB EEPROM: " << eepromEx.what();
    log.note(
        "weutil failed to read MCB EEPROM " + std::string(kMcbEepromPath) +
        ": " + eepromEx.what());
  }
  // Continue to check if this is the i801_smbus timeout issue

  // EEPROM read failed, now check if this is the i801_smbus timeout issue
  // Check if i801_smbus driver directory exists
  std::filesystem::path driverPath =
      std::filesystem::path(kSysPciDrivers) / std::string(kI801Driver);
  if (!host().exists(driverPath)) {
    XLOG(INFO) << "i801_smbus driver not found, check not applicable";
    log.note(driverPath.string() + " does not exist; check not applicable");
    return log.attachTo(makeOK());
  }

  // Check if PCI device exists at the expected location
  std::filesystem::path pciDevicePath = driverPath / std::string(kI801PciId);
  if (!host().exists(pciDevicePath)) {
    XLOG(INFO) << "PCI device " << kI801PciId
               << " not bound to i801_smbus driver, check not applicable";
    log.note(pciDevicePath.string() + " does not exist; check not applicable");
    return log.attachTo(makeOK());
  }

  // Try to read the MCB EEPROM using hexdump to detect timeout
  auto hexdump =
      log.run(host(), "hexdump -n 16 " + std::string(kMcbEepromPath));

  // Check for timeout error
  if (!hexdump.ok() &&
      hexdump.standardErr.find("Connection timed out") != std::string::npos) {
    std::string errorMsg = "MCB EEPROM read timed out on i801_smbus";
    std::string remediationMsg =
        "i801_smbus driver may need to be reset. Follow instructions in P2078895966";
    return log.attachTo(makeProblem(
        errorMsg, RemediationType::MANUAL_REMEDIATION, remediationMsg));
  }
  // Some other error occurred
  return log.attachTo(makeProblem(
      "MCB EEPROM read failed, but i801_smbus timeout not detected.",
      RemediationType::MANUAL_REMEDIATION,
      "Investigate further"));
}
} // namespace facebook::fboss::platform::platform_checks
