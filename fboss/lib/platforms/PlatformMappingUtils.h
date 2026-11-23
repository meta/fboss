// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/agent/gen-cpp2/platform_config_types.h"

#include <cstdint>
#include <map>
#include <string>

namespace facebook::fboss {

using PortIdToPortAssignment = std::map<int32_t, cfg::PortAssignment>;

cfg::PlatformMapping readRawPlatformMapping(const std::string& path);

std::string resolveRawPlatformMappingPath(const std::string& path);

PortIdToPortAssignment readPortIdToPortAssignment(const std::string& path);

cfg::PlatformMapping reconstructPlatformMapping(
    cfg::PlatformMapping rawPlatformMapping,
    const PortIdToPortAssignment& portIdToPortAssignment);

cfg::PlatformMapping loadPlatformMappingFromSplitFiles(
    const std::string& rawPlatformMappingPath,
    const std::string& portIdToPortAssignmentPath);

} // namespace facebook::fboss
