// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/lib/bsp/m4340nhp/M4340nhpBspPlatformMapping.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss {

M4340nhpBspPlatformMapping::M4340nhpBspPlatformMapping()
    : BspPlatformMapping("m4340nhp") {}

M4340nhpBspPlatformMapping::M4340nhpBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          apache::thrift::SimpleJSONSerializer::deserialize<
              BspPlatformMappingThrift>(platformMappingStr)) {}

} // namespace facebook::fboss
