// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/m4052actm/M4052ACTMBspPlatformMapping.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss {

M4052ACTMBspPlatformMapping::M4052ACTMBspPlatformMapping()
    : BspPlatformMapping("m4052actm") {}

M4052ACTMBspPlatformMapping::M4052ACTMBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          apache::thrift::SimpleJSONSerializer::deserialize<
              BspPlatformMappingThrift>(platformMappingStr)) {}

} // namespace facebook::fboss
