/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/gen-cpp2/switch_config_constants.h"

#include <gtest/gtest.h>

#include <unordered_set>

using namespace facebook::fboss;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;

DECLARE_bool(enable_acl_table_group);

const std::string kDscp1 = "dscp1";
const std::string kDscp2 = "dscp2";
const std::string kDscp3 = "dscp3";
const std::string kDscp4 = "dscp4";
const uint8_t kDscpVal1 = 1;
const uint8_t kDscpVal2 = 2;
const uint8_t kDscpVal3 = 3;
const uint8_t kDscpVal4 = 4;

const std::string kTable1 = "table1";
const std::string kTable2 = "table2";
const std::string kTable3 = "table3";

const cfg::AclStage kAclStage1 = cfg::AclStage::INGRESS;
const cfg::AclStage kAclStage2 = cfg::AclStage::INGRESS_MACSEC;

const std::string kGroup1 = "group1";
const std::string kGroup2 = "group2";
const std::string kGroup3 = "group3";

const std::string kAcl1a = "acl1a";
const std::string kAcl1b = "acl1b";
const std::string kAcl1c = "acl1c";
const std::string kAcl1d = "acl1d";
const std::string kAcl2a = "acl2a";
const std::string kAcl2b = "acl2b";
const std::string kAcl3a = "acl3a";

const std::vector<cfg::AclTableActionType> kActionTypes = {
    cfg::AclTableActionType::PACKET_ACTION,
    cfg::AclTableActionType::COUNTER,
    cfg::AclTableActionType::SET_TC};

const std::vector<cfg::AclTableQualifier> kQualifiers = {
    cfg::AclTableQualifier::SRC_IPV6,
    cfg::AclTableQualifier::DST_IPV6,
    cfg::AclTableQualifier::SRC_IPV4,
    cfg::AclTableQualifier::DST_IPV4};

const std::string kUdfGroup1 = "udfGroup1";
const std::string kUdfGroup2 = "udfGroup2";
const std::string kUdfGroup3 = "udfGroup3";

const std::vector<std::string> kAclUdfGroups = {
    kUdfGroup1,
    kUdfGroup2,
    kUdfGroup3};

namespace {

std::shared_ptr<const AclMap> getAclMapFromState(
    std::shared_ptr<SwitchState> state) {
  auto aclMap = state->getAclsForTable(
      kAclStage1, cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE());
  EXPECT_NE(nullptr, aclMap);
  return aclMap;
}

std::shared_ptr<SwitchState> thriftMultiAclSerializeDeserialize(
    const SwitchState& state,
    bool enableMultiAcl) {
  FLAGS_enable_acl_table_group = enableMultiAcl;
  auto thrifty = state.toThrift();
  FLAGS_enable_acl_table_group = !FLAGS_enable_acl_table_group;
  auto thriftIntrStateBack = SwitchState::fromThrift(thrifty);
  FLAGS_enable_acl_table_group = !FLAGS_enable_acl_table_group;
  return thriftIntrStateBack;
}

void verifyMultiAclSerialization(
    const SwitchState& state,
    bool enableMultiAcl) {
  FLAGS_enable_acl_table_group = enableMultiAcl;
  auto thriftIntrStateBack =
      thriftMultiAclSerializeDeserialize(state, enableMultiAcl);
  FLAGS_enable_acl_table_group = !FLAGS_enable_acl_table_group;
  auto thriftyIntr = thriftIntrStateBack->toThrift();
  FLAGS_enable_acl_table_group = !FLAGS_enable_acl_table_group;
  auto thriftStateBack = SwitchState::fromThrift(thriftyIntr);
  if (!enableMultiAcl) {
    EXPECT_EQ(
        state.getAcls()->toThrift(), thriftStateBack->getAcls()->toThrift());
  } else {
    EXPECT_EQ(
        state.cref<switch_state_tags::aclTableGroupMaps>()->toThrift(),
        thriftStateBack->cref<switch_state_tags::aclTableGroupMaps>()
            ->toThrift());
    EXPECT_EQ(
        state.cref<switch_state_tags::portAclTableGroupMaps>()->toThrift(),
        thriftStateBack->cref<switch_state_tags::portAclTableGroupMaps>()
            ->toThrift());
  }
}

void verifyAclHelper(
    std::shared_ptr<const AclMap> map,
    std::shared_ptr<AclEntry> entry1,
    std::shared_ptr<AclEntry> entry2,
    std::shared_ptr<AclEntry> entry3 = nullptr) {
  EXPECT_EQ(map->getEntryIf(entry1->getID())->getID(), kDscp1);
  EXPECT_EQ(map->getEntryIf(entry2->getID())->getID(), kDscp2);
  EXPECT_EQ(map->getEntryIf(entry1->getID())->getDscp(), kDscpVal1);
  EXPECT_EQ(map->getEntryIf(entry2->getID())->getDscp(), kDscpVal2);

  if (entry3 != nullptr) {
    EXPECT_EQ(map->getEntryIf(entry3->getID())->getID(), kDscp3);
    EXPECT_EQ(map->getEntryIf(entry3->getID())->getDscp(), kDscpVal3);
  }
}

void verifyAclHelper(
    std::shared_ptr<const MultiSwitchAclMap> map,
    std::shared_ptr<AclEntry> entry1,
    std::shared_ptr<AclEntry> entry2,
    std::shared_ptr<AclEntry> entry3 = nullptr) {
  EXPECT_EQ(map->getNodeIf(entry1->getID())->getID(), kDscp1);
  EXPECT_EQ(map->getNodeIf(entry2->getID())->getID(), kDscp2);
  EXPECT_EQ(map->getNodeIf(entry1->getID())->getDscp(), kDscpVal1);
  EXPECT_EQ(map->getNodeIf(entry2->getID())->getDscp(), kDscpVal2);

  if (entry3 != nullptr) {
    EXPECT_EQ(map->getNodeIf(entry3->getID())->getID(), kDscp3);
    EXPECT_EQ(map->getNodeIf(entry3->getID())->getDscp(), kDscpVal3);
  }
}

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

} // namespace

TEST(AclGroup, TestEquality) {
  // test AclEntry equality
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  auto entry1a = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2a = std::make_shared<AclEntry>(2, kDscp2);

  EXPECT_EQ(*entry1, *entry1a);
  EXPECT_EQ(*entry2, *entry2a);
  EXPECT_NE(*entry1, *entry2);

  // test AclMap equality
  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1);
  map1->addEntry(entry2);

  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry1a);
  map2->addEntry(entry2a);

  auto map3 = std::make_shared<AclMap>();
  map3->addEntry(entry1);

  EXPECT_EQ(*map1, *map2);
  EXPECT_NE(*map1, *map3);

  map1->removeEntry(entry1);
  EXPECT_NE(*map1, *map2);
  map2->removeEntry(entry1a);
  EXPECT_EQ(*map1, *map2);
  map1->addEntry(entry1);
  map2->addEntry(entry1a);

  // test AclTable equality
  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);
  table1->setActionTypes(kActionTypes);
  table1->setQualifiers(kQualifiers);
  table1->setUdfGroups(kAclUdfGroups);
  auto table2 = std::make_shared<AclTable>(2, kTable1);
  table2->setAclMap(map2);
  table2->setActionTypes(kActionTypes);
  table2->setQualifiers(kQualifiers);
  table2->setUdfGroups(kAclUdfGroups);
  validateNodeSerialization(*table1);
  validateNodeSerialization(*table2);

  EXPECT_NE(*table1, *table2);
  table2->setPriority(1);
  EXPECT_EQ(*table1, *table2);

  // test AclTableMap equality
  auto table3 = std::make_shared<AclTable>(3, kTable2);
  table3->setAclMap(map3);
  validateNodeSerialization(*table3);

  auto tableMap1 = std::make_shared<AclTableMap>();
  tableMap1->addTable(table1);
  tableMap1->addTable(table3);
  validateThriftMapMapSerialization(*tableMap1);

  auto tableMap2 = std::make_shared<AclTableMap>();
  tableMap2->addTable(table2);
  tableMap2->addTable(table3);
  validateThriftMapMapSerialization(*tableMap2);

  table2->setPriority(2);
  EXPECT_NE(*tableMap1, *tableMap2);
  table2->setPriority(1);
  tableMap2->removeTable(table3);
  EXPECT_NE(*tableMap1, *tableMap2);
  tableMap2->addTable(table3);
  EXPECT_EQ(*tableMap1, *tableMap2);

  // test AclTableGroup equality
  auto tableGroup1 = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup1->setAclTableMap(tableMap1);
  tableGroup1->setName(kGroup1);
  auto tableGroup2 = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup2->setAclTableMap(tableMap1);
  tableGroup2->setName(kGroup1);
  auto tableGroup3 = std::make_shared<AclTableGroup>(kAclStage2);
  tableGroup3->setAclTableMap(tableMap1);
  tableGroup2->setName(kGroup1);
  validateNodeSerialization(*tableGroup1);
  validateNodeSerialization(*tableGroup2);
  validateNodeSerialization(*tableGroup3);

  EXPECT_EQ(*tableGroup1, *tableGroup2);
  EXPECT_NE(*tableGroup1, *tableGroup3);
}

TEST(AclGroup, SerializeAclMap) {
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  entry1->setDscp(kDscpVal1);
  entry2->setDscp(kDscpVal2);

  auto map = std::make_shared<MultiSwitchAclMap>();
  map->addNode(entry1, scope());
  map->addNode(entry2, scope());

  auto serialized = map->toThrift();
  auto mapBack = std::make_shared<MultiSwitchAclMap>(serialized);

  EXPECT_EQ(map->toThrift(), mapBack->toThrift());
  verifyAclHelper(mapBack, entry1, entry2);
  EXPECT_EQ(mapBack->getNodeIf(entry1->getID())->getID(), kDscp1);
  EXPECT_EQ(mapBack->getNodeIf(entry2->getID())->getID(), kDscp2);
  EXPECT_EQ(mapBack->getNodeIf(entry1->getID())->getDscp(), kDscpVal1);
  EXPECT_EQ(mapBack->getNodeIf(entry2->getID())->getDscp(), kDscpVal2);

  auto state = SwitchState();
  state.resetAcls(map);
  verifyMultiAclSerialization(state, false);

  auto thriftConvertedState = thriftMultiAclSerializeDeserialize(state, false);
  auto thriftConvertedMap = getAclMapFromState(thriftConvertedState);
  verifyAclHelper(thriftConvertedMap, entry1, entry2);

  // remove an entry
  map->removeNode(entry1);
  EXPECT_FALSE(map->getNodeIf(entry1->getID()));

  serialized = map->toThrift();
  mapBack = std::make_shared<MultiSwitchAclMap>(serialized);

  EXPECT_EQ(map->toThrift(), mapBack->toThrift());
  EXPECT_FALSE(mapBack->getNodeIf(entry1->getID()));
  EXPECT_TRUE(mapBack->getNodeIf(entry2->getID()));
  EXPECT_EQ(mapBack->getNodeIf(entry2->getID())->getDscp(), kDscpVal2);
}

TEST(AclGroup, SerializeAclTable) {
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  entry1->setDscp(kDscpVal1);
  entry2->setDscp(kDscpVal2);

  auto map = std::make_shared<AclMap>();
  map->addEntry(entry1);
  map->addEntry(entry2);

  auto table = std::make_shared<AclTable>(1, kTable1);
  table->setAclMap(map);
  table->setActionTypes(kActionTypes);
  table->setQualifiers(kQualifiers);
  table->setUdfGroups(kAclUdfGroups);
  validateNodeSerialization(*table);

  auto serialized = table->toThrift();
  auto tableBack = std::make_shared<AclTable>(serialized);

  EXPECT_EQ(*table, *tableBack);
  EXPECT_EQ(tableBack->getPriority(), 1);
  EXPECT_EQ(tableBack->getID(), kTable1);
  EXPECT_EQ(*(tableBack->getAclMap()), *map);
  EXPECT_EQ(tableBack->getActionTypes(), kActionTypes);
  EXPECT_EQ(tableBack->getQualifiers(), kQualifiers);
  EXPECT_EQ(tableBack->getUdfGroups()->toThrift(), kAclUdfGroups);

  // change the priority
  table->setPriority(2);
  EXPECT_EQ(table->getPriority(), 2);

  serialized = table->toThrift();
  tableBack = std::make_shared<AclTable>(serialized);

  EXPECT_EQ(*table, *tableBack);
  EXPECT_EQ(tableBack->getPriority(), 2);
  EXPECT_EQ(tableBack->getID(), kTable1);
  EXPECT_EQ(*(tableBack->getAclMap()), *map);
  EXPECT_EQ(tableBack->getActionTypes(), kActionTypes);
  EXPECT_EQ(tableBack->getQualifiers(), kQualifiers);
  EXPECT_EQ(tableBack->getUdfGroups()->toThrift(), kAclUdfGroups);
}

TEST(AclGroup, SerializeAclTableMap) {
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  auto entry3 = std::make_shared<AclEntry>(3, kDscp3);
  entry1->setDscp(kDscpVal1);
  entry2->setDscp(kDscpVal2);
  entry3->setDscp(kDscpVal3);

  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1);
  map1->addEntry(entry2);
  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry3);

  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);
  auto table2 = std::make_shared<AclTable>(2, kTable2);
  table2->setAclMap(map2);
  validateNodeSerialization(*table1);
  validateNodeSerialization(*table2);

  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(table1);
  tableMap->addTable(table2);
  validateThriftMapMapSerialization(*tableMap);

  auto serialized = tableMap->toThrift();
  auto tableMapBack = std::make_shared<AclTableMap>(serialized);

  EXPECT_EQ(*tableMap, *tableMapBack);
  EXPECT_EQ(tableMapBack->getTableIf(kTable1)->getID(), kTable1);
  EXPECT_EQ(tableMapBack->getTableIf(kTable2)->getID(), kTable2);

  // add a table
  auto entry4 = std::make_shared<AclEntry>(4, kDscp4);
  entry4->setDscp(kDscpVal4);
  auto map3 = std::make_shared<AclMap>();
  map3->addEntry(entry4);
  auto table3 = std::make_shared<AclTable>(3, kTable3);
  table3->setAclMap(map3);
  tableMap->addTable(table3);

  serialized = tableMap->toThrift();
  tableMapBack = std::make_shared<AclTableMap>(serialized);

  EXPECT_EQ(*tableMap, *tableMapBack);
  EXPECT_EQ(tableMapBack->getTableIf(kTable1)->getID(), kTable1);
  EXPECT_EQ(tableMapBack->getTableIf(kTable2)->getID(), kTable2);
  EXPECT_EQ(tableMapBack->getTableIf(kTable3)->getID(), kTable3);
}

TEST(AclGroup, SerializeAclTableGroup) {
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  auto entry3 = std::make_shared<AclEntry>(3, kDscp3);
  entry1->setDscp(kDscpVal1);
  entry2->setDscp(kDscpVal2);
  entry3->setDscp(kDscpVal3);

  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1);
  map1->addEntry(entry2);
  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry3);

  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);
  auto table2 = std::make_shared<AclTable>(2, kTable2);
  table2->setAclMap(map2);
  validateThriftStructNodeSerialization(*table1);
  validateThriftStructNodeSerialization(*table2);

  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(table1);
  tableMap->addTable(table2);
  validateThriftMapMapSerialization(*tableMap);

  auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  tableGroup->setName(kGroup1);
  validateThriftStructNodeSerialization(*tableGroup);

  auto serialized = tableGroup->toThrift();
  auto tableGroupBack = std::make_shared<AclTableGroup>(serialized);

  EXPECT_EQ(*tableGroup, *tableGroupBack);
  EXPECT_EQ(tableGroupBack->getID(), kAclStage1);
  EXPECT_NE(tableGroupBack->getAclTableMap(), nullptr);
  EXPECT_EQ(tableGroupBack->getName(), kGroup1);
  EXPECT_EQ(*(tableGroupBack->getAclTableMap()), *tableMap);
}

TEST(AclGroup, SerializeMultiSwitchAclTableGroupMap) {
  /*
   * Simulate conditions similar to the default Acl Table Group
   * created in switch state and verify the non multi ACL to multi
   * ACL warmboot transition goes through
   */
  auto entry1 = std::make_shared<AclEntry>(1, kDscp1);
  auto entry2 = std::make_shared<AclEntry>(2, kDscp2);
  auto entry3 = std::make_shared<AclEntry>(3, kDscp3);
  entry1->setDscp(kDscpVal1);
  entry2->setDscp(kDscpVal2);
  entry3->setDscp(kDscpVal3);

  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1);
  map1->addEntry(entry2);
  map1->addEntry(entry3);

  const std::string table1Name =
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE();
  auto table1 = std::make_shared<AclTable>(0, table1Name);
  table1->setAclMap(map1);

  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(table1);

  auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  tableGroup->setName(
      cfg::switch_config_constants::DEFAULT_INGRESS_ACL_TABLE_GROUP());

  auto tableGroups = std::make_shared<MultiSwitchAclTableGroupMap>();
  tableGroups->addNode(tableGroup, scope());

  auto state = SwitchState();
  state.resetAclTableGroups(tableGroups);
  verifyMultiAclSerialization(state, true);

  auto thriftConvertedState = thriftMultiAclSerializeDeserialize(state, true);
  auto thriftConvertedAcls = thriftConvertedState->getAcls();
  CHECK_NE(thriftConvertedAcls->numNodes(), 0);
  verifyAclHelper(thriftConvertedAcls, entry1, entry2, entry3);
}

TEST(AclGroup, ApplyConfigColdbootMultipleAclTable) {
  FLAGS_enable_acl_table_group = true;
  int priority1 = AclTable::kDataplaneAclMaxPriority;
  int priority2 = AclTable::kDataplaneAclMaxPriority;

  auto platform = createMockPlatform();
  auto stateEmpty = make_shared<SwitchState>();

  // Config contains single acl table
  auto entry1a = make_shared<AclEntry>(priority1++, kAcl1a);
  entry1a->setActionType(cfg::AclActionType::DENY);
  entry1a->setEnabled(true);

  auto entry1b = make_shared<AclEntry>(priority1++, kAcl1b);
  entry1b->setActionType(cfg::AclActionType::DENY);
  auto counter1b = cfg::TrafficCounter();
  counter1b.name() = kAcl1b;
  MatchAction action1b = MatchAction();
  action1b.setTrafficCounter(counter1b);
  entry1b->setAclAction(action1b);
  entry1b->setEnabled(true);

  auto entry1c = make_shared<AclEntry>(priority1++, kAcl1c);
  entry1c->setEnabled(true);

  auto entry1d = make_shared<AclEntry>(priority1++, kAcl1d);
  auto counter1d = cfg::TrafficCounter();
  counter1d.name() = kAcl1d;
  MatchAction action1d = MatchAction();
  action1d.setTrafficCounter(counter1d);
  entry1d->setAclAction(action1d);
  entry1d->setEnabled(true);

  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1a);
  map1->addEntry(entry1b);
  map1->addEntry(entry1c);
  map1->addEntry(entry1d);

  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);
  table1->setActionTypes(kActionTypes);
  table1->setQualifiers(kQualifiers);
  table1->setUdfGroups(kAclUdfGroups);
  validateNodeSerialization(*table1);

  auto tableMap = make_shared<AclTableMap>();
  tableMap->addTable(table1);
  auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  tableGroup->setName(kGroup1);
  validateNodeSerialization(*tableGroup);

  cfg::AclTable cfgTable1;
  cfgTable1.name() = kTable1;
  cfgTable1.priority() = 1;
  cfgTable1.aclEntries()->resize(4);
  cfgTable1.aclEntries()[0].name() = kAcl1a;
  cfgTable1.aclEntries()[0].actionType() = cfg::AclActionType::DENY;
  cfgTable1.aclEntries()[1].name() = kAcl1b;
  cfgTable1.aclEntries()[1].actionType() = cfg::AclActionType::DENY;
  cfgTable1.aclEntries()[2].name() = kAcl1c;
  cfgTable1.aclEntries()[3].name() = kAcl1d;

  cfgTable1.actionTypes()->resize(kActionTypes.size());
  cfgTable1.actionTypes() = kActionTypes;

  cfgTable1.qualifiers()->resize(kQualifiers.size());
  cfgTable1.qualifiers() = kQualifiers;

  cfgTable1.udfGroups()->resize(kAclUdfGroups.size());
  cfgTable1.udfGroups() = kAclUdfGroups;

  cfg::SwitchConfig config;
  cfg::AclTableGroup cfgTableGroup;
  config.aclTableGroup() = cfgTableGroup;
  config.aclTableGroup()->stage() = kAclStage1;
  config.aclTableGroup()->name() = kGroup1;
  config.aclTableGroup()->aclTables()->resize(1);
  config.aclTableGroup()->aclTables()[0] = cfgTable1;
  // Make sure acl1b used so that it isn't ignored
  config.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig();
  config.dataPlaneTrafficPolicy()->matchToAction()->resize(
      2, cfg::MatchToAction());
  config.dataPlaneTrafficPolicy()->matchToAction()[0].matcher() = kAcl1b;
  auto matchAction1b = cfg::MatchAction();
  matchAction1b.counter() = kAcl1b;
  config.dataPlaneTrafficPolicy()->matchToAction()[0].action() = matchAction1b;
  config.dataPlaneTrafficPolicy()->matchToAction()[1].matcher() = kAcl1d;
  auto matchAction1d = cfg::MatchAction();
  matchAction1d.counter() = kAcl1d;
  config.dataPlaneTrafficPolicy()->matchToAction()[1].action() = matchAction1d;
  config.trafficCounters()->resize(2, cfg::TrafficCounter());
  *config.trafficCounters()[0].name() = kAcl1b;
  *config.trafficCounters()[1].name() = kAcl1d;

  auto stateV1 = publishAndApplyConfig(stateEmpty, &config, platform.get());

  EXPECT_NE(nullptr, stateV1);
  EXPECT_TRUE(stateV1->getAclTableGroups()
                  ->getNodeIf(kAclStage1)
                  ->getAclTableMap()
                  ->getTableIf(table1->getID()));
  EXPECT_EQ(
      *(stateV1->getAclTableGroups()
            ->getNodeIf(kAclStage1)
            ->getAclTableMap()
            ->getTableIf(table1->getID())),
      *table1);
  EXPECT_EQ(
      stateV1->getAclTableGroups()->getNodeIf(kAclStage1)->getName(), kGroup1);
  EXPECT_EQ(
      *(stateV1->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup);
  EXPECT_EQ(
      stateV1->getAclTableGroups()
          ->getNodeIf(kAclStage1)
          ->getAclTableMap()
          ->getTableIf(table1->getID())
          ->getActionTypes(),
      kActionTypes);
  EXPECT_EQ(
      stateV1->getAclTableGroups()
          ->getNodeIf(kAclStage1)
          ->getAclTableMap()
          ->getTableIf(table1->getID())
          ->getQualifiers(),
      kQualifiers);
  EXPECT_EQ(
      stateV1->getAclTableGroups()
          ->getNodeIf(kAclStage1)
          ->getAclTableMap()
          ->getTableIf(table1->getID())
          ->getUdfGroups()
          ->toThrift(),
      kAclUdfGroups);
  EXPECT_EQ(
      *(stateV1->getAclTableGroups()
            ->getNodeIf(kAclStage1)
            ->getAclTableMap()
            ->getTableIf(table1->getID())
            ->getAclMap()),
      *map1);

  auto verify = [table1](
                    shared_ptr<SwitchState> state,
                    shared_ptr<AclEntry> aclEntry,
                    std::string aclName,
                    cfg::AclActionType aclActionType,
                    bool verifyTrafficCounter) {
    EXPECT_EQ(
        *(state->getAclTableGroups()
              ->getNodeIf(kAclStage1)
              ->getAclTableMap()
              ->getTableIf(table1->getID())
              ->getAclMap()
              ->getEntry(aclName)),
        *aclEntry);
    EXPECT_EQ(
        state->getAclTableGroups()
            ->getNodeIf(kAclStage1)
            ->getAclTableMap()
            ->getTableIf(table1->getID())
            ->getAclMap()
            ->getEntry(aclName)
            ->getActionType(),
        aclActionType);
    if (verifyTrafficCounter) {
      EXPECT_TRUE(
          state->getAclTableGroups()
              ->getNodeIf(kAclStage1)
              ->getAclTableMap()
              ->getTableIf(table1->getID())
              ->getAclMap()
              ->getEntry(aclName)
              ->getAclAction() != nullptr);
      EXPECT_EQ(
          state->getAclTableGroups()
              ->getNodeIf(kAclStage1)
              ->getAclTableMap()
              ->getTableIf(table1->getID())
              ->getAclMap()
              ->getEntry(aclName)
              ->getAclAction()
              ->cref<switch_state_tags::trafficCounter>()
              ->cref<switch_config_tags::name>()
              ->cref(),
          aclName);
    } else {
      EXPECT_TRUE(
          state->getAclTableGroups()
              ->getNodeIf(kAclStage1)
              ->getAclTableMap()
              ->getTableIf(table1->getID())
              ->getAclMap()
              ->getEntry(aclName)
              ->getAclAction() == nullptr);
    }
  };

  verify(stateV1, entry1a, kAcl1a, cfg::AclActionType::DENY, false);
  verify(stateV1, entry1b, kAcl1b, cfg::AclActionType::DENY, true);
  verify(stateV1, entry1c, kAcl1c, cfg::AclActionType::PERMIT, false);
  verify(stateV1, entry1d, kAcl1d, cfg::AclActionType::PERMIT, true);

  // Config contains 2 acl tables
  auto entry2a = make_shared<AclEntry>(priority2, kAcl2a);
  entry2a->setActionType(cfg::AclActionType::DENY);
  entry2a->setEnabled(true);
  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry2a);
  auto table2 = std::make_shared<AclTable>(2, kTable2);
  table2->setAclMap(map2);
  auto newTableMap = tableGroup->getAclTableMap()->clone();
  newTableMap->addTable(table2);
  tableGroup->setAclTableMap(newTableMap);
  validateNodeSerialization(*table2);

  cfg::AclTable cfgTable2;
  cfgTable2.name() = kTable2;
  cfgTable2.priority() = 2;
  cfgTable2.aclEntries()->resize(1);
  cfgTable2.aclEntries()[0].name() = kAcl2a;
  cfgTable2.aclEntries()[0].actionType() = cfg::AclActionType::DENY;
  config.aclTableGroup()->aclTables()->resize(2);
  config.aclTableGroup()->aclTables()[1] = cfgTable2;

  auto stateV2 = publishAndApplyConfig(stateEmpty, &config, platform.get());

  EXPECT_NE(nullptr, stateV2);
  EXPECT_TRUE(stateV2->getAclTableGroups()
                  ->getNodeIf(kAclStage1)
                  ->getAclTableMap()
                  ->getTableIf(table1->getID()));
  EXPECT_EQ(
      *(stateV2->getAclTableGroups()
            ->getNodeIf(kAclStage1)
            ->getAclTableMap()
            ->getTableIf(table1->getID())),
      *table1);
  EXPECT_TRUE(stateV2->getAclTableGroups()
                  ->getNodeIf(kAclStage1)
                  ->getAclTableMap()
                  ->getTableIf(table2->getID()));
  EXPECT_EQ(
      *(stateV2->getAclTableGroups()
            ->getNodeIf(kAclStage1)
            ->getAclTableMap()
            ->getTableIf(table2->getID())),
      *table2);
  EXPECT_EQ(
      *(stateV2->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup);
}

TEST(AclGroup, ApplyConfigPortBoundAclTableGroup) {
  FLAGS_enable_acl_table_group = true;
  auto platform = createMockPlatform();
  auto state = make_shared<SwitchState>();
  registerPort(state, PortID(1), "port1", scope());
  registerPort(state, PortID(2), "port2", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(2);
  preparedMockPortConfig(
      config.ports()[0], 1, "port1", cfg::PortState::DISABLED);
  preparedMockPortConfig(
      config.ports()[1], 2, "port2", cfg::PortState::DISABLED);
  config.ports()[0].ingressAclTableName() = kTable2;
  config.ports()[1].ingressAclTableName() = kTable3;
  config.aclTableGroups() = {};

  auto addAclTableGroup = [&](const std::string& groupName,
                              const std::string& tableName,
                              cfg::AclTableGroupBindPoint bindPoint) {
    cfg::AclTableGroup group;
    group.name() = groupName;
    group.stage() = cfg::AclStage::INGRESS;
    group.bindPoint() = bindPoint;
    group.aclTables()->resize(1);
    group.aclTables()[0].name() = tableName;
    group.aclTables()[0].priority() = 1;
    config.aclTableGroups()->push_back(std::move(group));
  };
  addAclTableGroup(kGroup1, kTable1, cfg::AclTableGroupBindPoint::SWITCH);
  addAclTableGroup(kGroup2, kTable2, cfg::AclTableGroupBindPoint::PORT);
  config.aclTableGroups()->back().aclTables()->resize(2);
  config.aclTableGroups()->back().aclTables()[1].name() = kTable3;
  config.aclTableGroups()->back().aclTables()[1].priority() = 2;

  auto newState = publishAndApplyConfig(state, &config, platform.get());
  ASSERT_NE(nullptr, newState);
  EXPECT_EQ(
      newState->getPorts()->getNodeIf(PortID(1))->getIngressAclTableName(),
      kTable2);
  EXPECT_EQ(
      newState->getPorts()->getNodeIf(PortID(2))->getIngressAclTableName(),
      kTable3);
  EXPECT_EQ(
      newState->getAclTableGroups()
          ->getNodeIf(cfg::AclStage::INGRESS)
          ->getName(),
      kGroup1);
  EXPECT_EQ(
      newState->getPortAclTableGroups()
          ->getNodeIf(cfg::AclStage::INGRESS)
          ->getName(),
      kGroup2);
  auto portAclTables = newState->getPortAclTableGroups()
                           ->getNodeIf(cfg::AclStage::INGRESS)
                           ->getAclTableMap();
  EXPECT_NE(portAclTables->getTableIf(kTable2), nullptr);
  EXPECT_NE(portAclTables->getTableIf(kTable3), nullptr);
  auto thriftStateBack = SwitchState::fromThrift(newState->toThrift());
  EXPECT_EQ(
      newState->getAclTableGroups()->toThrift(),
      thriftStateBack->getAclTableGroups()->toThrift());
  EXPECT_EQ(
      newState->getPortAclTableGroups()->toThrift(),
      thriftStateBack->getPortAclTableGroups()->toThrift());
  EXPECT_EQ(nullptr, publishAndApplyConfig(newState, &config, platform.get()));

  config.ports()[0].ingressAclTableName() = kTable1;
  EXPECT_THROW(
      publishAndApplyConfig(newState, &config, platform.get()), FbossError);

  config.ports()[0].ingressAclTableName() = "missingTable";
  EXPECT_THROW(
      publishAndApplyConfig(newState, &config, platform.get()), FbossError);
}

TEST(AclGroup, ApplyConfigScopesSrcPortAclTableEntries) {
  FLAGS_enable_acl_table_group = true;
  auto platform = createMockPlatform();

  cfg::SwitchConfig config;
  config.aclTableGroup() = cfg::AclTableGroup();
  config.switchSettings()->switchIdToSwitchInfo() = {
      {0,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 0, 99, 0)},
      {1,
       createSwitchInfo(
           cfg::SwitchType::NPU, cfg::AsicType::ASIC_TYPE_MOCK, 100, 199, 1)}};

  config.aclTableGroup()->name() = kGroup1;
  config.aclTableGroup()->stage() = kAclStage1;
  config.aclTableGroup()->aclTables()->resize(1);
  auto& cfgTable = config.aclTableGroup()->aclTables()[0];
  cfgTable.name() = kTable1;
  cfgTable.priority() = 1;
  cfgTable.aclEntries()->resize(2);
  cfgTable.aclEntries()[0].name() = kAcl1a;
  cfgTable.aclEntries()[0].actionType() = cfg::AclActionType::DENY;
  cfgTable.aclEntries()[0].srcPort() = 2;
  cfgTable.aclEntries()[1].name() = kAcl1b;
  cfgTable.aclEntries()[1].actionType() = cfg::AclActionType::DENY;

  auto state = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  ASSERT_NE(nullptr, state);

  auto matcher0 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)});
  auto matcher1 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)});
  auto switch0AclGroups = state->getAclTableGroups()->getMapNodeIf(matcher0);
  auto switch1AclGroups = state->getAclTableGroups()->getMapNodeIf(matcher1);
  ASSERT_NE(nullptr, switch0AclGroups);
  ASSERT_NE(nullptr, switch1AclGroups);

  auto switch0Acls = switch0AclGroups->getAclTableGroup(kAclStage1)
                         ->getAclTableMap()
                         ->getTable(kTable1)
                         ->getAclMap();
  auto switch1Acls = switch1AclGroups->getAclTableGroup(kAclStage1)
                         ->getAclTableMap()
                         ->getTable(kTable1)
                         ->getAclMap();

  EXPECT_NE(*switch0Acls, *switch1Acls);
  EXPECT_EQ(2, switch0Acls->numEntries());
  EXPECT_EQ(1, switch1Acls->numEntries());
  EXPECT_NE(nullptr, switch0Acls->getEntryIf(kAcl1a));
  EXPECT_NE(nullptr, switch0Acls->getEntryIf(kAcl1b));
  EXPECT_EQ(nullptr, switch1Acls->getEntryIf(kAcl1a));
  EXPECT_NE(nullptr, switch1Acls->getEntryIf(kAcl1b));
}

TEST(AclGroup, ApplyConfigWarmbootMultipleAclTable) {
  FLAGS_enable_acl_table_group = true;
  int priority1 = AclTable::kDataplaneAclMaxPriority;
  int priority2 = AclTable::kDataplaneAclMaxPriority;
  auto platform = createMockPlatform();

  // State unchanged
  auto entry1a = make_shared<AclEntry>(priority1++, kAcl1a);
  entry1a->setActionType(cfg::AclActionType::DENY);
  entry1a->setEnabled(true);
  auto entry1b = make_shared<AclEntry>(priority1++, kAcl1b);
  entry1b->setActionType(cfg::AclActionType::DENY);
  entry1b->setEnabled(true);
  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1a);
  map1->addEntry(entry1b);
  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);
  validateNodeSerialization(*table1);

  auto entry2a = make_shared<AclEntry>(priority2++, kAcl2a);
  entry2a->setActionType(cfg::AclActionType::DENY);
  entry2a->setEnabled(true);
  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry2a);
  auto table2 = std::make_shared<AclTable>(2, kTable2);
  table2->setAclMap(map2);
  validateNodeSerialization(*table2);

  auto tableMap = make_shared<AclTableMap>();
  tableMap->addTable(table1);
  tableMap->addTable(table2);
  auto tableGroup = make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  tableGroup->setName(kGroup1);
  validateNodeSerialization(*tableGroup);

  auto tableGroups = make_shared<MultiSwitchAclTableGroupMap>();
  tableGroups->addNode(tableGroup, scope());
  validateThriftMapMapSerialization(*tableGroups);

  cfg::AclTable cfgTable1;
  cfgTable1.name() = kTable1;
  cfgTable1.priority() = 1;
  cfgTable1.aclEntries()->resize(2);
  cfgTable1.aclEntries()[0].name() = kAcl1a;
  cfgTable1.aclEntries()[0].actionType() = cfg::AclActionType::DENY;
  cfgTable1.aclEntries()[1].name() = kAcl1b;
  cfgTable1.aclEntries()[1].actionType() = cfg::AclActionType::DENY;
  cfg::AclTable cfgTable2;
  cfgTable2.name() = kTable2;
  cfgTable2.priority() = 2;
  cfgTable2.aclEntries()->resize(1);
  cfgTable2.aclEntries()[0].name() = kAcl2a;
  cfgTable2.aclEntries()[0].actionType() = cfg::AclActionType::DENY;

  cfg::SwitchConfig config;
  cfg::AclTableGroup cfgTableGroup;
  config.aclTableGroup() = cfgTableGroup;
  config.aclTableGroup()->name() = kGroup1;
  config.aclTableGroup()->stage() = kAclStage1;

  // Expect aclTableGroup to throw an error if its empty
  cfg::SwitchConfig emptyCfg{};
  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &emptyCfg, platform.get()),
      FbossError);
  /*
   * Need to have aclTableGroup in the config if acltablegroup is enabled
   * so create config with empty aclTableGroup before applying config
   */
  auto stateV0 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  addSwitchInfo(stateV0);

  config.aclTableGroup()->aclTables()->resize(2);
  config.aclTableGroup()->aclTables()[0] = cfgTable1;
  config.aclTableGroup()->aclTables()[1] = cfgTable2;

  stateV0->resetAclTableGroups(tableGroups);

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_EQ(nullptr, stateV1);

  // Add a table
  int priority3 = AclTable::kDataplaneAclMaxPriority;
  cfg::AclTable cfgTable3;
  cfgTable3.name() = kTable3;
  cfgTable3.priority() = 3;
  cfgTable3.aclEntries()->resize(1);
  cfgTable3.aclEntries()[0].name() = kAcl3a;
  cfgTable3.aclEntries()[0].actionType() = cfg::AclActionType::DENY;

  config.aclTableGroup()->aclTables()->resize(3);
  config.aclTableGroup()->aclTables()[2] = cfgTable3;

  auto stateV2 = publishAndApplyConfig(stateV0, &config, platform.get());
  EXPECT_NE(nullptr, stateV2);
  EXPECT_NE(
      *(stateV2->getAclTableGroups())->getNodeIf(kAclStage1), *tableGroup);

  auto entry3a = make_shared<AclEntry>(priority3++, kAcl3a);
  entry3a->setActionType(cfg::AclActionType::DENY);
  entry3a->setEnabled(true);
  auto map3 = std::make_shared<AclMap>();
  map3->addEntry(entry3a);
  auto table3 = std::make_shared<AclTable>(3, kTable3);
  table3->setAclMap(map3);
  auto tableMap1 = tableGroup->getAclTableMap()->clone();
  tableMap1->addTable(table3);
  /*
   * Directly getting tableMap and adding a new node will reflect only
   * in switchstate and not in thrift structure. That will cause tests to
   * fail. So clone a newtablegroup and do a setAclTableMap everytime its
   * updated so that the thrift structure is also updated
   */
  auto tableGroup1 = tableGroup->clone();
  tableGroup1->setAclTableMap(tableMap1);
  validateNodeSerialization(*table3);

  EXPECT_EQ(
      *(stateV2->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Remove a table
  config.aclTableGroup()->aclTables()->resize(2);

  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  EXPECT_NE(nullptr, stateV3);
  EXPECT_NE(
      *(stateV3->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto tableMap2 = tableGroup1->getAclTableMap()->clone();
  tableMap2->removeTable(table3->getID());
  tableGroup1->setAclTableMap(tableMap2);

  EXPECT_EQ(
      *(stateV3->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Change the priority of a table
  config.aclTableGroup()->aclTables()[1].priority() = 5;

  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  EXPECT_NE(nullptr, stateV4);
  EXPECT_NE(
      *(stateV4->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto tableMap3 = tableGroup1->getAclTableMap()->clone();
  tableMap3->updateNode(tableMap3->getTable(table2->getID())->clone());
  tableMap3->getTable(table2->getID())->setPriority(5);
  tableGroup1->setAclTableMap(tableMap3);

  EXPECT_EQ(
      *(stateV4->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Add an entry to a table
  config.aclTableGroup()->aclTables()[1].aclEntries()->resize(2);
  config.aclTableGroup()->aclTables()[1].aclEntries()[1].name() = kAcl2b;
  config.aclTableGroup()->aclTables()[1].aclEntries()[1].actionType() =
      cfg::AclActionType::DENY;

  auto stateV5 = publishAndApplyConfig(stateV4, &config, platform.get());
  EXPECT_NE(nullptr, stateV5);
  EXPECT_NE(
      *(stateV5->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto entry2b = make_shared<AclEntry>(priority2++, kAcl2b);
  entry2b->setActionType(cfg::AclActionType::DENY);
  entry2b->setEnabled(true);
  auto map2Version2 = table2->getAclMap()->clone();
  map2Version2->addEntry(entry2b);
  /*
   * Directly getting AclMap and adding a new node will reflect only
   * in switchstate and not in thrift structure. That will cause tests to
   * fail. So do a setAclMap everytime its updated so that the thrift
   * structure is also updated
   */
  table2 = table2->clone();
  table2->setAclMap(map2Version2);
  auto tableMap4 = tableGroup1->getAclTableMap()->clone();
  tableGroup1 = tableGroup1->clone();
  tableGroup1->setAclTableMap(tableMap4);
  auto groups = stateV5->getAclTableGroups()->clone();
  groups->updateNode(tableGroup1, scope());
  stateV5 = stateV5->clone();
  stateV5->resetAclTableGroups(groups);

  EXPECT_EQ(
      *(stateV5->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Remove an entry from a table
  config.aclTableGroup()->aclTables()[0].aclEntries()->resize(1);

  auto stateV6 = publishAndApplyConfig(stateV5, &config, platform.get());
  EXPECT_NE(nullptr, stateV6);
  EXPECT_NE(
      *(stateV6->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto map1Version2 = table1->getAclMap()->clone();
  map1Version2->removeEntry(entry1b);
  table1 = table1->clone();
  table1->setAclMap(map1Version2);
  auto tableMap5 = tableGroup1->getAclTableMap()->clone();
  tableGroup1 = tableGroup1->clone();
  tableGroup1->setAclTableMap(tableMap5);
  groups = stateV6->getAclTableGroups()->clone();
  groups->updateNode(tableGroup1, scope());
  stateV6 = stateV6->clone();
  stateV6->resetAclTableGroups(groups);

  EXPECT_EQ(
      *(stateV6->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Change an entry in a table
  auto proto = 6;
  config.aclTableGroup()->aclTables()[1].aclEntries()[0].proto() = proto;

  auto stateV7 = publishAndApplyConfig(stateV6, &config, platform.get());
  EXPECT_NE(nullptr, stateV7);
  EXPECT_NE(
      *(stateV7->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto map2Version3 = table2->getAclMap()->clone();
  auto entry2a2 = entry2a->clone();
  entry2a2->setProto(proto);
  map2Version3->updateNode(entry2a2);
  table2 = table2->clone();
  table2->setAclMap(map2Version3);
  auto tableMap6 = tableGroup1->getAclTableMap()->clone();
  tableGroup1 = tableGroup1->clone();
  tableGroup1->setAclTableMap(tableMap6);
  groups = stateV7->getAclTableGroups()->clone();
  groups->updateNode(tableGroup1, scope());
  stateV7 = stateV7->clone();
  stateV7->resetAclTableGroups(groups);
  EXPECT_EQ(
      *(stateV7->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  // Move an entry between tables
  config.aclTableGroup()->aclTables()[1].aclEntries()->resize(
      1); // delete entry2b from table 2
  config.aclTableGroup()->aclTables()[0].aclEntries()->resize(2);
  config.aclTableGroup()->aclTables()[0].aclEntries()[1].name() =
      kAcl2b; // add entry2b to table 1
  config.aclTableGroup()->aclTables()[0].aclEntries()[1].actionType() =
      cfg::AclActionType::DENY;

  auto stateV8 = publishAndApplyConfig(stateV7, &config, platform.get());
  EXPECT_NE(nullptr, stateV8);
  EXPECT_NE(
      *(stateV8->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);

  auto map2Version4 = table2->getAclMap()->clone();
  map2Version4->removeEntry(entry2b->getID());
  table2->setAclMap(map2Version4);
  auto map1Version3 = table1->getAclMap()->clone();
  map1Version3->addEntry(entry2b);
  table1->setAclMap(map1Version3);
  auto tableMap7 = tableGroup1->getAclTableMap()->clone();
  tableGroup1 = tableGroup1->clone();
  tableGroup1->setAclTableMap(
      tableMap7); // 2b will be the second entry in table1, so priority
                  // unchanged (originally second entry in table2)
  groups = stateV8->getAclTableGroups()->clone();
  groups->updateNode(tableGroup1, scope());
  stateV8 = stateV8->clone();
  stateV8->resetAclTableGroups(groups);

  EXPECT_EQ(
      *(stateV8->getAclTableGroups()->getNodeIf(kAclStage1)), *tableGroup1);
}

TEST(AclGroup, ModifyClonesTheAclTableChain) {
  auto tableAt = [](const std::shared_ptr<SwitchState>& state) {
    return state->getAclTableGroups()
        ->getMapNodeIf(scope())
        ->getAclTableGroup(kAclStage1)
        ->getAclTableMap()
        ->getTable(kTable1);
  };

  // 1. A published state holding a single INGRESS ACL table.
  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(std::make_shared<AclTable>(0, kTable1));
  auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  auto tableGroups = std::make_shared<MultiSwitchAclTableGroupMap>();
  tableGroups->addNode(tableGroup, scope());

  auto state = std::make_shared<SwitchState>();
  state->resetAclTableGroups(tableGroups);
  state->publish();

  auto publishedState = state;
  auto publishedTable = tableAt(state);

  // 2. modify() reseats the state and hands back an unpublished table.
  auto* modified = publishedTable->modify(&state, scope(), kAclStage1);
  EXPECT_NE(state, publishedState);
  EXPECT_NE(modified, publishedTable.get());
  EXPECT_FALSE(modified->isPublished());

  // 3. The clone sits at the same coordinates in the new state, and the
  // published state still holds the original.
  EXPECT_EQ(tableAt(state).get(), modified);
  EXPECT_EQ(tableAt(publishedState).get(), publishedTable.get());

  // 4. Modifying the already-unpublished table clones nothing further.
  auto unpublishedState = state;
  EXPECT_EQ(modified->modify(&state, scope(), kAclStage1), modified);
  EXPECT_EQ(state, unpublishedState);
}

TEST(AclGroup, ModifyLeavesOtherMatchersUntouched) {
  auto matcher0 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)});
  auto matcher1 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)});
  auto groupAt = [](const std::shared_ptr<SwitchState>& state,
                    const HwSwitchMatcher& matcher) {
    return state->getAclTableGroups()->getMapNodeIf(matcher)->getAclTableGroup(
        kAclStage1);
  };

  // 1. Both switches carry a same-named ACL table, as config scoping produces.
  auto tableGroups = std::make_shared<MultiSwitchAclTableGroupMap>();
  for (const auto& matcher : {matcher0, matcher1}) {
    auto tableMap = std::make_shared<AclTableMap>();
    tableMap->addTable(std::make_shared<AclTable>(0, kTable1));
    auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
    tableGroup->setAclTableMap(tableMap);
    tableGroups->addNode(tableGroup, matcher);
  }

  auto state = std::make_shared<SwitchState>();
  state->resetAclTableGroups(tableGroups);
  state->publish();
  auto switch1Group = groupAt(state, matcher1);

  // 2. Modifying switch 0's table rewires only switch 0.
  auto table0 = groupAt(state, matcher0)->getAclTableMap()->getTable(kTable1);
  auto* modified = table0->modify(&state, matcher0, kAclStage1);

  EXPECT_EQ(
      groupAt(state, matcher0)->getAclTableMap()->getTable(kTable1).get(),
      modified);
  EXPECT_EQ(groupAt(state, matcher1), switch1Group);
}

TEST(AclGroup, ModifyRejectsCoordinatesTheTableIsNotAt) {
  auto matcher0 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)});
  auto matcher1 = HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(1)});

  // Both switches hold a table of the same name, so only an identity check
  // can tell the caller they named the wrong one.
  auto tableGroups = std::make_shared<MultiSwitchAclTableGroupMap>();
  for (const auto& matcher : {matcher0, matcher1}) {
    auto tableMap = std::make_shared<AclTableMap>();
    tableMap->addTable(std::make_shared<AclTable>(0, kTable1));
    auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
    tableGroup->setAclTableMap(tableMap);
    tableGroups->addNode(tableGroup, matcher);
  }

  auto state = std::make_shared<SwitchState>();
  state->resetAclTableGroups(tableGroups);
  state->publish();

  auto table = state->getAclTableGroups()
                   ->getMapNodeIf(matcher0)
                   ->getAclTableGroup(kAclStage1)
                   ->getAclTableMap()
                   ->getTable(kTable1);

  EXPECT_THROW(table->modify(&state, matcher1, kAclStage1), FbossError);
  EXPECT_THROW(table->modify(&state, matcher0, kAclStage2), FbossError);
}

namespace {

const std::string kPbrTable =
    cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();

cfg::SwitchConfig makePbrTableConfig() {
  cfg::SwitchConfig config;
  config.aclTableGroup() = cfg::AclTableGroup();
  config.aclTableGroup()->name() = kGroup1;
  config.aclTableGroup()->stage() = kAclStage1;
  config.aclTableGroup()->aclTables()->resize(2);

  auto& cfgTable1 = config.aclTableGroup()->aclTables()[0];
  cfgTable1.name() = kTable1;
  cfgTable1.priority() = 1;
  cfgTable1.aclEntries()->resize(1);
  cfgTable1.aclEntries()[0].name() = kAcl1a;
  cfgTable1.aclEntries()[0].actionType() = cfg::AclActionType::DENY;

  auto& cfgPbr = config.aclTableGroup()->aclTables()[1];
  cfgPbr.name() = kPbrTable;
  cfgPbr.priority() = 2;
  return config;
}

std::shared_ptr<SwitchState> addPbrEntry(
    const std::shared_ptr<SwitchState>& base,
    const std::string& name,
    int priority) {
  auto state = base->clone();
  auto groups = state->getAclTableGroups()->clone();
  const auto matcherStr = groups->cbegin()->first;
  auto groupMap = groups->getMapNodeIf(HwSwitchMatcher(matcherStr));
  auto group = groupMap->getAclTableGroupIf(kAclStage1);
  auto table = group->getAclTableMap()->getTableIf(kPbrTable)->clone();
  auto aclMap = table->getAclMap()->clone();
  auto entry = std::make_shared<AclEntry>(priority, name);
  entry->setActionType(cfg::AclActionType::PERMIT);
  aclMap->addNode(entry);
  table->setAclMap(aclMap);
  auto newTableMap = group->getAclTableMap()->clone();
  newTableMap->updateNode(table);
  auto newGroup = group->clone();
  newGroup->setAclTableMap(newTableMap);
  auto newGroupMap = groupMap->clone();
  newGroupMap->updateNode(newGroup);
  groups->updateMapNode(newGroupMap, HwSwitchMatcher(matcherStr));
  state->resetAclTableGroups(groups);
  state->publish();
  return state;
}

} // namespace

TEST(AclGroup, ApplyConfigPreservesComputedPbrEntries) {
  FLAGS_enable_acl_table_group = true;
  auto platform = createMockPlatform();
  auto config = makePbrTableConfig();

  auto stateV1 = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get());
  ASSERT_NE(nullptr, stateV1);
  EXPECT_EQ(0, stateV1->getAclsForTable(kAclStage1, kPbrTable)->numEntries());

  auto stateV2 = addPbrEntry(stateV1, "pbrPolicy_tc1", 3505);
  ASSERT_EQ(1, stateV2->getAclsForTable(kAclStage1, kPbrTable)->numEntries());

  // Re-applying the same config is a no-op: the PBR table's empty config
  // entry list no longer reads as "the computed entry was removed".
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV2, &config, platform.get()));

  // A PBR table attribute change rebuilds the table, but its entries are
  // carried over rather than taken from config.
  config.aclTableGroup()->aclTables()[1].qualifiers() = kQualifiers;
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  ASSERT_NE(nullptr, stateV3);
  auto pbrAcls = stateV3->getAclsForTable(kAclStage1, kPbrTable);
  ASSERT_EQ(1, pbrAcls->numEntries());
  auto entry = pbrAcls->getEntryIf("pbrPolicy_tc1");
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(3505, entry->getPriority());
  // Other tables are still rebuilt from config as usual.
  EXPECT_EQ(1, stateV3->getAclsForTable(kAclStage1, kTable1)->numEntries());
}

TEST(AclGroup, ApplyConfigRejectsConfiguredPbrEntries) {
  FLAGS_enable_acl_table_group = true;
  auto platform = createMockPlatform();
  auto config = makePbrTableConfig();

  auto& cfgPbr = config.aclTableGroup()->aclTables()[1];
  cfgPbr.aclEntries()->resize(1);
  cfgPbr.aclEntries()[0].name() = kAcl2a;
  cfgPbr.aclEntries()[0].actionType() = cfg::AclActionType::DENY;

  EXPECT_THROW(
      publishAndApplyConfig(
          std::make_shared<SwitchState>(), &config, platform.get()),
      FbossError);
}
