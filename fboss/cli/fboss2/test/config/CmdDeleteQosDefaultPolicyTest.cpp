// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/delete/qos/default_policy/CmdDeleteQosDefaultPolicy.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

class CmdDeleteQosDefaultPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteQosDefaultPolicyTestFixture()
      : CmdConfigTestBase(
            "fboss_qos_delete_default_policy_test_%%%%-%%%%-%%%%-%%%%",
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
  const std::string cmdPrefix_ = "delete qos default-policy";
};

TEST_F(CmdDeleteQosDefaultPolicyTestFixture, deletesExistingDefaultPolicy) {
  setupTestableConfigSession(cmdPrefix_, "");

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  switchConfig.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig{};
  switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy() =
      "default_qos_policy";

  auto cmd = CmdDeleteQosDefaultPolicy();
  auto result = cmd.queryClient(localhost());

  EXPECT_THAT(result, ::testing::HasSubstr("Successfully removed"));
  EXPECT_THAT(result, ::testing::HasSubstr("default_qos_policy"));

  if (switchConfig.dataPlaneTrafficPolicy().has_value()) {
    EXPECT_FALSE(
        switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().has_value());
  }
}

TEST_F(CmdDeleteQosDefaultPolicyTestFixture, noOpWhenNoPolicySet) {
  setupTestableConfigSession(cmdPrefix_, "");

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  switchConfig.dataPlaneTrafficPolicy().reset();

  auto cmd = CmdDeleteQosDefaultPolicy();
  auto result = cmd.queryClient(localhost());

  EXPECT_THAT(result, ::testing::HasSubstr("No default QoS policy"));
}

TEST_F(
    CmdDeleteQosDefaultPolicyTestFixture,
    noOpWhenTrafficPolicyHasNoPolicyField) {
  setupTestableConfigSession(cmdPrefix_, "");

  auto& agentConfig = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();
  switchConfig.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig{};
  // defaultQosPolicy not set on the policy object

  auto cmd = CmdDeleteQosDefaultPolicy();
  auto result = cmd.queryClient(localhost());

  EXPECT_THAT(result, ::testing::HasSubstr("No default QoS policy"));
}

TEST_F(CmdDeleteQosDefaultPolicyTestFixture, printOutputWritesToStdout) {
  auto cmd = CmdDeleteQosDefaultPolicy();
  std::string msg = "Successfully removed default QoS policy 'my_policy'";

  std::stringstream buffer;
  std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
  cmd.printOutput(msg);
  std::cout.rdbuf(old);

  EXPECT_EQ(buffer.str(), msg + "\n");
}

} // namespace facebook::fboss
