// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/led_service/M4061clsclLedManager.h"
#include "fboss/lib/bsp/M4061clscl/M4061clsclBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * M4061clsclLedManager ctor()
 *
 * M4061clsclLedManager constructor will create the LedManager object for
 * M4061clscl platform
 */
M4061clsclLedManager::M4061clsclLedManager() : BspLedManager() {
  init<M4061clsclBspPlatformMapping>(PlatformType::PLATFORM_M4061clscl);
  XLOG(INFO) << "Created M4061clscl BSP LED Manager";
}

} // namespace facebook::fboss
