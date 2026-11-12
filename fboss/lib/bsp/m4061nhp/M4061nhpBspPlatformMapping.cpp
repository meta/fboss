// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/lib/bsp/m4061nhp/M4061nhpBspPlatformMapping.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss {

M4061nhpBspPlatformMapping::M4061nhpBspPlatformMapping()
    : BspPlatformMapping("m4061nhp") {}

M4061nhpBspPlatformMapping::M4061nhpBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          apache::thrift::SimpleJSONSerializer::deserialize<
              BspPlatformMappingThrift>(platformMappingStr)) {}

} // namespace facebook::fboss
