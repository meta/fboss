// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/lib/bsp/m4061clscl/M4061clsclBspPlatformMapping.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss {

M4061clsclBspPlatformMapping::M4061clsclBspPlatformMapping()
    : BspPlatformMapping("m4061clscl") {}

M4061clsclBspPlatformMapping::M4061clsclBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          apache::thrift::SimpleJSONSerializer::deserialize<
              BspPlatformMappingThrift>(platformMappingStr)) {}

} // namespace facebook::fboss
