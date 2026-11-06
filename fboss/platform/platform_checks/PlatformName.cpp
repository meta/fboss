/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/PlatformName.h"

#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/helpers/PlatformNameLib.h"

namespace facebook::fboss::platform::platform_checks {

using helpers::PlatformNameLib;

std::optional<std::string> getPlatformName(const Host& host) {
  if (auto cached = host.readFile(PlatformNameLib::kCachePath)) {
    auto name = folly::trimWhitespace(*cached).str();
    if (!name.empty()) {
      return name;
    }
  }
  auto result = host.run(PlatformNameLib::dmidecodeCommand);
  if (!result.ok()) {
    XLOG(ERR) << "Failed to get platform name of " << host.name() << ": "
              << result.standardErr;
    return std::nullopt;
  }
  auto biosName = folly::trimWhitespace(result.standardOut).str();
  if (biosName.empty()) {
    XLOG(ERR) << "dmidecode on " << host.name()
              << " reported no system product name";
    return std::nullopt;
  }
  return PlatformNameLib::sanitizePlatformName(biosName);
}

} // namespace facebook::fboss::platform::platform_checks
