/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/NdpEntry.h"
#include "fboss/agent/state/Vlan.h"

#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

template <typename VlanOrIntfT>
void verifyNeighborResponseTableHelper(
    const std::shared_ptr<VlanOrIntfT> vlanOrIntf) {
  auto ip1 = folly::IPAddressV4("192.168.0.1"),
       ip2 = folly::IPAddressV4("192.168.0.2");
  auto mac1 = folly::MacAddress("01:01:01:01:01:01"),
       mac2 = folly::MacAddress("01:01:01:01:01:02");

  auto state = std::make_shared<SwitchState>();
  auto arpResponseTable = std::make_shared<ArpResponseTable>();
  arpResponseTable->setEntry(ip1, mac1, InterfaceID(0));
  vlanOrIntf->setArpResponseTable(arpResponseTable);

  if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
    state->getVlans()->addNode(vlanOrIntf, scope());
  } else {
    state->getInterfaces()->addNode(vlanOrIntf, scope());
  }

  // modify unpublished state
  EXPECT_EQ(vlanOrIntf.get(), vlanOrIntf->modify(&state));

  arpResponseTable = std::make_shared<ArpResponseTable>();
  arpResponseTable->setEntry(ip2, mac2, InterfaceID(1));
  vlanOrIntf->setArpResponseTable(arpResponseTable);

  // modify unpublished state
  EXPECT_EQ(vlanOrIntf.get(), vlanOrIntf->modify(&state));

  vlanOrIntf->publish();
  auto modifiedVlanOrIntf = vlanOrIntf->modify(&state);

  arpResponseTable = std::make_shared<ArpResponseTable>();
  arpResponseTable->setEntry(ip1, mac1, InterfaceID(0));
  modifiedVlanOrIntf->setArpResponseTable(arpResponseTable);

  EXPECT_NE(vlanOrIntf.get(), modifiedVlanOrIntf);

  EXPECT_FALSE(vlanOrIntf->getArpResponseTable()->getEntry(ip1) != nullptr);
  EXPECT_TRUE(vlanOrIntf->getArpResponseTable()->getEntry(ip2) != nullptr);
  EXPECT_TRUE(
      modifiedVlanOrIntf->getArpResponseTable()->getEntry(ip1) != nullptr);
  EXPECT_FALSE(
      modifiedVlanOrIntf->getArpResponseTable()->getEntry(ip2) != nullptr);
}

} // namespace

using namespace facebook::fboss;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

template <typename NeighborEntryT>
void serializeTest(const NeighborEntryT& entry) {
  auto entryBack = NeighborEntryT(entry.toThrift());
  EXPECT_EQ(entry.toThrift(), entryBack.toThrift());
}

TEST(ArpEntry, serialize) {
  auto entry = std::make_unique<ArpEntry>(
      IPAddressV4("192.168.0.1"),
      MacAddress("01:01:01:01:01:01"),
      PortDescriptor(PortID(1)),
      InterfaceID(10),
      state::NeighborEntryType::DYNAMIC_ENTRY,
      NeighborState::REACHABLE,
      std::optional<cfg::AclLookupClass>(std::nullopt),
      std::optional<int64_t>(42),
      false);
  validateNodeSerialization(*entry);
  serializeTest(*entry);
}

TEST(NdpEntry, serialize) {
  auto entry = std::make_unique<NdpEntry>(
      IPAddressV6("2401:db00:21:70cb:face:0:96:0"),
      MacAddress("01:01:01:01:01:01"),
      PortDescriptor(PortID(10)),
      InterfaceID(10),
      state::NeighborEntryType::DYNAMIC_ENTRY,
      NeighborState::REACHABLE,
      std::optional<cfg::AclLookupClass>(std::nullopt),
      std::optional<int64_t>(42),
      false);
  validateNodeSerialization(*entry);
  serializeTest(*entry);
}

TEST(ArpTable, serialize) {
  ArpTable table;
  table.addEntry(
      IPAddressV4("192.168.0.1"),
      MacAddress("01:01:01:01:01:01"),
      PortDescriptor(PortID(10)),
      InterfaceID(10),
      NeighborState::REACHABLE);
  table.addEntry(
      IPAddressV4("192.168.0.2"),
      MacAddress("01:01:01:01:01:02"),
      PortDescriptor(PortID(11)),
      InterfaceID(11),
      NeighborState::PENDING);

  serializeTest(table);
}

TEST(NdpTable, serialize) {
  NdpTable table;
  table.addEntry(
      IPAddressV6("2401:db00:21:70cb:face:0:96:0"),
      MacAddress("01:01:01:01:01:01"),
      PortDescriptor(PortID(10)),
      InterfaceID(10),
      NeighborState::REACHABLE);
  table.addEntry(
      IPAddressV6("2401:db00:21:70cb:face:0:96:1"),
      MacAddress("01:01:01:01:01:02"),
      PortDescriptor(PortID(11)),
      InterfaceID(11),
      NeighborState::PENDING);

  serializeTest(table);
}

TEST(NeighborResponseEntry, serialize) {
  auto entry = std::make_unique<ArpResponseEntry>(
      IPAddressV4("192.168.0.1"),
      MacAddress("01:01:01:01:01:01"),
      InterfaceID(0));

  auto serialized = entry->toThrift();
  auto entryBack = std::make_shared<ArpResponseEntry>(serialized);

  EXPECT_TRUE(*entry == *entryBack);
}

TEST(NeighborResponseTableTest, modifyForVlan) {
  auto vlan = std::make_shared<Vlan>(VlanID(2001), std::string("vlan1"));
  verifyNeighborResponseTableHelper(vlan);
}

TEST(NeighborResponseTableTest, modifyForIntf) {
  auto intf = std::make_shared<Interface>(
      InterfaceID(1001),
      std::optional<VlanID>(std::nullopt),
      folly::StringPiece("1001"),
      folly::MacAddress{},
      9000,
      false,
      false,
      cfg::InterfaceType::VLAN);

  verifyNeighborResponseTableHelper(intf);
}
