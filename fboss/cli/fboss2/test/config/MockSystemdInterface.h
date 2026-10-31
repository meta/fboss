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

#include <gmock/gmock.h>
#include "fboss/cli/fboss2/session/SystemdInterface.h"

namespace facebook::fboss {

/**
 * Mock implementation of SystemdInterface for unit testing.
 * Derives from SystemdInterface to allow partial mocking - you can
 * override specific methods while keeping real implementations for others.
 * Uses Google Mock to allow setting expectations on systemd operations.
 */
class MockSystemdInterface : public SystemdInterface {
 public:
  MOCK_METHOD(
      bool,
      isServiceEnabled,
      (const std::string& serviceName),
      (override));
  MOCK_METHOD(
      std::vector<std::string>,
      getMatchingServices,
      (const std::string& pattern),
      (override));
  MOCK_METHOD(void, stopService, (const std::string& serviceName), (override));
  MOCK_METHOD(void, startService, (const std::string& serviceName), (override));
  MOCK_METHOD(
      void,
      restartService,
      (const std::string& serviceName),
      (override));
  MOCK_METHOD(
      bool,
      isServiceActive,
      (const std::string& serviceName),
      (override));
  MOCK_METHOD(
      void,
      waitForServiceActive,
      (const std::string& serviceName, int maxWaitSeconds, int pollIntervalMs),
      (override));
  MOCK_METHOD(
      void,
      waitForServiceInactive,
      (const std::string& serviceName, int maxWaitSeconds, int pollIntervalMs),
      (override));
};

} // namespace facebook::fboss
