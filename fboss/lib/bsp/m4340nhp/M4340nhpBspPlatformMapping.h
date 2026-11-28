// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook::fboss {

class M4340nhpBspPlatformMapping : public BspPlatformMapping {
 public:
  M4340nhpBspPlatformMapping();
  explicit M4340nhpBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace facebook::fboss
