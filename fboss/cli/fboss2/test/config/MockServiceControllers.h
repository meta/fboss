// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gmock/gmock.h>
#include "fboss/cli/fboss2/session/AgentServiceController.h"
#include "fboss/cli/fboss2/session/BgpServiceController.h"
#include "fboss/cli/fboss2/test/config/MockSystemdInterface.h"

namespace facebook::fboss {

// Stands in for the agent's service controller without touching systemd,
// thrift or the filesystem.
class MockAgentServiceController : public AgentServiceController {
 public:
  // Uses a MockSystemdInterface so the base class never calls real systemctl.
  MockAgentServiceController()
      : AgentServiceController(
            std::vector<int>{},
            /*multiSwitch=*/false,
            std::make_unique<MockSystemdInterface>()) {}

  MOCK_METHOD(bool, isSplitMode, (), (const, override));
  MOCK_METHOD(
      std::vector<std::string>,
      restart,
      (cli::ConfigActionLevel level),
      (override));
  MOCK_METHOD(bool, isAgentConfigured, (const HostInfo& hostInfo), (override));
  MOCK_METHOD(
      std::vector<std::string>,
      reloadConfig,
      (const HostInfo& hostInfo),
      (override));
};

class MockBgpServiceController : public BgpServiceController {
 public:
  MockBgpServiceController()
      : BgpServiceController(std::make_unique<MockSystemdInterface>()) {}

  MOCK_METHOD(std::vector<std::string>, restart, (), (override));
};

} // namespace facebook::fboss
