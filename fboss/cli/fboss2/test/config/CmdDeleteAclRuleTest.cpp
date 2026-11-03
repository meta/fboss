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
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/acl/rule/CmdDeleteAclRule.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed: two rules in the same table. rule-1 also has a MatchAction attached
// via dataPlaneTrafficPolicy.matchToAction (setDscp). The deleteRuleWithAction
// test verifies the matchToAction is removed too — otherwise it would dangle
// and ApplyThriftConfig's checkTrafficPolicyAclsExistInConfig would reject the
// committed config.
static constexpr auto kSeedConfig = R"({
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1",
        "priority": 0,
        "aclEntries": [
          {
            "name": "rule-1",
            "actionType": 1,
            "dscp": 46
          },
          {
            "name": "rule-2",
            "actionType": 1,
            "dscp": 10
          }
        ],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }]
    }],
    "dataPlaneTrafficPolicy": {
      "matchToAction": [
        {
          "matcher": "rule-1",
          "action": {"setDscp": {"dscpValue": 46}}
        },
        {
          "matcher": "rule-2",
          "action": {"setDscp": {"dscpValue": 10}}
        }
      ]
    }
  }
})";

class CmdDeleteAclRuleTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteAclRuleTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_rule_delete_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  const std::string cmdPrefix_ = "delete acl rule";

  std::vector<cfg::AclEntry>& getEntries() {
    auto& cfg = ConfigSession::getInstance().getAgentConfig();
    auto& tables = *(*cfg.sw()->aclTableGroups())[0].aclTables();
    return *tables[0].aclEntries();
  }

  bool hasRule(const std::string& name) {
    auto& entries = getEntries();
    return std::any_of(
        entries.begin(), entries.end(), [&](const cfg::AclEntry& e) {
          return *e.name() == name;
        });
  }

  bool hasMatchToAction(const std::string& ruleName) {
    auto& cfg = ConfigSession::getInstance().getAgentConfig();
    auto policy = cfg.sw()->dataPlaneTrafficPolicy();
    if (!policy) {
      return false;
    }
    const auto& mtaList = *policy->matchToAction();
    return std::any_of(
        mtaList.begin(), mtaList.end(), [&](const cfg::MatchToAction& mta) {
          return *mta.matcher() == ruleName;
        });
  }
};

// =============================================================
// AclRuleDeleteArgs validation tests
// =============================================================

TEST_F(CmdDeleteAclRuleTestFixture, argValidation_badArity) {
  EXPECT_THROW(AclRuleDeleteArgs({}), std::invalid_argument);
  EXPECT_THROW(AclRuleDeleteArgs({"AclTable1"}), std::invalid_argument);
  EXPECT_THROW(
      AclRuleDeleteArgs({"AclTable1", "rule-1", "dscp"}),
      std::invalid_argument);
}

TEST_F(CmdDeleteAclRuleTestFixture, argValidation_validArity) {
  EXPECT_NO_THROW(AclRuleDeleteArgs({"AclTable1", "rule-1"}));
}

// =============================================================
// queryClient() tests
// =============================================================

TEST_F(CmdDeleteAclRuleTestFixture, ruleNotFoundIsNoop) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 nope");
  const auto sessionBefore = sessionConfigText();
  CmdDeleteAclRule cmd;
  HostInfo host("testhost");
  AclRuleDeleteArgs args({"AclTable1", "nope"});
  EXPECT_THAT(
      cmd.queryClient(host, args), HasSubstr("not found in table 'AclTable1'"));
  // The existing rules are untouched.
  EXPECT_TRUE(hasRule("rule-1"));
  EXPECT_TRUE(hasRule("rule-2"));
  EXPECT_EQ(sessionConfigText(), sessionBefore)
      << "a no-op delete must not save the config";
}

TEST_F(CmdDeleteAclRuleTestFixture, tableNotFound) {
  setupTestableConfigSession(cmdPrefix_, "Bogus rule-1");
  CmdDeleteAclRule cmd;
  HostInfo host("testhost");
  AclRuleDeleteArgs args({"Bogus", "rule-1"});
  EXPECT_THROW(cmd.queryClient(host, args), std::runtime_error);
}

TEST_F(CmdDeleteAclRuleTestFixture, deleteRule) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1");
  ASSERT_TRUE(hasRule("rule-1"));
  ASSERT_TRUE(hasRule("rule-2"));

  CmdDeleteAclRule cmd;
  HostInfo host("testhost");
  AclRuleDeleteArgs args({"AclTable1", "rule-1"});
  EXPECT_NO_THROW(cmd.queryClient(host, args));

  EXPECT_FALSE(hasRule("rule-1"));
  EXPECT_TRUE(hasRule("rule-2"));
}

TEST_F(CmdDeleteAclRuleTestFixture, deleteRuleAlsoDropsMatchToAction) {
  // rule-1 has a MatchAction attached in the seed; deleting the rule must
  // also drop the MatchToAction. Otherwise ApplyThriftConfig rejects the
  // committed config (matcher references a non-existent AclEntry) and the
  // stale action would silently re-attach on rule re-creation.
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1");
  ASSERT_TRUE(hasMatchToAction("rule-1"));
  ASSERT_TRUE(hasMatchToAction("rule-2"));

  CmdDeleteAclRule cmd;
  HostInfo host("testhost");
  AclRuleDeleteArgs args({"AclTable1", "rule-1"});
  EXPECT_NO_THROW(cmd.queryClient(host, args));

  EXPECT_FALSE(hasMatchToAction("rule-1"));
  EXPECT_TRUE(hasMatchToAction("rule-2"));
}

} // namespace facebook::fboss
