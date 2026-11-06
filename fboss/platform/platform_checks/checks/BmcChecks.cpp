/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/BmcChecks.h"

#include <vector>

#include <folly/String.h>

namespace facebook::fboss::platform::platform_checks {

namespace {

constexpr auto kEth0AddressCmd = "cat /sys/class/net/eth0/address";

std::optional<folly::MacAddress> parseMac(const std::string& text) {
  auto mac = folly::MacAddress::tryFromString(folly::trimWhitespace(text));
  if (!mac) {
    return std::nullopt;
  }
  return *mac;
}

} // namespace

std::optional<std::string> findWeutilField(
    const std::string& weutilOutput,
    const std::string& label) {
  std::vector<folly::StringPiece> lines;
  folly::split('\n', weutilOutput, lines);
  for (const auto& line : lines) {
    auto trimmed = folly::trimWhitespace(line);
    if (trimmed.startsWith(label + ":")) {
      return folly::trimWhitespace(trimmed.subpiece(label.size() + 1)).str();
    }
  }
  return std::nullopt;
}

CheckResult BmcMacAddressCheck::run() {
  CommandLog log;
  auto weutil = log.run(host(), "weutil --all");
  auto field = findWeutilField(weutil.standardOut, "BMC MAC Base");
  if (!weutil.ok() || !field) {
    return log.attachTo(makeError("Could not read BMC MAC Base from weutil"));
  }
  auto eth0 = log.run(host(), kEth0AddressCmd);
  auto eepromMac = parseMac(*field);
  auto eth0Mac = parseMac(eth0.standardOut);
  if (!eepromMac || !eth0.ok() || !eth0Mac) {
    return log.attachTo(makeError("Could not parse BMC MAC addresses"));
  }
  if (*eepromMac == *eth0Mac) {
    return log.attachTo(makeOK());
  }
  return log.attachTo(makeProblem(
      "BMC MAC mismatch: EEPROM=" + eepromMac->toString() +
          " eth0=" + eth0Mac->toString(),
      RemediationType::RMA_REQUIRED,
      "RMA device to correct MAC address"));
}

CheckResult BmcEepromCheck::run() {
  CommandLog log;
  auto list = log.run(host(), "weutil --list");
  if (!list.ok()) {
    return log.attachTo(makeError("weutil --list failed"));
  }
  std::vector<std::string> eeproms;
  std::vector<folly::StringPiece> lines;
  folly::split('\n', list.standardOut, lines, /*ignoreEmpty=*/true);
  for (const auto& line : lines) {
    std::vector<folly::StringPiece> words;
    folly::split(' ', folly::trimWhitespace(line), words, /*ignoreEmpty=*/true);
    if (!words.empty()) {
      eeproms.push_back(words.front().str());
    }
  }
  if (eeproms.empty()) {
    return log.attachTo(makeError("weutil --list returned no EEPROMs"));
  }

  std::vector<std::string> failed;
  for (const auto& eeprom : eeproms) {
    if (!log.run(host(), "weutil --eeprom " + eeprom).ok()) {
      failed.push_back(eeprom);
    }
  }
  if (failed.empty()) {
    return log.attachTo(makeOK());
  }
  return log.attachTo(makeProblem(
      "weutil failed to read: " + folly::join(", ", failed),
      RemediationType::MANUAL_REMEDIATION,
      "Inspect the weutil output in the details (-v)"));
}

std::optional<std::string> X86MacConsistencyCheck::getSkipReason() const {
  if (!bmc_.host) {
    return bmc_.unavailableReason;
  }
  return PlatformCheck::getSkipReason();
}

CheckResult X86MacConsistencyCheck::run() {
  CommandLog log;
  const auto& bmc = *bmc_.host;
  auto wedgeUsMac = log.run(bmc, "wedge_us_mac.sh");
  auto weutil = log.run(bmc, "weutil --all");
  auto eth0 = log.run(host(), kEth0AddressCmd);

  auto bmcView = parseMac(wedgeUsMac.standardOut);
  auto field = findWeutilField(weutil.standardOut, "X86 CPU MAC Base");
  auto eepromMac = field ? parseMac(*field) : std::nullopt;
  auto eth0Mac = parseMac(eth0.standardOut);
  if (!wedgeUsMac.ok() || !bmcView || !eepromMac || !eth0.ok() || !eth0Mac) {
    return log.attachTo(makeError("Could not read all x86 MAC addresses"));
  }
  if (*bmcView == *eepromMac && *eepromMac == *eth0Mac) {
    return log.attachTo(makeOK());
  }
  return log.attachTo(makeProblem(
      "x86 MAC mismatch: wedge_us_mac.sh=" + bmcView->toString() +
          " EEPROM=" + eepromMac->toString() + " eth0=" + eth0Mac->toString(),
      RemediationType::RMA_REQUIRED,
      "RMA device to correct MAC address"));
}

} // namespace facebook::fboss::platform::platform_checks
