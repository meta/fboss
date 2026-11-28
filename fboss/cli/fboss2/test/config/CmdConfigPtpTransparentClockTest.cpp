/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/ptp/transparent_clock/CmdConfigPtpTransparentClock.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors a production switch config where ptpTcEnable is true
class CmdConfigPtpTransparentClockTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigPtpTransparentClockTestFixture()
      : CmdConfigTestBase(
            "fboss_ptp_tc_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "switchSettings": {
      "ptpTcEnable": true
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config ptp transparent-clock";
};

TEST_F(CmdConfigPtpTransparentClockTestFixture, argValidation) {
  // Valid values
  EXPECT_TRUE(PtpTransparentClockArg({"enable"}).getEnable());
  EXPECT_FALSE(PtpTransparentClockArg({"disable"}).getEnable());

  // Case-insensitive
  EXPECT_TRUE(PtpTransparentClockArg({"ENABLE"}).getEnable());
  EXPECT_FALSE(PtpTransparentClockArg({"DISABLE"}).getEnable());
  EXPECT_TRUE(PtpTransparentClockArg({"Enable"}).getEnable());

  // Invalid
  EXPECT_THROW(PtpTransparentClockArg({}), std::invalid_argument);
  EXPECT_THROW(
      PtpTransparentClockArg({"enable", "extra"}), std::invalid_argument);
  EXPECT_THROW(PtpTransparentClockArg({"on"}), std::invalid_argument);
  EXPECT_THROW(PtpTransparentClockArg({"true"}), std::invalid_argument);
}

TEST_F(CmdConfigPtpTransparentClockTestFixture, disableWhenEnabled) {
  // Seed has ptpTcEnable=true; disable it
  setupTestableConfigSession(cmdPrefix_, "disable");
  CmdConfigPtpTransparentClock cmd;
  HostInfo hostInfo("testhost");
  PtpTransparentClockArg arg({"disable"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("Successfully disabled"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(*config.sw()->switchSettings()->ptpTcEnable());
}

TEST_F(CmdConfigPtpTransparentClockTestFixture, enableWhenDisabled) {
  // First disable, then re-enable
  setupTestableConfigSession(cmdPrefix_, "disable");
  CmdConfigPtpTransparentClock cmd;
  HostInfo hostInfo("testhost");

  PtpTransparentClockArg disableArg({"disable"});
  cmd.queryClient(hostInfo, disableArg);

  PtpTransparentClockArg enableArg({"enable"});
  auto result = cmd.queryClient(hostInfo, enableArg);
  EXPECT_THAT(result, HasSubstr("Successfully enabled"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_TRUE(*config.sw()->switchSettings()->ptpTcEnable());
}

TEST_F(CmdConfigPtpTransparentClockTestFixture, alreadyEnabled) {
  // Seed has ptpTcEnable=true; enabling again is a no-op
  setupTestableConfigSession(cmdPrefix_, "enable");
  CmdConfigPtpTransparentClock cmd;
  HostInfo hostInfo("testhost");
  PtpTransparentClockArg arg({"enable"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already enabled"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_TRUE(*config.sw()->switchSettings()->ptpTcEnable());
}

TEST_F(CmdConfigPtpTransparentClockTestFixture, alreadyDisabled) {
  // Disable from seed-enabled state, then disable again — second call is a
  // no-op
  setupTestableConfigSession(cmdPrefix_, "disable");
  CmdConfigPtpTransparentClock cmd;
  HostInfo hostInfo("testhost");

  PtpTransparentClockArg disableArg({"disable"});
  cmd.queryClient(hostInfo, disableArg);

  auto result = cmd.queryClient(hostInfo, disableArg);
  EXPECT_THAT(result, HasSubstr("already disabled"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_FALSE(*config.sw()->switchSettings()->ptpTcEnable());
}

} // namespace facebook::fboss
