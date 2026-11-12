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

#include <optional>
#include <string_view>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/lib/if/gen-cpp2/fboss_common_types.h"

namespace facebook::fboss::utility {

std::optional<std::string_view> getDefaultConnectionHandle(
    cfg::AsicType asicType);

cfg::SwitchSettings genNpuSwitchSettingsConfig(
    cfg::AsicType asicType,
    std::optional<PlatformType> platformType = std::nullopt,
    std::optional<PlatformCPUType> platformCPUType = std::nullopt);

} // namespace facebook::fboss::utility
