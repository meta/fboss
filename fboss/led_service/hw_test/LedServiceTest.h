/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <gtest/gtest.h>
#include "fboss/agent/types.h"
#include "fboss/led_service/hw_test/LedEnsemble.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

class LedServiceTest : public ::testing::Test {
 public:
  LedServiceTest() {}
  ~LedServiceTest() override {}

  LedEnsemble* getLedEnsemble() {
    return ensemble_.get();
  }

  std::vector<TransceiverID> getAllTransceivers(
      const PlatformMapping* platformMapping) const;

  void SetUp() override;
  void TearDown() override;
  // Check the LED color.
  void checkLedColor(PortID port, led::LedColor color, int testNum);
  void checkLedBlink(PortID port, led::Blink blink, int testNum);

  facebook::fboss::LedManager* ledManager_ = nullptr;
  const facebook::fboss::PlatformMapping* platformMap_ = nullptr;

 private:
  void checkLedMemoryInBounds() const;
  // Forbidden copy constructor and assignment operator
  LedServiceTest(LedServiceTest const&) = delete;
  LedServiceTest& operator=(LedServiceTest const&) = delete;

  std::unique_ptr<LedEnsemble> ensemble_;
};
} // namespace facebook::fboss
