// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/default_policy/CmdConfigQosDefaultPolicy.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

class CmdConfigQosDefaultPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigQosDefaultPolicyTestFixture()
      : CmdConfigTestBase(
            "fboss_qos_default_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1,
        "name": "eth1/1/1",
        "state": 2,
        "speed": 100000
      }
    ],
    "qosPolicies": [
      {
        "name": "default_qos_policy"
      }
    ]
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "config qos default-policy";
};

TEST_F(CmdConfigQosDefaultPolicyTestFixture, validPolicyName) {
  EXPECT_NO_THROW(DefaultPolicyName({"default_qos_policy"}));
  EXPECT_NO_THROW(DefaultPolicyName({"MyPolicy"}));
  EXPECT_NO_THROW(DefaultPolicyName({"policy-123"}));
  EXPECT_NO_THROW(DefaultPolicyName({"a"}));
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, emptyArgThrows) {
  EXPECT_THROW(DefaultPolicyName({}), std::invalid_argument);
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, multipleArgsThrows) {
  EXPECT_THROW(
      DefaultPolicyName({"policy1", "policy2"}), std::invalid_argument);
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, invalidNameStartsWithDigitThrows) {
  EXPECT_THROW(DefaultPolicyName({"123policy"}), std::invalid_argument);
}

TEST_F(
    CmdConfigQosDefaultPolicyTestFixture,
    invalidNameStartsWithUnderscoreThrows) {
  EXPECT_THROW(DefaultPolicyName({"_policy"}), std::invalid_argument);
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, invalidNameTooLongThrows) {
  // 65 chars starting with a letter
  EXPECT_THROW(
      DefaultPolicyName({std::string(65, 'a')}), std::invalid_argument);
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, setsDefaultQosPolicy) {
  setupTestableConfigSession(cmdPrefix_, "default_qos_policy");

  auto cmd = CmdConfigQosDefaultPolicy();
  DefaultPolicyName policyName(getCmdArgsList());

  auto result = cmd.queryClient(localhost(), policyName);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully set"));
  EXPECT_THAT(result, ::testing::HasSubstr("default_qos_policy"));

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  ASSERT_TRUE(switchConfig.dataPlaneTrafficPolicy().has_value());
  ASSERT_TRUE(
      switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().has_value());
  EXPECT_EQ(
      *switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy(),
      "default_qos_policy");
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, overwritesExistingDefaultPolicy) {
  setupTestableConfigSession(cmdPrefix_, "new_policy");

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();

  // Add new_policy to qosPolicies so the existence check passes
  cfg::QosPolicy newPolicy;
  newPolicy.name() = "new_policy";
  (*switchConfig.qosPolicies()).push_back(std::move(newPolicy));

  // Pre-set old default
  switchConfig.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig{};
  switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy() = "old_policy";

  auto cmd = CmdConfigQosDefaultPolicy();
  DefaultPolicyName policyName(getCmdArgsList());
  auto result = cmd.queryClient(localhost(), policyName);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully set"));
  EXPECT_THAT(result, ::testing::HasSubstr("new_policy"));
  ASSERT_TRUE(
      switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().has_value());
  EXPECT_EQ(
      *switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy(), "new_policy");
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, createsTrafficPolicyIfAbsent) {
  setupTestableConfigSession(cmdPrefix_, "default_qos_policy");

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  switchConfig.dataPlaneTrafficPolicy().reset();

  auto cmd = CmdConfigQosDefaultPolicy();
  DefaultPolicyName policyName(getCmdArgsList());
  auto result = cmd.queryClient(localhost(), policyName);

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully set"));
  ASSERT_TRUE(switchConfig.dataPlaneTrafficPolicy().has_value());
  ASSERT_TRUE(
      switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().has_value());
  EXPECT_EQ(
      *switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy(),
      "default_qos_policy");
}

TEST_F(CmdConfigQosDefaultPolicyTestFixture, unknownPolicyThrows) {
  setupTestableConfigSession(cmdPrefix_, "nonexistent_policy");

  auto cmd = CmdConfigQosDefaultPolicy();
  DefaultPolicyName policyName(getCmdArgsList());

  EXPECT_THROW(cmd.queryClient(localhost(), policyName), std::invalid_argument);
}

} // namespace facebook::fboss
