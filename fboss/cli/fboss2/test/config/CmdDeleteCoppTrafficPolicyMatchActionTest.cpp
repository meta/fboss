/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// Seed config: a single cpuTrafficPolicy.trafficPolicy.matchToAction entry
// named "acl_copp_bgp" with all four deletable action fields populated
// (sendToQueue, counter, setTc, userDefinedTrap).  The fixture exercises
// each delete path independently; the remaining three fields must survive
// untouched in each test.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/match/CmdDeleteCoppTrafficPolicyMatch.h"
#include "fboss/cli/fboss2/commands/delete/copp/traffic_policy/match/action/CmdDeleteCoppTrafficPolicyMatchAction.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteCoppTrafficPolicyMatchActionTestFixture
    : public CmdConfigTestBase {
 public:
  CmdDeleteCoppTrafficPolicyMatchActionTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_match_action_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "cpuTrafficPolicy": {
      "trafficPolicy": {
        "matchToAction": [
          {
            "matcher": "acl_copp_bgp",
            "action": {
              "sendToQueue": {"queueId": 9},
              "counter": "cpu_bgp_counter",
              "setTc": {"tcValue": 7},
              "userDefinedTrap": {"queueId": 3}
            }
          },
          {
            "matcher": "acl_copp_arp",
            "action": {
              "sendToQueue": {"queueId": 2}
            }
          },
          {
            "matcher": "acl_copp_empty",
            "action": {}
          }
        ]
      }
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp traffic-policy match";

  cfg::MatchAction getActionForMatcher(const std::string& matcherName) {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    const auto& matchToActions =
        *config.sw()->cpuTrafficPolicy()->trafficPolicy()->matchToAction();
    for (const auto& mta : matchToActions) {
      if (*mta.matcher() == matcherName) {
        return *mta.action();
      }
    }
    throw std::runtime_error("Matcher not found: " + matcherName);
  }

  bool hasMatcher(const std::string& matcherName) {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    const auto& matchToActions =
        *config.sw()->cpuTrafficPolicy()->trafficPolicy()->matchToAction();
    for (const auto& mta : matchToActions) {
      if (*mta.matcher() == matcherName) {
        return true;
      }
    }
    return false;
  }
};

// ==============================================================================
// MatchActionArg Validation Tests
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, actionArgValidation) {
  // Valid action types
  EXPECT_EQ(
      traffic_policy::MatchActionArg({"send-to-queue"}).getActionType(),
      "send-to-queue");
  EXPECT_EQ(
      traffic_policy::MatchActionArg({"counter"}).getActionType(), "counter");
  EXPECT_EQ(
      traffic_policy::MatchActionArg({"set-tc"}).getActionType(), "set-tc");
  EXPECT_EQ(
      traffic_policy::MatchActionArg({"user-defined-trap"}).getActionType(),
      "user-defined-trap");

  // Invalid: empty
  EXPECT_THROW(traffic_policy::MatchActionArg({}), std::invalid_argument);

  // Invalid: wrong arity
  EXPECT_THROW(
      traffic_policy::MatchActionArg({"send-to-queue", "counter"}),
      std::invalid_argument);

  // Invalid: unknown action type
  EXPECT_THROW(traffic_policy::MatchActionArg({"drop"}), std::invalid_argument);
  EXPECT_THROW(
      traffic_policy::MatchActionArg({"queue"}), std::invalid_argument);
  EXPECT_THROW(traffic_policy::MatchActionArg({""}), std::invalid_argument);
}

TEST_F(
    CmdDeleteCoppTrafficPolicyMatchActionTestFixture,
    matcherNameArgValidation) {
  // Valid
  EXPECT_EQ(traffic_policy::MatcherName({"acl_bgp"}).getName(), "acl_bgp");
  EXPECT_EQ(
      traffic_policy::MatcherName({"my-matcher"}).getName(), "my-matcher");

  // Invalid: empty vector
  EXPECT_THROW(traffic_policy::MatcherName({}), std::invalid_argument);

  // Invalid: multiple names
  EXPECT_THROW(traffic_policy::MatcherName({"a", "b"}), std::invalid_argument);

  // Invalid: empty string
  EXPECT_THROW(traffic_policy::MatcherName({""}), std::invalid_argument);
}

// ==============================================================================
// Delete send-to-queue
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, deleteSendToQueue) {
  setupTestableConfigSession(cmdPrefix_, "acl_copp_bgp action send-to-queue");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_bgp"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("send-to-queue"));
  EXPECT_THAT(result, HasSubstr("acl_copp_bgp"));

  auto action = getActionForMatcher("acl_copp_bgp");
  EXPECT_FALSE(action.sendToQueue().has_value());
  // Other fields preserved
  EXPECT_TRUE(action.counter().has_value());
  EXPECT_EQ(*action.counter(), "cpu_bgp_counter");
  EXPECT_TRUE(action.setTc().has_value());
  EXPECT_EQ(*action.setTc()->tcValue(), 7);
  EXPECT_TRUE(action.userDefinedTrap().has_value());
  EXPECT_EQ(*action.userDefinedTrap()->queueId(), 3);
}

// ==============================================================================
// Delete counter
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, deleteCounter) {
  setupTestableConfigSession(cmdPrefix_, "acl_copp_bgp action counter");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_bgp"});
  traffic_policy::MatchActionArg actionType({"counter"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("counter"));

  auto action = getActionForMatcher("acl_copp_bgp");
  EXPECT_FALSE(action.counter().has_value());
  EXPECT_TRUE(action.sendToQueue().has_value());
  EXPECT_EQ(*action.sendToQueue()->queueId(), 9);
  EXPECT_TRUE(action.setTc().has_value());
  EXPECT_TRUE(action.userDefinedTrap().has_value());
}

// ==============================================================================
// Delete set-tc
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, deleteSetTc) {
  setupTestableConfigSession(cmdPrefix_, "acl_copp_bgp action set-tc");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_bgp"});
  traffic_policy::MatchActionArg actionType({"set-tc"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("set-tc"));

  auto action = getActionForMatcher("acl_copp_bgp");
  EXPECT_FALSE(action.setTc().has_value());
  EXPECT_TRUE(action.sendToQueue().has_value());
  EXPECT_TRUE(action.counter().has_value());
  EXPECT_TRUE(action.userDefinedTrap().has_value());
}

// ==============================================================================
// Delete user-defined-trap
// ==============================================================================

TEST_F(
    CmdDeleteCoppTrafficPolicyMatchActionTestFixture,
    deleteUserDefinedTrap) {
  setupTestableConfigSession(
      cmdPrefix_, "acl_copp_bgp action user-defined-trap");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_bgp"});
  traffic_policy::MatchActionArg actionType({"user-defined-trap"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("user-defined-trap"));

  auto action = getActionForMatcher("acl_copp_bgp");
  EXPECT_FALSE(action.userDefinedTrap().has_value());
  EXPECT_TRUE(action.sendToQueue().has_value());
  EXPECT_TRUE(action.counter().has_value());
  EXPECT_TRUE(action.setTc().has_value());
}

// ==============================================================================
// Already-absent cases
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, alreadyAbsentCounter) {
  // acl_copp_arp has only sendToQueue, so deleting its counter is a no-op.
  setupTestableConfigSession(cmdPrefix_, "acl_copp_arp action counter");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_arp"});
  traffic_policy::MatchActionArg actionType({"counter"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("already absent"));
  EXPECT_THAT(result, HasSubstr("acl_copp_arp"));
}

// A pre-existing matcher entry whose action has no fields set (possible via
// a hand-edited config) must be pruned even though the requested field is
// already absent.
TEST_F(
    CmdDeleteCoppTrafficPolicyMatchActionTestFixture,
    alreadyAbsentPrunesEmptyEntry) {
  setupTestableConfigSession(cmdPrefix_, "acl_copp_empty action counter");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_empty"});
  traffic_policy::MatchActionArg actionType({"counter"});

  ASSERT_TRUE(hasMatcher("acl_copp_empty"));
  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("removed the empty copp traffic-policy entry"));
  EXPECT_FALSE(hasMatcher("acl_copp_empty"));
  // Sibling matchers must be untouched.
  EXPECT_TRUE(hasMatcher("acl_copp_bgp"));
  EXPECT_TRUE(hasMatcher("acl_copp_arp"));
}

// Deleting the last remaining action field must erase the matcher entry,
// not leave an empty MatchAction orphan.
TEST_F(
    CmdDeleteCoppTrafficPolicyMatchActionTestFixture,
    deleteLastActionRemovesMatcher) {
  setupTestableConfigSession(cmdPrefix_, "acl_copp_arp action send-to-queue");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_copp_arp"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  ASSERT_TRUE(hasMatcher("acl_copp_arp"));
  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_FALSE(hasMatcher("acl_copp_arp"));
  // Sibling matcher must be untouched.
  EXPECT_TRUE(hasMatcher("acl_copp_bgp"));
}

// ==============================================================================
// Error cases
// ==============================================================================

TEST_F(CmdDeleteCoppTrafficPolicyMatchActionTestFixture, matcherNotFound) {
  setupTestableConfigSession(
      cmdPrefix_, "nonexistent_matcher action send-to-queue");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"nonexistent_matcher"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  EXPECT_THROW(
      cmd.queryClient(hostInfo, matcher, actionType), std::runtime_error);
}

// Deleting when the device has no cpuTrafficPolicy at all must throw rather
// than dereference an empty optional.
class CmdDeleteCoppNoCpuTrafficPolicyFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppNoCpuTrafficPolicyFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_no_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp traffic-policy match";
};

TEST_F(CmdDeleteCoppNoCpuTrafficPolicyFixture, cpuTrafficPolicyNotConfigured) {
  setupTestableConfigSession(cmdPrefix_, "acl_x action send-to-queue");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_x"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  EXPECT_THROW(
      cmd.queryClient(hostInfo, matcher, actionType), std::runtime_error);
}

// cpuTrafficPolicy present but trafficPolicy absent must also throw.
class CmdDeleteCoppNoTrafficPolicyFixture : public CmdConfigTestBase {
 public:
  CmdDeleteCoppNoTrafficPolicyFixture()
      : CmdConfigTestBase(
            "fboss_delete_copp_no_traffic_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {"cpuTrafficPolicy": {}}})") {}

 protected:
  const std::string cmdPrefix_ = "delete copp traffic-policy match";
};

TEST_F(CmdDeleteCoppNoTrafficPolicyFixture, trafficPolicyNotConfigured) {
  setupTestableConfigSession(cmdPrefix_, "acl_x action send-to-queue");
  CmdDeleteCoppTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_x"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  EXPECT_THROW(
      cmd.queryClient(hostInfo, matcher, actionType), std::runtime_error);
}

} // namespace facebook::fboss
