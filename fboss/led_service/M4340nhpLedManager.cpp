/*
 *  Copyright (c) 2026 Nexthop Systems Inc.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include "fboss/led_service/M4340nhpLedManager.h"
#include "fboss/lib/bsp/m4340nhp/M4340nhpBspPlatformMapping.h"

namespace facebook::fboss {

M4340nhpLedManager::M4340nhpLedManager() : BspLedManager() {
  init<M4340nhpBspPlatformMapping>(PlatformType::PLATFORM_M4340NHP);
  XLOG(INFO) << "Created M4340nhp BSP LED Manager";
}

} // namespace facebook::fboss
