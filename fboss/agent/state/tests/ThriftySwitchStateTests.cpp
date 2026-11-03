/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/ScopeGuard.h>
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/ArpResponseTable.h"
#include "fboss/agent/state/BufferPoolConfig.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/test/LabelForwardingUtils.h"
#include "fboss/agent/test/TestUtils.h"
#include "folly/IPAddress.h"
#include "folly/IPAddressV4.h"

using namespace facebook::fboss;
using folly::IPAddressV4;
using folly::IPAddressV6;

namespace {

void verifySwitchStateSerialization(const SwitchState& state) {
  auto stateBack = SwitchState::fromThrift(state.toThrift());
  EXPECT_EQ(state, *stateBack);
}

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

template <typename VlanOrIntfT>
void setNeighborTablesAndDHCPRelay(
    const std::shared_ptr<VlanOrIntfT> vlanOrIntf1,
    const std::shared_ptr<VlanOrIntfT> vlanOrIntf2) {
  vlanOrIntf1->setDhcpV4Relay(IPAddressV4("1.2.3.4"));
  vlanOrIntf1->setDhcpV4RelayOverrides(
      {{MacAddress("02:00:00:00:00:02"), IPAddressV4("1.2.3.4")}});

  auto arpTable = std::make_shared<ArpTable>();
  arpTable->addEntry(
      IPAddressV4("1.2.3.4"),
      MacAddress("02:00:00:00:00:03"),
      PortDescriptor(PortID(1)),
      InterfaceID(1));
  vlanOrIntf1->setArpTable(arpTable);

  auto ndpTable = std::make_shared<NdpTable>();
  ndpTable->addEntry(
      IPAddressV6("2401:db00:21:70cb:face:0:96:0"),
      MacAddress("02:00:00:00:00:04"),
      PortDescriptor(PortID(2)),
      InterfaceID(2));
  vlanOrIntf1->setNdpTable(ndpTable);

  auto arpResponseTable = std::make_shared<ArpResponseTable>();
  arpResponseTable->setEntry(
      IPAddressV4("1.2.3.5"), MacAddress("02:00:00:00:00:06"), InterfaceID(3));
  vlanOrIntf1->setArpResponseTable(arpResponseTable);

  auto ndpResponseTable = std::make_shared<NdpResponseTable>();
  ndpResponseTable->setEntry(
      IPAddressV6("2401:db00:21:70cb:face:0:96:1"),
      MacAddress("02:00:00:00:00:07"),
      InterfaceID(4));
  vlanOrIntf1->setNdpResponseTable(ndpResponseTable);

  vlanOrIntf2->setDhcpV6Relay(IPAddressV6("2401:db00:21:70cb:face:0:96:0"));
  vlanOrIntf2->setDhcpV6RelayOverrides(
      {{MacAddress("02:00:00:00:00:03"),
        IPAddressV6("2401:db00:21:70cb:face:0:96:0")}});
}

constexpr int64_t kOverrideId = 300;

NextHopThrift makeNextHopThrift(const std::string& addr, int intf) {
  NextHopThrift nh;
  nh.address() = facebook::network::toBinaryAddress(folly::IPAddress(addr));
  nh.address()->ifName() = folly::to<std::string>("fboss", intf);
  *nh.weight() = UCMP_DEFAULT_WEIGHT;
  return nh;
}

// One merged v4 route carrying override id kOverrideId, listed in FibInfo's
// override id map. withNextHopIds also persists the NextHop id maps.
state::SwitchState makeOverrideIdState(bool withNextHopIds) {
  auto stateThrift = SwitchState().toThrift();
  RouteNextHopSet nhops;
  nhops.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  nhops.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));
  auto routeFields =
      Route<folly::IPAddressV4>::makeThrift(makePrefixV4("10.0.0.0/24"));
  routeFields.fwd() =
      RouteNextHopEntry(nhops, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  routeFields.fwd()->overrideNextHopSetID() = kOverrideId;

  state::FibInfoFields fibInfo;
  fibInfo.overrideIdToNextHopIdSet()->emplace(
      kOverrideId, std::set<int64_t>{1, 2});
  if (withNextHopIds) {
    routeFields.fwd()->resolvedNextHopSetID() = 100;
    routeFields.fwd()->normalizedResolvedNextHopSetID() = 100;
    fibInfo.idToNextHop()->emplace(1, makeNextHopThrift("10.0.1.1", 1));
    fibInfo.idToNextHop()->emplace(2, makeNextHopThrift("10.0.1.2", 2));
    fibInfo.idToNextHopIdSet()->emplace(100, std::set<int64_t>{1, 2});
  }
  state::FibContainerFields fibContainer;
  fibContainer.vrf() = 0;
  fibContainer.fibV4()->emplace("10.0.0.0/24", routeFields);
  fibInfo.fibsMap()->emplace(0, fibContainer);
  stateThrift.fibsInfoMap()->emplace(
      HwSwitchMatcher::defaultHwSwitchMatcherKey(), fibInfo);
  return stateThrift;
}

std::optional<NextHopSetID> routeOverrideId(
    const std::shared_ptr<SwitchState>& state) {
  auto fibV4 = state->getFibsInfoMap()
                   ->getFibInfo(scope())
                   ->getfibsMap()
                   ->getFibContainerIf(RouterID(0))
                   ->getFibV4();
  CHECK_EQ(fibV4->size(), 1);
  return fibV4->cbegin()->second->getForwardInfo().getOverrideNextHopSetID();
}

size_t overrideIdMapSize(const std::shared_ptr<SwitchState>& state) {
  return state->getFibsInfoMap()
      ->getFibInfo(scope())
      ->getOverrideIdToNextHopIdSetMap()
      ->size();
}

} // namespace

TEST(ThriftySwitchState, BasicTest) {
  auto state = SwitchState();
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, PortMap) {
  state::PortFields portFields1;
  portFields1.portId() = PortID(1);
  portFields1.portName() = "eth2/1/1";
  auto port1 = std::make_shared<Port>(std::move(portFields1));
  state::PortFields portFields2;
  portFields2.portId() = PortID(2);
  portFields2.portName() = "eth2/2/1";
  auto port2 = std::make_shared<Port>(std::move(portFields2));

  auto state = SwitchState();
  state.getPorts()->addNode(port1, scope());
  state.getPorts()->addNode(port2, scope());
  auto portMap = state.getPorts()->begin()->second;
  validateThriftMapMapSerialization(*portMap);

  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, VlanMap) {
  // With intf_nbr_tables enabled (default), neighbor tables are on interfaces,
  // not VLANs. Only test MAC table on VLAN.
  auto vlan1 = std::make_shared<Vlan>(VlanID(1), std::string("vlan1"));
  auto vlan2 = std::make_shared<Vlan>(VlanID(2), std::string("vlan2"));
  vlan1->setInterfaceID(InterfaceID(1));
  vlan1->setInterfaceID(InterfaceID(2));

  auto macTable = std::make_shared<MacTable>();
  auto macEntry = std::make_shared<MacEntry>(
      MacAddress("02:00:00:00:00:08"),
      PortDescriptor(PortID(4)),
      std::optional<cfg::AclLookupClass>(cfg::AclLookupClass::CLASS_DROP));
  macTable->addEntry(macEntry);

  auto vlanMap = std::make_shared<MultiSwitchVlanMap>();
  vlanMap->addNode(vlan1, scope());
  vlanMap->addNode(vlan2, scope());

  auto state = SwitchState();
  state.resetVlans(vlanMap);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, AclMap) {
  auto acl1 = std::make_shared<AclEntry>(1, std::string("acl1"));
  auto acl2 = std::make_shared<AclEntry>(2, std::string("acl2"));

  auto aclMap = std::make_shared<MultiSwitchAclMap>();
  aclMap->addNode(acl1, scope());
  aclMap->addNode(acl2, scope());

  auto state = SwitchState();
  state.resetAcls(aclMap);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, TransceiverMap) {
  auto transceiver1 = std::make_shared<TransceiverSpec>(TransceiverID(1));
  auto transceiver2 = std::make_shared<TransceiverSpec>(TransceiverID(2));

  auto transceiverMap = std::make_shared<MultiSwitchTransceiverMap>();
  transceiverMap->addNode(transceiver1, scope());
  transceiverMap->addNode(transceiver2, scope());

  auto state = SwitchState();
  state.resetTransceivers(transceiverMap);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, BufferPoolCfgMap) {
  const std::string buffer1 = "pool1";
  const std::string buffer2 = "pool2";
  auto pool1 = std::make_shared<BufferPoolCfg>(buffer1);
  auto pool2 = std::make_shared<BufferPoolCfg>(buffer2);
  pool1->setHeadroomBytes(100);
  pool2->setHeadroomBytes(200);

  auto map = std::make_shared<MultiSwitchBufferPoolCfgMap>();
  map->addNode(pool1, scope());
  map->addNode(pool2, scope());

  auto state = SwitchState();
  state.resetBufferPoolCfgs(map);
}

TEST(ThriftySwitchState, QosPolicyMap) {
  const std::string kQosPolicy1Name = "qosPolicy1";
  const std::string kQosPolicy2Name = "qosPolicy2";
  auto qosPolicy1 = std::make_shared<QosPolicy>(kQosPolicy1Name, DscpMap());
  auto qosPolicy2 = std::make_shared<QosPolicy>(kQosPolicy2Name, DscpMap());

  auto map = std::make_shared<MultiSwitchQosPolicyMap>();
  map->addNode(qosPolicy1, scope());
  map->addNode(qosPolicy2, scope());

  auto state = SwitchState();
  state.resetQosPolicies(map);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, SflowCollectorMap) {
  auto sflowCollector1 = std::make_shared<SflowCollector>(
      std::string("1.2.3.4"), static_cast<uint16_t>(8080));
  auto sflowCollector2 = std::make_shared<SflowCollector>(
      std::string("2::3"), static_cast<uint16_t>(9090));

  auto map = std::make_shared<MultiSwitchSflowCollectorMap>();
  map->addNode(sflowCollector1, scope());
  map->addNode(sflowCollector2, scope());

  auto state = SwitchState();
  state.resetSflowCollectors(map);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, AggregatePortMap) {
  auto platform = createMockPlatform();
  auto startState = testStateA();
  std::vector<int> memberPort1 = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::vector<int> memberPort2 = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

  auto config = testConfigA();
  config.aggregatePorts()->resize(2);
  *config.aggregatePorts()[0].key() = 55;
  *config.aggregatePorts()[0].name() = "lag55";
  *config.aggregatePorts()[0].description() = "upwards facing link-bundle";
  (*config.aggregatePorts()[0].memberPorts()).resize(10);
  for (int i = 0; i < memberPort1.size(); ++i) {
    *config.aggregatePorts()[0].memberPorts()[i].memberPortID() =
        memberPort1[i];
  }
  *config.aggregatePorts()[1].key() = 155;
  *config.aggregatePorts()[1].name() = "lag155";
  *config.aggregatePorts()[1].description() = "downwards facing link-bundle";
  (*config.aggregatePorts()[1].memberPorts()).resize(10);
  *config.aggregatePorts()[1].memberPorts()[0].memberPortID() = 1;
  for (int i = 0; i < memberPort2.size(); ++i) {
    *config.aggregatePorts()[1].memberPorts()[i].memberPortID() =
        memberPort2[i];
  }

  auto endState = publishAndApplyConfig(startState, &config, platform.get());
  ASSERT_NE(nullptr, endState);
  auto aggPorts = endState->getAggregatePorts();

  auto state = SwitchState();
  state.resetAggregatePorts(aggPorts);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, AclTableGroupMap) {
  MockPlatform platform;

  const std::string kTable1 = "table1";
  const std::string kTable2 = "table2";
  const std::string kAcl1a = "acl1a";
  const std::string kAcl1b = "acl1b";
  const std::string kAcl2a = "acl2a";
  const std::string kGroup1 = "group1";
  int priority1 = AclTable::kDataplaneAclMaxPriority;
  int priority2 = AclTable::kDataplaneAclMaxPriority;
  const cfg::AclStage kAclStage1 = cfg::AclStage::INGRESS;

  auto entry1a = std::make_shared<AclEntry>(priority1++, kAcl1a);
  entry1a->setActionType(cfg::AclActionType::DENY);
  auto entry1b = std::make_shared<AclEntry>(priority1++, kAcl1b);
  entry1b->setActionType(cfg::AclActionType::DENY);
  auto map1 = std::make_shared<AclMap>();
  map1->addEntry(entry1a);
  map1->addEntry(entry1b);
  auto table1 = std::make_shared<AclTable>(1, kTable1);
  table1->setAclMap(map1);

  auto entry2a = std::make_shared<AclEntry>(priority2++, kAcl2a);
  entry2a->setActionType(cfg::AclActionType::DENY);
  auto map2 = std::make_shared<AclMap>();
  map2->addEntry(entry2a);
  auto table2 = std::make_shared<AclTable>(2, kTable2);
  table2->setAclMap(map2);

  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(table1);
  tableMap->addTable(table2);
  auto tableGroup = std::make_shared<AclTableGroup>(kAclStage1);
  tableGroup->setAclTableMap(tableMap);
  tableGroup->setName(kGroup1);

  auto tableGroups = std::make_shared<MultiSwitchAclTableGroupMap>();
  tableGroups->addNode(tableGroup, scope());

  auto state = SwitchState();
  state.resetAclTableGroups(tableGroups);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, InterfaceMap) {
  auto platform = createMockPlatform();
  auto startState = testStateA();

  auto config = testConfigA();
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 1;
  config.vlans()[0].intfID() = 1;

  *config.vlans()[1].id() = 55;
  config.vlans()[1].intfID() = 55;

  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";

  *config.interfaces()[1].intfID() = 55;
  *config.interfaces()[1].vlanID() = 55;
  config.interfaces()[1].mac() = "00:00:00:00:00:55";

  auto endState = publishAndApplyConfig(startState, &config, platform.get());
  ASSERT_NE(nullptr, endState);
  auto interfaces = endState->getInterfaces();

  auto state = SwitchState();
  state.resetIntfs(interfaces);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, InterfaceMapNbrTables) {
  auto intf1 = make_shared<Interface>(
      InterfaceID(1),
      RouterID(0),
      std::optional<VlanID>(1),
      folly::StringPiece("fboss1"),
      MacAddress("00:02:00:00:00:01"),
      9000,
      false, /* is virtual */
      false /* is state_sync disabled */);
  auto intf2 = make_shared<Interface>(
      InterfaceID(2),
      RouterID(0),
      std::optional<VlanID>(2),
      folly::StringPiece("fboss2"),
      MacAddress("00:02:00:00:00:01"),
      9000,
      false, /* is virtual */
      false /* is state_sync disabled */);

  setNeighborTablesAndDHCPRelay(intf1, intf2);

  auto intfMap = std::make_shared<MultiSwitchInterfaceMap>();
  intfMap->addNode(intf1, scope());
  intfMap->addNode(intf2, scope());

  auto state = SwitchState();
  state.resetIntfs(intfMap);
  verifySwitchStateSerialization(state);
}

TEST(ThriftySwitchState, IpAddressConversion) {
  for (auto ipStr : {"212.12.45.89", "2401:ab:de::30"}) {
    auto ip_0 = folly::IPAddress(ipStr);
    auto addr_0 = facebook::network::toBinaryAddress(ip_0);

    auto ip_0_dynamic = ThriftyUtils::toFollyDynamic(ip_0);
    auto addr_0_dynamic = ThriftyUtils::toFollyDynamic(addr_0);

    auto addr_1 = ThriftyUtils::toThriftBinaryAddress(ip_0_dynamic);
    auto ip_1 = ThriftyUtils::toFollyIPAddress(addr_0_dynamic);

    auto ip_1_dynamic = ThriftyUtils::toFollyDynamic(ip_0);
    auto addr_1_dynamic = ThriftyUtils::toFollyDynamic(addr_0);

    EXPECT_EQ(ip_0, ip_1);
    EXPECT_EQ(addr_0, addr_1);

    EXPECT_EQ(ip_0_dynamic, ip_1_dynamic);
    EXPECT_EQ(addr_0_dynamic, addr_1_dynamic);
  }
}

TEST(ThriftySwitchState, FromThriftIdMapCleanupOnRollback) {
  // When FLAGS_enable_nexthop_id_manager is OFF, fromThrift() must wipe
  // every resolved/normalized/override NextHopSetID from routes
  // (V4, V6, label) and mySid entries, and clear FibInfo's id maps. This
  // guarantees a future flag-ON warmboot starts from a clean slate without
  // orphan IDs colliding with newly allocated IDs.
  auto savedFlag = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = false;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedFlag;
  };

  auto state = SwitchState();
  auto stateThrift = state.toThrift();
  const auto& matcherKey = HwSwitchMatcher::defaultHwSwitchMatcherKey();

  // Build a NextHopThrift mirroring a ResolvedNextHop(addr, intf), so the
  // FibInfo id map entries correspond to the routes' actual nexthops.
  auto makeNhThrift = [](const std::string& addr, int intf) {
    NextHopThrift nh;
    nh.address() = facebook::network::toBinaryAddress(folly::IPAddress(addr));
    nh.address()->ifName() = folly::to<std::string>("fboss", intf);
    *nh.weight() = UCMP_DEFAULT_WEIGHT;
    return nh;
  };

  RouteNextHopSet v4NhSet;
  v4NhSet.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  v4NhSet.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));
  auto v4Fields =
      Route<folly::IPAddressV4>::makeThrift(makePrefixV4("10.0.0.0/24"));
  v4Fields.fwd() =
      RouteNextHopEntry(v4NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v4Fields.fwd()->resolvedNextHopSetID() = 100;
  v4Fields.fwd()->normalizedResolvedNextHopSetID() = 100;
  v4Fields.fwd()->overrideNextHopSetID() = 300;
  // Per-client entry carrying a stray clientNextHopSetID — must be wiped.
  auto v4ClientEntry =
      RouteNextHopEntry(v4NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v4ClientEntry.clientNextHopSetID() = 100;
  v4Fields.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, v4ClientEntry);

  RouteNextHopSet v6NhSet;
  v6NhSet.insert(ResolvedNextHop(
      folly::IPAddress("2001:db8:1::1"), InterfaceID(3), UCMP_DEFAULT_WEIGHT));
  v6NhSet.insert(ResolvedNextHop(
      folly::IPAddress("2001:db8:1::2"), InterfaceID(4), UCMP_DEFAULT_WEIGHT));
  auto v6Fields =
      Route<folly::IPAddressV6>::makeThrift(makePrefixV6("2001:db8::/32"));
  v6Fields.fwd() =
      RouteNextHopEntry(v6NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v6Fields.fwd()->resolvedNextHopSetID() = 200;
  v6Fields.fwd()->normalizedResolvedNextHopSetID() = 200;
  auto v6ClientEntry =
      RouteNextHopEntry(v6NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v6ClientEntry.clientNextHopSetID() = 200;
  v6Fields.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, v6ClientEntry);

  state::FibContainerFields fibContainer;
  fibContainer.vrf() = 0;
  fibContainer.fibV4()->emplace("10.0.0.0/24", v4Fields);
  fibContainer.fibV6()->emplace("2001:db8::/32", v6Fields);

  // FibInfo id maps consistent with the routes above:
  //   set 100 -> {nh 1, nh 2} = v4 route's nexthops
  //   set 200 -> {nh 3, nh 4} = v6 route's nexthops
  state::FibInfoFields fibInfo;
  fibInfo.fibsMap()->emplace(0, fibContainer);
  fibInfo.idToNextHop()->emplace(1, makeNhThrift("10.0.1.1", 1));
  fibInfo.idToNextHop()->emplace(2, makeNhThrift("10.0.1.2", 2));
  fibInfo.idToNextHop()->emplace(3, makeNhThrift("2001:db8:1::1", 3));
  fibInfo.idToNextHop()->emplace(4, makeNhThrift("2001:db8:1::2", 4));
  fibInfo.idToNextHopIdSet()->emplace(100, std::set<int64_t>{1, 2});
  fibInfo.idToNextHopIdSet()->emplace(200, std::set<int64_t>{3, 4});
  fibInfo.nameToNextHopSetId()->emplace("v4_group", 100);
  fibInfo.nameToNextHopSetId()->emplace("v6_group", 200);
  fibInfo.overrideIdToNextHopIdSet()->emplace(300, std::set<int64_t>{1});

  stateThrift.fibsInfoMap()->emplace(matcherKey, fibInfo);

  auto labelFields = LabelForwardingEntry::makeThrift(LabelID(1234));
  labelFields.fwd() =
      util::getSwapLabelNextHopEntry(AdminDistance::DIRECTLY_CONNECTED)
          .toThrift();
  labelFields.fwd()->resolvedNextHopSetID() = 500;
  labelFields.fwd()->normalizedResolvedNextHopSetID() = 600;
  std::map<int32_t, state::LabelForwardingEntryFields> labelFib;
  labelFib[1234] = labelFields;
  stateThrift.labelFibMap()->emplace(matcherKey, labelFib);

  state::MySidFields mySid;
  mySid.type() = MySidType::NODE_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("fc00:100::1"));
  prefix.prefixLength() = 48;
  mySid.mySid() = prefix;
  mySid.resolvedNextHopsId() = 700;
  mySid.unresolveNextHopsId() = 800;
  std::map<std::string, state::MySidFields> mySidMap;
  mySidMap["fc00:100::1/48"] = mySid;
  stateThrift.mySidMaps()->emplace(matcherKey, mySidMap);

  // Trigger fromThrift -> uniquePtrFromThrift cleanup.
  auto deserialized = SwitchState::fromThrift(stateThrift);

  // V4/V6 route IDs cleared.
  auto fibsInfoMapOut = deserialized->getFibsInfoMap();
  ASSERT_NE(fibsInfoMapOut, nullptr);
  auto fibInfoOut = fibsInfoMapOut->getFibInfo(scope());
  ASSERT_NE(fibInfoOut, nullptr);
  auto fibsMapOut = fibInfoOut->getfibsMap();
  ASSERT_NE(fibsMapOut, nullptr);
  auto containerOut = fibsMapOut->getFibContainerIf(RouterID(0));
  ASSERT_NE(containerOut, nullptr);

  auto assertRouteIdsCleared = [](const auto& fib) {
    for (const auto& [_, route] : std::as_const(*fib)) {
      EXPECT_FALSE(
          route->getForwardInfo().getResolvedNextHopSetID().has_value());
      EXPECT_FALSE(route->getForwardInfo()
                       .getNormalizedResolvedNextHopSetID()
                       .has_value());
      EXPECT_FALSE(
          route->getForwardInfo().getOverrideNextHopSetID().has_value());
      for (const auto& [_clientId, entry] :
           std::as_const(route->getEntryForClients())) {
        EXPECT_FALSE(entry->getClientNextHopSetID().has_value());
      }
    }
  };
  ASSERT_EQ(containerOut->getFibV4()->size(), 1);
  ASSERT_EQ(containerOut->getFibV6()->size(), 1);
  assertRouteIdsCleared(containerOut->getFibV4());
  assertRouteIdsCleared(containerOut->getFibV6());

  // FibInfo id maps wiped.
  EXPECT_EQ(fibInfoOut->getIdToNextHopMap()->size(), 0);
  EXPECT_EQ(fibInfoOut->getIdToNextHopIdSetMap()->size(), 0);
  EXPECT_TRUE(fibInfoOut->getNameToNextHopSetId().empty());
  EXPECT_EQ(fibInfoOut->getOverrideIdToNextHopIdSetMap()->size(), 0);

  // Label route IDs cleared.
  auto multiLabelFib = deserialized->getLabelForwardingInformationBase();
  ASSERT_NE(multiLabelFib, nullptr);
  ASSERT_EQ(multiLabelFib->size(), 1);
  for (const auto& [_, labelFibOut] : std::as_const(*multiLabelFib)) {
    ASSERT_EQ(labelFibOut->size(), 1);
    assertRouteIdsCleared(labelFibOut);
  }

  // MySid IDs cleared.
  auto mySidMapsOut = deserialized->getMySids();
  ASSERT_NE(mySidMapsOut, nullptr);
  ASSERT_EQ(mySidMapsOut->size(), 1);
  for (const auto& [_, mySidMapOut] : std::as_const(*mySidMapsOut)) {
    ASSERT_EQ(mySidMapOut->size(), 1);
    for (const auto& [_id, mySidEntry] : std::as_const(*mySidMapOut)) {
      EXPECT_FALSE(mySidEntry->getResolvedNextHopsId().has_value());
      EXPECT_FALSE(mySidEntry->getUnresolveNextHopsId().has_value());
    }
  }
}

TEST(ThriftySwitchState, FromThriftIdMapCleanupOnRollForward) {
  // Roll-forward safety: when FLAGS_enable_nexthop_id_manager is ON but the
  // persisted FibInfo id maps are empty, fromThrift() must wipe stray
  // resolvedNextHopSetID / normalizedResolvedNextHopSetID values from every
  // route (V4, V6, label) and mySid entry. Empty id maps with non-empty
  // route IDs indicates either a first-time enable or a botched prior OFF
  // cleanup that left orphan IDs behind; reconstruction would otherwise
  // rebuild IDManager state from inconsistent input.
  auto savedFlag = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = true;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedFlag;
  };

  auto state = SwitchState();
  auto stateThrift = state.toThrift();
  const auto& matcherKey = HwSwitchMatcher::defaultHwSwitchMatcherKey();

  RouteNextHopSet v4NhSet;
  v4NhSet.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  v4NhSet.insert(ResolvedNextHop(
      folly::IPAddress("10.0.1.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));
  auto v4Fields =
      Route<folly::IPAddressV4>::makeThrift(makePrefixV4("10.0.0.0/24"));
  v4Fields.fwd() =
      RouteNextHopEntry(v4NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v4Fields.fwd()->resolvedNextHopSetID() = 111;
  v4Fields.fwd()->normalizedResolvedNextHopSetID() = 222;
  // Per-client entry carrying a stray clientNextHopSetID — must be wiped.
  auto v4ClientEntry =
      RouteNextHopEntry(v4NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v4ClientEntry.clientNextHopSetID() = 111;
  v4Fields.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, v4ClientEntry);

  RouteNextHopSet v6NhSet;
  v6NhSet.insert(ResolvedNextHop(
      folly::IPAddress("2001:db8:1::1"), InterfaceID(3), UCMP_DEFAULT_WEIGHT));
  v6NhSet.insert(ResolvedNextHop(
      folly::IPAddress("2001:db8:1::2"), InterfaceID(4), UCMP_DEFAULT_WEIGHT));
  auto v6Fields =
      Route<folly::IPAddressV6>::makeThrift(makePrefixV6("2001:db8::/32"));
  v6Fields.fwd() =
      RouteNextHopEntry(v6NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v6Fields.fwd()->resolvedNextHopSetID() = 333;
  v6Fields.fwd()->normalizedResolvedNextHopSetID() = 444;
  auto v6ClientEntry =
      RouteNextHopEntry(v6NhSet, AdminDistance::DIRECTLY_CONNECTED).toThrift();
  v6ClientEntry.clientNextHopSetID() = 333;
  v6Fields.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, v6ClientEntry);

  state::FibContainerFields fibContainer;
  fibContainer.vrf() = 0;
  fibContainer.fibV4()->emplace("10.0.0.0/24", v4Fields);
  fibContainer.fibV6()->emplace("2001:db8::/32", v6Fields);

  // Trigger condition: routes carry IDs but FibInfo's id maps are empty.
  state::FibInfoFields fibInfo;
  fibInfo.fibsMap()->emplace(0, fibContainer);
  ASSERT_TRUE(fibInfo.idToNextHop()->empty());
  ASSERT_TRUE(fibInfo.idToNextHopIdSet()->empty());
  ASSERT_TRUE(fibInfo.nameToNextHopSetId()->empty());

  stateThrift.fibsInfoMap()->emplace(matcherKey, fibInfo);

  auto labelFields = LabelForwardingEntry::makeThrift(LabelID(1234));
  labelFields.fwd() =
      util::getSwapLabelNextHopEntry(AdminDistance::DIRECTLY_CONNECTED)
          .toThrift();
  labelFields.fwd()->resolvedNextHopSetID() = 555;
  labelFields.fwd()->normalizedResolvedNextHopSetID() = 666;
  std::map<int32_t, state::LabelForwardingEntryFields> labelFib;
  labelFib[1234] = labelFields;
  stateThrift.labelFibMap()->emplace(matcherKey, labelFib);

  state::MySidFields mySid;
  mySid.type() = MySidType::NODE_MICRO_SID;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("fc00:100::1"));
  prefix.prefixLength() = 48;
  mySid.mySid() = prefix;
  mySid.resolvedNextHopsId() = 777;
  mySid.unresolveNextHopsId() = 888;
  std::map<std::string, state::MySidFields> mySidMap;
  mySidMap["fc00:100::1/48"] = mySid;
  stateThrift.mySidMaps()->emplace(matcherKey, mySidMap);

  // Trigger fromThrift -> uniquePtrFromThrift roll-forward cleanup.
  auto deserialized = SwitchState::fromThrift(stateThrift);

  // V4/V6 route IDs cleared.
  auto fibsInfoMapOut = deserialized->getFibsInfoMap();
  ASSERT_NE(fibsInfoMapOut, nullptr);
  auto fibInfoOut = fibsInfoMapOut->getFibInfo(scope());
  ASSERT_NE(fibInfoOut, nullptr);
  auto fibsMapOut = fibInfoOut->getfibsMap();
  ASSERT_NE(fibsMapOut, nullptr);
  auto containerOut = fibsMapOut->getFibContainerIf(RouterID(0));
  ASSERT_NE(containerOut, nullptr);

  auto assertRouteIdsCleared = [](const auto& fib) {
    for (const auto& [_, route] : std::as_const(*fib)) {
      EXPECT_FALSE(
          route->getForwardInfo().getResolvedNextHopSetID().has_value());
      EXPECT_FALSE(route->getForwardInfo()
                       .getNormalizedResolvedNextHopSetID()
                       .has_value());
      for (const auto& [_clientId, entry] :
           std::as_const(route->getEntryForClients())) {
        EXPECT_FALSE(entry->getClientNextHopSetID().has_value());
      }
    }
  };
  ASSERT_EQ(containerOut->getFibV4()->size(), 1);
  ASSERT_EQ(containerOut->getFibV6()->size(), 1);
  assertRouteIdsCleared(containerOut->getFibV4());
  assertRouteIdsCleared(containerOut->getFibV6());

  // FibInfo id maps remain empty (they were the gate signal).
  EXPECT_EQ(fibInfoOut->getIdToNextHopMap()->size(), 0);
  EXPECT_EQ(fibInfoOut->getIdToNextHopIdSetMap()->size(), 0);
  EXPECT_TRUE(fibInfoOut->getNameToNextHopSetId().empty());

  // Label route IDs cleared.
  auto multiLabelFib = deserialized->getLabelForwardingInformationBase();
  ASSERT_NE(multiLabelFib, nullptr);
  ASSERT_EQ(multiLabelFib->size(), 1);
  for (const auto& [_, labelFibOut] : std::as_const(*multiLabelFib)) {
    ASSERT_EQ(labelFibOut->size(), 1);
    assertRouteIdsCleared(labelFibOut);
  }

  // MySid IDs cleared.
  auto mySidMapsOut = deserialized->getMySids();
  ASSERT_NE(mySidMapsOut, nullptr);
  ASSERT_EQ(mySidMapsOut->size(), 1);
  for (const auto& [_, mySidMapOut] : std::as_const(*mySidMapsOut)) {
    ASSERT_EQ(mySidMapOut->size(), 1);
    for (const auto& [_id, mySidEntry] : std::as_const(*mySidMapOut)) {
      EXPECT_FALSE(mySidEntry->getResolvedNextHopsId().has_value());
      EXPECT_FALSE(mySidEntry->getUnresolveNextHopsId().has_value());
    }
  }
}

TEST(ThriftySwitchState, FromThriftClearsOverrideIdsWhenOnlyOverrideMapLeft) {
  // Manager off and only the override id map left (a manager-off binary that
  // cleared just the NextHop id maps): override ids must still be cleared.
  auto savedFlag = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = false;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedFlag;
  };

  auto state =
      SwitchState::fromThrift(makeOverrideIdState(false /*withNextHopIds*/));

  EXPECT_EQ(routeOverrideId(state), std::nullopt);
  EXPECT_EQ(overrideIdMapSize(state), 0);
}

TEST(ThriftySwitchState, FromThriftClearsLeftoverOverrideIdsOnRollForward) {
  // Manager on with empty NextHop id maps: leftover override ids point at
  // NextHop ids that no longer exist and must be cleared.
  auto savedFlag = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = true;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedFlag;
  };

  auto state =
      SwitchState::fromThrift(makeOverrideIdState(false /*withNextHopIds*/));

  EXPECT_EQ(routeOverrideId(state), std::nullopt);
  EXPECT_EQ(overrideIdMapSize(state), 0);
}

TEST(ThriftySwitchState, FromThriftKeepsOverrideIdsWithNextHopIds) {
  // Manager on with NextHop id maps present: override ids are kept.
  auto savedFlag = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = true;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedFlag;
  };

  auto state =
      SwitchState::fromThrift(makeOverrideIdState(true /*withNextHopIds*/));

  EXPECT_EQ(routeOverrideId(state), NextHopSetID(kOverrideId));
  EXPECT_EQ(overrideIdMapSize(state), 1);
}
