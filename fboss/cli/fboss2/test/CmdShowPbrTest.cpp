// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/show/pbr/CmdShowPbr.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

namespace {

Policy makePolicy(
    const std::string& name,
    const std::string& defaultNhg,
    const std::map<ForwardingClass, std::string>& classToNhg) {
  ClassBasedPolicy cbfPolicy;
  cbfPolicy.name() = name;
  cbfPolicy.defaultNexthopGroup() = defaultNhg;
  cbfPolicy.class2NamedNextHopGroup() = classToNhg;
  Policy policy;
  policy.cbfPolicy() = std::move(cbfPolicy);
  return policy;
}

AclEntryThrift makeAclEntry(
    const std::string& name,
    const std::string& counterName) {
  AclEntryThrift entry;
  entry.name() = name;
  entry.counterName() = counterName;
  return entry;
}

} // namespace

class CmdShowPbrTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<Policy> policies;
  AclTableThrift aclTables;
  std::map<std::string, int64_t> counters;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    policies = {
        makePolicy(
            "installed",
            "gold_a",
            {{ForwardingClass::CLASS_2, "silver_a"},
             {ForwardingClass::CLASS_3, "bronze_a"}}),
        makePolicy(
            "temp_entry_only",
            "gold_b",
            {{ForwardingClass::CLASS_2, "silver_b"}}),
        makePolicy("no_classes", "gold_c", {}),
    };
    aclTables.aclTableEntries()["PbrAclTable"] = {
        makeAclEntry("installed_tc2", "counter_a2"),
        makeAclEntry("installed_tc3", "counter_a3"),
        makeAclEntry("#temp_entry_only_tc2", "counter_b2")};
    aclTables.aclTableEntries()["AclTable1"] = {
        makeAclEntry("temp_entry_only_tc2", "counter_b2")};
    counters = {
        {"counter_a2.packets.sum", 100},
        {"counter_a2.bytes.sum", 6400},
        {"counter_a2.packets.sum.60", 1},
        {"counter_b2.packets.sum", 7},
    };
  }
};

TEST_F(CmdShowPbrTestFixture, createModel) {
  auto model = CmdShowPbr().createModel(policies, aclTables, counters);

  ASSERT_EQ(model.policies()->size(), 3);
  const auto& installed = model.policies()->at(0);
  EXPECT_EQ(*installed.name(), "installed");
  EXPECT_EQ(*installed.defaultNextHopGroup(), "gold_a");
  EXPECT_TRUE(*installed.programmed());
  ASSERT_EQ(installed.classes()->size(), 2);
  EXPECT_EQ(*installed.classes()->at(0).forwardingClass(), "CLASS_2");
  EXPECT_EQ(*installed.classes()->at(0).redirectNextHopGroup(), "silver_a");
  EXPECT_EQ(*installed.classes()->at(0).aclEntry(), "installed_tc2");
  EXPECT_EQ(installed.classes()->at(0).packets(), 100);
  EXPECT_EQ(installed.classes()->at(0).bytes(), 6400);
  EXPECT_EQ(*installed.classes()->at(1).aclEntry(), "installed_tc3");
  EXPECT_FALSE(installed.classes()->at(1).packets().has_value());

  const auto& tempEntryOnly = model.policies()->at(1);
  EXPECT_FALSE(*tempEntryOnly.programmed());
  EXPECT_FALSE(tempEntryOnly.classes()->at(0).packets().has_value());

  const auto& noClasses = model.policies()->at(2);
  EXPECT_FALSE(*noClasses.programmed());
  EXPECT_TRUE(noClasses.classes()->empty());
}

TEST_F(CmdShowPbrTestFixture, createModelWithoutPbrAclTableThrows) {
  EXPECT_THROW(
      CmdShowPbr().createModel(policies, AclTableThrift(), counters),
      std::runtime_error);
}

TEST_F(CmdShowPbrTestFixture, printOutput) {
  auto cmd = CmdShowPbr();
  std::stringstream ss;
  cmd.printOutput(cmd.createModel(policies, aclTables, counters), ss);

  std::string expectOutput =
      " Policy           Default NHG  Programmed  Class    Redirect NHG  ACL Entry            Packets  Bytes \n"
      "---------------------------------------------------------------------------------------------------------------\n"
      " installed        gold_a       yes         CLASS_2  silver_a      installed_tc2        100      6400  \n"
      " installed        gold_a       yes         CLASS_3  bronze_a      installed_tc3        --       --    \n"
      " temp_entry_only  gold_b       no          CLASS_2  silver_b      temp_entry_only_tc2  --       --    \n"
      " no_classes       gold_c       no          --       --            --                   --       --    \n\n";
  EXPECT_EQ(ss.str(), expectOutput);
}

TEST_F(CmdShowPbrTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowPbrTraits::description().empty());
  EXPECT_FALSE(CmdShowPbr::sampleModel().policies()->empty());
}

} // namespace facebook::fboss
