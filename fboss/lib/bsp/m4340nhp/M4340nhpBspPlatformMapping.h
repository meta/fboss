/*
 *  Copyright (c) 2026 Nexthop Systems Inc.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook::fboss {

class M4340nhpBspPlatformMapping : public BspPlatformMapping {
 public:
  M4340nhpBspPlatformMapping();
  explicit M4340nhpBspPlatformMapping(const std::string& platformMappingStr);
};

} // namespace facebook::fboss
