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

#include "fboss/agent/L2Entry.h"
#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/hw/mock/MockPlatform.h" // NOLINT(misc-include-cleaner)
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/MacEntry.h"
#include "fboss/agent/state/MacTable.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>

using folly::MacAddress;

namespace facebook::fboss {

class MacTableUtilsTest : public ::testing::Test {
 public:
  VlanID kVlan() const {
    return VlanID(1);
  }

  PortID kPortID() const {
    return PortID(1);
  }

  folly::MacAddress kMacAddress() const {
    return MacAddress("01:02:03:04:05:06");
  }

  L2Entry kL2Entry() {
    return L2Entry(
        kMacAddress(),
        kVlan(),
        PortDescriptor(kPortID()),
        L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
  }

  cfg::AclLookupClass kClassID() {
    return cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2;
  }

  std::shared_ptr<SwitchState> verifyAddOrUpdateClassIDHelper(
      const std::shared_ptr<SwitchState>& state) {
    auto macEntry =
        std::make_shared<MacEntry>(kMacAddress(), PortDescriptor(kPortID()));
    auto newState = MacTableUtils::updateOrAddEntryWithClassID(
        state, kVlan(), macEntry, kClassID());

    auto newMacEntry = getMacEntry(newState);
    EXPECT_NE(newMacEntry, nullptr);
    auto classID = newMacEntry->getClassID();
    EXPECT_TRUE(classID.has_value());
    EXPECT_EQ(classID.value(), kClassID());

    return newState;
  }

  std::shared_ptr<SwitchState> verifyRemoveClassIDHelper(
      const std::shared_ptr<SwitchState>& state,
      bool macPresent) {
    auto macEntry =
        std::make_shared<MacEntry>(kMacAddress(), PortDescriptor(kPortID()));
    auto newState =
        MacTableUtils::removeClassIDForEntry(state, kVlan(), macEntry);

    auto newMacEntry = getMacEntry(newState);
    if (macPresent) {
      EXPECT_NE(newMacEntry, nullptr);
      EXPECT_FALSE(newMacEntry->getClassID().has_value());
    } else {
      EXPECT_EQ(newMacEntry, nullptr);
    }

    return newState;
  }

  std::shared_ptr<SwitchState> verifyAddMac(
      const std::shared_ptr<SwitchState>& state) {
    return verifyAddAndDeleteMacHelper(
        state, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
  }

  std::shared_ptr<SwitchState> verifyDeleteMac(
      const std::shared_ptr<SwitchState>& state) {
    return verifyAddAndDeleteMacHelper(
        state, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE);
  }

 private:
  std::shared_ptr<SwitchState> verifyAddAndDeleteMacHelper(
      const std::shared_ptr<SwitchState>& state,
      L2EntryUpdateType l2EntryUpdateType) {
    auto newState =
        MacTableUtils::updateMacTable(state, kL2Entry(), l2EntryUpdateType);

    auto vlan = newState->getVlans()->getNodeIf(kVlan()).get();
    auto* macTable = vlan->getMacTable().get();
    auto node = macTable->getMacIf(kL2Entry().getMac());

    if (l2EntryUpdateType == L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD) {
      EXPECT_NE(nullptr, node);
    } else {
      EXPECT_EQ(nullptr, node);
    }

    return newState;
  }

  std::shared_ptr<MacEntry> getMacEntry(
      const std::shared_ptr<SwitchState>& state) {
    auto vlan = state->getVlans()->getNodeIf(kVlan()).get();
    auto* macTable = vlan->getMacTable().get();
    return macTable->getMacIf(kMacAddress());
  }
};

/*
 * A learn event can name an AggregatePort that config has since removed: the
 * ASIC generates it before the aggregate is torn down and the sw agent applies
 * it afterwards. Programming it would leave a MacEntry pointing at a dead
 * aggregate that SaiFdbManager::addMac() cannot resolve. The
 * event must be dropped when the aggregate is absent from state, and applied
 * as usual when it is present.
 */
TEST_F(MacTableUtilsTest, VerifyUpdateMacTableSkipsAddOnMissingAggregatePort) {
  const AggregatePortID kMissingAggPort(55);
  auto state = testStateA();
  ASSERT_EQ(nullptr, state->getAggregatePorts()->getNodeIf(kMissingAggPort));

  L2Entry l2Entry(
      kMacAddress(),
      kVlan(),
      PortDescriptor(kMissingAggPort),
      L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
  auto newState = MacTableUtils::updateMacTable(
      state, l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);

  EXPECT_EQ(
      nullptr,
      newState->getVlans()->getNodeIf(kVlan())->getMacTable()->getMacIf(
          kMacAddress()));
}

TEST_F(MacTableUtilsTest, VerifyUpdateMacTableAddOnPresentAggregatePort) {
  const AggregatePortID kAggPort(55);
  auto platform = createMockPlatform();
  auto config = testConfigA();
  config.aggregatePorts()->resize(1);
  *config.aggregatePorts()[0].key() = static_cast<int16_t>(kAggPort);
  *config.aggregatePorts()[0].name() = "lag55";
  config.aggregatePorts()[0].memberPorts()->resize(1);
  *config.aggregatePorts()[0].memberPorts()[0].memberPortID() = kPortID();
  auto state = publishAndApplyConfig(testStateA(), &config, platform.get());
  ASSERT_NE(nullptr, state);
  ASSERT_NE(nullptr, state->getAggregatePorts()->getNodeIf(kAggPort));

  L2Entry l2Entry(
      kMacAddress(),
      kVlan(),
      PortDescriptor(kAggPort),
      L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);
  auto newState = MacTableUtils::updateMacTable(
      state, l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);

  auto entry =
      newState->getVlans()->getNodeIf(kVlan())->getMacTable()->getMacIf(
          kMacAddress());
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(entry->getPort(), PortDescriptor(kAggPort));
}

/*
 * A neighbor resolved on an AggregatePort outlives the aggregate until
 * NeighborUpdater flushes it. Mirroring it into a static MacEntry in that
 * window would hand the hw agent a MAC on a LAG it has already removed.
 */
class MacTableUtilsStaticNbrTest : public MacTableUtilsTest {
 public:
  std::shared_ptr<SwitchState> stateWithNeighborOn(const PortDescriptor& port) {
    auto state = testStateA()->clone();
    const InterfaceID intfId(1); // testStateA(): VLAN 1 -> interface 1
    auto* ndpTable =
        state->getInterfaces()->getNode(intfId)->getNdpTable()->modify(
            intfId, &state);
    ndpTable->addEntry(
        folly::IPAddressV6("2401:db00:2110:3001::2"),
        kMacAddress(),
        port,
        intfId);
    state->publish();
    return state;
  }

  std::shared_ptr<MacEntry> macEntryIn(
      const std::shared_ptr<SwitchState>& state) {
    return state->getVlans()->getNodeIf(kVlan())->getMacTable()->getMacIf(
        kMacAddress());
  }
};

TEST_F(MacTableUtilsStaticNbrTest, StaticEntryIfNbrExistsOnPhysicalPort) {
  auto newState = MacTableUtils::updateOrAddStaticEntryIfNbrExists(
      stateWithNeighborOn(PortDescriptor(kPortID())), kVlan(), kMacAddress());
  auto entry = macEntryIn(newState);
  ASSERT_NE(nullptr, entry);
  EXPECT_EQ(entry->getPort(), PortDescriptor(kPortID()));
  EXPECT_EQ(entry->getType(), MacEntryType::STATIC_ENTRY);
}

TEST_F(MacTableUtilsStaticNbrTest, StaticEntryIfNbrExistsSkipsMissingAggPort) {
  const AggregatePortID kMissingAggPort(55);
  auto state = stateWithNeighborOn(PortDescriptor(kMissingAggPort));
  ASSERT_EQ(nullptr, state->getAggregatePorts()->getNodeIf(kMissingAggPort));

  auto newState = MacTableUtils::updateOrAddStaticEntryIfNbrExists(
      state, kVlan(), kMacAddress());
  EXPECT_EQ(nullptr, macEntryIn(newState));
}

TEST_F(MacTableUtilsTest, VerifyStaticEntrySkipsMissingAggregatePort) {
  auto newState = MacTableUtils::updateOrAddStaticEntry(
      testStateA(),
      PortDescriptor(AggregatePortID(55)),
      kVlan(),
      kMacAddress());
  EXPECT_EQ(
      nullptr,
      newState->getVlans()->getNodeIf(kVlan())->getMacTable()->getMacIf(
          kMacAddress()));
}

TEST_F(MacTableUtilsTest, VerifyAddWithClassIDSkipsMissingAggregatePort) {
  auto macEntry = std::make_shared<MacEntry>(
      kMacAddress(), PortDescriptor(AggregatePortID(55)));
  auto newState = MacTableUtils::updateOrAddEntryWithClassID(
      testStateA(), kVlan(), macEntry, kClassID());
  EXPECT_EQ(
      nullptr,
      newState->getVlans()->getNodeIf(kVlan())->getMacTable()->getMacIf(
          kMacAddress()));
}

TEST_F(MacTableUtilsTest, VerifyUpdateMacTableAddMac) {
  verifyAddMac(testStateA());
}

TEST_F(MacTableUtilsTest, VerifyUpdateMacTableAddMacTwice) {
  auto state = verifyAddMac(testStateA());
  verifyAddMac(state);
}

TEST_F(MacTableUtilsTest, VerifyUpdateMacTableDeleteMac) {
  auto state = verifyAddMac(testStateA());
  verifyDeleteMac(state);
}

TEST_F(MacTableUtilsTest, VerifyUpdateMacTableDeleteMacTwice) {
  auto state1 = verifyAddMac(testStateA());
  auto state2 = verifyDeleteMac(state1);
  verifyDeleteMac(state2);
}

TEST_F(MacTableUtilsTest, VerifyUpdateWithClassID) {
  auto state = verifyAddMac(testStateA());
  verifyAddOrUpdateClassIDHelper(state);
}

TEST_F(MacTableUtilsTest, VerifyAddWithClassID) {
  auto state = testStateA();
  verifyAddOrUpdateClassIDHelper(state);
}

TEST_F(MacTableUtilsTest, VerifyRemoveClassID) {
  auto state1 = testStateA();
  auto state2 = verifyAddOrUpdateClassIDHelper(state1);
  verifyRemoveClassIDHelper(state2, true /* macPresent */);
}

TEST_F(MacTableUtilsTest, VerifyRemoveClassIDForAbsentEntry) {
  auto state = testStateA();
  verifyRemoveClassIDHelper(state, false /* macPresent */);
}

} // namespace facebook::fboss
