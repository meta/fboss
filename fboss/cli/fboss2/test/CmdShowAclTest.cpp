// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <folly/IPAddressV4.h>
#include "fboss/cli/fboss2/commands/show/acl/CmdShowAcl.h"
#include "fboss/cli/fboss2/commands/show/acl/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {

/*
 * Set up test data
 */
std::vector<facebook::fboss::AclEntryThrift> createAclEntries(
    std::string aclTableName) {
  facebook::fboss::AclEntryThrift aclEntry1;
  aclEntry1.name() =
      folly::to<std::string>(aclTableName, "_cpuPolicing-CPU-Port-Mcast-v6");
  aclEntry1.priority() = 7;
  aclEntry1.actionType() = "permit";

  fboss::AclEntryThrift aclEntry2;
  aclEntry2.name() = folly::to<std::string>(
      aclTableName, "_cpuPolicing-high-BGPDstPort-dstLocalIp4");
  aclEntry2.priority() = 2;
  aclEntry2.l4DstPort() = 179;
  aclEntry2.actionType() = "permit";

  fboss::AclEntryThrift aclEntry3;
  aclEntry3.name() = folly::to<std::string>(
      aclTableName, "_cpuPolicing-high-slow-protocols-mac");
  aclEntry3.priority() = 14;
  aclEntry3.dstMac() = "01:80:c2:00:00:02";
  aclEntry3.lookupClassPort() = cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;
  aclEntry3.actionType() = "permit";

  return {aclEntry1, aclEntry2, aclEntry3};
}

std::map<std::string, std::vector<AclEntryThrift>> createAclTableEntries() {
  std::map<std::string, std::vector<AclEntryThrift>> aclTableEntries;
  aclTableEntries["aclTable1"] = createAclEntries("aclTable1");
  aclTableEntries["aclTable2"] = createAclEntries("aclTable2");
  return aclTableEntries;
}

class CmdShowAclTestFixture : public CmdHandlerTestBase {
 public:
  AclTableThrift aclTableEntries;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    aclTableEntries.aclTableEntries() = createAclTableEntries();
  }
};

TEST_F(CmdShowAclTestFixture, queryClient) {
  setupMockedAgentServer();
  EXPECT_CALL(getMockAgent(), getAclTableGroup(_))
      .WillOnce(Invoke([&](auto& entries) { entries = aclTableEntries; }));

  auto cmd = CmdShowAcl();
  auto model = cmd.queryClient(localhost());
  auto aclTableEntries = model.aclTableEntries();
  int tableNum = 1;
  for (auto& [aclTableName, entries] : *aclTableEntries) {
    auto expectedAclTableName = folly::to<std::string>("aclTable", tableNum++);
    EXPECT_EQ(aclTableName, expectedAclTableName);

    EXPECT_EQ(entries.size(), 3);

    EXPECT_EQ(
        entries[0].name().value(),
        folly::to<std::string>(
            expectedAclTableName, "_cpuPolicing-CPU-Port-Mcast-v6"));
    EXPECT_EQ(entries[0].priority().value(), 7);
    EXPECT_EQ(entries[0].actionType().value(), "permit");

    EXPECT_EQ(
        entries[1].name().value(),
        folly::to<std::string>(
            expectedAclTableName, "_cpuPolicing-high-BGPDstPort-dstLocalIp4"));
    EXPECT_EQ(entries[1].priority().value(), 2);
    EXPECT_EQ(entries[1].l4DstPort().value(), 179);
    EXPECT_EQ(entries[1].actionType().value(), "permit");

    EXPECT_EQ(
        entries[2].name().value(),
        folly::to<std::string>(
            expectedAclTableName, "_cpuPolicing-high-slow-protocols-mac"));
    EXPECT_EQ(entries[2].priority().value(), 14);
    EXPECT_EQ(entries[2].dstMac().value(), "01:80:c2:00:00:02");
    EXPECT_EQ(entries[2].actionType().value(), "permit");
  }
}

TEST_F(CmdShowAclTestFixture, printOutput) {
  auto cmd = CmdShowAcl();
  auto model = cmd.createModel(aclTableEntries, {});

  std::stringstream ss;
  cmd.printOutput(model, ss);

  std::string output = ss.str();
  std::string expectOutput =
      R"(ACL Table Name: aclTable1
Acl: aclTable1_cpuPolicing-CPU-Port-Mcast-v6
   priority: 7
   action: permit

Acl: aclTable1_cpuPolicing-high-BGPDstPort-dstLocalIp4
   priority: 2
   L4 dst port: 179
   action: permit

Acl: aclTable1_cpuPolicing-high-slow-protocols-mac
   priority: 14
   dst mac: 01:80:c2:00:00:02
   lookup class port: Restricted
   action: permit

ACL Table Name: aclTable2
Acl: aclTable2_cpuPolicing-CPU-Port-Mcast-v6
   priority: 7
   action: permit

Acl: aclTable2_cpuPolicing-high-BGPDstPort-dstLocalIp4
   priority: 2
   L4 dst port: 179
   action: permit

Acl: aclTable2_cpuPolicing-high-slow-protocols-mac
   priority: 14
   dst mac: 01:80:c2:00:00:02
   lookup class port: Restricted
   action: permit

)";
  EXPECT_EQ(output, expectOutput);
}

TEST_F(CmdShowAclTestFixture, printOutputPbrFields) {
  AclEntryThrift pbrEntry;
  pbrEntry.name() = "te_pol_tc2";
  pbrEntry.priority() = 50000;
  pbrEntry.trafficClass() = 2;
  pbrEntry.nextHopGroupId() = 100;
  pbrEntry.redirectNextHopGroupId() = 200;
  pbrEntry.counterName() = "CLASS_2_lspgrp_silver";
  pbrEntry.actionType() = "permit";

  AclEntryThrift countingEntry;
  countingEntry.name() = "srv6_decap";
  countingEntry.priority() = 100000;
  countingEntry.etherType() = cfg::EtherType::IPv6;
  countingEntry.dstIpV6Word3() = 0xfdadff00;
  countingEntry.dstIpV6Word2() = 0x2704;
  countingEntry.counterName() = "srv6_decap_counter";
  countingEntry.actionType() = "permit";

  AclTableThrift tables;
  tables.aclTableEntries()["PbrAclTable"] = {pbrEntry, countingEntry};

  auto cmd = CmdShowAcl();
  std::stringstream ss;
  cmd.printOutput(cmd.createModel(tables, {}), ss);
  EXPECT_EQ(ss.str(), R"(ACL Table Name: PbrAclTable
Acl: te_pol_tc2
   priority: 50000
   traffic class: 2
   route dst nhg id: 100
   action: permit
   redirect nhg id: 200
   counter: CLASS_2_lspgrp_silver

Acl: srv6_decap
   priority: 100000
   ether type: IPv6
   dst ipv6 word3: 0xfdadff00
   dst ipv6 word2: 0x00002704
   action: permit
   counter: srv6_decap_counter

)");
}

// CLI reference wiki hooks: a human description and a non-empty sample model.
// Property checks only (no golden text).
TEST_F(CmdShowAclTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowAclTraits::description().empty());
  EXPECT_FALSE(CmdShowAcl::sampleModel().aclTableEntries()->empty());
}

} // namespace facebook::fboss
