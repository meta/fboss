/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

// The action-shape and value-validation coverage lives in
// CmdDeleteCoppTrafficPolicyMatchActionTest.cpp and
// CmdConfigDataPlaneTrafficPolicyTest.cpp, since both policies share
// traffic_policy::deleteAction. This file only exercises the data-plane CLI
// wrapper: that it targets dataPlaneTrafficPolicy (not cpuTrafficPolicy).

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/match/action/CmdDeleteDataPlaneTrafficPolicyMatchAction.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

class CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture
    : public CmdConfigTestBase {
 public:
  CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_dp_traffic_policy_match_action_test_%%%%-%%%%-%%%%-%%%%",
            R"({
  "sw": {
    "dataPlaneTrafficPolicy": {
      "matchToAction": [
        {
          "matcher": "acl_dp_queue0",
          "action": {
            "sendToQueue": {"queueId": 0},
            "counter": "dp_queue0_counter"
          }
        }
      ]
    }
  }
})") {}

 protected:
  const std::string cmdPrefix_ = "delete data-plane traffic-policy match";

  cfg::MatchAction getActionForMatcher(const std::string& matcherName) {
    auto& config = ConfigSession::getInstance().getAgentConfig();
    const auto& matchToActions =
        *config.sw()->dataPlaneTrafficPolicy()->matchToAction();
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
        *config.sw()->dataPlaneTrafficPolicy()->matchToAction();
    for (const auto& mta : matchToActions) {
      if (*mta.matcher() == matcherName) {
        return true;
      }
    }
    return false;
  }
};

TEST_F(
    CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture,
    deleteOneActionKeepsSiblingAndEntry) {
  setupTestableConfigSession(cmdPrefix_, "acl_dp_queue0 action send-to-queue");
  CmdDeleteDataPlaneTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");
  traffic_policy::MatcherName matcher({"acl_dp_queue0"});
  traffic_policy::MatchActionArg actionType({"send-to-queue"});

  auto result = cmd.queryClient(hostInfo, matcher, actionType);
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  EXPECT_THAT(result, HasSubstr("send-to-queue"));

  auto action = getActionForMatcher("acl_dp_queue0");
  EXPECT_FALSE(action.sendToQueue().has_value());
  EXPECT_TRUE(action.counter().has_value());
  EXPECT_EQ(*action.counter(), "dp_queue0_counter");
}

TEST_F(
    CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture,
    deleteLastActionDropsEntry) {
  setupTestableConfigSession(cmdPrefix_, "acl_dp_queue0 action send-to-queue");
  CmdDeleteDataPlaneTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");

  cmd.queryClient(
      hostInfo,
      traffic_policy::MatcherName({"acl_dp_queue0"}),
      traffic_policy::MatchActionArg({"send-to-queue"}));
  cmd.queryClient(
      hostInfo,
      traffic_policy::MatcherName({"acl_dp_queue0"}),
      traffic_policy::MatchActionArg({"counter"}));

  EXPECT_FALSE(hasMatcher("acl_dp_queue0"));
}

TEST_F(
    CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture,
    deleteAbsentActionIsReported) {
  setupTestableConfigSession(cmdPrefix_, "acl_dp_queue0 action set-tc");
  CmdDeleteDataPlaneTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");

  auto result = cmd.queryClient(
      hostInfo,
      traffic_policy::MatcherName({"acl_dp_queue0"}),
      traffic_policy::MatchActionArg({"set-tc"}));
  EXPECT_THAT(result, HasSubstr("already absent"));
  // Untouched fields survive an absent-action delete.
  EXPECT_TRUE(getActionForMatcher("acl_dp_queue0").sendToQueue().has_value());
}

TEST_F(CmdDeleteDataPlaneTrafficPolicyMatchActionTestFixture, matcherNotFound) {
  setupTestableConfigSession(
      cmdPrefix_, "nonexistent_matcher action send-to-queue");
  CmdDeleteDataPlaneTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");

  EXPECT_THROW(
      cmd.queryClient(
          hostInfo,
          traffic_policy::MatcherName({"nonexistent_matcher"}),
          traffic_policy::MatchActionArg({"send-to-queue"})),
      std::runtime_error);
}

// Deleting when the device has no dataPlaneTrafficPolicy at all must throw
// (policyFor auto-creates an empty one, then the matcher lookup misses).
class CmdDeleteDataPlaneNoTrafficPolicyFixture : public CmdConfigTestBase {
 public:
  CmdDeleteDataPlaneNoTrafficPolicyFixture()
      : CmdConfigTestBase(
            "fboss_delete_dp_no_traffic_policy_test_%%%%-%%%%-%%%%-%%%%",
            R"({"sw": {}})") {}

 protected:
  const std::string cmdPrefix_ = "delete data-plane traffic-policy match";
};

TEST_F(
    CmdDeleteDataPlaneNoTrafficPolicyFixture,
    dataPlaneTrafficPolicyNotConfigured) {
  setupTestableConfigSession(cmdPrefix_, "acl_x action send-to-queue");
  CmdDeleteDataPlaneTrafficPolicyMatchAction cmd;
  HostInfo hostInfo("testhost");

  EXPECT_THROW(
      cmd.queryClient(
          hostInfo,
          traffic_policy::MatcherName({"acl_x"}),
          traffic_policy::MatchActionArg({"send-to-queue"})),
      std::runtime_error);
}

} // namespace facebook::fboss
