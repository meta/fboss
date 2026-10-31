// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/delete/qos/priority_group_policy/CmdDeleteQosPriorityGroupPolicy.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

// "free" is bound to no port; "bound" is named by eth1/1/1's pfc config.
class CmdDeleteQosPriorityGroupPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteQosPriorityGroupPolicyTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_qos_pg_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "ports": [
      {
        "logicalID": 1, "name": "eth1/1/1", "state": 2, "speed": 100000,
        "pfc": {"tx": true, "rx": true, "portPgConfigName": "bound"}
      },
      {"logicalID": 2, "name": "eth1/2/1", "state": 2, "speed": 100000}
    ],
    "bufferPoolConfigs": {"pool": {"sharedBytes": 1000}},
    "portPgConfigs": {
      "free": [{"id": 0, "bufferPoolName": "pool"}],
      "bound": [{"id": 0, "bufferPoolName": "pool"}]
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete qos";

  static PriorityGroupPolicyName name(const std::string& n) {
    return PriorityGroupPolicyName(std::vector<std::string>{n});
  }

  bool hasPolicy(const std::string& n) {
    auto pgs =
        ConfigSession::getInstance().getAgentConfig().sw()->portPgConfigs();
    return pgs.has_value() && pgs->count(n) > 0;
  }
};

TEST_F(CmdDeleteQosPriorityGroupPolicyTestFixture, deletesUnboundPolicy) {
  setupTestableConfigSession(cmdPrefix_, "priority-group-policy free");
  auto cmd = CmdDeleteQosPriorityGroupPolicy();

  auto result = cmd.queryClient(localhost(), name("free"));

  EXPECT_THAT(result, HasSubstr("deleted priority group policy 'free'"));
  EXPECT_FALSE(hasPolicy("free"));
  EXPECT_TRUE(hasPolicy("bound"));
  // Same action level as `config qos priority-group-policy`.
  EXPECT_EQ(
      ConfigSession::getInstance().getRequiredAction(cli::ServiceType::AGENT),
      cli::ConfigActionLevel::SERVICE_RESTART);
  // The pool the groups pointed at is not touched.
  EXPECT_TRUE(
      ConfigSession::getInstance()
          .getAgentConfig()
          .sw()
          ->bufferPoolConfigs()
          ->count("pool"));
}

TEST_F(CmdDeleteQosPriorityGroupPolicyTestFixture, refusedWhenPortBinds) {
  setupTestableConfigSession(cmdPrefix_, "priority-group-policy bound");
  auto cmd = CmdDeleteQosPriorityGroupPolicy();

  try {
    cmd.queryClient(localhost(), name("bound"));
    FAIL() << "expected refusal";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("still bound to interface(s) eth1/1/1"));
  }
  EXPECT_TRUE(hasPolicy("bound"));
}

TEST_F(CmdDeleteQosPriorityGroupPolicyTestFixture, missingPolicyFails) {
  setupTestableConfigSession(cmdPrefix_, "priority-group-policy nope");
  auto cmd = CmdDeleteQosPriorityGroupPolicy();

  EXPECT_THROW(cmd.queryClient(localhost(), name("nope")), std::runtime_error);
}

} // namespace facebook::fboss
