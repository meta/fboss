// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/Morgan800ccLedManager.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include "fboss/lib/bsp/morgan800cc/Morgan800ccBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * Morgan800ccLedManager ctor()
 *
 * Morgan800ccLedManager constructor will create the LedManager object for
 * Morgan800cc platform
 */
Morgan800ccLedManager::Morgan800ccLedManager() : BspLedManager() {
  init<Morgan800ccBspPlatformMapping>(PlatformType::PLATFORM_MORGAN800CC);
  XLOG(INFO) << "Created Morgan800cc BSP LED Manager";
}

} // namespace facebook::fboss
