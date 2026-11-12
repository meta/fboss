/*
 *  Copyright (c) 2026 Nexthop Systems Inc.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook::fboss {

class M5120cscBspPlatformMapping : public BspPlatformMapping {
 public:
  M5120cscBspPlatformMapping();
  explicit M5120cscBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace facebook::fboss
