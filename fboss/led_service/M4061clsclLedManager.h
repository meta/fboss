// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/led_service/BspLedManager.h"

namespace facebook::fboss {

/*
 * M4061clsclLedManager class definition:
 *
 * The BspLedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class M4061clsclLedManager : public BspLedManager {
 public:
  M4061clsclLedManager();
  virtual ~M4061clsclLedManager() override {}

  // Forbidden copy constructor and assignment operator
  M4061clsclLedManager(M4061clsclLedManager const&) = delete;
  M4061clsclLedManager& operator=(M4061clsclLedManager const&) = delete;
};

} // namespace facebook::fboss
