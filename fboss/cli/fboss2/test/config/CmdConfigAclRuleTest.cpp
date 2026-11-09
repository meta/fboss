/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/acl/rule/AclRuleAttrs.h"
#include "fboss/cli/fboss2/commands/config/acl/rule/CmdConfigAclRule.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Seed mirrors a production ingress ACL: one AclTableGroup with one
// AclTable holding two AclEntries by name. Most match-fields default to
// unset so each test sees a clean baseline mutation.
static constexpr auto kSeedConfig = R"({
  "sw": {
    "aclTableGroups": [{
      "name": "ingress-ACL-Table-Group",
      "stage": 0,
      "aclTables": [{
        "name": "AclTable1",
        "priority": 0,
        "aclEntries": [
          {"name": "rule-1", "actionType": 1},
          {"name": "rule-2", "actionType": 0, "proto": 6}
        ],
        "actionTypes": [],
        "qualifiers": [],
        "udfGroups": []
      }]
    }]
  }
})";

class CmdConfigAclRuleTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigAclRuleTestFixture()
      : CmdConfigTestBase(
            "fboss_acl_rule_config_test_%%%%-%%%%-%%%%-%%%%",
            kSeedConfig) {}

 protected:
  const std::string cmdPrefix_ = "config acl rule";

  cfg::AclEntry& getRule(const std::string& name) {
    auto& cfg = ConfigSession::getInstance().getAgentConfig();
    auto& tables = *(*cfg.sw()->aclTableGroups())[0].aclTables();
    auto& entries = *tables[0].aclEntries();
    for (auto& e : entries) {
      if (*e.name() == name) {
        return e;
      }
    }
    throw std::runtime_error("rule not found: " + name);
  }
};

// =============================================================
// AclRuleConfigArgs validation tests
// =============================================================

TEST_F(CmdConfigAclRuleTestFixture, argValidation_badArity) {
  EXPECT_THROW(AclRuleConfigArgs({}), std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1"}), std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "dscp"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "dscp", "10", "extra", "more"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_unknownAttr) {
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "bogus", "1"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_extraTokenOnlyForTtl) {
  // 5-token form is only valid for ttl.
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "dscp", "10", "20"}),
      std::invalid_argument);
  // ttl with 4 or 5 tokens is fine.
  EXPECT_NO_THROW(AclRuleConfigArgs({"AclTable1", "rule-1", "ttl", "64"}));
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "ttl", "64", "255"}));
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_outOfRange) {
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1", "rule-1", "dscp", std::to_string(kDscpRange.max + 1)}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1", "rule-1", "dscp", std::to_string(kDscpRange.min - 1)}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1",
           "rule-1",
           "vlan",
           std::to_string(utils::kVlanIdMin - 1)}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1",
           "rule-1",
           "vlan",
           std::to_string(utils::kVlanIdMax + 1)}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_badIp) {
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "source-ip", "not-an-ip"}),
      std::invalid_argument);
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "source-ip", "10.0.0.0/24"}));
  EXPECT_NO_THROW(AclRuleConfigArgs(
      {"AclTable1", "rule-1", "destination-ip", "fe80::/64"}));
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_badMac) {
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1", "rule-1", "destination-mac", "not-a-mac"}),
      std::invalid_argument);
  EXPECT_NO_THROW(AclRuleConfigArgs(
      {"AclTable1", "rule-1", "destination-mac", "aa:bb:cc:dd:ee:ff"}));
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_protocolKeyword) {
  AclRuleConfigArgs a({"AclTable1", "rule-1", "protocol", "tcp"});
  EXPECT_EQ(a.getAttribute(), "protocol");
  AclRuleConfigArgs b({"AclTable1", "rule-1", "protocol", "icmpv6"});
  AclRuleConfigArgs c({"AclTable1", "rule-1", "protocol", "58"});
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "protocol", "bogus"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_ipFragmentEnum) {
  EXPECT_NO_THROW(AclRuleConfigArgs(
      {"AclTable1", "rule-1", "ip-fragment", "any-fragment"}));
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "ip-fragment", "bogus"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_etherTypeNumericOrName) {
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "ethertype", "ipv4"}));
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "ethertype", "0x0800"}));
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "ethertype", "2048"}));
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1",
           "rule-1",
           "ethertype",
           std::to_string(kU16Range.max + 1)}),
      std::invalid_argument);
}

// =============================================================
// queryClient() tests — one per attribute
// =============================================================

TEST_F(CmdConfigAclRuleTestFixture, ruleAutoCreated) {
  // Missing rule + attr/value: rule is created and the attribute applied
  // in the same command (auto-upsert).
  setupTestableConfigSession(cmdPrefix_, "AclTable1 brand-new dscp 10");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "brand-new", "dscp", "10"});
  std::string result;
  EXPECT_NO_THROW(result = cmd.queryClient(host, args));
  // Auto-create path reports "Created and set" and echoes rule/attr/value.
  EXPECT_THAT(result, HasSubstr("Created and set"));
  EXPECT_THAT(result, HasSubstr("brand-new"));
  EXPECT_THAT(result, HasSubstr("dscp"));
  EXPECT_THAT(result, HasSubstr("10"));
  EXPECT_EQ(*getRule("brand-new").dscp(), 10);
}

TEST_F(CmdConfigAclRuleTestFixture, tableNotFound) {
  setupTestableConfigSession(cmdPrefix_, "Bogus rule-1 dscp 10");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"Bogus", "rule-1", "dscp", "10"});
  EXPECT_THROW(cmd.queryClient(host, args), std::runtime_error);
}

TEST_F(CmdConfigAclRuleTestFixture, setSourceIp) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 source-ip 10.0.0.0/24");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "source-ip", "10.0.0.0/24"});
  EXPECT_NO_THROW(cmd.queryClient(host, args));
  EXPECT_EQ(*getRule("rule-1").srcIp(), "10.0.0.0/24");
}

TEST_F(CmdConfigAclRuleTestFixture, setDestinationIp) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 destination-ip fe80::/64");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1", "rule-1", "destination-ip", "fe80::/64"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").dstIp(), "fe80::/64");
}

TEST_F(CmdConfigAclRuleTestFixture, setProtocol) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 protocol tcp");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "protocol", "tcp"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").proto(), 6);
}

TEST_F(CmdConfigAclRuleTestFixture, setSourcePort) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 source-port 443");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "source-port", "443"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").l4SrcPort(), 443);
}

TEST_F(CmdConfigAclRuleTestFixture, setDestinationPort) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 destination-port 443");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "destination-port", "443"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").l4DstPort(), 443);
}

TEST_F(CmdConfigAclRuleTestFixture, setDscp) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 dscp 46");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "dscp", "46"});
  // rule-1 already exists, so this is an update: "Set", not "Created and set".
  auto result = cmd.queryClient(host, args);
  EXPECT_THAT(result, HasSubstr("Set"));
  EXPECT_THAT(result, Not(HasSubstr("Created")));
  EXPECT_THAT(result, HasSubstr("46"));
  EXPECT_EQ(*getRule("rule-1").dscp(), 46);
}

TEST_F(CmdConfigAclRuleTestFixture, setTcpFlags) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 tcp-flags 0x12");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "tcp-flags", "0x12"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").tcpFlagsBitMap(), 0x12);
}

TEST_F(CmdConfigAclRuleTestFixture, setIcmpType) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 icmp-type 8");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "icmp-type", "8"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").icmpType(), 8);
}

TEST_F(CmdConfigAclRuleTestFixture, setIcmpCode) {
  // Use a non-zero code: 0 coincides with the thrift default for an unset
  // optional i16, so it would not prove the CLI actually wrote the field.
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 icmp-code 3");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "icmp-code", "3"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").icmpCode(), 3);
}

TEST_F(CmdConfigAclRuleTestFixture, setIpFragment) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 ip-fragment any-fragment");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1", "rule-1", "ip-fragment", "any-fragment"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").ipFrag(), cfg::IpFragMatch::MATCH_ANY_FRAGMENT);
}

TEST_F(CmdConfigAclRuleTestFixture, setTtlDefaultMask) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 ttl 64");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "ttl", "64"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").ttl()->value(), 64);
  EXPECT_EQ(*getRule("rule-1").ttl()->mask(), kTtlMaskDefault);
}

TEST_F(CmdConfigAclRuleTestFixture, setTtlExplicitMask) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 ttl 64 240");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "ttl", "64", "240"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").ttl()->value(), 64);
  EXPECT_EQ(*getRule("rule-1").ttl()->mask(), 240);
}

TEST_F(CmdConfigAclRuleTestFixture, setDestinationMac) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 destination-mac aa:bb:cc:dd:ee:ff");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1", "rule-1", "destination-mac", "aa:bb:cc:dd:ee:ff"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").dstMac(), "aa:bb:cc:dd:ee:ff");
}

TEST_F(CmdConfigAclRuleTestFixture, setEtherType) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 ethertype ipv4");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "ethertype", "ipv4"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").etherType(), cfg::EtherType::IPv4);
}

TEST_F(CmdConfigAclRuleTestFixture, setVlan) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 vlan 100");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "vlan", "100"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").vlanID(), 100);
}

TEST_F(CmdConfigAclRuleTestFixture, setIpType) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 ip-type ipv6");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "ip-type", "ipv6"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").ipType(), cfg::IpType::IP6);
}

TEST_F(CmdConfigAclRuleTestFixture, setLookupClassL2) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 lookup-class-l2 10");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "lookup-class-l2", "10"});
  cmd.queryClient(host, args);
  EXPECT_EQ(
      *getRule("rule-1").lookupClassL2(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
}

TEST_F(CmdConfigAclRuleTestFixture, setLookupClassNeighbor) {
  // By name rather than by id, and at the top of the range.
  setupTestableConfigSession(
      cmdPrefix_,
      "AclTable1 rule-1 lookup-class-neighbor CLASS_QUEUE_PER_HOST_QUEUE_9");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1",
       "rule-1",
       "lookup-class-neighbor",
       "CLASS_QUEUE_PER_HOST_QUEUE_9"});
  cmd.queryClient(host, args);
  EXPECT_EQ(
      *getRule("rule-1").lookupClassNeighbor(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_9);
}

TEST_F(CmdConfigAclRuleTestFixture, setLookupClassRoute) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-2 lookup-class-route 13");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-2", "lookup-class-route", "13"});
  cmd.queryClient(host, args);
  EXPECT_EQ(
      *getRule("rule-2").lookupClassRoute(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
  // The three lookup-class attrs are independent fields on the same entry.
  EXPECT_FALSE(getRule("rule-2").lookupClassL2().has_value());
  EXPECT_FALSE(getRule("rule-2").lookupClassNeighbor().has_value());
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_lookupClassAcceptsIdOrName) {
  // Same two spellings `config interface <intf> lookup-class` accepts.
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "lookup-class-l2", "10"}));
  EXPECT_NO_THROW(AclRuleConfigArgs(
      {"AclTable1",
       "rule-1",
       "lookup-class-neighbor",
       "CLASS_QUEUE_PER_HOST_QUEUE_3"}));
  EXPECT_NO_THROW(AclRuleConfigArgs(
      {"AclTable1",
       "rule-1",
       "lookup-class-route",
       "class_queue_per_host_queue_9"}));
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_lookupClassRejectsReserved) {
  // CLASS_DROP (9) and the DST_CLASS_L3_LOCAL_* values are the agent's to
  // assign, so an operator must not be able to match on them here either.
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "lookup-class-l2", "9"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs(
          {"AclTable1", "rule-1", "lookup-class-l2", "CLASS_DROP"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_lookupClassRejectsJunk) {
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "lookup-class-l2", "999"}),
      std::invalid_argument);
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "lookup-class-l2", "bogus"}),
      std::invalid_argument);
}

// =============================================================
// action sub-attribute tests
// =============================================================

TEST_F(CmdConfigAclRuleTestFixture, argValidation_unknownActionSubattr) {
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "action", "bogus"}),
      std::invalid_argument);
}

TEST_F(CmdConfigAclRuleTestFixture, argValidation_actionPermitNoExtra) {
  EXPECT_NO_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "action", "permit"}));
  EXPECT_THROW(
      AclRuleConfigArgs({"AclTable1", "rule-1", "action", "permit", "x"}),
      std::invalid_argument);
}

// Richer actions are no longer settable here: they live on a MatchAction in a
// traffic policy, and which policy a rule lands in changes what the value
// means (updateAclsImpl reads a CPU-matched rule's queue id as a CPU queue and
// a dataplane-matched one's as a port queue). `config copp traffic-policy` and
// `config data-plane traffic-policy` name the policy explicitly instead.
TEST_F(CmdConfigAclRuleTestFixture, argValidation_matchActionsRejected) {
  for (const auto& sub :
       {"send-to-queue",
        "set-dscp",
        "set-tc",
        "mirror-ingress",
        "mirror-egress",
        "counter",
        "trap-to-cpu",
        "copy-to-cpu",
        "redirect"}) {
    EXPECT_THROW(
        AclRuleConfigArgs({"AclTable1", "rule-1", "action", sub}),
        std::invalid_argument)
        << "action '" << sub << "' should no longer be accepted";
    EXPECT_THROW(
        AclRuleConfigArgs({"AclTable1", "rule-1", "action", sub, "7"}),
        std::invalid_argument)
        << "action '" << sub << " 7' should no longer be accepted";
  }
}

TEST_F(CmdConfigAclRuleTestFixture, setActionPermit) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-2 action permit");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-2", "action", "permit"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-2").actionType(), cfg::AclActionType::PERMIT);
}

TEST_F(CmdConfigAclRuleTestFixture, setActionDeny) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 action deny");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "action", "deny"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").actionType(), cfg::AclActionType::DENY);
}

TEST_F(CmdConfigAclRuleTestFixture, setActionDenyDataAndControlPlane) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 action deny-data-and-control-plane");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1", "rule-1", "action", "deny-data-and-control-plane"});
  cmd.queryClient(host, args);
  EXPECT_EQ(
      *getRule("rule-1").actionType(),
      cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE);
}

TEST_F(CmdConfigAclRuleTestFixture, setPacketLookupResult) {
  setupTestableConfigSession(
      cmdPrefix_, "AclTable1 rule-1 packet-lookup-result mpls-no-match");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args(
      {"AclTable1", "rule-1", "packet-lookup-result", "mpls-no-match"});
  cmd.queryClient(host, args);
  EXPECT_EQ(
      *getRule("rule-1").packetLookupResult(),
      cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH);
}

// Overwriting an attribute already set on an existing rule replaces the value
// (rule-2 ships with proto=6/tcp in the seed; set it to udp/17).
TEST_F(CmdConfigAclRuleTestFixture, overwriteExistingAttr) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-2 protocol udp");
  ASSERT_EQ(*getRule("rule-2").proto(), 6);
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-2", "protocol", "udp"});
  auto result = cmd.queryClient(host, args);
  EXPECT_THAT(result, HasSubstr("Set"));
  EXPECT_EQ(*getRule("rule-2").proto(), 17);
}

// Setting the same value twice is idempotent and stays an update ("Set").
TEST_F(CmdConfigAclRuleTestFixture, idempotentSameValue) {
  setupTestableConfigSession(cmdPrefix_, "AclTable1 rule-1 dscp 46");
  CmdConfigAclRule cmd;
  HostInfo host("testhost");
  AclRuleConfigArgs args({"AclTable1", "rule-1", "dscp", "46"});
  cmd.queryClient(host, args);
  EXPECT_EQ(*getRule("rule-1").dscp(), 46);
  // Second identical apply: still succeeds, value unchanged.
  AclRuleConfigArgs again({"AclTable1", "rule-1", "dscp", "46"});
  auto result = cmd.queryClient(host, again);
  EXPECT_THAT(result, HasSubstr("Set"));
  EXPECT_EQ(*getRule("rule-1").dscp(), 46);
}

} // namespace facebook::fboss
