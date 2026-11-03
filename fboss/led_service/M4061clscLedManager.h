// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once

#include "fboss/led_service/BspLedManager.h"

namespace facebook::fboss {

/*
 * M4061clscLedManager class definition:
 *
 * The BspLedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class M4061clscLedManager : public BspLedManager {
 public:
  M4061clscLedManager();
  virtual ~M4061clscLedManager() override {}

  // Forbidden copy constructor and assignment operator
  M4061clscLedManager(M4061clscLedManager const&) = delete;
  M4061clscLedManager& operator=(M4061clscLedManager const&) = delete;
};

} // namespace facebook::fboss
