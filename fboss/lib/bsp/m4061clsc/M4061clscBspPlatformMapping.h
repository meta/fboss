// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook {
namespace fboss {

class M4061clscBspPlatformMapping : public BspPlatformMapping {
 public:
  M4061clscBspPlatformMapping();
  explicit M4061clscBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace fboss
} // namespace facebook
