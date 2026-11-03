// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/M4052ACTMLedManager.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include "fboss/lib/bsp/m4052actm/M4052ACTMBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * M4052ACTMLedManager ctor()
 *
 * M4052ACTMLedManager constructor will create the LedManager object for
 * M4052ACTM platform
 */
M4052ACTMLedManager::M4052ACTMLedManager() : BspLedManager() {
  init<M4052ACTMBspPlatformMapping>(PlatformType::PLATFORM_M4052ACTM);
  XLOG(INFO) << "Created M4052ACTM BSP LED Manager";
}

} // namespace facebook::fboss
