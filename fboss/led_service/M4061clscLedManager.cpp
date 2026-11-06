// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/led_service/M4061clscLedManager.h"
#include "fboss/lib/bsp/m4061clsc/M4061clscBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * M4061clscLedManager ctor()
 *
 * M4061clscLedManager constructor will create the LedManager object for
 * M4061clsc platform
 */
M4061clscLedManager::M4061clscLedManager() : BspLedManager() {
  init<M4061clscBspPlatformMapping>(PlatformType::PLATFORM_M4061CLSC);
  XLOG(INFO) << "Created M4061clsc BSP LED Manager";
}

} // namespace facebook::fboss
