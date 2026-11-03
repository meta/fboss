/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/HostEeprom.h"

#include <stdexcept>

#include <folly/String.h>
#include <folly/json/json.h>
#include <folly/system/Shell.h>

namespace facebook::fboss::platform::platform_checks {

namespace {

EepromFields runWeutil(const Host& host, const std::string& selector) {
  auto cmd = "weutil --json " + selector;
  auto result = host.run(cmd);
  if (!result.ok()) {
    // weutil reports errors on stdout.
    throw std::runtime_error(
        "`" + cmd + "` on " + host.name() + " failed: " +
        folly::trimWhitespace(result.standardOut + result.standardErr).str());
  }
  return parseWeutilJson(result.standardOut);
}

} // namespace

EepromFields parseWeutilJson(const std::string& output) {
  auto json = folly::parseJson(output);
  if (!json.isObject()) {
    throw std::runtime_error("weutil output is not a JSON object");
  }
  EepromFields fields;
  for (const auto& [key, value] : json.items()) {
    fields[key.asString()] = value.isString() ? value.asString() : "";
  }
  return fields;
}

EepromFields readEepromByName(const Host& host, const std::string& eepromName) {
  return runWeutil(host, "--eeprom " + folly::shellQuote(eepromName));
}

EepromFields readEepromByPath(
    const Host& host,
    const std::filesystem::path& eepromPath) {
  return runWeutil(host, "--path " + folly::shellQuote(eepromPath.string()));
}

} // namespace facebook::fboss::platform::platform_checks
