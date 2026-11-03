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
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/delete/acl/table/CmdDeleteAclTable.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Two groups. The ingress one holds two tables; AclTable1's rules are named by
// both traffic policies, so deleting it has to cascade into each. AclTable3
// sits in the second group so the "last group" guard can be exercised without
// tripping first.
static constexpr auto kSeedConfig = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "true"},
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1",
        "priority": 0,
        "aclEntries": [
          {"name": "dp-rule", "actionType": 1},
          {"name": "cpu-rule", "actionType": 1}
        ],
        "actionTypes": [], "qualifiers": [], "udfGroups": []
      }, {
        "name": "AclTable2",
        "priority": 1,
        "aclEntries": [{"name": "other-rule", "actionType": 1}],
        "actionTypes": [], "qualifiers": [], "udfGroups": []
      }]
    }, {
      "name": "egress-ACL-Table-Group",
      "stage": 2,
      "aclTables": [{
        "name": "AclTable3",
        "priority": 0,
        "aclEntries": [],
        "actionTypes": [], "qualifiers": [], "udfGroups": []
      }]
    }],
    "dataPlaneTrafficPolicy": {
      "matchToAction": [
        {"matcher": "dp-rule", "action": {"setDscp": {"dscpValue": 46}}},
        {"matcher": "other-rule", "action": {"setDscp": {"dscpValue": 10}}}
      ]
    },
    "cpuTrafficPolicy": {
      "trafficPolicy": {
        "matchToAction": [
          {"matcher": "cpu-rule", "action": {"sendToQueue": {"queueId": 9}}}
        ]
      }
    }
  }
})";

class CmdDeleteAclTableTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteAclTableTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_table_delete_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  cfg::SwitchConfig& sw() {
    return *ConfigSession::getInstance().getAgentConfig().sw();
  }

  bool hasTable(const std::string& name) {
    for (const auto& group : *sw().aclTableGroups()) {
      const auto& tables = *group.aclTables();
      if (std::any_of(
              tables.begin(), tables.end(), [&](const cfg::AclTable& t) {
                return *t.name() == name;
              })) {
        return true;
      }
    }
    return false;
  }

  bool hasGroup(const std::string& name) {
    const auto& groups = *sw().aclTableGroups();
    return std::any_of(
        groups.begin(), groups.end(), [&](const cfg::AclTableGroup& g) {
          return *g.name() == name;
        });
  }

  bool hasDataPlaneMatcher(const std::string& rule) {
    auto policy = sw().dataPlaneTrafficPolicy();
    if (!policy) {
      return false;
    }
    const auto& list = *policy->matchToAction();
    return std::any_of(
        list.begin(), list.end(), [&](const cfg::MatchToAction& mta) {
          return *mta.matcher() == rule;
        });
  }

  bool hasCpuMatcher(const std::string& rule) {
    auto cpu = sw().cpuTrafficPolicy();
    if (!cpu || !cpu->trafficPolicy()) {
      return false;
    }
    const auto& list = *cpu->trafficPolicy()->matchToAction();
    return std::any_of(
        list.begin(), list.end(), [&](const cfg::MatchToAction& mta) {
          return *mta.matcher() == rule;
        });
  }
};

// =============================================================
// Argument validation
// =============================================================

TEST_F(CmdDeleteAclTableTestFixture, argValidation) {
  EXPECT_THROW(AclTableDeleteArgs({}), std::invalid_argument);
  EXPECT_THROW(
      AclTableDeleteArgs({"AclTable1", "extra"}), std::invalid_argument);
  EXPECT_EQ(AclTableDeleteArgs({"AclTable1"}).getTableName(), "AclTable1");
}

// =============================================================
// delete acl table
// =============================================================

TEST_F(CmdDeleteAclTableTestFixture, deleteTableCascadesIntoBothPolicies) {
  setupTestableConfigSession("delete acl table", "AclTable1");
  ASSERT_TRUE(hasDataPlaneMatcher("dp-rule"));
  ASSERT_TRUE(hasCpuMatcher("cpu-rule"));

  CmdDeleteAclTable cmd;
  HostInfo host("testhost");
  auto msg = cmd.queryClient(host, AclTableDeleteArgs({"AclTable1"}));
  EXPECT_THAT(msg, HasSubstr("AclTable1"));

  EXPECT_FALSE(hasTable("AclTable1"));
  // Both matchers went with the rules; leaving either would fail
  // checkTrafficPolicyAclsExistInConfig at commit.
  EXPECT_FALSE(hasDataPlaneMatcher("dp-rule"));
  EXPECT_FALSE(hasCpuMatcher("cpu-rule"));

  // The sibling table and its matcher are untouched.
  EXPECT_TRUE(hasTable("AclTable2"));
  EXPECT_TRUE(hasDataPlaneMatcher("other-rule"));
}

TEST_F(CmdDeleteAclTableTestFixture, deleteTableInASecondGroup) {
  setupTestableConfigSession("delete acl table", "AclTable3");
  CmdDeleteAclTable cmd;
  HostInfo host("testhost");
  cmd.queryClient(host, AclTableDeleteArgs({"AclTable3"}));

  EXPECT_FALSE(hasTable("AclTable3"));
  // The now-empty group survives; only `delete acl table-group` removes it.
  EXPECT_TRUE(hasGroup("egress-ACL-Table-Group"));
}

TEST_F(CmdDeleteAclTableTestFixture, deleteMissingTableThrows) {
  setupTestableConfigSession("delete acl table", "nope");
  CmdDeleteAclTable cmd;
  HostInfo host("testhost");
  EXPECT_THROW(
      cmd.queryClient(host, AclTableDeleteArgs({"nope"})), std::runtime_error);
}

// =============================================================
// delete acl table-group
// =============================================================

// A config with no aclTableGroups at all: both delete commands must throw
// (there is nothing to delete, and dereferencing the unset field would
// otherwise crash) and must not invent the field as a side effect.
static constexpr auto kSeedConfigEmpty = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "true"},
  "sw": {}
})";

class CmdDeleteAclEmptyConfigTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteAclEmptyConfigTestFixture()
      : CmdConfigTestBase(
            "fboss_delete_acl_empty_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfigEmpty) {}
};

TEST_F(CmdDeleteAclEmptyConfigTestFixture, deleteTableOnEmptyConfigThrows) {
  setupTestableConfigSession("delete acl table", "AclTable1");
  CmdDeleteAclTable cmd;
  HostInfo host("testhost");
  EXPECT_THROW(
      cmd.queryClient(host, AclTableDeleteArgs({"AclTable1"})),
      std::runtime_error);
  EXPECT_FALSE(
      ConfigSession::getInstance()
          .getAgentConfig()
          .sw()
          ->aclTableGroups()
          .has_value());
}

// A single table config-wide, so deleting it would drop all classification.
static constexpr auto kSeedOneTable = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "true"},
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1", "priority": 0, "aclEntries": [],
        "actionTypes": [], "qualifiers": [], "udfGroups": []
      }]
    }]
  }
})";

class CmdDeleteAclOneTableTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteAclOneTableTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_last_table_delete_test_%%%%-%%%%-%%%%-%%%%",
            kSeedOneTable) {}
};

TEST_F(CmdDeleteAclOneTableTestFixture, refusesDeletingTheLastTable) {
  setupTestableConfigSession("delete acl table", "AclTable1");
  CmdDeleteAclTable cmd;
  HostInfo host("testhost");
  EXPECT_THROW(
      cmd.queryClient(host, AclTableDeleteArgs({"AclTable1"})),
      std::runtime_error);

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  EXPECT_EQ(groups[0].aclTables()->size(), 1u);
}

} // namespace facebook::fboss
