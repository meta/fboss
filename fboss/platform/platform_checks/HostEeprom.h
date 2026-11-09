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

#include <filesystem>
#include <map>
#include <string>

#include "fboss/platform/platform_checks/Host.h"

namespace facebook::fboss::platform::platform_checks {

using EepromFields = std::map<std::string, std::string>;

// Fields of an EEPROM on `host` as reported by `weutil --json`, which knows
// every EEPROM format and offset the platform uses. Throws std::runtime_error
// if weutil cannot read or parse the EEPROM.
EepromFields readEepromByName(const Host& host, const std::string& eepromName);
EepromFields readEepromByPath(
    const Host& host,
    const std::filesystem::path& eepromPath);

// Parses `weutil --json` output.
EepromFields parseWeutilJson(const std::string& output);

} // namespace facebook::fboss::platform::platform_checks
