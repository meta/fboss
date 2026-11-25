// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "fboss/agent/NeighborResourceAccountant.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

using NeighborResourceAccountantTest = ResourceAccountantTestBase;

TEST_F(NeighborResourceAccountantTest, checkNeighborResource) {
  auto asicNdpMax =
      asicTable_->getHwAsic(SwitchID(0))->getMaxNdpTableSize().value();
  auto asicArpMax =
      asicTable_->getHwAsic(SwitchID(0))->getMaxArpTableSize().value();
  // asicUnifiedNeighborMax is set to <  (asicNdpMax + asicArpMax)  and >
  // (asicNdpMax or asicArpMax)
  auto asicUnifiedNeighborMax = asicTable_->getHwAsic(SwitchID(0))
                                    ->getMaxUnifiedNeighborTableSize()
                                    .value();
  NeighborResourceAccountant accountant(asicTable_.get(), scopeResolver_.get());
  auto& ndpEntriesMap = accountant.ndpEntriesMap_;
  auto& arpEntriesMap = accountant.arpEntriesMap_;

  // Test with configured max neighbor entries
  FLAGS_max_ndp_entries = asicNdpMax + 100;
  FLAGS_max_arp_entries = asicArpMax + 100;
  FLAGS_enforce_resource_hw_limits = false;
  ndpEntriesMap.insert_or_assign(SwitchID(0), FLAGS_max_ndp_entries);
  arpEntriesMap.insert_or_assign(SwitchID(0), FLAGS_max_arp_entries);
  EXPECT_TRUE(accountant.checkNeighborResource());
  ndpEntriesMap.insert_or_assign(SwitchID(0), FLAGS_max_ndp_entries + 1);
  EXPECT_FALSE(accountant.checkNeighborResource());
  ndpEntriesMap.insert_or_assign(SwitchID(0), FLAGS_max_ndp_entries);
  arpEntriesMap.insert_or_assign(SwitchID(0), FLAGS_max_arp_entries + 1);
  EXPECT_FALSE(accountant.checkNeighborResource());

  // check with ASIC limits
  FLAGS_enforce_resource_hw_limits = true;
  ndpEntriesMap.insert_or_assign(SwitchID(0), asicNdpMax);
  arpEntriesMap.insert_or_assign(
      SwitchID(0), asicUnifiedNeighborMax - asicArpMax);
  EXPECT_TRUE(accountant.checkNeighborResource());
  arpEntriesMap.insert_or_assign(
      SwitchID(0), asicUnifiedNeighborMax - asicArpMax + 1);
  EXPECT_FALSE(accountant.checkNeighborResource());
  ndpEntriesMap.insert_or_assign(SwitchID(0), asicNdpMax + 1);
  arpEntriesMap.clear();
  EXPECT_FALSE(accountant.checkNeighborResource());
  arpEntriesMap.insert_or_assign(SwitchID(0), asicArpMax + 1);
  ndpEntriesMap.clear();
  EXPECT_FALSE(accountant.checkNeighborResource());
}

TEST_F(NeighborResourceAccountantTest, removeInterfaceWithNeighbors) {
  NeighborResourceAccountant accountant(asicTable_.get(), scopeResolver_.get());
  const InterfaceID intfId(1);
  const PortDescriptor port(PortID(1));
  const folly::MacAddress mac("02:00:00:00:00:01");

  auto oldState = testStateA();
  oldState->getInterfaces()
      ->getNode(intfId)
      ->getArpTable()
      ->modify(intfId, &oldState)
      ->addEntry(folly::IPAddressV4("10.0.0.2"), mac, port, intfId);
  oldState->getInterfaces()
      ->getNode(intfId)
      ->getNdpTable()
      ->modify(intfId, &oldState)
      ->addEntry(
          folly::IPAddressV6("2401:db00:2110:3001::2"), mac, port, intfId);
  oldState->publish();
  EXPECT_TRUE(accountant.isValidUpdate(
      StateDelta(std::make_shared<SwitchState>(), oldState)));
  EXPECT_EQ(accountant.arpEntriesMap_[SwitchID(0)], 1);
  EXPECT_EQ(accountant.ndpEntriesMap_[SwitchID(0)], 1);

  // Removing the interface and its VLAN implicitly deletes its neighbors
  auto newState = oldState->clone();
  newState->getInterfaces()->modify(&newState)->removeNode(intfId);
  newState->getVlans()->modify(&newState)->removeNode(VlanID(1));
  newState->publish();
  EXPECT_TRUE(accountant.isValidUpdate(StateDelta(oldState, newState)));
  EXPECT_EQ(accountant.arpEntriesMap_[SwitchID(0)], 0);
  EXPECT_EQ(accountant.ndpEntriesMap_[SwitchID(0)], 0);
}

} // namespace facebook::fboss
