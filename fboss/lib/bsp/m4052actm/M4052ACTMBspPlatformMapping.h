// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook {
namespace fboss {

class M4052ACTMBspPlatformMapping : public BspPlatformMapping {
 public:
  M4052ACTMBspPlatformMapping();
  explicit M4052ACTMBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace fboss
} // namespace facebook
