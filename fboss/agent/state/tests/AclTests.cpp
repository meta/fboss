/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"
#include "folly/IPAddress.h"

#include <folly/MacAddress.h>
#include <gtest/gtest.h>

using namespace facebook::fboss;
using facebook::fboss::InterfaceID;
using folly::MacAddress;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;

DECLARE_bool(enable_acl_table_group);

namespace {
HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

const std::vector<std::string> kUdfList = {"foo1", "foo2"};
const std::vector<signed char> kRoceBytes = {0x40, 0x40};
const std::vector<signed char> kRoceMask = {0x40, 0x40};
static int kUdpProto(17);
static int kUdpDstPort(4791);
} // namespace

cfg::UdfConfig makeUdfConfig(const std::vector<std::string>& udfNameList) {
  cfg::UdfConfig udf;
  std::map<std::string, cfg::UdfGroup> udfMap;

  for (const auto& udfName : udfNameList) {
    cfg::UdfGroup udfGroup;
    udfGroup.name() = udfName;
    udfGroup.startOffsetInBytes() =
        utility::kUdfAclRoceOpcodeStartOffsetInBytes;
    udfGroup.fieldSizeInBytes() = utility::kUdfAclRoceOpcodeFieldSizeInBytes;
    udfMap.insert(std::make_pair(udfName, udfGroup));
  }

  udf.udfGroups() = udfMap;
  return udf;
}

std::vector<cfg::AclUdfEntry> makeAclUdfEntry() {
  std::vector<cfg::AclUdfEntry> aclUdfEntryList;
  for (const auto& udfGroup : kUdfList) {
    cfg::AclUdfEntry aclUdfEntry;
    aclUdfEntry.udfGroup() = udfGroup;
    aclUdfEntry.roceBytes() = kRoceBytes;
    aclUdfEntry.roceMask() = kRoceMask;
    aclUdfEntryList.push_back(aclUdfEntry);
  }
  return aclUdfEntryList;
}

TEST(Acl, applyConfig) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto aclEntry = make_shared<AclEntry>(0, std::string("acl0"));
  auto aclsV0 = stateV0->getAcls()->modify(&stateV0);
  aclsV0->addNode(aclEntry, scope());
  auto aclsV1 = stateV0->getAcls();
  auto aclV0 = aclsV1->getNodeIf("acl0");
  EXPECT_EQ(0, aclV0->getGeneration());
  EXPECT_FALSE(aclV0->isPublished());
  EXPECT_EQ(0, aclV0->getPriority());
  registerPort(stateV0, PortID(1), "port1", scope());

  aclV0->publish();
  EXPECT_TRUE(aclV0->isPublished());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);

  config.acls()->resize(1);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].srcIp() = "192.168.0.1";
  config.acls()[0].dstIp() = "192.168.0.0/24";
  config.acls()[0].srcPort() = 5;
  config.acls()[0].dstPort() = 8;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("acl1");
  ASSERT_NE(nullptr, aclV1);
  EXPECT_NE(aclV0, aclV1);

  EXPECT_EQ(0 + AclTable::kDataplaneAclMaxPriority, aclV1->getPriority());
  EXPECT_EQ(cfg::AclActionType::DENY, aclV1->getActionType());
  EXPECT_EQ(5, aclV1->getSrcPort());
  EXPECT_EQ(8, aclV1->getDstPort());

  EXPECT_FALSE(aclV1->isPublished());

  config.acls()[0].dstIp() = "invalid address";
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()),
      folly::IPAddressFormatException);

  *config.acls()[0].name() = "acl2";
  config.acls()[0].dstIp().reset();
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto aclV2 = stateV2->getAcl("acl2");
  // We should handle the field removal correctly
  EXPECT_NE(nullptr, aclV2);
  EXPECT_FALSE(aclV2->getDstIp().first);

  // Ensure ACL is created with no traffic policy for PERMIT
  config.acls()->resize(2);
  *config.acls()[1].name() = "acl22";
  *config.acls()[1].actionType() = cfg::AclActionType::PERMIT;
  auto stateV22 = publishAndApplyConfig(stateV2, &config, platform.get());
  EXPECT_NE(nullptr, stateV22);

  // Non-existent entry
  auto acl2V2 = stateV2->getAcl("something");
  ASSERT_EQ(nullptr, acl2V2);

  cfg::SwitchConfig configV1;
  configV1.ports()->resize(1);
  preparedMockPortConfig(configV1.ports()[0], 1);

  configV1.acls()->resize(1);
  *configV1.acls()[0].name() = "acl3";

  configV1.acls()[0].l4SrcPort() = 1;
  configV1.acls()[0].l4DstPort() = 3;
  // Make sure it's used so that it isn't ignored
  configV1.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  configV1.dataPlaneTrafficPolicy()->matchToAction()->resize(
      1, cfg::MatchToAction());
  *configV1.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl3";
  *configV1.dataPlaneTrafficPolicy()->matchToAction()[0].action() =
      cfg::MatchAction();
  configV1.dataPlaneTrafficPolicy()
      ->matchToAction()[0]
      .action()
      ->sendToQueue() = cfg::QueueMatchAction();
  *configV1.dataPlaneTrafficPolicy()
       ->matchToAction()[0]
       .action()
       ->sendToQueue()
       ->queueId() = 1;

  auto stateV3 = publishAndApplyConfig(stateV2, &configV1, platform.get());
  EXPECT_NE(nullptr, stateV3);
  auto acls = stateV3->getAcls();
  validateThriftMapMapSerialization(*stateV3->getAcls());
  auto aclV3 = stateV3->getAcl("acl3");
  ASSERT_NE(nullptr, aclV3);
  EXPECT_NE(aclV0, aclV3);
  EXPECT_EQ(0 + AclTable::kDataplaneAclMaxPriority, aclV3->getPriority());
  EXPECT_EQ(cfg::AclActionType::PERMIT, aclV3->getActionType());
  EXPECT_FALSE(!aclV3->getL4SrcPort());
  EXPECT_EQ(aclV3->getL4SrcPort().value(), 1);
  EXPECT_FALSE(!aclV3->getL4DstPort());
  EXPECT_EQ(aclV3->getL4DstPort().value(), 3);

  // test max > 65535 case
  configV1.acls()[0].l4SrcPort() = 65536;
  EXPECT_THROW(
      publishAndApplyConfig(stateV3, &configV1, platform.get()), FbossError);

  cfg::SwitchConfig configV2;
  configV2.ports()->resize(1);
  preparedMockPortConfig(configV2.ports()[0], 1);
  configV2.acls()->resize(1);
  *configV2.acls()[0].name() = "acl3";
  // Make sure it's used so that it isn't ignored
  configV2.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  configV2.dataPlaneTrafficPolicy()->matchToAction()->resize(
      1, cfg::MatchToAction());
  *configV2.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl3";
  *configV2.dataPlaneTrafficPolicy()->matchToAction()[0].action() =
      cfg::MatchAction();
  configV2.dataPlaneTrafficPolicy()
      ->matchToAction()[0]
      .action()
      ->sendToQueue() = cfg::QueueMatchAction();
  *configV2.dataPlaneTrafficPolicy()
       ->matchToAction()[0]
       .action()
       ->sendToQueue()
       ->queueId() = 1;

  // set the ip frag option
  configV2.acls()[0].ipFrag() = cfg::IpFragMatch::MATCH_NOT_FRAGMENTED;

  auto stateV5 = publishAndApplyConfig(stateV3, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV5);
  auto aclV5 = stateV5->getAcl("acl3");
  EXPECT_NE(nullptr, aclV5);
  EXPECT_EQ(aclV5->getIpFrag().value(), cfg::IpFragMatch::MATCH_NOT_FRAGMENTED);

  // set dst Mac
  auto dstMacStr = "01:01:01:01:01:01";
  configV2.acls()[0].dstMac() = dstMacStr;

  auto stateV6 = publishAndApplyConfig(stateV5, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV6);
  auto aclV6 = stateV6->getAcl("acl3");
  EXPECT_NE(nullptr, aclV6);

  EXPECT_EQ(MacAddress(dstMacStr), aclV6->getDstMac());
  // Remove src, dst Mac
  configV2.acls()[0].dstMac().reset();

  auto stateV7 = publishAndApplyConfig(stateV6, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV7);
  auto aclV7 = stateV7->getAcl("acl3");
  EXPECT_NE(nullptr, aclV7);

  EXPECT_FALSE(aclV7->getDstMac());

  // set packet lookup result matching
  configV2.acls()[0].packetLookupResult() =
      cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH;
  auto stateV8 = publishAndApplyConfig(stateV7, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV5);
  auto aclV8 = stateV8->getAcl("acl3");
  EXPECT_NE(nullptr, aclV8);
  EXPECT_EQ(
      aclV8->getPacketLookupResult().value(),
      cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH);

  // remove packet lookup result matching
  configV2.acls()[0].packetLookupResult().reset();
  auto stateV9 = publishAndApplyConfig(stateV8, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV9);
  auto aclV9 = stateV9->getAcl("acl3");
  EXPECT_NE(nullptr, aclV9);
  EXPECT_FALSE(aclV9->getPacketLookupResult());

  // set vlan_id matching
  configV2.acls()[0].vlanID() = 2001;
  auto stateV10 = publishAndApplyConfig(stateV9, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV10);
  auto aclV10 = stateV10->getAcl("acl3");
  EXPECT_NE(nullptr, aclV10);
  EXPECT_EQ(aclV10->getVlanID().value(), 2001);

  // remove vlan_id matching
  configV2.acls()[0].vlanID().reset();
  auto stateV11 = publishAndApplyConfig(stateV10, &configV2, platform.get());
  EXPECT_NE(nullptr, stateV11);
  auto aclV11 = stateV11->getAcl("acl3");
  EXPECT_NE(nullptr, aclV11);
  EXPECT_FALSE(aclV11->getVlanID());
}

TEST(Acl, stateDelta) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(4);
  *config.acls()[0].name() = "acl0";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].srcIp() = "192.168.0.1";
  config.acls()[0].srcPort() = 1;
  *config.acls()[1].name() = "acl1";
  config.acls()[1].srcIp() = "192.168.0.2";
  *config.acls()[1].actionType() = cfg::AclActionType::DENY;
  *config.acls()[2].name() = "acl3";
  *config.acls()[2].actionType() = cfg::AclActionType::DENY;
  config.acls()[2].srcIp() = "192.168.0.3";
  config.acls()[2].srcPort() = 5;
  config.acls()[2].dstPort() = 8;
  *config.acls()[3].name() = "acl4";
  *config.acls()[3].actionType() = cfg::AclActionType::DENY;
  config.acls()[3].srcIp() = "192.168.0.4";
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_EQ(stateV2, nullptr);

  // Only change one action
  config.acls()[0].srcPort() = 2;
  auto stateV3 = publishAndApplyConfig(stateV1, &config, platform.get());
  StateDelta delta13(stateV1, stateV3);
  auto aclDelta13 = delta13.getAclsDelta();
  auto iter = aclDelta13.begin();
  EXPECT_EQ(iter->getOld()->getSrcPort(), 1);
  EXPECT_EQ(iter->getNew()->getSrcPort(), 2);
  ++iter;
  EXPECT_EQ(iter, aclDelta13.end());

  // Remove tail element
  config.acls()->pop_back();
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  StateDelta delta34(stateV3, stateV4);
  auto aclDelta34 = delta34.getAclsDelta();
  iter = aclDelta34.begin();
  EXPECT_EQ(
      iter->getOld()->getSrcIp(),
      folly::CIDRNetwork(folly::IPAddress("192.168.0.4"), 32));
  EXPECT_EQ(iter->getNew(), nullptr);
  ++iter;
  EXPECT_EQ(iter, aclDelta34.end());
  // Remove tail element
  config.acls()->pop_back();
  auto stateV5 = publishAndApplyConfig(stateV4, &config, platform.get());
  StateDelta delta45(stateV4, stateV5);
  auto aclDelta45 = delta45.getAclsDelta();
  iter = aclDelta45.begin();
  EXPECT_EQ(
      iter->getOld()->getSrcIp(),
      folly::CIDRNetwork(folly::IPAddress("192.168.0.3"), 32));
  EXPECT_EQ(iter->getOld()->getSrcPort(), 5);
  EXPECT_EQ(iter->getOld()->getDstPort(), 8);
  EXPECT_EQ(iter->getOld()->getActionType(), cfg::AclActionType::DENY);
  EXPECT_EQ(iter->getNew(), nullptr);
  ++iter;
  EXPECT_EQ(iter, aclDelta45.end());
}

TEST(Acl, Udf) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  std::vector<std::string> udfList = {"foo1", "foo2"};

  cfg::SwitchConfig config;
  config.acls()->resize(2);
  *config.acls()[0].name() = "aclUdf";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].udfGroups() = {};
  config.acls()[0].roceOpcode() = utility::kUdfRoceOpcodeAck;

  *config.acls()[1].name() = "aclUdfTable";
  *config.acls()[1].actionType() = cfg::AclActionType::DENY;
  config.acls()[1].udfTable() = makeAclUdfEntry();

  // empty groups section is not valid
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);

  config.acls()[0].udfGroups() = kUdfList;
  // still no good, if we don't find the relevant configuration in the udf group
  // section
  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);

  config.udfConfig() = makeUdfConfig(kUdfList);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("aclUdf");
  ASSERT_NE(nullptr, aclV1);
  EXPECT_EQ(cfg::AclActionType::DENY, aclV1->getActionType());
  EXPECT_EQ(aclV1->getUdfGroups().value(), kUdfList);
  EXPECT_EQ(aclV1->getRoceOpcode().value(), utility::kUdfRoceOpcodeAck);

  auto aclV11 = stateV1->getAcl("aclUdfTable");
  auto aclUdfEntryList = aclV11->getUdfTable().value();

  EXPECT_EQ(aclUdfEntryList[0].udfGroup(), kUdfList[0]);
  EXPECT_EQ(aclUdfEntryList[0].roceBytes(), kRoceBytes);
  EXPECT_EQ(aclUdfEntryList[0].roceMask(), kRoceMask);
  EXPECT_EQ(aclUdfEntryList[1].udfGroup(), kUdfList[1]);
  EXPECT_EQ(aclUdfEntryList[1].roceBytes(), kRoceBytes);
  EXPECT_EQ(aclUdfEntryList[1].roceMask(), kRoceMask);

  // update udf list, ensure it gets reflected
  std::vector<std::string> newUdfList = {"foo3"};
  config.acls()[0].udfGroups() = newUdfList;

  // negative test case, foo3 is not in the cfg.
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);

  config.udfConfig() = makeUdfConfig({"foo3"});

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto aclV2 = stateV2->getAcl("aclUdf");
  ASSERT_NE(nullptr, aclV2);
  EXPECT_EQ(aclV2->getUdfGroups().value(), newUdfList);
}

TEST(Acl, UdfRoceBytesMask) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "aclUdf";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].udfGroups() = kUdfList;
  config.udfConfig() = makeUdfConfig(kUdfList);
  config.acls()[0].roceBytes() = kRoceBytes;
  config.acls()[0].roceMask() = kRoceMask;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("aclUdf");
  ASSERT_NE(nullptr, aclV1);
  EXPECT_EQ(cfg::AclActionType::DENY, aclV1->getActionType());
  EXPECT_EQ(aclV1->getUdfGroups().value(), kUdfList);
  EXPECT_EQ(aclV1->getRoceBytes().value(), kRoceBytes);
  EXPECT_EQ(aclV1->getRoceMask().value(), kRoceMask);

  const std::vector<signed char> roceBytes = {0x40};
  const std::vector<signed char> roceMask = {0x40};
  config.acls()[0].roceBytes() = roceBytes;
  config.acls()[0].roceMask() = roceMask;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto aclV2 = stateV2->getAcl("aclUdf");
  ASSERT_NE(nullptr, aclV2);
  EXPECT_EQ(aclV2->getRoceBytes().value(), roceBytes);
  EXPECT_EQ(aclV2->getRoceMask().value(), roceMask);
}

TEST(Acl, validateUdfAclGroupFields) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  std::vector<std::string> udfList = {utility::kUdfAclRoceOpcodeGroupName};

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  config.acls()[0].udfGroups() = udfList;
  config.udfConfig() = makeUdfConfig(udfList);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_EQ(
      stateV1->getUdfConfig()
          ->getUdfGroupMap()
          ->getUdfGroupIf(utility::kUdfAclRoceOpcodeGroupName)
          ->getStartOffsetInBytes(),
      utility::kUdfAclRoceOpcodeStartOffsetInBytes);
  EXPECT_EQ(
      stateV1->getUdfConfig()
          ->getUdfGroupMap()
          ->getUdfGroupIf(utility::kUdfAclRoceOpcodeGroupName)
          ->getFieldSizeInBytes(),
      utility::kUdfAclRoceOpcodeFieldSizeInBytes);
}

TEST(Acl, Icmp) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].proto() = 58;
  config.acls()[0].icmpType() = 128;
  config.acls()[0].icmpCode() = 0;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("acl1");
  ASSERT_NE(nullptr, aclV1);
  EXPECT_EQ(0 + AclTable::kDataplaneAclMaxPriority, aclV1->getPriority());
  EXPECT_EQ(cfg::AclActionType::DENY, aclV1->getActionType());
  EXPECT_EQ(128, aclV1->getIcmpType().value());
  EXPECT_EQ(0, aclV1->getIcmpCode().value());

  // test config exceptions
  config.acls()[0].proto() = 4;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  config.acls()[0].proto() = 58;
  config.acls()[0].icmpType().reset();
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
}

TEST(Acl, aclModifyUnpublished) {
  auto state = make_shared<SwitchState>();
  auto acls = state->getAcls();
  EXPECT_EQ(acls.get(), acls->modify(&state));
}

TEST(Acl, aclModifyPublished) {
  auto state = make_shared<SwitchState>();
  state->publish();
  auto acls = state->getAcls();
  validateThriftMapMapSerialization(*acls);
  EXPECT_NE(acls.get(), acls->modify(&state));
}

TEST(Acl, aclEntryModifyUnpublished) {
  auto state = make_shared<SwitchState>();
  auto aclEntry = make_shared<AclEntry>(0, std::string("acl0"));
  state->getAcls()->addNode(aclEntry, scope());
  EXPECT_EQ(aclEntry.get(), aclEntry->modify(&state, scope()));
}

TEST(Acl, aclEntryModifyPublished) {
  auto state = make_shared<SwitchState>();
  auto aclEntry = make_shared<AclEntry>(0, std::string("acl0"));
  state->getAcls()->addNode(aclEntry, scope());
  state->publish();
  EXPECT_NE(aclEntry.get(), aclEntry->modify(&state, scope()));
}

TEST(Acl, AclGeneration) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);

  config.acls()->resize(7);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].srcIp() = "192.168.0.1";
  config.acls()[0].dstIp() = "192.168.0.0/24";
  config.acls()[0].srcPort() = 5;
  config.acls()[0].dstPort() = 8;
  *config.acls()[1].name() = "acl2";
  config.acls()[1].dscp() = 16;
  *config.acls()[2].name() = "acl3";
  config.acls()[2].dscp() = 16;
  *config.acls()[3].name() = "acl4";
  *config.acls()[3].actionType() = cfg::AclActionType::DENY;
  *config.acls()[4].name() = "acl5";
  config.acls()[4].srcIp() = "2401:db00:21:7147:face:0:7:0/128";
  config.acls()[4].srcPort() = 5;
  *config.acls()[5].name() = "acl6";
  config.acls()[5].proto() = kUdpProto;
  config.acls()[5].l4DstPort() = kUdpDstPort;
  *config.acls()[6].name() = "acl7";
  config.acls()[6].proto() = kUdpProto;
  config.acls()[6].l4DstPort() = 1024;

  config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  config.dataPlaneTrafficPolicy()->matchToAction()->resize(
      5, cfg::MatchToAction());
  *config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl2";
  *config.dataPlaneTrafficPolicy()->matchToAction()[0].action() =
      cfg::MatchAction();
  config.dataPlaneTrafficPolicy()->matchToAction()[0].action()->sendToQueue() =
      cfg::QueueMatchAction();
  *config.dataPlaneTrafficPolicy()
       ->matchToAction()[0]
       .action()
       ->sendToQueue()
       ->queueId() = 1;
  *config.dataPlaneTrafficPolicy()->matchToAction()[1].matcher() = "acl3";
  *config.dataPlaneTrafficPolicy()->matchToAction()[1].action() =
      cfg::MatchAction();
  config.dataPlaneTrafficPolicy()->matchToAction()[1].action()->sendToQueue() =
      cfg::QueueMatchAction();
  *config.dataPlaneTrafficPolicy()
       ->matchToAction()[1]
       .action()
       ->sendToQueue()
       ->queueId() = 2;
  *config.dataPlaneTrafficPolicy()->matchToAction()[2].matcher() = "acl5";
  *config.dataPlaneTrafficPolicy()->matchToAction()[2].action() =
      cfg::MatchAction();
  config.dataPlaneTrafficPolicy()->matchToAction()[2].action()->setDscp() =
      cfg::SetDscpMatchAction();
  *config.dataPlaneTrafficPolicy()
       ->matchToAction()[2]
       .action()
       ->setDscp()
       ->dscpValue() = 8;

  *config.dataPlaneTrafficPolicy()->matchToAction()[3].matcher() = "acl6";
  *config.dataPlaneTrafficPolicy()->matchToAction()[3].action() =
      cfg::MatchAction();
  config.dataPlaneTrafficPolicy()
      ->matchToAction()[3]
      .action()
      ->flowletAction() = cfg::FlowletAction::FORWARD;

  *config.dataPlaneTrafficPolicy()->matchToAction()[4].matcher() = "acl7";
  *config.dataPlaneTrafficPolicy()->matchToAction()[4].action() =
      cfg::MatchAction();
  config.dataPlaneTrafficPolicy()
      ->matchToAction()[4]
      .action()
      ->ecmpHashAction() = cfg::SetEcmpHashAction();
  config.dataPlaneTrafficPolicy()
      ->matchToAction()[4]
      .action()
      ->ecmpHashAction()
      ->switchingMode() = cfg::SwitchingMode::FIXED_ASSIGNMENT;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(stateV1, nullptr);
  auto acls = stateV1->getAcls();
  validateThriftMapMapSerialization(*acls);
  EXPECT_NE(acls, nullptr);
  EXPECT_NE(acls->getNodeIf("acl1"), nullptr);
  EXPECT_NE(acls->getNodeIf("acl2"), nullptr);
  EXPECT_NE(acls->getNodeIf("acl3"), nullptr);
  EXPECT_NE(acls->getNodeIf("acl5"), nullptr);
  EXPECT_NE(acls->getNodeIf("acl6"), nullptr);

  EXPECT_EQ(
      acls->getNodeIf("acl1")->getPriority(),
      AclTable::kDataplaneAclMaxPriority);
  EXPECT_EQ(
      acls->getNodeIf("acl2")->getPriority(),
      AclTable::kDataplaneAclMaxPriority + 1);
  EXPECT_EQ(
      acls->getNodeIf("acl3")->getPriority(),
      AclTable::kDataplaneAclMaxPriority + 2);
  EXPECT_EQ(
      acls->getNodeIf("acl4")->getPriority(),
      AclTable::kDataplaneAclMaxPriority + 3);
  EXPECT_EQ(
      acls->getNodeIf("acl5")->getPriority(),
      AclTable::kDataplaneAclMaxPriority + 4);
  EXPECT_EQ(
      acls->getNodeIf("acl6")->getPriority(),
      AclTable::kDataplaneAclMaxPriority + 5);

  // Ensure that the global actions in global traffic policy has been added to
  // the ACL entries
  EXPECT_TRUE(acls->getNodeIf("acl5")->getAclAction() != nullptr);
  EXPECT_EQ(
      8,
      acls->getNodeIf("acl5")
          ->getAclAction()
          ->cref<switch_state_tags::setDscp>()
          ->cref<switch_config_tags::dscpValue>()
          ->cref());
  // Ensure Flowlet action is added to ACL entries
  EXPECT_TRUE(acls->getNodeIf("acl6")->getAclAction() != nullptr);
  EXPECT_EQ(
      cfg::FlowletAction::FORWARD,
      acls->getNodeIf("acl6")
          ->getAclAction()
          ->cref<switch_state_tags::flowletAction>()
          ->cref());
  EXPECT_TRUE(acls->getNodeIf("acl7")->getAclAction() != nullptr);
  EXPECT_EQ(
      cfg::SwitchingMode::FIXED_ASSIGNMENT,
      acls->getNodeIf("acl7")
          ->getAclAction()
          ->cref<switch_state_tags::ecmpHashAction>()
          ->cref<switch_config_tags::switchingMode>()
          ->cref());
}

TEST(Acl, SerializeAclEntry) {
  auto entry = std::make_unique<AclEntry>(0, std::string("dscp1"));
  entry->setDscp(1);
  entry->setL4SrcPort(179);
  entry->setL4DstPort(179);
  MatchAction action = MatchAction();
  cfg::QueueMatchAction queueAction = cfg::QueueMatchAction();
  *queueAction.queueId() = 3;
  action.setSendToQueue(make_pair(queueAction, false));
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getAclAction());
  auto aclAction = entryBack->getAclAction();
  EXPECT_TRUE(aclAction->cref<switch_state_tags::sendToQueue>() != nullptr);
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::sendToQueue>()
          ->cref<switch_state_tags::sendToCPU>()
          ->cref(),
      false);
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::sendToQueue>()
          ->cref<switch_state_tags::action>()
          ->cref<switch_config_tags::queueId>()
          ->cref(),
      3);
  // change to sendToCPU = true
  action.setSendToQueue(make_pair(queueAction, true));
  EXPECT_EQ(action.getSendToQueue().value().second, true);
  entry->setAclAction(action);
  serialized = entry->toThrift();
  entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getAclAction());
  aclAction = entryBack->getAclAction();
  EXPECT_TRUE(aclAction->cref<switch_state_tags::sendToQueue>() != nullptr);
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::sendToQueue>()
          ->cref<switch_state_tags::sendToCPU>()
          ->cref(),
      true);
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::sendToQueue>()
          ->cref<switch_state_tags::action>()
          ->cref<switch_config_tags::queueId>()
          ->cref(),
      3);
}

TEST(Acl, SerializeRedirectToNextHop) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  MatchAction action = MatchAction();
  auto cfgRedirectToNextHop = cfg::RedirectToNextHopAction();
  std::vector<std::string> nexthops = {
      "2401:db00:e112:9103:1028::1b",
      "fe80:db00:e112:9103:2028::1b",
      "10.0.0.1",
      "10.0.0.2"};
  int intfID = 0;
  for (auto nh : nexthops) {
    cfg::RedirectNextHop nhop;
    nhop.ip() = nh;
    nhop.intfID() = ++intfID;
    cfgRedirectToNextHop.redirectNextHops()->push_back(nhop);
  }
  auto redirectToNextHop = MatchAction::RedirectToNextHopAction();
  redirectToNextHop.first = cfgRedirectToNextHop;

  // Only nexthops with V6 link local (fe80 subnet) address can be treated as
  // ResolvedNextHops
  std::vector<folly::IPAddress> nhAddrs = {
      folly::IPAddress("fe80:db00:e113:9103:1028::1b"),
      folly::IPAddress("fe80:db00:e113:9103:1028::1c"),
      folly::IPAddress("2401:db00:e112:9103:1028::1b"),
      folly::IPAddress("100.0.0.1"),
      folly::IPAddress("100.0.0.2")};
  auto nhset = MatchAction::NextHopSet();

  auto setNhAddrs = [](MatchAction::NextHopSet& nhset,
                       std::vector<folly::IPAddress>& nhAddrs) {
    int outIntfID = 0;
    int weight = 100;
    for (auto nhAddr : nhAddrs) {
      if (nhAddr.isV6() && nhAddr.isLinkLocal()) {
        nhset.insert(ResolvedNextHop(nhAddr, InterfaceID(outIntfID), weight));
        ++outIntfID;
      } else {
        nhset.insert(UnresolvedNextHop(nhAddr, weight));
      }
      ++weight;
    }
  };
  setNhAddrs(nhset, nhAddrs);

  redirectToNextHop.second = nhset;
  action.setRedirectToNextHop(redirectToNextHop);
  entry->setAclAction(action);
  entry->setEnabled(true);

  auto verifyEntries = [](AclEntry& entry,
                          std::vector<std::string>& nexthops,
                          MatchAction::NextHopSet& nhset) {
    auto serialized = entry.toThrift();
    auto entryBack = std::make_shared<AclEntry>(serialized);
    EXPECT_TRUE(entry == *entryBack);
    validateThriftStructNodeSerialization(entry);
    const auto& aclAction = entryBack->getAclAction();
    const auto& newRedirectToNextHop =
        aclAction->cref<switch_state_tags::redirectToNextHop>();
    int i = 0;
    int outIntfID = 0;
    const auto& redirectAction =
        newRedirectToNextHop->cref<switch_state_tags::action>();
    for (const auto& nh : std::as_const(
             *(redirectAction->cref<switch_config_tags::redirectNextHops>()))) {
      EXPECT_EQ(nh->cref<switch_config_tags::ip>()->cref(), nexthops[i]);
      EXPECT_EQ(nh->cref<switch_config_tags::intfID>()->cref(), ++outIntfID);
      ++i;
    }

    auto nhops = util::toRouteNextHopSet(
        newRedirectToNextHop->cref<switch_state_tags::resolvedNexthops>()
            ->toThrift());
    EXPECT_EQ(nhset, nhops);
    EXPECT_EQ(entry.isEnabled(), entryBack->isEnabled());
  };
  verifyEntries(*entry, nexthops, nhset);

  // Update nexthops
  nexthops.pop_back();
  nexthops.emplace_back("1000:db00:e112:9103:1028::1b");
  nexthops.emplace_back("10.0.0.3");
  redirectToNextHop.first.redirectNextHops()->clear();
  intfID = 0;
  for (auto nh : nexthops) {
    cfg::RedirectNextHop nhop;
    nhop.ip() = nh;
    nhop.intfID() = ++intfID;
    redirectToNextHop.first.redirectNextHops()->push_back(nhop);
  }
  action.setRedirectToNextHop(redirectToNextHop);
  entry->setAclAction(action);

  verifyEntries(*entry, nexthops, nhset);

  nhAddrs.pop_back();
  nhAddrs.pop_back();
  nhAddrs.emplace_back("fe80:db00:e113:9103:1028::2a");
  nhAddrs.emplace_back("100.0.0.3");
  nhset = MatchAction::NextHopSet();
  setNhAddrs(nhset, nhAddrs);

  // Update resoved nexthops
  redirectToNextHop.second = nhset;
  action.setRedirectToNextHop(redirectToNextHop);
  entry->setAclAction(action);

  verifyEntries(*entry, nexthops, nhset);
}

TEST(Acl, SerializePacketCounter) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  MatchAction action = MatchAction();
  auto counter = cfg::TrafficCounter();
  *counter.name() = "stat0.c";
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getAclAction());
  const auto& aclAction = entryBack->getAclAction();

  EXPECT_TRUE(aclAction->cref<switch_state_tags::trafficCounter>() != nullptr);

  EXPECT_EQ(
      aclAction->cref<switch_state_tags::trafficCounter>()
          ->cref<switch_config_tags::name>()
          ->cref(),
      "stat0.c");
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::trafficCounter>()
          ->cref<switch_config_tags::types>()
          ->size(),
      1);
  EXPECT_EQ(
      aclAction->cref<switch_state_tags::trafficCounter>()
          ->cref<switch_config_tags::types>()
          ->cref(0)
          ->toThrift(),
      cfg::CounterType::PACKETS);

  // Test SetDscpMatchAction
  entry = std::make_unique<AclEntry>(0, std::string("DspNew"));
  action = MatchAction();
  auto setDscpMatchAction = cfg::SetDscpMatchAction();
  *setDscpMatchAction.dscpValue() = 8;
  action.setSetDscp(setDscpMatchAction);
  entry->setAclAction(action);

  serialized = entry->toThrift();
  entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getAclAction());
  const auto& aclAction1 = entryBack->getAclAction();
  EXPECT_TRUE(aclAction1->cref<switch_state_tags::setDscp>() != nullptr);
  EXPECT_EQ(
      aclAction1->cref<switch_state_tags::setDscp>()
          ->cref<switch_config_tags::dscpValue>()
          ->toThrift(),
      8);

  // Set 2 counter types
  *counter.types() = {cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  serialized = entry->toThrift();
  entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  const auto& aclAction2 = entryBack->getAclAction();
  EXPECT_EQ(
      aclAction2->cref<switch_state_tags::trafficCounter>()
          ->cref<switch_config_tags::types>()
          ->size(),
      2);
  EXPECT_EQ(
      aclAction2->cref<switch_state_tags::trafficCounter>()
          ->cref<switch_config_tags::types>()
          ->toThrift(),
      *counter.types());
}

TEST(Acl, Ttl) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  // set ttl
  config.acls()[0].ttl() = cfg::Ttl();
  *config.acls()[0].ttl()->value() = 42;
  *config.acls()[0].ttl()->mask() = 0xff;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("acl1");
  ASSERT_NE(nullptr, aclV1);
  EXPECT_TRUE(aclV1->getTtl());
  EXPECT_EQ(aclV1->getTtl().value().getValue(), 42);
  EXPECT_EQ(aclV1->getTtl().value().getMask(), 0xff);

  // check invalid configs
  *config.acls()[0].ttl()->value() = 256;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  *config.acls()[0].ttl()->value() = -1;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  *config.acls()[0].ttl()->value() = 42;
  *config.acls()[0].ttl()->mask() = 256;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
  *config.acls()[0].ttl()->mask() = -1;
  EXPECT_THROW(
      publishAndApplyConfig(stateV1, &config, platform.get()), FbossError);
}

TEST(Acl, TtlSerialization) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  entry->setTtl(AclTtl(42, 0xff));
  auto action = MatchAction();
  auto counter = cfg::TrafficCounter();
  *counter.name() = "stat0.c";
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getTtl());
  EXPECT_EQ(entryBack->getTtl().value().getValue(), 42);
  EXPECT_EQ(entryBack->getTtl().value().getMask(), 0xff);

  // check invalid configs
  auto ttl = AclTtl(42, 0xff);
  EXPECT_THROW(AclTtl(256, 0xff), FbossError);
  EXPECT_THROW(AclTtl(42, 256), FbossError);
  EXPECT_THROW(ttl.setValue(256), FbossError);
  EXPECT_THROW(ttl.setValue(-1), FbossError);
  EXPECT_THROW(ttl.setMask(256), FbossError);
  EXPECT_THROW(ttl.setMask(-1), FbossError);
}

TEST(Acl, PacketLookupResultSerialization) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  entry->setPacketLookupResult(
      cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH);
  auto action = MatchAction();
  auto counter = cfg::TrafficCounter();
  counter.name() = "stat0.c";
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getPacketLookupResult());
  EXPECT_EQ(
      entryBack->getPacketLookupResult().value(),
      cfg::PacketLookupResultType::PACKET_LOOKUP_RESULT_MPLS_NO_MATCH);
}

TEST(Acl, VlanIDSerialization) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  entry->setVlanID(2001);
  auto action = MatchAction();
  auto counter = cfg::TrafficCounter();
  counter.name() = "stat0.c";
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getVlanID());
  EXPECT_EQ(entryBack->getVlanID().value(), 2001);
}

TEST(Acl, IpType) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  // set IpType
  auto ipType = cfg::IpType::IP6;
  config.acls()[0].ipType() = ipType;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);
  auto aclV1 = stateV1->getAcl("acl1");
  EXPECT_NE(nullptr, aclV1);
  EXPECT_TRUE(aclV1->getIpType());
  EXPECT_EQ(aclV1->getIpType().value(), ipType);
}

TEST(Acl, LookupClass) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl1";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;

  // set lookupClassL2
  auto lookupClassL2 = cfg::AclLookupClass::DST_CLASS_L3_LOCAL_1;
  config.acls()[0].lookupClassL2() = lookupClassL2;

  // apply lookupClassL2 config and validate
  auto stateV2 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  auto aclV2 = stateV2->getAcl("acl1");
  ASSERT_NE(nullptr, aclV2);
  EXPECT_TRUE(aclV2->getLookupClassL2());
  EXPECT_EQ(aclV2->getLookupClassL2().value(), lookupClassL2);

  // set lookupClassNeighbor
  auto lookupClassNeighbor = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0;
  config.acls()[0].lookupClassNeighbor() = lookupClassNeighbor;

  // apply lookupClassNeighbor config and validate
  auto stateV3 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV3);
  auto aclV3 = stateV3->getAcl("acl1");
  ASSERT_NE(nullptr, aclV3);
  EXPECT_TRUE(aclV3->getLookupClassNeighbor());
  EXPECT_EQ(aclV3->getLookupClassNeighbor().value(), lookupClassNeighbor);

  // set lookupClassRoute
  auto lookupClassRoute = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0;
  config.acls()[0].lookupClassRoute() = lookupClassRoute;

  // apply lookupClassRoute config and validate
  auto stateV4 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV4);
  auto aclV4 = stateV4->getAcl("acl1");
  ASSERT_NE(nullptr, aclV4);
  EXPECT_TRUE(aclV4->getLookupClassRoute());
  EXPECT_EQ(aclV4->getLookupClassRoute().value(), lookupClassRoute);
}

TEST(Acl, LookupClassSerialization) {
  auto action = MatchAction();
  auto counter = cfg::TrafficCounter();
  *counter.name() = "stat0.c";
  action.setTrafficCounter(counter);

  // test for lookupClassL2 serialization/de-serialization
  auto entryL2 = std::make_unique<AclEntry>(0, std::string("stat1"));
  auto lookupClassL2 = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_7;
  entryL2->setLookupClassL2(lookupClassL2);
  entryL2->setAclAction(action);

  auto serialized = entryL2->toThrift();
  auto entryBackL2 = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entryL2);

  EXPECT_TRUE(*entryL2 == *entryBackL2);
  EXPECT_TRUE(entryBackL2->getLookupClassL2());
  EXPECT_EQ(entryBackL2->getLookupClassL2().value(), lookupClassL2);

  // test for lookupClassNeighbor serialization/de-serialization
  auto entryNeighbor = std::make_unique<AclEntry>(0, std::string("stat1"));
  auto lookupClassNeighbor = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_7;
  entryNeighbor->setLookupClassNeighbor(lookupClassNeighbor);
  entryNeighbor->setAclAction(action);

  serialized = entryNeighbor->toThrift();
  auto entryBackNeighbor = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entryNeighbor);

  EXPECT_TRUE(*entryNeighbor == *entryBackNeighbor);
  EXPECT_TRUE(entryBackNeighbor->getLookupClassNeighbor());
  EXPECT_EQ(
      entryBackNeighbor->getLookupClassNeighbor().value(), lookupClassNeighbor);

  // test for lookupClassRoute serialization/de-serialization
  auto entryRoute = std::make_unique<AclEntry>(0, std::string("stat1"));
  auto lookupClassRoute = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_7;
  entryRoute->setLookupClassRoute(lookupClassRoute);
  entryRoute->setAclAction(action);

  serialized = entryRoute->toThrift();
  auto entryBackRoute = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entryRoute);

  EXPECT_TRUE(*entryRoute == *entryBackRoute);
  EXPECT_TRUE(entryBackRoute->getLookupClassRoute());
  EXPECT_EQ(entryBackRoute->getLookupClassRoute().value(), lookupClassRoute);
}

TEST(Acl, InvalidTrafficCounter) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl0";
  *config.acls()[0].actionType() = cfg::AclActionType::PERMIT;
  config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  config.dataPlaneTrafficPolicy()->matchToAction()->resize(1);
  *config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl0";
  config.dataPlaneTrafficPolicy()->matchToAction()[0].action()->counter() =
      "stat0";

  EXPECT_THROW(
      publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
}

TEST(Acl, VerifyTrafficCounter) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(4);
  *config.acls()[0].name() = "acl0";
  *config.acls()[0].actionType() = cfg::AclActionType::PERMIT;
  *config.acls()[1].name() = "acl1";
  *config.acls()[1].actionType() = cfg::AclActionType::DENY;
  config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  config.dataPlaneTrafficPolicy()->matchToAction()->resize(2);
  *config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl0";
  config.dataPlaneTrafficPolicy()->matchToAction()[0].action()->counter() =
      "stat0";
  *config.dataPlaneTrafficPolicy()->matchToAction()[1].matcher() = "acl1";
  config.dataPlaneTrafficPolicy()->matchToAction()[1].action()->counter() =
      "stat1";
  config.trafficCounters()->resize(2);
  *config.trafficCounters()[0].name() = "stat0";
  *config.trafficCounters()[1].name() = "stat1";
  *config.acls()[2].name() = "acl2";
  *config.acls()[2].actionType() = cfg::AclActionType::PERMIT;
  *config.acls()[3].name() = "acl3";
  *config.acls()[3].actionType() = cfg::AclActionType::DENY;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV1);

  auto verify = [](shared_ptr<SwitchState> state,
                   std::string aclName,
                   std::string counterName,
                   cfg::AclActionType aclActionType,
                   bool verifyTrafficCounter) {
    auto acl = state->getAcl(aclName);
    ASSERT_NE(nullptr, acl);
    ASSERT_EQ(acl->getActionType(), aclActionType);
    auto aclAction = acl->getAclAction();
    if (verifyTrafficCounter) {
      ASSERT_NE(nullptr, aclAction);
      EXPECT_TRUE(
          aclAction->cref<switch_state_tags::trafficCounter>() != nullptr);
      EXPECT_EQ(
          aclAction->cref<switch_state_tags::trafficCounter>()
              ->cref<switch_config_tags::name>()
              ->cref(),
          counterName);
      EXPECT_EQ(
          aclAction->cref<switch_state_tags::trafficCounter>()
              ->cref<switch_config_tags::types>()
              ->size(),
          1);
      EXPECT_EQ(
          aclAction->cref<switch_state_tags::trafficCounter>()
              ->cref<switch_config_tags::types>()
              ->cref(0)
              ->toThrift(),
          cfg::CounterType::PACKETS);
    } else {
      ASSERT_EQ(nullptr, aclAction);
    }
  };

  verify(stateV1, "acl0", "stat0", cfg::AclActionType::PERMIT, true);
  verify(stateV1, "acl1", "stat1", cfg::AclActionType::DENY, true);
  verify(stateV1, "acl2", "", cfg::AclActionType::PERMIT, false);
  verify(stateV1, "acl3", "", cfg::AclActionType::DENY, false);
}

TEST(Acl, GetRequiredAclTableQualifiers) {
  cfg::SwitchConfig config;
  config.acls();
  config.acls()->resize(2);

  auto setAclQualifiers = [&](std::string ip, std::string name) {
    cfg::AclEntry acl;
    acl.name() = name;
    acl.srcIp() = ip;
    acl.dstIp() = ip;
    acl.proto() = 50;
    acl.tcpFlagsBitMap() = 123;
    acl.srcPort() = 1001;
    acl.dstPort() = 2002;
    acl.ipFrag() = cfg::IpFragMatch::MATCH_ANY_FRAGMENT;
    acl.dscp() = 8;
    acl.ipType() = cfg::IpType::ANY;
    cfg::Ttl ttl;
    ttl.value() = 255;
    ttl.mask() = 0xff;
    acl.ttl() = ttl;
    acl.dstMac() = "a:b:c:d:e:f";
    acl.l4SrcPort() = 4005;
    acl.l4DstPort() = 4005;
    acl.lookupClassL2() = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0;
    acl.lookupClassNeighbor() =
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1;
    acl.lookupClassRoute() = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2;
    acl.actionType() = cfg::AclActionType::DENY;
    acl.udfGroups() = kUdfList;
    acl.roceBytes() = kRoceBytes;
    acl.roceMask() = kRoceMask;
    return acl;
  };
  config.acls()[0] = setAclQualifiers("10.0.0.1/32", "acl0");
  config.acls()[1] = setAclQualifiers("1::1/128", "acl1");

  config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  config.dataPlaneTrafficPolicy()->matchToAction()->resize(2);
  config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl0";
  config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = "acl1";

  config.udfConfig() = makeUdfConfig(kUdfList);

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  validateThriftMapMapSerialization(*stateV1->getAcls());
  auto q0 =
      stateV1->getAcls()->getNodeIf("acl0")->getRequiredAclTableQualifiers();
  auto q1 =
      stateV1->getAcls()->getNodeIf("acl1")->getRequiredAclTableQualifiers();

  std::set<cfg::AclTableQualifier> qualifiers0{
      cfg::AclTableQualifier::SRC_IPV4,
      cfg::AclTableQualifier::DST_IPV4,
      cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
      cfg::AclTableQualifier::TCP_FLAGS,
      cfg::AclTableQualifier::IP_FRAG,
      cfg::AclTableQualifier::DSCP,
      cfg::AclTableQualifier::IP_TYPE,
      cfg::AclTableQualifier::TTL,
      cfg::AclTableQualifier::DST_MAC,
      cfg::AclTableQualifier::L4_SRC_PORT,
      cfg::AclTableQualifier::L4_DST_PORT,
      cfg::AclTableQualifier::SRC_PORT,
      cfg::AclTableQualifier::OUT_PORT,
      cfg::AclTableQualifier::LOOKUP_CLASS_L2,
      cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
      cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE,
      cfg::AclTableQualifier::UDF};

  std::set<cfg::AclTableQualifier> qualifiers1{
      cfg::AclTableQualifier::SRC_IPV6,
      cfg::AclTableQualifier::DST_IPV6,
      cfg::AclTableQualifier::IP_PROTOCOL_NUMBER,
      cfg::AclTableQualifier::TCP_FLAGS,
      cfg::AclTableQualifier::IP_FRAG,
      cfg::AclTableQualifier::DSCP,
      cfg::AclTableQualifier::IP_TYPE,
      cfg::AclTableQualifier::TTL,
      cfg::AclTableQualifier::DST_MAC,
      cfg::AclTableQualifier::L4_SRC_PORT,
      cfg::AclTableQualifier::L4_DST_PORT,
      cfg::AclTableQualifier::SRC_PORT,
      cfg::AclTableQualifier::OUT_PORT,
      cfg::AclTableQualifier::LOOKUP_CLASS_L2,
      cfg::AclTableQualifier::LOOKUP_CLASS_NEIGHBOR,
      cfg::AclTableQualifier::LOOKUP_CLASS_ROUTE,
      cfg::AclTableQualifier::UDF};

  EXPECT_EQ(q0, qualifiers0);
  EXPECT_EQ(q1, qualifiers1);
}

TEST(Acl, L4DstPortRangeSerialization) {
  auto entry = std::make_unique<AclEntry>(0, std::string("stat0"));
  cfg::Range range;
  range.minimum() = 1000;
  range.maximum() = 2000;
  entry->setL4DstPortRange(range);
  auto action = MatchAction();
  auto counter = cfg::TrafficCounter();
  counter.name() = "stat0.c";
  action.setTrafficCounter(counter);
  entry->setAclAction(action);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_TRUE(entryBack->getL4DstPortRange());
  EXPECT_EQ(*entryBack->getL4DstPortRange()->minimum(), 1000);
  EXPECT_EQ(*entryBack->getL4DstPortRange()->maximum(), 2000);
}

TEST(Acl, L4DstPortRangeHasMatcher) {
  auto entry = std::make_unique<AclEntry>(0, std::string("acl0"));
  EXPECT_FALSE(entry->hasMatcher());
  cfg::Range range;
  range.minimum() = 80;
  range.maximum() = 443;
  entry->setL4DstPortRange(range);
  EXPECT_TRUE(entry->hasMatcher());
}

TEST(Acl, L4DstPortRangeQualifier) {
  auto entry = std::make_unique<AclEntry>(0, std::string("acl0"));
  cfg::Range range;
  range.minimum() = 80;
  range.maximum() = 443;
  entry->setL4DstPortRange(range);
  auto qualifiers = entry->getRequiredAclTableQualifiers();
  EXPECT_TRUE(
      qualifiers.find(cfg::AclTableQualifier::L4_DST_PORT_RANGE) !=
      qualifiers.end());
}

TEST(Acl, LookupClassPort) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();

  cfg::SwitchConfig config;
  config.acls()->resize(1);
  config.acls()[0].name() = "acl1";
  config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].lookupClassPort() =
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED;

  auto newState = publishAndApplyConfig(state, &config, platform.get());
  ASSERT_NE(nullptr, newState);
  auto acl = newState->getAcl("acl1");
  ASSERT_NE(nullptr, acl);
  ASSERT_TRUE(acl->getLookupClassPort().has_value());
  EXPECT_EQ(
      acl->getLookupClassPort().value(),
      cfg::AclLookupClassPort::CLASS_PORT_RESTRICTED);
  EXPECT_TRUE(acl->hasMatcher());
  EXPECT_TRUE(acl->getRequiredAclTableQualifiers().contains(
      cfg::AclTableQualifier::LOOKUP_CLASS_PORT));

  auto serialized = std::make_shared<AclEntry>(acl->toThrift());
  EXPECT_EQ(serialized->getLookupClassPort(), acl->getLookupClassPort());
}

TEST(Acl, PbrFieldsSerialize) {
  auto entry = std::make_unique<AclEntry>(0, std::string("pbr0"));
  entry->setTrafficClass(3);
  entry->setNextHopGroupId(42);

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<AclEntry>(serialized);
  validateNodeSerialization(*entry);

  EXPECT_TRUE(*entry == *entryBack);
  EXPECT_EQ(entryBack->getTrafficClass(), 3);
  EXPECT_EQ(entryBack->getNextHopGroupId(), 42);
}

TEST(Acl, PbrFieldsHasMatcher) {
  auto entry = std::make_unique<AclEntry>(0, std::string("pbr0"));
  EXPECT_FALSE(entry->hasMatcher());
  entry->setTrafficClass(3);
  EXPECT_TRUE(entry->hasMatcher());

  auto entry2 = std::make_unique<AclEntry>(0, std::string("pbr1"));
  EXPECT_FALSE(entry2->hasMatcher());
  entry2->setNextHopGroupId(42);
  EXPECT_TRUE(entry2->hasMatcher());
}

TEST(Acl, PbrFieldsQualifier) {
  auto entry = std::make_unique<AclEntry>(0, std::string("pbr0"));
  entry->setTrafficClass(3);
  entry->setNextHopGroupId(42);
  auto qualifiers = entry->getRequiredAclTableQualifiers();
  EXPECT_TRUE(qualifiers.find(cfg::AclTableQualifier::TC) != qualifiers.end());
  EXPECT_TRUE(
      qualifiers.find(cfg::AclTableQualifier::NEXT_HOP_GROUP_ID) !=
      qualifiers.end());
}

TEST(Acl, ActionTypeDenyDataAndControlPlane) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl0";
  config.acls()[0].l4DstPort() = 179;
  *config.acls()[0].actionType() =
      cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  auto acl = stateV1->getAcl("acl0");
  ASSERT_NE(nullptr, acl);
  EXPECT_EQ(
      cfg::AclActionType::DENY_DATA_AND_CONTROL_PLANE, acl->getActionType());
}

TEST(Acl, L4DstPortRangeValidation) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  auto makeConfig = []() {
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(config.ports()[0], 1);
    config.acls()->resize(1);
    *config.acls()[0].name() = "acl0";
    *config.acls()[0].actionType() = cfg::AclActionType::DENY;
    return config;
  };

  // min > max
  {
    auto config = makeConfig();
    cfg::Range range;
    range.minimum() = 2000;
    range.maximum() = 1000;
    config.acls()[0].l4DstPortRange() = range;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  }

  // minimum out of bounds
  {
    auto config = makeConfig();
    cfg::Range range;
    range.minimum() = 65536;
    range.maximum() = 65536;
    config.acls()[0].l4DstPortRange() = range;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  }

  // maximum out of bounds
  {
    auto config = makeConfig();
    cfg::Range range;
    range.minimum() = 0;
    range.maximum() = 65536;
    config.acls()[0].l4DstPortRange() = range;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  }

  // mutual exclusivity with l4DstPort
  {
    auto config = makeConfig();
    config.acls()[0].l4DstPort() = 80;
    cfg::Range range;
    range.minimum() = 80;
    range.maximum() = 443;
    config.acls()[0].l4DstPortRange() = range;
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  }

  // valid range succeeds
  {
    auto config = makeConfig();
    cfg::Range range;
    range.minimum() = 1000;
    range.maximum() = 2000;
    config.acls()[0].l4DstPortRange() = range;
    auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
    auto acl = stateV1->getAcl("acl0");
    ASSERT_NE(nullptr, acl);
    EXPECT_TRUE(acl->getL4DstPortRange());
    EXPECT_EQ(*acl->getL4DstPortRange()->minimum(), 1000);
    EXPECT_EQ(*acl->getL4DstPortRange()->maximum(), 2000);
  }
}

TEST(Acl, TcpFlagsMaskValidation) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  auto makeConfig = [](std::optional<int16_t> tcpFlagsBitMap,
                       std::optional<int16_t> tcpFlagsMask) {
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(config.ports()[0], 1);
    config.acls()->resize(1);
    *config.acls()[0].name() = "acl0";
    *config.acls()[0].actionType() = cfg::AclActionType::DENY;
    config.acls()[0].proto() = 6;
    if (tcpFlagsBitMap) {
      config.acls()[0].tcpFlagsBitMap() = *tcpFlagsBitMap;
    }
    if (tcpFlagsMask) {
      config.acls()[0].tcpFlagsMask() = *tcpFlagsMask;
    }
    return config;
  };
  auto expectRejected = [&](std::optional<int16_t> tcpFlagsBitMap,
                            std::optional<int16_t> tcpFlagsMask) {
    auto config = makeConfig(tcpFlagsBitMap, tcpFlagsMask);
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config, platform.get()), FbossError);
  };

  // The bitmap must fit in the 8-bit TCP flags field.
  expectRejected(256, std::nullopt);
  // A negative bitmap is not a valid set of flags.
  expectRejected(-1, std::nullopt);
  // A mask has nothing to apply to without a bitmap.
  expectRejected(std::nullopt, 0x12);
  // A zero mask compares no flags, so the rule would match all TCP.
  expectRejected(0x02, 0);
  // A negative mask is not a valid set of flags.
  expectRejected(0x02, -1);
  // 0x40 (ECE) is past the six flags a mask may cover.
  expectRejected(0x02, 0x40);
  // The state field is a byte, so this must not be truncated to 0x12.
  expectRejected(0x02, 0x112);
  // 0x08 (PSH) is outside the mask, so it would never be compared.
  expectRejected(0x0A, 0x12);

  auto config = makeConfig(0x02, 0x12);
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto acl = stateV1->getAcl("acl0");
  ASSERT_NE(nullptr, acl);
  EXPECT_EQ(acl->getTcpFlagsBitMap(), 0x02);
  EXPECT_EQ(acl->getTcpFlagsMask(), 0x12);
}

TEST(Acl, TcpFlagsMaskChange) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(1);
  preparedMockPortConfig(config.ports()[0], 1);
  config.acls()->resize(1);
  *config.acls()[0].name() = "acl0";
  *config.acls()[0].actionType() = cfg::AclActionType::DENY;
  config.acls()[0].proto() = 6;
  config.acls()[0].tcpFlagsBitMap() = 0x02;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_EQ(stateV1->getAcl("acl0")->getTcpFlagsMask(), std::nullopt);

  config.acls()[0].tcpFlagsMask() = 0x12;
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  ASSERT_NE(nullptr, stateV2);
  EXPECT_EQ(stateV2->getAcl("acl0")->getTcpFlagsMask(), 0x12);

  config.acls()[0].tcpFlagsMask().reset();
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  EXPECT_EQ(stateV3->getAcl("acl0")->getTcpFlagsMask(), std::nullopt);
}

TEST(Acl, DstIpV6WordValidation) {
  FLAGS_enable_acl_table_group = false;
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());

  auto makeConfig = []() {
    cfg::SwitchConfig config;
    config.ports()->resize(1);
    preparedMockPortConfig(config.ports()[0], 1);
    config.acls()->resize(1);
    config.acls()[0].name() = "acl0";
    config.acls()[0].actionType() = cfg::AclActionType::DENY;
    return config;
  };

  auto applyConfig = [&](cfg::SwitchConfig* config) {
    return publishAndApplyConfig(stateV0, config, platform.get());
  };

  // dstIp alone is valid.
  {
    auto config = makeConfig();
    config.acls()[0].dstIp() = "1234:5678:9abc:def0::1/128";
    auto stateV1 = applyConfig(&config);
    auto acl = stateV1->getAcl("acl0");
    ASSERT_NE(nullptr, acl);
    EXPECT_EQ(acl->getDstIp().first.str(), "1234:5678:9abc:def0::1");
    EXPECT_EQ(acl->getDstIp().second, 128);
    EXPECT_FALSE(acl->getDstIpV6Word2());
    EXPECT_FALSE(acl->getDstIpV6Word3());
  }

  // dstIpV6Word2 alone is valid.
  {
    auto config = makeConfig();
    config.acls()[0].dstIpV6Word2() = 0x9abcdef0;
    auto stateV1 = applyConfig(&config);
    auto acl = stateV1->getAcl("acl0");
    ASSERT_NE(nullptr, acl);
    EXPECT_FALSE(acl->getDstIp().first);
    EXPECT_EQ(acl->getDstIpV6Word2(), 0x9abcdef0);
    EXPECT_FALSE(acl->getDstIpV6Word3());
  }

  // dstIpV6Word3 alone is valid.
  {
    auto config = makeConfig();
    config.acls()[0].dstIpV6Word3() = 0x12345678;
    auto stateV1 = applyConfig(&config);
    auto acl = stateV1->getAcl("acl0");
    ASSERT_NE(nullptr, acl);
    EXPECT_FALSE(acl->getDstIp().first);
    EXPECT_FALSE(acl->getDstIpV6Word2());
    EXPECT_EQ(acl->getDstIpV6Word3(), 0x12345678);
  }

  // dstIpV6Word2 and dstIpV6Word3 together are valid.
  {
    auto config = makeConfig();
    config.acls()[0].dstIpV6Word2() = 0x9abcdef0;
    config.acls()[0].dstIpV6Word3() = 0x12345678;
    auto stateV1 = applyConfig(&config);
    auto acl = stateV1->getAcl("acl0");
    ASSERT_NE(nullptr, acl);
    EXPECT_FALSE(acl->getDstIp().first);
    EXPECT_EQ(acl->getDstIpV6Word2(), 0x9abcdef0);
    EXPECT_EQ(acl->getDstIpV6Word3(), 0x12345678);
  }

  // dstIp cannot be combined with dstIpV6Word2.
  {
    auto config = makeConfig();
    config.acls()[0].dstIp() = "1234:5678:9abc:def0::1/128";
    config.acls()[0].dstIpV6Word2() = 0x9abcdef0;
    EXPECT_THROW(applyConfig(&config), FbossError);
  }

  // dstIp cannot be combined with dstIpV6Word3.
  {
    auto config = makeConfig();
    config.acls()[0].dstIp() = "1234:5678:9abc:def0::1/128";
    config.acls()[0].dstIpV6Word3() = 0x12345678;
    EXPECT_THROW(applyConfig(&config), FbossError);
  }

  // dstIp cannot be combined with both DST IPv6 word qualifiers.
  {
    auto config = makeConfig();
    config.acls()[0].dstIp() = "1234:5678:9abc:def0::1/128";
    config.acls()[0].dstIpV6Word2() = 0x9abcdef0;
    config.acls()[0].dstIpV6Word3() = 0x12345678;
    EXPECT_THROW(applyConfig(&config), FbossError);
  }
}

namespace {

std::shared_ptr<AclEntry> makeAclEntryWithCounter(
    const std::optional<std::string>& counterName,
    uint8_t dscp = 0x24) {
  auto entry = std::make_shared<AclEntry>(1, std::string("acl0"));
  entry->setDscp(dscp);
  entry->setActionType(cfg::AclActionType::PERMIT);
  if (counterName.has_value()) {
    MatchAction action;
    cfg::TrafficCounter counter;
    counter.name() = *counterName;
    counter.types() = {cfg::CounterType::PACKETS};
    action.setTrafficCounter(counter);
    entry->setAclAction(action);
  }
  return entry;
}

} // namespace

TEST(Acl, onlyCounterChangedCounterRenamed) {
  EXPECT_TRUE(onlyCounterChanged(
      makeAclEntryWithCounter("counter1"),
      makeAclEntryWithCounter("counter2")));
}

TEST(Acl, onlyCounterChangedCounterAdded) {
  EXPECT_TRUE(onlyCounterChanged(
      makeAclEntryWithCounter(std::nullopt),
      makeAclEntryWithCounter("counter1")));
}

TEST(Acl, onlyCounterChangedCounterRemoved) {
  EXPECT_TRUE(onlyCounterChanged(
      makeAclEntryWithCounter("counter1"),
      makeAclEntryWithCounter(std::nullopt)));
}

TEST(Acl, onlyCounterChangedCounterTypesChanged) {
  auto oldEntry = makeAclEntryWithCounter("counter1");
  auto newEntry = makeAclEntryWithCounter("counter1");
  MatchAction action;
  cfg::TrafficCounter counter;
  counter.name() = "counter1";
  counter.types() = {cfg::CounterType::PACKETS, cfg::CounterType::BYTES};
  action.setTrafficCounter(counter);
  newEntry->setAclAction(action);

  EXPECT_TRUE(onlyCounterChanged(oldEntry, newEntry));
}

TEST(Acl, onlyCounterChangedNothingChanged) {
  EXPECT_FALSE(onlyCounterChanged(
      makeAclEntryWithCounter("counter1"),
      makeAclEntryWithCounter("counter1")));
}

TEST(Acl, onlyCounterChangedNothingChangedNoCounter) {
  EXPECT_FALSE(onlyCounterChanged(
      makeAclEntryWithCounter(std::nullopt),
      makeAclEntryWithCounter(std::nullopt)));
}

TEST(Acl, onlyCounterChangedQualifierChangedToo) {
  EXPECT_FALSE(onlyCounterChanged(
      makeAclEntryWithCounter("counter1", 0x24),
      makeAclEntryWithCounter("counter2", 0x30)));
}

TEST(Acl, onlyCounterChangedOnlyQualifierChanged) {
  EXPECT_FALSE(onlyCounterChanged(
      makeAclEntryWithCounter("counter1", 0x24),
      makeAclEntryWithCounter("counter1", 0x30)));
}

TEST(Acl, onlyCounterChangedNonCounterActionChangedToo) {
  auto oldEntry = makeAclEntryWithCounter("counter1");
  auto newEntry = makeAclEntryWithCounter(std::nullopt);
  MatchAction action;
  cfg::TrafficCounter counter;
  counter.name() = "counter2";
  counter.types() = {cfg::CounterType::PACKETS};
  action.setTrafficCounter(counter);
  action.setIngressMirror("mirror0");
  newEntry->setAclAction(action);

  EXPECT_FALSE(onlyCounterChanged(oldEntry, newEntry));
}

TEST(Acl, PrioAclMapHoldsSharedPbrPriority) {
  // 1. Two PBR entries at the one shared priority, told apart only by name.
  auto acls = std::make_shared<AclMap>();
  acls->addNode(
      std::make_shared<AclEntry>(
          FLAGS_pbr_acl_priority, std::string("policyA_tc1")));
  acls->addNode(
      std::make_shared<AclEntry>(
          FLAGS_pbr_acl_priority, std::string("policyA_tc2")));

  // 2. Both survive: before the name joined the key this threw duplicate node
  // ID, which reaches XLOG(FATAL) via getAclsDelta on every state update.
  PrioAclMap prioAcls;
  prioAcls.addAcls(acls);
  EXPECT_EQ(prioAcls.size(), 2);
}

TEST(Acl, PrioAclMapRenameIsAddAndRemove) {
  // 1. Same priority, different name -- a config ACL rename.
  auto makePrioAcls = [](const std::string& name) {
    auto acls = std::make_shared<AclMap>();
    acls->addNode(std::make_shared<AclEntry>(1, name));
    auto prioAcls = std::make_unique<PrioAclMap>();
    prioAcls->addAcls(acls);
    return prioAcls;
  };

  // 2. The name is always part of the key, so a rename is a distinct add and
  // remove rather than one changed event.
  AclMapDelta delta(makePrioAcls("zzz"), makePrioAcls("aaa"));
  int changed = 0, addedOrRemoved = 0;
  for (const auto& entry : delta) {
    (entry.getOld() && entry.getNew()) ? ++changed : ++addedOrRemoved;
  }
  EXPECT_EQ(changed, 0);
  EXPECT_EQ(addedOrRemoved, 2);
}
