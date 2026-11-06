/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/MacAddressCheck.h"

#include <stdexcept>

#include <folly/logging/xlog.h>
#include "fboss/platform/platform_checks/HostEeprom.h"
#include "fboss/platform/weutil/ConfigUtils.h"

namespace facebook::fboss::platform::platform_checks {

CheckResult MacAddressCheck::run() {
  try {
    auto ifaceMac = getMacAddress(interfaceName_);
    if (ifaceMac == folly::MacAddress::ZERO) {
      return makeError(interfaceName_ + " interface has zero MAC address");
    }

    auto eepromMacList = getEepromMacAddressList();
    if (eepromMacList.empty()) {
      return makeError("No EEPROM MAC address found");
    }

    for (const auto& [eepromName, eepromMac] : eepromMacList) {
      if (ifaceMac != eepromMac) {
        std::string errorMsg = "MAC address mismatch: " + interfaceName_ + "=" +
            ifaceMac.toString() + " " + eepromName + "=" + eepromMac.toString();
        std::string remediationMsg = "RMA device to correct MAC address";
        return makeProblem(
            errorMsg, RemediationType::RMA_REQUIRED, remediationMsg);
      }
    }
  } catch (const std::exception& ex) {
    return makeError("Unexpected error: " + std::string(ex.what()));
  }
  return makeOK();
}

folly::MacAddress MacAddressCheck::getMacAddress(const std::string& interface) {
  auto address =
      host().readTrimmedFile("/sys/class/net/" + interface + "/address");
  if (!address) {
    throw std::runtime_error("Failed to get mac address of " + interface);
  }
  auto mac = folly::MacAddress::tryFromString(*address);
  if (!mac) {
    throw std::runtime_error("Invalid mac address: " + *address);
  }
  return *mac;
}

std::unordered_map<std::string, folly::MacAddress>
MacAddressCheck::getEepromMacAddressList() {
  std::unordered_map<std::string, folly::MacAddress> eepromMacList;
  auto fruEepromList = weutil::ConfigUtils(platformName()).getFruEepromList();

  for (const auto& [eepromName, eeprom] : fruEepromList) {
    // An EEPROM without a devmap path (DARWIN's CHASSIS) is read by weutil
    // dumping the BIOS flash with flashrom, too heavy for a routine check.
    if (eeprom.path.empty()) {
      continue;
    }
    auto fields = readEepromByName(host(), eepromName);
    // DARWIN EEPROMs (Arista prefdl) only have the switch's "Local MAC".
    auto field =
        fields.contains("X86 CPU MAC Base") ? "X86 CPU MAC Base" : "Local MAC";
    const std::string& eepromMacStr = fields[field];
    if (eepromMacStr.empty()) {
      continue;
    }
    auto eepromMac = folly::MacAddress::tryFromString(eepromMacStr);
    // Another EEPROM may still hold a usable MAC; if none does, run() reports
    // that no EEPROM MAC address was found.
    if (!eepromMac) {
      XLOG(WARN) << "Ignoring malformed MAC '" << eepromMacStr << "' in "
                 << eepromName;
      continue;
    }
    if (*eepromMac == folly::MacAddress::ZERO) {
      // For Icecube/tahansb800bc/ladakh800bcls, the MAC address is
      // actually in CHASSIS_EEPROM, the x86CpuMac field in COME_EEPROM is
      // zero.
      continue;
    }
    eepromMacList[eepromName] = *eepromMac;
    XLOG(INFO) << "eepromName: " << eepromName << " x86CpuMac: " << *eepromMac;
  }

  return eepromMacList;
}

} // namespace facebook::fboss::platform::platform_checks
