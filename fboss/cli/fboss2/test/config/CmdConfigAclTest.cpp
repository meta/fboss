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
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/acl/AclConfigUtils.h"
#include "fboss/cli/fboss2/commands/config/acl/table/CmdConfigAclTable.h"
#include "fboss/cli/fboss2/commands/config/acl/table_group/CmdConfigAclTableGroup.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors an RSW device's aclTableGroups (simplified subset). Two
// groups, the first with two tables, so queryClient() tests can assert that
// mutating the targeted group/table leaves its siblings untouched -- with
// only one group/table, a `find_if` bug that matched "first element" instead
// of "match by name" would pass undetected.
static constexpr auto kSeedConfig = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "true"},
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1",
        "priority": 0,
        "aclEntries": [],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }, {
        "name": "AclTable2",
        "priority": 1,
        "aclEntries": [],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }]
    }, {
      "name": "egress-ACL-Table-Group",
      "stage": 2,
      "aclTables": [{
        "name": "AclTable3",
        "priority": 0,
        "aclEntries": [],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }]
    }]
  }
})";

// Seed that never opted into the table-group path: no flag, no groups. Both
// commands must refuse rather than write config the agent would ignore
// (FLAGS_enable_acl_table_group defaults to false).
static constexpr auto kSeedConfigNoAclTableGroups = R"({
  "sw": {}
})";

// Same, but with the flag explicitly off rather than absent.
static constexpr auto kSeedConfigFlagOff = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "false"},
  "sw": {}
})";

// The bootstrap shape: the agent config opts into the table-group path but no
// group exists yet. `config acl table-group` must be able to create the first
// one, or a fresh box could never reach ACL config through the CLI.
static constexpr auto kSeedConfigFlagOnNoGroups = R"({
  "defaultCommandLineArgs": {"enable_acl_table_group": "true"},
  "sw": {}
})";

class CmdConfigAclTableGroupTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclTableGroupTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_table_group_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}
};

class CmdConfigAclTableTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclTableTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_table_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}
};

class CmdConfigAclNoGroupsTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclNoGroupsTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_no_groups_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfigNoAclTableGroups) {}
};

class CmdConfigAclFlagOffTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclFlagOffTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_flag_off_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfigFlagOff) {}
};

class CmdConfigAclBootstrapTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclBootstrapTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_bootstrap_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfigFlagOnNoGroups) {}
};

// =============================================================
// AclTableGroupConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigAclTableGroupTestFixture, argValidation_valid) {
  AclTableGroupConfigArgs a({"ingress-ACL-Table-Group", "stage", "ingress"});
  EXPECT_EQ(a.getGroupName(), "ingress-ACL-Table-Group");
  EXPECT_EQ(a.getStage(), cfg::AclStage::INGRESS);

  AclTableGroupConfigArgs b(
      {"ingress-ACL-Table-Group", "stage", "ingress-macsec"});
  EXPECT_EQ(b.getStage(), cfg::AclStage::INGRESS_MACSEC);

  AclTableGroupConfigArgs c(
      {"ingress-ACL-Table-Group", "stage", "egress-macsec"});
  EXPECT_EQ(c.getStage(), cfg::AclStage::EGRESS_MACSEC);

  AclTableGroupConfigArgs d(
      {"ingress-ACL-Table-Group", "stage", "ingress-post-lookup"});
  EXPECT_EQ(d.getStage(), cfg::AclStage::INGRESS_POST_LOOKUP);
}

TEST_F(CmdConfigAclTableGroupTestFixture, argValidation_stageNamesOnly) {
  // Bare enum numbers are not a spelling: they parse into the thrift enum but
  // say nothing at the call site about which stage they select.
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage", "0"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage", "3"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableGroupTestFixture, argValidation_badArity) {
  EXPECT_THROW(AclTableGroupConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs(
          {"ingress-ACL-Table-Group", "stage", "ingress", "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableGroupTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(
      AclTableGroupConfigArgs(
          {"ingress-ACL-Table-Group", "priority", "ingress"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableGroupTestFixture, argValidation_invalidStage) {
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage", "egress"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage", "4"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableGroupConfigArgs({"ingress-ACL-Table-Group", "stage", "abc"}),
      std::invalid_argument);
}

// =============================================================
// queryClient() tests for table-group
// =============================================================

TEST_F(CmdConfigAclTableGroupTestFixture, refusesMovingGroupToAnotherStage) {
  // updateAclTableGroups looks the original group up by stage
  // (getNodeIf(*cfgAclTableGroup.stage())), so a move reads as one group
  // disappearing and another appearing. Committing that on a TH4 device
  // restarted the agent and left the stage unchanged, so refuse it here.
  setupTestableConfigSession(
      "config acl table-group",
      "ingress-ACL-Table-Group stage ingress-post-lookup");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args(
      {"ingress-ACL-Table-Group", "stage", "ingress-post-lookup"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(*groups[0].stage(), cfg::AclStage::INGRESS);
  EXPECT_EQ(*groups[1].stage(), cfg::AclStage::EGRESS_MACSEC);
}

TEST_F(CmdConfigAclTableGroupTestFixture, sameStageIsANoop) {
  setupTestableConfigSession(
      "config acl table-group", "ingress-ACL-Table-Group stage ingress");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"ingress-ACL-Table-Group", "stage", "ingress"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("already at stage"));

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(*groups[0].stage(), cfg::AclStage::INGRESS);
}

TEST_F(CmdConfigAclTableGroupTestFixture, createsGroupOnFreeStage) {
  // The seed occupies stage 0 and stage 2; ingress-macsec (1) is free.
  setupTestableConfigSession(
      "config acl table-group", "macsec-group stage ingress-macsec");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"macsec-group", "stage", "ingress-macsec"});

  auto msg = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(msg, HasSubstr("Created"));

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  auto it = std::find_if(
      groups.begin(), groups.end(), [](const cfg::AclTableGroup& g) {
        return *g.name() == "macsec-group";
      });
  ASSERT_NE(it, groups.end());
  EXPECT_EQ(*it->stage(), cfg::AclStage::INGRESS_MACSEC);
  EXPECT_TRUE(it->aclTables()->empty());
}

TEST_F(CmdConfigAclTableGroupTestFixture, refusesSecondGroupOnOccupiedStage) {
  // AclTableGroupMap is keyed by stage, so a second group at ingress would
  // silently replace the existing one rather than coexist with it.
  setupTestableConfigSession(
      "config acl table-group", "another-ingress stage ingress");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"another-ingress", "stage", "ingress"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(*groups[0].name(), "ingress-ACL-Table-Group");
  EXPECT_EQ(*groups[1].name(), "egress-ACL-Table-Group");
}

TEST_F(CmdConfigAclNoGroupsTestFixture, tableGroupNoAclTableGroupsConfigured) {
  setupTestableConfigSession(
      "config acl table-group", "ingress-ACL-Table-Group stage ingress");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"ingress-ACL-Table-Group", "stage", "ingress"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);
}

TEST_F(CmdConfigAclFlagOffTestFixture, tableGroupRefusedWhenFlagOff) {
  setupTestableConfigSession(
      "config acl table-group", "ingress-ACL-Table-Group stage ingress");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"ingress-ACL-Table-Group", "stage", "ingress"});

  try {
    cmd.queryClient(hostInfo, args);
    FAIL() << "expected the flag gate to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("enable_acl_table_group"));
  }
}

TEST_F(CmdConfigAclBootstrapTestFixture, tableGroupBootstrapsFirstGroup) {
  setupTestableConfigSession(
      "config acl table-group", "ingress-ACL-Table-Group stage ingress");
  CmdConfigAclTableGroup cmd;
  HostInfo hostInfo("testhost");
  AclTableGroupConfigArgs args({"ingress-ACL-Table-Group", "stage", "ingress"});

  auto msg = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(msg, HasSubstr("Created"));

  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  ASSERT_TRUE(swConfig.aclTableGroups().has_value());
  ASSERT_EQ(swConfig.aclTableGroups()->size(), 1);
  const auto& group = swConfig.aclTableGroups()->front();
  EXPECT_EQ(*group.name(), "ingress-ACL-Table-Group");
  EXPECT_EQ(*group.stage(), cfg::AclStage::INGRESS);
  EXPECT_TRUE(group.aclTables()->empty());
}

// =============================================================
// AclTableConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigAclTableTestFixture, argValidation_valid) {
  AclTableConfigArgs a(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "5"});
  EXPECT_EQ(a.getGroupName(), "ingress-ACL-Table-Group");
  EXPECT_EQ(a.getTableName(), "AclTable1");
  EXPECT_EQ(a.getPriority(), 5);

  AclTableConfigArgs b(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "0"});
  EXPECT_EQ(b.getPriority(), 0);
}

TEST_F(CmdConfigAclTableTestFixture, argValidation_badArity) {
  EXPECT_THROW(AclTableConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(AclTableConfigArgs({"AclTable1"}), std::invalid_argument);
  EXPECT_THROW(
      AclTableConfigArgs({"AclTable1", "group"}), std::invalid_argument);
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1", "group", "ingress-ACL-Table-Group", "priority"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1",
           "group",
           "ingress-ACL-Table-Group",
           "priority",
           "1",
           "extra"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableTestFixture, argValidation_missingGroupKeyword) {
  // The old form put the group first with no keyword; it must not silently
  // parse as the new one.
  EXPECT_THROW(
      AclTableConfigArgs(
          {"ingress-ACL-Table-Group", "AclTable1", "priority", "1", "x"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1", "group", "ingress-ACL-Table-Group", "stage", "1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableTestFixture, argValidation_nonInteger) {
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "abc"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1",
           "group",
           "ingress-ACL-Table-Group",
           "priority",
           "3.14"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclTableTestFixture, argValidation_negative) {
  EXPECT_THROW(
      AclTableConfigArgs(
          {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "-1"}),
      std::invalid_argument);
}

// =============================================================
// queryClient() tests for table
// =============================================================

TEST_F(CmdConfigAclTableTestFixture, setPriority) {
  setupTestableConfigSession(
      "config acl table", "ingress-ACL-Table-Group AclTable1 priority 10");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "10"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("AclTable1"));
  EXPECT_THAT(result, HasSubstr("priority"));
  EXPECT_THAT(result, HasSubstr("10"));

  auto& config = ConfigSession::getInstance().getAgentConfig();
  auto& groups = *config.sw()->aclTableGroups();
  ASSERT_EQ(groups.size(), 2u);
  auto& tables = *groups[0].aclTables();
  ASSERT_EQ(tables.size(), 2u);
  EXPECT_EQ(*tables[0].name(), "AclTable1");
  EXPECT_EQ(*tables[0].priority(), 10);
  // The sibling table in the same group must be untouched.
  EXPECT_EQ(*tables[1].name(), "AclTable2");
  EXPECT_EQ(*tables[1].priority(), 1);
  // The table in the sibling group must also be untouched.
  auto& otherGroupTables = *groups[1].aclTables();
  ASSERT_EQ(otherGroupTables.size(), 1u);
  EXPECT_EQ(*otherGroupTables[0].priority(), 0);
}

// Targets the second (non-first) table by name, within a group that also
// isn't first, so a lookup bug that always matched the first element
// regardless of name would be caught here (setPriority above only ever
// mutates index 0 of both the group and table lists).
TEST_F(CmdConfigAclTableTestFixture, setPriorityOnNonFirstTable) {
  setupTestableConfigSession(
      "config acl table", "ingress-ACL-Table-Group AclTable2 priority 20");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable2", "group", "ingress-ACL-Table-Group", "priority", "20"});

  auto result = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(result, HasSubstr("AclTable2"));

  auto& config = ConfigSession::getInstance().getAgentConfig();
  auto& tables = *config.sw()->aclTableGroups()->at(0).aclTables();
  ASSERT_EQ(tables.size(), 2u);
  // The first table must be untouched.
  EXPECT_EQ(*tables[0].name(), "AclTable1");
  EXPECT_EQ(*tables[0].priority(), 0);
  EXPECT_EQ(*tables[1].name(), "AclTable2");
  EXPECT_EQ(*tables[1].priority(), 20);
}

TEST_F(CmdConfigAclTableTestFixture, createsTableWhenAbsent) {
  setupTestableConfigSession(
      "config acl table", "new-table group ingress-ACL-Table-Group priority 7");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"new-table", "group", "ingress-ACL-Table-Group", "priority", "7"});

  auto msg = cmd.queryClient(hostInfo, args);
  EXPECT_THAT(msg, HasSubstr("Created"));

  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  const auto& tables = *groups[0].aclTables();
  auto it =
      std::find_if(tables.begin(), tables.end(), [](const cfg::AclTable& t) {
        return *t.name() == "new-table";
      });
  ASSERT_NE(it, tables.end());
  EXPECT_EQ(*it->priority(), 7);
  // Left empty on purpose: SAI expands an empty list to the ASIC default set.
  EXPECT_TRUE(it->actionTypes()->empty());
  EXPECT_TRUE(it->qualifiers()->empty());
  EXPECT_TRUE(it->udfGroups()->empty());
}

TEST_F(CmdConfigAclTableTestFixture, refusesTableNameUsedInAnotherGroup) {
  // AclTable3 lives in the egress group; acl rule commands resolve table names
  // across all groups, so a duplicate would make those lookups ambiguous.
  setupTestableConfigSession(
      "config acl table", "AclTable3 group ingress-ACL-Table-Group priority 1");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable3", "group", "ingress-ACL-Table-Group", "priority", "1"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);

  // The refusal must leave the config unmutated: no table added anywhere.
  auto& groups =
      *ConfigSession::getInstance().getAgentConfig().sw()->aclTableGroups();
  ASSERT_EQ(groups.size(), 2u);
  EXPECT_EQ(groups[0].aclTables()->size(), 2u);
  EXPECT_EQ(groups[1].aclTables()->size(), 1u);
}

TEST_F(CmdConfigAclTableTestFixture, groupNotFound) {
  setupTestableConfigSession(
      "config acl table", "no-such-group AclTable1 priority 1");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable1", "group", "no-such-group", "priority", "1"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);
}

// The table command behind the same gate: explicit "false" refuses with the
// opt-in line in the message.
TEST_F(CmdConfigAclFlagOffTestFixture, tableRefusedWhenFlagOff) {
  setupTestableConfigSession(
      "config acl table", "AclTable1 group ingress-ACL-Table-Group priority 1");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "1"});

  try {
    cmd.queryClient(hostInfo, args);
    FAIL() << "expected the flag gate to throw";
  } catch (const std::runtime_error& e) {
    EXPECT_THAT(e.what(), HasSubstr("enable_acl_table_group"));
  }
}

// Past the gate (flag on) but no group exists yet: the table command does not
// bootstrap groups, so the named-group lookup must throw.
TEST_F(CmdConfigAclBootstrapTestFixture, tableFlagOnButGroupMissing) {
  setupTestableConfigSession(
      "config acl table", "AclTable1 group ingress-ACL-Table-Group priority 1");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "1"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);
}

// Neither flag nor groups: refused at the gate (flag absent reads as off).
TEST_F(CmdConfigAclNoGroupsTestFixture, tableNoAclTableGroupsConfigured) {
  setupTestableConfigSession(
      "config acl table", "ingress-ACL-Table-Group AclTable1 priority 1");
  CmdConfigAclTable cmd;
  HostInfo hostInfo("testhost");
  AclTableConfigArgs args(
      {"AclTable1", "group", "ingress-ACL-Table-Group", "priority", "1"});

  EXPECT_THROW(cmd.queryClient(hostInfo, args), std::runtime_error);
}

// =============================================================
// acl_utils::resolveAclTable() — the one place table names are resolved
// =============================================================

namespace {
cfg::SwitchConfig makeConfig(
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        groupsAndTables) {
  cfg::SwitchConfig sw;
  std::vector<cfg::AclTableGroup> groups;
  for (const auto& [groupName, tableNames] : groupsAndTables) {
    cfg::AclTableGroup group;
    group.name() = groupName;
    std::vector<cfg::AclTable> tables;
    for (const auto& tableName : tableNames) {
      cfg::AclTable table;
      table.name() = tableName;
      tables.push_back(std::move(table));
    }
    group.aclTables() = std::move(tables);
    groups.push_back(std::move(group));
  }
  sw.aclTableGroups() = std::move(groups);
  return sw;
}
} // namespace

TEST(AclResolveTableTest, throwsWhenNoAclTableGroups) {
  // A config with no aclTableGroups is on the flat sw.acls path, where
  // anything we write under aclTableGroups is ignored by the agent.
  cfg::SwitchConfig sw;
  EXPECT_THROW(
      acl_utils::resolveAclTable(sw, std::string("AclTable1")),
      std::runtime_error);
  EXPECT_THROW(
      acl_utils::resolveAclTable(sw, std::nullopt), std::runtime_error);
}

TEST(AclResolveTableTest, findsNamedTableAndItsGroup) {
  auto sw = makeConfig({{"grp", {"AclTable1"}}});
  auto [table, groupName] =
      acl_utils::resolveAclTable(sw, std::string("AclTable1"));
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(*table->name(), "AclTable1");
  EXPECT_EQ(groupName, "grp");
}

TEST(AclResolveTableTest, findsNamedTableInASecondGroup) {
  auto sw = makeConfig({{"a", {"T1"}}, {"b", {"T2"}}});
  auto [table, groupName] = acl_utils::resolveAclTable(sw, std::string("T2"));
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(groupName, "b");
}

TEST(AclResolveTableTest, throwsWhenNamedTableMissing) {
  auto sw = makeConfig({{"grp", {"AclTable1"}}});
  EXPECT_THROW(
      acl_utils::resolveAclTable(sw, std::string("nope")), std::runtime_error);
}

TEST(AclResolveTableTest, omittedNameResolvesTheSingleTable) {
  // The shape a stock config ships: one group holding one table.
  auto sw = makeConfig({{"ingress-ACL-Table-Group", {"AclTable1"}}});
  auto [table, groupName] = acl_utils::resolveAclTable(sw, std::nullopt);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(*table->name(), "AclTable1");
  EXPECT_EQ(groupName, "ingress-ACL-Table-Group");
}

TEST(AclResolveTableTest, omittedNameIsAmbiguousWithTwoTables) {
  auto sw = makeConfig({{"a", {"T1"}}, {"b", {"T2"}}});
  EXPECT_THROW(
      acl_utils::resolveAclTable(sw, std::nullopt), std::runtime_error);
}

TEST(AclResolveTableTest, throwsWhenGroupsExistButHoldNoTables) {
  auto sw = makeConfig({{"grp", {}}});
  EXPECT_THROW(
      acl_utils::resolveAclTable(sw, std::nullopt), std::runtime_error);
}

} // namespace facebook::fboss
