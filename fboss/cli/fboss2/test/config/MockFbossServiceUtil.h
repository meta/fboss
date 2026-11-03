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
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"

namespace facebook::fboss {

/**
 * Mock implementation of FbossServiceUtil for unit testing ConfigSession.
 * Allows injecting controlled behavior for isSplitMode() and restartService()
 * without touching systemd or the filesystem.
 */
class MockFbossServiceUtil : public FbossServiceUtil {
 public:
  // Uses a MockSystemdInterface so the base class never calls real systemctl.
  MockFbossServiceUtil()
      : FbossServiceUtil(
            std::vector<int>{},
            /*multiSwitch=*/false,
            std::make_unique<MockSystemdInterface>()) {}

  MOCK_METHOD(bool, isSplitMode, (), (const, override));
  MOCK_METHOD(
      std::vector<std::string>,
      restartService,
      (cli::ServiceType service, cli::ConfigActionLevel level),
      (override));
  MOCK_METHOD(bool, isAgentConfigured, (const HostInfo& hostInfo), (override));
  MOCK_METHOD(
      std::vector<std::string>,
      reloadConfig,
      (cli::ServiceType service, const HostInfo& hostInfo),
      (override));
};

} // namespace facebook::fboss
