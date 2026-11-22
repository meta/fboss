/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/mac/aging_time/CmdConfigMacAgingTime.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors the FBOSS default: switchSettings.l2AgeTimerSeconds = 300.
// The agent initialises this field to 300s when no explicit value is present
// in the running config.
class CmdConfigMacAgingTimeTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigMacAgingTimeTestFixture()
      : CmdConfigTestBase(
            "fboss_mac_aging_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "switchSettings": {
      "l2AgeTimerSeconds": 300
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config mac aging-time";
};

// ==============================================================================
// MacAgingTimeArg validation tests
// ==============================================================================

TEST_F(CmdConfigMacAgingTimeTestFixture, macAgingTimeArg_validSeconds) {
  // Any strictly positive integer must parse without error and be accessible
  // via getSeconds().  The boundary values 1 and a large number are included
  // to confirm there is no hidden upper-bound clamp.
  EXPECT_EQ(MacAgingTimeArg({"1"}).getSeconds(), 1);
  EXPECT_EQ(MacAgingTimeArg({"300"}).getSeconds(), 300);
  EXPECT_EQ(MacAgingTimeArg({"1000000"}).getSeconds(), 1000000);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, macAgingTimeArg_noArgs) {
  // Passing an empty token list must throw because the required positional
  // argument is missing.
  EXPECT_THROW(MacAgingTimeArg({}), std::invalid_argument);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, macAgingTimeArg_tooManyArgs) {
  // The argument accepts exactly one token; a second token must be rejected.
  EXPECT_THROW(MacAgingTimeArg({"300", "600"}), std::invalid_argument);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, macAgingTimeArg_notAnInteger) {
  // Non-numeric strings and floating-point notation must be rejected — the
  // argument requires a whole-number count of seconds.
  EXPECT_THROW(MacAgingTimeArg({"abc"}), std::invalid_argument);
  EXPECT_THROW(MacAgingTimeArg({"30.5"}), std::invalid_argument);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, macAgingTimeArg_zeroOrNegative) {
  // Zero and negative values must be rejected: a MAC aging timer of ≤ 0
  // seconds has no meaningful interpretation for the agent.
  EXPECT_THROW(MacAgingTimeArg({"0"}), std::invalid_argument);
  EXPECT_THROW(MacAgingTimeArg({"-1"}), std::invalid_argument);
  EXPECT_THROW(MacAgingTimeArg({"-300"}), std::invalid_argument);
}

// ==============================================================================
// queryClient() tests
// ==============================================================================

TEST_F(CmdConfigMacAgingTimeTestFixture, queryClient_setsNewValue) {
  // Changing l2AgeTimerSeconds from the seed value (300) to 600 must update
  // the in-session config and return a confirmation string that contains the
  // new value.
  setupTestableConfigSession(cmdPrefix_, "600");
  CmdConfigMacAgingTime cmd;
  HostInfo hostInfo("testhost");
  MacAgingTimeArg arg({"600"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("600"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->switchSettings()->l2AgeTimerSeconds(), 600);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, queryClient_setsMinimumValue) {
  // Setting aging-time to 1 (the minimum accepted value) must succeed and
  // write the value through to switchSettings.l2AgeTimerSeconds.
  setupTestableConfigSession(cmdPrefix_, "1");
  CmdConfigMacAgingTime cmd;
  HostInfo hostInfo("testhost");
  MacAgingTimeArg arg({"1"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("1"));

  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  EXPECT_EQ(*config.sw()->switchSettings()->l2AgeTimerSeconds(), 1);
}

TEST_F(CmdConfigMacAgingTimeTestFixture, queryClient_noOpWhenAlreadySet) {
  // Re-applying the current value (300 in the seed) must be detected as a
  // no-op: the command should return a message containing "already" rather
  // than writing a redundant change to the session.
  setupTestableConfigSession(cmdPrefix_, "300");
  CmdConfigMacAgingTime cmd;
  HostInfo hostInfo("testhost");
  MacAgingTimeArg arg({"300"});

  auto result = cmd.queryClient(hostInfo, arg);
  EXPECT_THAT(result, HasSubstr("already"));
}

} // namespace facebook::fboss
