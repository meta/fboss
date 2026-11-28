// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook {
namespace fboss {

class M4061clsclBspPlatformMapping : public BspPlatformMapping {
 public:
  M4061clsclBspPlatformMapping();
  explicit M4061clsclBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace fboss
} // namespace facebook
