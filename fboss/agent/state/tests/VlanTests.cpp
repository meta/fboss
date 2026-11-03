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
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/state/ArpResponseTable.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/NdpResponseTable.h"
#include "fboss/agent/state/NodeMapDelta.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/test/TestUtils.h"

#include <fmt/core.h>
#include <gtest/gtest.h>
#include <string>

using namespace facebook::fboss;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::make_pair;
using std::make_shared;
using std::shared_ptr;
using std::string;
using testing::Return;

namespace {
static const string kVlan1234("Vlan1234");
static const string kVlan99("Vlan99");
static const string kVlan1299("Vlan1299");

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

state::VlanInfo makeVlanInfo(bool tagged, bool priorityTagged) {
  state::VlanInfo info;
  *info.tagged() = tagged;
  *info.priorityTagged() = priorityTagged;
  return info;
}
} // namespace

template <typename Entry>
void validateRespEntry(
    std::shared_ptr<Entry> entry,
    folly::MacAddress mac,
    InterfaceID intf) {
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(entry->getMac(), mac);
  EXPECT_EQ(entry->getInterfaceID(), intf);
}

TEST(Vlan, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  auto vlanV0 = make_shared<Vlan>(VlanID(1234), kVlan1234);

  stateV0->getVlans()->addNode(vlanV0, scope());
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());

  NodeID nodeID = vlanV0->getNodeID();
  EXPECT_EQ(0, vlanV0->getGeneration());
  EXPECT_FALSE(vlanV0->isPublished());
  EXPECT_EQ(VlanID(1234), vlanV0->getID());
  Vlan::MemberPorts emptyPorts;
  EXPECT_EQ(emptyPorts, vlanV0->getPortsInfo());

  vlanV0->publish();
  EXPECT_TRUE(vlanV0->isPublished());

  cfg::SwitchConfig config;
  config.ports()->resize(2);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  config.vlans()->resize(1);
  config.vlans()[0].id() = 1234;
  config.vlans()[0].name() = kVlan1234;
  config.vlans()[0].dhcpRelayAddressV4() = "30.1.1.1";
  config.vlans()[0].dhcpRelayAddressV6() = "2a03:2880:10:1f07:face:b00c:0:0";
  config.vlans()[0].dhcpRelayOverridesV4() = {};
  (*config.vlans()[0].dhcpRelayOverridesV4())["02:00:00:00:00:02"] = "1.2.3.4";
  config.vlans()[0].dhcpRelayOverridesV6() = {};
  (*config.vlans()[0].dhcpRelayOverridesV6())["02:00:00:00:00:02"] =
      "2a03:2880:10:1f07:face:b00c:0:0";
  config.vlans()[0].intfID() = 1234;
  config.vlanPorts()->resize(2);
  config.vlanPorts()[0].logicalPort() = 1;
  config.vlanPorts()[0].vlanID() = 1234;
  config.vlanPorts()[0].emitTags() = false;
  config.vlanPorts()[1].logicalPort() = 2;
  config.vlanPorts()[1].vlanID() = 1234;
  config.vlanPorts()[1].emitTags() = true;

  config.interfaces()->resize(1);
  config.interfaces()[0].intfID() = 1234;
  config.interfaces()[0].vlanID() = 1234;

  Vlan::MemberPorts expectedPorts;
  expectedPorts.insert(std::make_pair(1, makeVlanInfo(false, false)));
  expectedPorts.insert(std::make_pair(2, makeVlanInfo(true, false)));

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto vlanV1 = stateV1->getVlans()->getNode(VlanID(1234));
  ASSERT_NE(nullptr, vlanV1);
  auto vlanV1_byName = stateV1->getVlans()->getVlanSlow(kVlan1234);
  EXPECT_EQ(vlanV1, vlanV1_byName);
  EXPECT_EQ(nodeID, vlanV1->getNodeID());
  EXPECT_EQ(1, vlanV1->getGeneration());
  EXPECT_FALSE(vlanV1->isPublished());
  EXPECT_EQ(VlanID(1234), vlanV1->getID());
  EXPECT_EQ(kVlan1234, vlanV1->getName());
  EXPECT_EQ(expectedPorts, vlanV1->getPortsInfo());
  EXPECT_EQ(0, vlanV1->getArpResponseTable()->size());
  EXPECT_EQ(InterfaceID(1234), vlanV1->getInterfaceID());
  EXPECT_EQ(folly::IPAddressV4("30.1.1.1"), vlanV1->getDhcpV4Relay());
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:0"),
      vlanV1->getDhcpV6Relay());

  auto map4 = vlanV1->getDhcpV4RelayOverrides();
  EXPECT_EQ(
      IPAddressV4("1.2.3.4"),
      IPAddressV4(map4[MacAddress("02:00:00:00:00:02")]));
  auto map6 = vlanV1->getDhcpV6RelayOverrides();
  EXPECT_EQ(
      IPAddressV6("2a03:2880:10:1f07:face:b00c:0:0"),
      IPAddressV6(map6[MacAddress("02:00:00:00:00:02")]));

  // Applying the same config again should return null
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV1, &config, platform.get()));

  // Add an interface
  config.interfaces()->resize(2);
  *config.interfaces()[1].intfID() = 1;
  *config.interfaces()[1].routerID() = 0;
  *config.interfaces()[1].vlanID() = 1;
  config.interfaces()[1].ipAddresses()->resize(2);
  config.interfaces()[1].ipAddresses()[0] = "10.1.1.1/24";
  config.interfaces()[1].ipAddresses()[1] =
      "2a03:2880:10:1f07:face:b00c:0:0/96";
  config.vlans()->resize(2);
  config.vlans()[1].id() = 1;
  config.vlans()[1].name() = "vlan1";
  config.vlans()[1].intfID() = 1;

  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  auto vlanV2 = stateV2->getVlans()->getNode(VlanID(1));
  auto intfV2 = stateV2->getInterfaces()->getNode(InterfaceID(1));
  EXPECT_EQ(0, vlanV2->getGeneration());
  EXPECT_FALSE(vlanV2->isPublished());
  EXPECT_EQ(VlanID(1), vlanV2->getID());
  EXPECT_EQ("vlan1", vlanV2->getName());
  EXPECT_EQ(0, vlanV2->getPortsInfo().size());
  EXPECT_EQ(InterfaceID(1), vlanV2->getInterfaceID());

  // Check the ArpResponseTable (from interface)
  auto arpRespTable = intfV2->getArpResponseTable();
  auto ndpRespTable = intfV2->getNdpResponseTable();
  EXPECT_EQ(arpRespTable->size(), 1);
  validateRespEntry(
      arpRespTable->getEntry(IPAddressV4("10.1.1.1")),
      MockPlatform::getMockLocalMac(),
      InterfaceID(1));
  // Check the NdpResponseTable
  EXPECT_EQ(ndpRespTable->size(), 2);
  validateRespEntry(
      ndpRespTable->getEntry(IPAddressV6("2a03:2880:10:1f07:face:b00c:0:0")),
      MockPlatform::getMockLocalMac(),
      InterfaceID(1));

  // The link-local IPv6 address should also have been automatically added
  // to the NDP response table.
  validateRespEntry(
      ndpRespTable->getEntry(MockPlatform::getMockLinkLocalIp6()),
      MockPlatform::getMockLocalMac(),
      InterfaceID(1));

  // Add another vlan and interface
  config.vlans()->resize(3);
  *config.vlans()[2].id() = 1299;
  *config.vlans()[2].name() = kVlan1299;
  config.vlans()[2].intfID() = 1299;
  config.interfaces()->resize(3);
  *config.interfaces()[2].intfID() = 1299;
  *config.interfaces()[2].routerID() = 0;
  *config.interfaces()[2].vlanID() = 1299;
  config.interfaces()[2].ipAddresses()->resize(2);
  config.interfaces()[2].ipAddresses()[0] = "10.1.10.1/24";
  config.interfaces()[2].ipAddresses()[1] = "192.168.0.1/31";
  MacAddress intf2Mac("02:01:02:ab:cd:78");
  config.interfaces()[2].mac() = intf2Mac.toString();
  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  auto vlanV3 = stateV3->getVlans()->getNode(VlanID(1299));
  auto intfV3 = stateV3->getInterfaces()->getNode(InterfaceID(1299));
  EXPECT_EQ(0, vlanV3->getGeneration());
  EXPECT_FALSE(vlanV3->isPublished());
  EXPECT_EQ(VlanID(1299), vlanV3->getID());
  EXPECT_EQ(kVlan1299, vlanV3->getName());
  EXPECT_EQ(InterfaceID(1299), vlanV3->getInterfaceID());

  // Check the ArpResponseTable (from interface)
  arpRespTable = intfV3->getArpResponseTable();
  ndpRespTable = intfV3->getNdpResponseTable();
  EXPECT_EQ(arpRespTable->size(), 2);
  validateRespEntry(
      arpRespTable->getEntry(IPAddressV4("10.1.10.1")),
      intf2Mac,
      InterfaceID(1299));
  validateRespEntry(
      arpRespTable->getEntry(IPAddressV4("192.168.0.1")),
      intf2Mac,
      InterfaceID(1299));

  // The new interface has no IPv6 address, but the NDP table should still
  // be updated with the link-local address.
  EXPECT_EQ(ndpRespTable->size(), 1);
  validateRespEntry(
      ndpRespTable->getEntry(IPAddressV6("fe80::1:02ff:feab:cd78")),
      intf2Mac,
      InterfaceID(1299));

  // Add a new VLAN with an ArpResponseTable that needs to be set up
  // when the VLAN is first created
  config.vlans()->resize(4);
  *config.vlans()[3].id() = 99;
  *config.vlans()[3].name() = kVlan99;
  config.vlans()[3].intfID() = 99;
  config.interfaces()->resize(4);
  *config.interfaces()[3].intfID() = 99;
  *config.interfaces()[3].routerID() = 1;
  config.vrfs().ensure().emplace_back().vrfId() = 1;
  *config.interfaces()[3].vlanID() = 99;
  config.interfaces()[3].ipAddresses()->resize(2);
  config.interfaces()[3].ipAddresses()[0] = "1.2.3.4/24";
  config.interfaces()[3].ipAddresses()[1] = "10.0.0.1/9";
  auto stateV4 = publishAndApplyConfig(stateV3, &config, platform.get());
  ASSERT_NE(nullptr, stateV4);
  // VLAN 1 should be unchanged
  EXPECT_EQ(vlanV2, stateV4->getVlans()->getNode(VlanID(1)));
  auto vlan99 = stateV4->getVlans()->getNode(VlanID(99));
  auto intf99 = stateV4->getInterfaces()->getNode(InterfaceID(99));
  auto vlan99_byName = stateV4->getVlans()->getVlanSlow(kVlan99);
  ASSERT_NE(nullptr, vlan99);
  EXPECT_EQ(vlan99, vlan99_byName);
  EXPECT_EQ(0, vlan99->getGeneration());
  EXPECT_EQ(InterfaceID(99), vlan99->getInterfaceID());

  // Check the ArpResponseTable (from interface)
  arpRespTable = intf99->getArpResponseTable();
  EXPECT_EQ(arpRespTable->size(), 2);
  validateRespEntry(
      arpRespTable->getEntry(IPAddressV4("1.2.3.4")),
      MockPlatform::getMockLocalMac(),
      InterfaceID(99));
  validateRespEntry(
      arpRespTable->getEntry(IPAddressV4("10.0.0.1")),
      MockPlatform::getMockLocalMac(),
      InterfaceID(99));

  // Check vlan congfig with no intfID set
  config.vlans()->resize(5);
  *config.vlans()[4].id() = 100;
  config.vlans()[4].intfID().reset();
  config.interfaces()->resize(5);
  *config.interfaces()[4].intfID() = 100;
  *config.interfaces()[4].routerID() = 0;
  *config.interfaces()[4].vlanID() = 100;
  config.interfaces()[4].ipAddresses()->resize(2);
  config.interfaces()[4].ipAddresses()[0] = "10.50.3.7/24";
  config.interfaces()[4].ipAddresses()[1] = "10.50.0.3/9";
  auto stateV5 = publishAndApplyConfig(stateV4, &config, platform.get());
  ASSERT_NE(nullptr, stateV5);
  auto vlan100 = stateV5->getVlans()->getNode(VlanID(100));
  EXPECT_EQ(InterfaceID(100), vlan100->getInterfaceID());

  // Change DHCP relay configuration
  config.vlans()[0].dhcpRelayAddressV4() = "30.1.1.2";
  config.vlans()[0].dhcpRelayAddressV6() = "2a03:2880:10:1f07:face:b00c:0:2";
  auto stateV6 = publishAndApplyConfig(stateV5, &config, platform.get());
  auto vlanV6 = stateV6->getVlans()->getNode(VlanID(1234));

  EXPECT_EQ(folly::IPAddressV4("30.1.1.2"), vlanV6->getDhcpV4Relay());
  EXPECT_EQ(
      folly::IPAddressV6("2a03:2880:10:1f07:face:b00c:0:2"),
      vlanV6->getDhcpV6Relay());
}

/*
 * Test that forEachChanged(StateDelta::getVlansDelta(), ...) invokes the
 * callback for the specified list of changed VLANs.
 */
void checkChangedVlans(
    const shared_ptr<MultiSwitchVlanMap>& oldVlans,
    const shared_ptr<MultiSwitchVlanMap>& newVlans,
    const std::set<uint16_t> changedIDs,
    const std::set<uint16_t> addedIDs,
    const std::set<uint16_t> removedIDs) {
  auto oldState = make_shared<SwitchState>();
  oldState->resetVlans(oldVlans);
  auto newState = make_shared<SwitchState>();
  newState->resetVlans(newVlans);

  std::set<uint16_t> foundChanged;
  std::set<uint16_t> foundAdded;
  std::set<uint16_t> foundRemoved;
  StateDelta delta(oldState, newState);
  DeltaFunctions::forEachChanged(
      delta.getVlansDelta(),
      [&](const shared_ptr<Vlan>& oldVlan, const shared_ptr<Vlan>& newVlan) {
        EXPECT_EQ(oldVlan->getID(), newVlan->getID());
        EXPECT_NE(oldVlan, newVlan);

        auto ret = foundChanged.insert(oldVlan->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<Vlan>& vlan) {
        auto ret = foundAdded.insert(vlan->getID());
        EXPECT_TRUE(ret.second);
      },
      [&](const shared_ptr<Vlan>& vlan) {
        auto ret = foundRemoved.insert(vlan->getID());
        EXPECT_TRUE(ret.second);
      });

  EXPECT_EQ(changedIDs, foundChanged);
  EXPECT_EQ(addedIDs, foundAdded);
  EXPECT_EQ(removedIDs, foundRemoved);
}

TEST(VlanMap, applyConfig) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  cfg::SwitchConfig config;

  std::vector<int> ports = {1, 2, 3, 4, 9, 19, 20};
  config.ports()->resize(ports.size());
  for (int i = 0; i < ports.size(); i++) {
    int port = ports[i];
    registerPort(stateV0, PortID(port), fmt::format("port{}", port), scope());

    preparedMockPortConfig(
        config.ports()[i],
        port,
        fmt::format("port{}", port),
        cfg::PortState::DISABLED);
  }

  auto vlansV0 = stateV0->getVlans();

  // Apply new config settings
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 1234;
  *config.vlans()[0].name() = kVlan1234;
  *config.vlans()[1].id() = 99;
  *config.vlans()[1].name() = kVlan99;
  config.vlanPorts()->resize(7);
  *config.vlanPorts()[0].vlanID() = 1234;
  *config.vlanPorts()[0].logicalPort() = 1;
  *config.vlanPorts()[1].vlanID() = 1234;
  *config.vlanPorts()[1].logicalPort() = 2;
  *config.vlanPorts()[2].vlanID() = 1234;
  *config.vlanPorts()[2].logicalPort() = 3;
  *config.vlanPorts()[3].vlanID() = 1234;
  *config.vlanPorts()[3].logicalPort() = 4;
  *config.vlanPorts()[4].vlanID() = 99;
  *config.vlanPorts()[4].logicalPort() = 9;
  *config.vlanPorts()[5].vlanID() = 99;
  *config.vlanPorts()[5].logicalPort() = 19;
  *config.vlanPorts()[6].vlanID() = 99;
  *config.vlanPorts()[6].logicalPort() = 29;

  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1234;
  *config.interfaces()[0].vlanID() = 1234;
  *config.interfaces()[0].routerID() = 0;
  *config.interfaces()[1].intfID() = 99;
  *config.interfaces()[1].vlanID() = 99;
  *config.interfaces()[1].routerID() = 0;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto vlansV1 = stateV1->getVlans();
  ASSERT_NE(nullptr, vlansV1);
  EXPECT_EQ(2, vlansV1->numNodes());

  // Check the new settings for VLAN 1234
  auto vlan1234v0 = vlansV1->getNode(VlanID(1234));
  auto vlan1234v0_byName = stateV1->getVlans()->getVlanSlow(kVlan1234);
  ASSERT_NE(nullptr, vlan1234v0);
  EXPECT_EQ(vlan1234v0, vlan1234v0_byName);
  NodeID id1234 = vlan1234v0->getNodeID();
  EXPECT_EQ(VlanID(1234), vlan1234v0->getID());
  EXPECT_EQ(kVlan1234, vlan1234v0->getName());
  EXPECT_EQ(0, vlan1234v0->getGeneration());
  Vlan::MemberPorts ports1234v0;
  auto untaggedInfo = makeVlanInfo(false, false);
  ports1234v0.insert(make_pair(1, untaggedInfo));
  ports1234v0.insert(make_pair(2, untaggedInfo));
  ports1234v0.insert(make_pair(3, untaggedInfo));
  ports1234v0.insert(make_pair(4, untaggedInfo));
  EXPECT_EQ(ports1234v0, vlan1234v0->getPortsInfo());

  // Check the new settings for VLAN 99
  auto vlan99v0 = vlansV1->getNode(VlanID(99));
  ASSERT_NE(nullptr, vlan99v0);
  auto vlan99v0_byName = stateV1->getVlans()->getVlanSlow(kVlan99);
  EXPECT_EQ(vlan99v0, vlan99v0_byName);
  NodeID id99 = vlan99v0->getNodeID();
  EXPECT_NE(id1234, id99);
  EXPECT_EQ(VlanID(99), vlan99v0->getID());
  EXPECT_EQ(kVlan99, vlan99v0->getName());
  EXPECT_EQ(0, vlan99v0->getGeneration());
  Vlan::MemberPorts ports99v1;
  ports99v1.insert(make_pair(9, untaggedInfo));
  ports99v1.insert(make_pair(19, untaggedInfo));
  ports99v1.insert(make_pair(29, untaggedInfo));
  EXPECT_EQ(ports99v1, vlan99v0->getPortsInfo());

  // getVlan() should throw on a non-existent VLAN
  EXPECT_THROW(vlansV1->getNode(VlanID(1)), FbossError);
  // getVlanIf() should return null on a non-existent VLAN
  EXPECT_EQ(nullptr, vlansV1->getNodeIf(VlanID(1233)));

  checkChangedVlans(vlansV0, vlansV1, {}, {99, 1234}, {});

  // Applying the same config again should result in no change
  EXPECT_EQ(nullptr, publishAndApplyConfig(stateV1, &config, platform.get()));

  // Drop one port from VLAN 1234
  config.vlanPorts()[0] = config.vlanPorts()[6];
  config.vlanPorts()->resize(6);
  auto stateV2 = publishAndApplyConfig(stateV1, &config, platform.get());
  auto vlansV2 = stateV2->getVlans();
  ASSERT_NE(nullptr, vlansV2);
  EXPECT_EQ(2, vlansV2->numNodes());

  // VLAN 1234 should have changed
  auto vlan1234v1 = vlansV2->getNode(VlanID(1234));
  EXPECT_NE(vlan1234v0, vlan1234v1);
  EXPECT_EQ(1, vlan1234v1->getGeneration());
  Vlan::MemberPorts ports1234v1;
  ports1234v1.insert(make_pair(2, untaggedInfo));
  ports1234v1.insert(make_pair(3, untaggedInfo));
  ports1234v1.insert(make_pair(4, untaggedInfo));
  EXPECT_EQ(ports1234v1, vlan1234v1->getPortsInfo());

  // VLAN 99 should not have changed
  EXPECT_EQ(vlan99v0, vlansV2->getNode(VlanID(99)));

  checkChangedVlans(vlansV1, vlansV2, {1234}, {}, {});
  EXPECT_EQ(id1234, vlansV2->getNode(VlanID(1234))->getNodeID());
  EXPECT_EQ(id99, vlansV2->getNode(VlanID(99))->getNodeID());

  // Remove VLAN 99
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1234;
  config.vlanPorts()->resize(3);
  *config.vlanPorts()[0].vlanID() = 1234;
  *config.vlanPorts()[0].logicalPort() = 2;
  *config.vlanPorts()[1].vlanID() = 1234;
  *config.vlanPorts()[1].logicalPort() = 3;
  *config.vlanPorts()[2].vlanID() = 1234;
  *config.vlanPorts()[2].logicalPort() = 4;
  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = 1234;

  auto stateV3 = publishAndApplyConfig(stateV2, &config, platform.get());
  auto vlansV3 = stateV3->getVlans();
  ASSERT_NE(nullptr, vlansV3);
  EXPECT_EQ(1, vlansV3->numNodes());

  // VLAN 1234 should not have changed
  EXPECT_EQ(vlan1234v1, vlansV3->getNode(VlanID(1234)));
  // VLAN 99 should no longer exist
  EXPECT_EQ(nullptr, vlansV3->getNodeIf(VlanID(99)));

  checkChangedVlans(vlansV2, vlansV3, {}, {}, {99});
}

TEST(Vlan, priorityTagging) {
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());
  registerPort(stateV0, PortID(3), "port3", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(3);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  preparedMockPortConfig(config.ports()[2], 3);

  config.vlans()->resize(1);
  config.vlans()[0].id() = 100;
  config.vlans()[0].name() = "vlan100";
  config.vlans()[0].intfID() = 100;

  // Configure ports with different tagging modes
  config.vlanPorts()->resize(3);
  // Port 1: untagged (no tags, no priority tags)
  config.vlanPorts()[0].logicalPort() = 1;
  config.vlanPorts()[0].vlanID() = 100;
  config.vlanPorts()[0].emitTags() = false;
  config.vlanPorts()[0].emitPriorityTags() = false;

  // Port 2: tagged (VLAN tags, no priority tags)
  config.vlanPorts()[1].logicalPort() = 2;
  config.vlanPorts()[1].vlanID() = 100;
  config.vlanPorts()[1].emitTags() = true;
  config.vlanPorts()[1].emitPriorityTags() = false;

  // Port 3: priority tagged (no VLAN ID, but has 802.1p priority)
  config.vlanPorts()[2].logicalPort() = 3;
  config.vlanPorts()[2].vlanID() = 100;
  config.vlanPorts()[2].emitTags() = false;
  config.vlanPorts()[2].emitPriorityTags() = true;

  config.interfaces()->resize(1);
  config.interfaces()[0].intfID() = 100;
  config.interfaces()[0].vlanID() = 100;

  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto vlan = stateV1->getVlans()->getNode(VlanID(100));
  ASSERT_NE(nullptr, vlan);

  // Verify the port membership with correct tagging modes
  auto ports = vlan->getPortsInfo();
  EXPECT_EQ(3, ports.size());

  // Port 1: untagged
  auto port1Info = ports.find(1);
  ASSERT_NE(port1Info, ports.end());
  EXPECT_FALSE(*port1Info->second.tagged());
  EXPECT_FALSE(*port1Info->second.priorityTagged());

  // Port 2: tagged
  auto port2Info = ports.find(2);
  ASSERT_NE(port2Info, ports.end());
  EXPECT_TRUE(*port2Info->second.tagged());
  EXPECT_FALSE(*port2Info->second.priorityTagged());

  // Port 3: priority tagged
  auto port3Info = ports.find(3);
  ASSERT_NE(port3Info, ports.end());
  EXPECT_FALSE(*port3Info->second.tagged());
  EXPECT_TRUE(*port3Info->second.priorityTagged());
}

TEST(Vlan, vlanModifyUnpublished) {
  auto state = make_shared<SwitchState>();
  auto vlan = make_shared<Vlan>(VlanID(1234), kVlan1234);
  state->getVlans()->addNode(vlan, scope());
  EXPECT_EQ(vlan.get(), vlan->modify(&state));
}

TEST(Vlan, vlanModifyPublished) {
  auto state = make_shared<SwitchState>();
  auto vlan = make_shared<Vlan>(VlanID(1234), kVlan1234);
  state->getVlans()->addNode(vlan, scope());
  state->publish();
  EXPECT_NE(vlan.get(), vlan->modify(&state));
}

TEST(Vlan, warmbootMigration) {
  // This verifies that toThrift() properly populates both old ports field
  // and new portsInfo field for backward compatibility

  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();
  registerPort(stateV0, PortID(1), "port1", scope());
  registerPort(stateV0, PortID(2), "port2", scope());
  registerPort(stateV0, PortID(3), "port3", scope());

  cfg::SwitchConfig config;
  config.ports()->resize(3);
  preparedMockPortConfig(config.ports()[0], 1);
  preparedMockPortConfig(config.ports()[1], 2);
  preparedMockPortConfig(config.ports()[2], 3);

  config.vlans()->resize(1);
  config.vlans()[0].id() = 200;
  config.vlans()[0].name() = "vlan200";
  config.vlans()[0].intfID() = 200;

  config.vlanPorts()->resize(3);
  // Port 1: untagged
  config.vlanPorts()[0].logicalPort() = 1;
  config.vlanPorts()[0].vlanID() = 200;
  config.vlanPorts()[0].emitTags() = false;
  config.vlanPorts()[0].emitPriorityTags() = false;

  // Port 2: tagged
  config.vlanPorts()[1].logicalPort() = 2;
  config.vlanPorts()[1].vlanID() = 200;
  config.vlanPorts()[1].emitTags() = true;
  config.vlanPorts()[1].emitPriorityTags() = false;

  // Port 3: priority tagged
  config.vlanPorts()[2].logicalPort() = 3;
  config.vlanPorts()[2].vlanID() = 200;
  config.vlanPorts()[2].emitTags() = false;
  config.vlanPorts()[2].emitPriorityTags() = true;

  config.interfaces()->resize(1);
  config.interfaces()[0].intfID() = 200;
  config.interfaces()[0].vlanID() = 200;

  // Create initial state with all ports configured
  auto stateV1 = publishAndApplyConfig(stateV0, &config, platform.get());
  auto vlan = stateV1->getVlans()->getNode(VlanID(200));
  ASSERT_NE(nullptr, vlan);

  // Verify initial state has correct portsInfo via getPortsInfo()
  auto ports = vlan->getPortsInfo();
  EXPECT_EQ(3, ports.size());
  EXPECT_FALSE(*ports[1].tagged());
  EXPECT_FALSE(*ports[1].priorityTagged());
  EXPECT_TRUE(*ports[2].tagged());
  EXPECT_FALSE(*ports[2].priorityTagged());
  EXPECT_FALSE(*ports[3].tagged());
  EXPECT_TRUE(*ports[3].priorityTagged());

  // Serialize state and verify both old and new fields are populated
  auto serializedState = stateV1->toThrift();

  // Find the VLAN in serialized state
  bool foundVlan = false;
  for (const auto& [matcherKey, vlanMapThrift] : *serializedState.vlanMaps()) {
    for (const auto& [vlanId, vlanThrift] : vlanMapThrift) {
      if (vlanId == 200) {
        foundVlan = true;

        // Verify new portsInfo field has all the data including priorityTagged
        EXPECT_EQ(3, vlanThrift.portsInfo()->size());

        auto& portsInfo = *vlanThrift.portsInfo();
        EXPECT_FALSE(*portsInfo.at(1).tagged());
        EXPECT_FALSE(*portsInfo.at(1).priorityTagged());
        EXPECT_TRUE(*portsInfo.at(2).tagged());
        EXPECT_FALSE(*portsInfo.at(2).priorityTagged());
        EXPECT_FALSE(*portsInfo.at(3).tagged());
        EXPECT_TRUE(*portsInfo.at(3).priorityTagged());

        // Verify old ports field was also populated by toThrift() for warmboot
        ASSERT_TRUE(
            apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
                vlanThrift.ports_DEPRECATED()));
        EXPECT_EQ(3, vlanThrift.ports_DEPRECATED()->size());

        auto& oldPorts = *vlanThrift.ports_DEPRECATED();
        EXPECT_FALSE(oldPorts.at(1)); // untagged
        EXPECT_TRUE(oldPorts.at(2)); // tagged
        EXPECT_FALSE(
            oldPorts.at(3)); // priority-tagged appears as untagged in old field
      }
    }
  }
  EXPECT_TRUE(foundVlan);

  // Test round-trip: deserialize and re-serialize
  auto restoredState = SwitchState::fromThrift(serializedState);
  ASSERT_NE(nullptr, restoredState);

  auto restoredVlan = restoredState->getVlans()->getNodeIf(VlanID(200));
  ASSERT_NE(nullptr, restoredVlan);

  // Verify deserialized state preserves all data
  auto restoredPorts = restoredVlan->getPortsInfo();
  EXPECT_EQ(3, restoredPorts.size());
  EXPECT_FALSE(*restoredPorts[1].tagged());
  EXPECT_FALSE(*restoredPorts[1].priorityTagged());
  EXPECT_TRUE(*restoredPorts[2].tagged());
  EXPECT_FALSE(*restoredPorts[2].priorityTagged());
  EXPECT_FALSE(*restoredPorts[3].tagged());
  EXPECT_TRUE(*restoredPorts[3].priorityTagged());

  // Verify re-serialization still populates both fields
  auto reserializedState = restoredState->toThrift();
  foundVlan = false;
  for (const auto& [matcherKey, vlanMapThrift] :
       *reserializedState.vlanMaps()) {
    for (const auto& [vlanId, vlanThrift] : vlanMapThrift) {
      if (vlanId == 200) {
        foundVlan = true;
        EXPECT_EQ(3, vlanThrift.portsInfo()->size());
        ASSERT_TRUE(
            apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
                vlanThrift.ports_DEPRECATED()));
        EXPECT_EQ(3, vlanThrift.ports_DEPRECATED()->size());
      }
    }
  }
  EXPECT_TRUE(foundVlan);
}
