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

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "fboss/configs/platforms/generic/forwarding_stack/gen-cpp2/config_generation_types.h"
#include "fboss/lib/if/gen-cpp2/fboss_common_types.h"

namespace facebook::fboss::configgen {

std::string getPlatformName(PlatformType platform);

struct ConfigGenerationTarget {
  PlatformType platform;
  std::filesystem::path platformDirectory;
  ConfigGenerationSpec spec;
};

ConfigGenerationSpec parseConfigGenerationSpec(std::string_view contents);

ConfigGenerationTarget loadConfigGenerationTarget(
    const std::filesystem::path& platformDirectory);

std::vector<ConfigGenerationTarget> loadConfigGenerationTargets(
    const std::filesystem::path& fbossRoot);

} // namespace facebook::fboss::configgen
