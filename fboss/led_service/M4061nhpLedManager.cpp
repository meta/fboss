// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/led_service/M4061nhpLedManager.h"
#include "fboss/lib/bsp/m4061nhp/M4061nhpBspPlatformMapping.h"

namespace facebook::fboss {

M4061nhpLedManager::M4061nhpLedManager() : BspLedManager() {
  init<M4061nhpBspPlatformMapping>(PlatformType::PLATFORM_M4061NHP);
  XLOG(INFO) << "Created M4061nhp BSP LED Manager";
}

} // namespace facebook::fboss
