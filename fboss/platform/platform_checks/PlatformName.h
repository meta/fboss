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

#include <optional>
#include <string>

#include "fboss/platform/platform_checks/Host.h"

namespace facebook::fboss::platform::platform_checks {

// Resolves the platform name of `host` the same way as
// helpers::PlatformNameLib::getPlatformName(): the cached name if present,
// otherwise dmidecode.
std::optional<std::string> getPlatformName(const Host& host);

} // namespace facebook::fboss::platform::platform_checks
