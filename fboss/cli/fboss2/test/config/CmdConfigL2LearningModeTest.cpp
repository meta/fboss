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

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/l2/learning_mode/CmdConfigL2LearningMode.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdConfigL2LearningModeTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigL2LearningModeTestFixture()
      : CmdConfigTestBase(
            "fboss_l2_learning_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "switchSettings": {
      "l2LearningMode": 0
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config l2 learning-mode";
};

// ==============================================================================
// L2LearningModeArg Validation Tests
// ==============================================================================

TEST_F(CmdConfigL2LearningModeTestFixture, learningModeArgValidation) {
  // Test valid modes (lowercase)
  EXPECT_EQ(
      L2LearningModeArg({"hardware"}).getL2LearningMode(),
      cfg::L2LearningMode::HARDWARE);
  EXPECT_EQ(
      L2LearningModeArg({"software"}).getL2LearningMode(),
      cfg::L2LearningMode::SOFTWARE);
  EXPECT_EQ(
      L2LearningModeArg({"disabled"}).getL2LearningMode(),
      cfg::L2LearningMode::DISABLED);

  // Test valid modes (uppercase)
  EXPECT_EQ(
      L2LearningModeArg({"HARDWARE"}).getL2LearningMode(),
      cfg::L2LearningMode::HARDWARE);
  EXPECT_EQ(
      L2LearningModeArg({"SOFTWARE"}).getL2LearningMode(),
      cfg::L2LearningMode::SOFTWARE);
  EXPECT_EQ(
      L2LearningModeArg({"DISABLED"}).getL2LearningMode(),
      cfg::L2LearningMode::DISABLED);

  // Test valid modes (mixed case)
  EXPECT_EQ(
      L2LearningModeArg({"HaRdWaRe"}).getL2LearningMode(),
      cfg::L2LearningMode::HARDWARE);

  // Test invalid cases
  EXPECT_THROW(L2LearningModeArg({}), std::invalid_argument);
  EXPECT_THROW(L2LearningModeArg({"hardware", "extra"}), std::invalid_argument);
  EXPECT_THROW(L2LearningModeArg({"invalid"}), std::invalid_argument);
}

// ==============================================================================
// Command Execution Tests
// ==============================================================================

TEST_F(CmdConfigL2LearningModeTestFixture, setLearningModeToSoftware) {
  setupTestableConfigSession(cmdPrefix_, "software");
  CmdConfigL2LearningMode cmd;
  HostInfo hostInfo("testhost");
  L2LearningModeArg modeArg({"software"});

  auto result = cmd.queryClient(hostInfo, modeArg);
  EXPECT_THAT(result, HasSubstr("software"));

  // Verify the config was updated
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto mode = *config.sw()->switchSettings()->l2LearningMode();
  EXPECT_EQ(mode, cfg::L2LearningMode::SOFTWARE);
}

TEST_F(CmdConfigL2LearningModeTestFixture, setLearningModeToHardware) {
  setupTestableConfigSession(cmdPrefix_, "software");
  // First set to software, then back to hardware
  CmdConfigL2LearningMode cmd;
  HostInfo hostInfo("testhost");

  L2LearningModeArg softwareArg({"software"});
  cmd.queryClient(hostInfo, softwareArg);

  L2LearningModeArg hardwareArg({"hardware"});
  auto result = cmd.queryClient(hostInfo, hardwareArg);
  EXPECT_THAT(result, HasSubstr("hardware"));

  // Verify the config was updated
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto mode = *config.sw()->switchSettings()->l2LearningMode();
  EXPECT_EQ(mode, cfg::L2LearningMode::HARDWARE);
}

TEST_F(CmdConfigL2LearningModeTestFixture, setLearningModeToDisabled) {
  setupTestableConfigSession(cmdPrefix_, "disabled");
  CmdConfigL2LearningMode cmd;
  HostInfo hostInfo("testhost");
  L2LearningModeArg modeArg({"disabled"});

  auto result = cmd.queryClient(hostInfo, modeArg);
  EXPECT_THAT(result, HasSubstr("disabled"));

  // Verify the config was updated
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto mode = *config.sw()->switchSettings()->l2LearningMode();
  EXPECT_EQ(mode, cfg::L2LearningMode::DISABLED);
}

TEST_F(CmdConfigL2LearningModeTestFixture, setLearningModeAlreadySet) {
  setupTestableConfigSession(cmdPrefix_, "hardware");
  CmdConfigL2LearningMode cmd;
  HostInfo hostInfo("testhost");

  // Default is HARDWARE (mode=0)
  L2LearningModeArg hardwareArg({"hardware"});
  auto result = cmd.queryClient(hostInfo, hardwareArg);
  EXPECT_THAT(result, HasSubstr("already"));
}

} // namespace facebook::fboss
