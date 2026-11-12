// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook::fboss {

class M4061nhpBspPlatformMapping : public BspPlatformMapping {
 public:
  M4061nhpBspPlatformMapping();
  explicit M4061nhpBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace facebook::fboss
