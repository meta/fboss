/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/led_service/BspLedManager.h"
#include "fboss/lib/bsp/BspSystemContainer.h"

namespace facebook::fboss {

/*
 * M4052ACTMLedManager class definiton:
 *
 * The BspLedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class M4052ACTMLedManager : public BspLedManager {
 public:
  M4052ACTMLedManager();
  virtual ~M4052ACTMLedManager() override {}

  // Forbidden copy constructor and assignment operator
  M4052ACTMLedManager(M4052ACTMLedManager const&) = delete;
  M4052ACTMLedManager& operator=(M4052ACTMLedManager const&) = delete;
};

} // namespace facebook::fboss
