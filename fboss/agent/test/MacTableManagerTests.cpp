/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/format.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/ResourceAccountant.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/MacEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/MacAddress.h>

namespace facebook::fboss {

class MacTableManagerTest : public ::testing::Test {
 public:
  using Func = folly::Function<void()>;
  using StateUpdateFn = SwSwitch::StateUpdateFn;

  void SetUp() override {
    handle_ = createTestHandle(testStateA());
    sw_ = handle_->getSw();
    FLAGS_enable_hw_update_protection = true;
  }

  void verifyStateUpdate(Func func) {
    runInUpdateEventBaseAndWait(std::move(func));
  }

  void TearDown() override {
    schedulePendingTestStateUpdates();
  }

  VlanID kVlan() const {
    return VlanID(1);
  }

  PortID kPortID() const {
    return PortID(1);
  }

  folly::MacAddress kMacAddress() const {
    return MacAddress("01:02:03:04:05:06");
  }

  void triggerMacLearnedCb(bool wait = true) {
    triggerMacCbHelper(
        facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD, wait);
  }

  void triggerMacAgedCb(bool wait = true) {
    triggerMacCbHelper(
        facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE, wait);
  }

  void triggerMacBulkLearnedCb(
      const std::vector<folly::MacAddress>& macs,
      bool wait = true) {
    triggerMacBulkCbHelper(
        macs,
        facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD,
        wait);
  }

  void triggerMacBulkAgedCb(
      const std::vector<folly::MacAddress>& macs,
      bool wait = true) {
    triggerMacBulkCbHelper(
        macs,
        facebook::fboss::L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE,
        wait);
  }

  void verifyMacIsAdded() {
    verifyStateUpdate([=, this]() {
      auto vlan = sw_->getState()->getVlans()->getNode(kVlan());
      auto* macTable = vlan->getMacTable().get();
      auto node = macTable->getMacIf(kMacAddress());

      EXPECT_NE(nullptr, node);
      EXPECT_EQ(kMacAddress(), node->getMac());
      EXPECT_EQ(kPortID(), node->getPort().phyPortID());
    });
  }

  // verify mac entris in switchState is in sync with resourceAccountant
  // l2Entries_
  void verifyMacEntryCount(int l2ResourceAccountantEntries) {
    verifyStateUpdate([=, this]() {
      auto vlan = sw_->getState()->getVlans()->getNode(kVlan());
      auto* macTable = vlan->getMacTable().get();
      XLOG(DBG2) << "macTable size: " << macTable->size()
                 << ", l2ResourceAccountantEntries: "
                 << l2ResourceAccountantEntries
                 << ", getMacTableUpdateFailure: "
                 << sw_->stats()->getMacTableUpdateFailure();

      EXPECT_EQ(l2ResourceAccountantEntries, macTable->size());
    });
  }

  void verifyMacIsDeleted() {
    verifyStateUpdate([=, this]() {
      auto vlan = sw_->getState()->getVlans()->getNode(kVlan());
      auto* macTable = vlan->getMacTable().get();
      auto node = macTable->getMacIf(kMacAddress());

      EXPECT_EQ(nullptr, node);
    });
  }

  SwSwitch* getSw() {
    return sw_;
  }

 private:
  void runInUpdateEventBaseAndWait(Func func) {
    auto* evb = sw_->getUpdateEvb();
    evb->runInFbossEventBaseThreadAndWait(std::move(func));
  }

  void schedulePendingTestStateUpdates() {
    runInUpdateEventBaseAndWait([]() {});
  }

  void triggerMacCbHelper(
      L2EntryUpdateType l2EntryUpdateType,
      bool wait = true) {
    auto l2Entry = L2Entry(
        kMacAddress(),
        kVlan(),
        PortDescriptor(kPortID()),
        L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING);

    sw_->l2LearningUpdateReceived(l2Entry, l2EntryUpdateType);

    if (wait) {
      waitForBackgroundThread(sw_);
      waitForStateUpdates(sw_);
    }
  }

  void triggerMacBulkCbHelper(
      const std::vector<folly::MacAddress>& macs,
      L2EntryUpdateType l2EntryUpdateType,
      bool wait = true) {
    for (const auto& mac : macs) {
      auto l2Entry = L2Entry(
          mac,
          kVlan(),
          PortDescriptor(kPortID()),
          L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING);
      sw_->l2LearningUpdateReceived(l2Entry, l2EntryUpdateType);
    }
    if (wait) {
      waitForBackgroundThread(sw_);
      waitForStateUpdates(sw_);
    }
  }

  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_;
};

TEST_F(MacTableManagerTest, MacLearnedCb) {
  triggerMacLearnedCb();

  verifyMacIsAdded();
}

TEST_F(MacTableManagerTest, SameMacLearnedCbTwice) {
  triggerMacLearnedCb();
  triggerMacLearnedCb();

  verifyMacIsAdded();
}

TEST_F(MacTableManagerTest, MacAgedLearnedCb) {
  WaitForMacEntryAddedOrDeleted macAdded1(
      getSw(), kMacAddress(), kVlan(), true);
  triggerMacLearnedCb(true);
  EXPECT_TRUE(macAdded1.wait());

  // Mac age and learn callback should cause
  // entry remove and add in switch state,
  // these state updates should not be coalesced.
  // It is because SW mac learning always need
  // to re-program learnt L2 entry so as to move
  // it out of pending state.
  WaitForMacEntryAddedOrDeleted macDeleted(
      getSw(), kMacAddress(), kVlan(), false);
  WaitForMacEntryAddedOrDeleted macAdded2(
      getSw(), kMacAddress(), kVlan(), true);
  triggerMacAgedCb(false);
  triggerMacLearnedCb(true);
  EXPECT_TRUE(macDeleted.wait());
  EXPECT_TRUE(macAdded2.wait());
}

TEST_F(MacTableManagerTest, MacAgedCb) {
  triggerMacLearnedCb();
  triggerMacAgedCb();

  verifyMacIsDeleted();
}

TEST_F(MacTableManagerTest, SameMacAgedCbTwice) {
  triggerMacLearnedCb();
  triggerMacAgedCb();
  triggerMacAgedCb();

  verifyMacIsDeleted();
}
// This test is to verify resourceAccountant is updated correctly
TEST_F(MacTableManagerTest, MacLearnedBulkCb) {
  int16_t numMac = 100;
  int16_t deleted = 0;
  int16_t batchSize = 10;
  std::vector<folly::MacAddress> macs;
  for (int16_t i = 0; i < numMac; i++) {
    macs.push_back(MacAddress::fromHBO(i));
  }

  // learn 10 mac entries
  triggerMacBulkLearnedCb(
      std::vector<folly::MacAddress>(macs.begin(), macs.begin() + batchSize));
  // expect 10 mac entries in switchState and resourceAccountant
  verifyMacEntryCount(getSw()->getResourceAccountant()->l2Entries_);

  // age 10 mac entries
  triggerMacBulkAgedCb(
      std::vector<folly::MacAddress>(macs.begin(), macs.begin() + batchSize));
  deleted = batchSize;
  // expect 0 mac entries in switchState and resourceAccountant
  verifyMacEntryCount(getSw()->getResourceAccountant()->l2Entries_);

  // learn 90 mac entries
  triggerMacBulkLearnedCb(
      std::vector<folly::MacAddress>(macs.begin() + batchSize, macs.end()));
  // expect 90 mac entries in switchState and resourceAccountant
  verifyMacEntryCount(getSw()->getResourceAccountant()->l2Entries_);

  // age 20 mac entries
  batchSize = 20;
  triggerMacBulkAgedCb(
      std::vector<folly::MacAddress>(
          macs.begin() + deleted, macs.begin() + deleted + batchSize));
  deleted += batchSize;
  // expect 70 mac entries in switchState and resourceAccountant
  verifyMacEntryCount(getSw()->getResourceAccountant()->l2Entries_);
}

/*
 * Test ApplyThriftConfig functionality for static MAC entries
 */
class StaticMacConfigTest : public MacTableManagerTest {
 protected:
  void SetUp() override {
    MacTableManagerTest::SetUp();

    // Create a basic config with VLANs and ports
    cfg_ = testConfigA();
  }

  void applyConfig(const cfg::SwitchConfig& config) {
    auto platform = createMockPlatform();
    auto newState =
        publishAndApplyConfig(getSw()->getState(), &config, platform.get());
    getSw()->updateStateBlocking(
        "test config update",
        std::function<std::shared_ptr<SwitchState>(
            const std::shared_ptr<SwitchState>&)>(
            [newState](const std::shared_ptr<SwitchState>&)
                -> std::shared_ptr<SwitchState> { return newState; }));

    // Wait for state updates to complete
    waitForStateUpdates(getSw());
  }

  void addStaticMacToConfig(
      const folly::MacAddress& mac,
      VlanID vlanId,
      PortID portId) {
    cfg::StaticMacEntry staticMac;
    staticMac.macAddress() = mac.toString();
    staticMac.vlanID() = static_cast<int32_t>(vlanId);
    staticMac.egressLogicalPortID() = static_cast<int32_t>(portId);

    if (!cfg_.staticMacAddrs().has_value()) {
      cfg_.staticMacAddrs() = std::vector<cfg::StaticMacEntry>();
    }
    cfg_.staticMacAddrs()->push_back(staticMac);
  }

  void addStaticMacs(int first, int last) {
    for (int i = first; i <= last; ++i) {
      addStaticMacToConfig(
          MacAddress(fmt::format("02:00:00:00:00:{:02x}", i)),
          kVlan(),
          kPortID());
    }
  }

  void removeStaticMacFromConfig(
      const folly::MacAddress& mac,
      VlanID vlanId,
      PortID portId) {
    if (!cfg_.staticMacAddrs().has_value()) {
      return;
    }

    auto& staticMacs = cfg_.staticMacAddrs().value();
    staticMacs.erase(
        std::remove_if(
            staticMacs.begin(),
            staticMacs.end(),
            [&](const cfg::StaticMacEntry& entry) {
              return folly::MacAddress(entry.macAddress().value()) == mac &&
                  VlanID(entry.vlanID().value()) == vlanId &&
                  PortID(entry.egressLogicalPortID().value()) == portId;
            }),
        staticMacs.end());
  }

  void verifyStaticMacEntry(
      const folly::MacAddress& mac,
      VlanID vlanId,
      PortID portId,
      bool shouldExist,
      bool shouldBeConfigured = true) {
    auto state = getSw()->getState();
    auto vlan = state->getVlans()->getNodeIf(vlanId);
    EXPECT_TRUE(vlan) << "VLAN " << vlanId << " not found";

    auto macTable = vlan->getMacTable();
    auto macEntry = macTable->getMacIf(mac);

    if (!shouldExist) {
      // When shouldExist is false, verify the MAC entry either:
      // 1. Doesn't exist at all in this VLAN, OR
      // 2. Exists but is NOT associated with the specified port
      if (macEntry) {
        EXPECT_NE(macEntry->getPort(), PortDescriptor(portId))
            << "MAC entry " << mac.toString() << " should not exist on port "
            << portId << " in VLAN " << vlanId
            << ", but found: " << macEntry->str();
      }
      // If macEntry is null, that's also acceptable - MAC doesn't exist at all
      return;
    }

    ASSERT_TRUE(macEntry) << "MAC entry " << mac.toString()
                          << " not found in VLAN " << vlanId;
    EXPECT_EQ(macEntry->getPort(), PortDescriptor(portId))
        << "MAC entry port mismatch";
    EXPECT_EQ(macEntry->getType(), MacEntryType::STATIC_ENTRY)
        << "MAC entry should be static";

    if (shouldBeConfigured) {
      EXPECT_TRUE(macEntry->getConfigured().has_value())
          << "MAC entry should have configured field set";
      EXPECT_TRUE(macEntry->getConfigured().value())
          << "MAC entry should be marked as configured";
    } else {
      EXPECT_FALSE(
          macEntry->getConfigured().has_value() &&
          macEntry->getConfigured().value())
          << "MAC entry should not be marked as configured";
    }
  }

  inline HwSwitchMatcher hwMatcher() {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  }
  cfg::SwitchConfig cfg_;
};

TEST_F(StaticMacConfigTest, AddMultipleStaticMacs) {
  auto mac1 = folly::MacAddress("00:11:22:33:44:55");
  auto mac2 = folly::MacAddress("00:11:22:33:44:66");
  auto vlanId = VlanID(1);
  auto portId1 = PortID(1);
  auto portId2 = PortID(2);

  // Add multiple static MACs to config
  addStaticMacToConfig(mac1, vlanId, portId1);
  addStaticMacToConfig(mac2, vlanId, portId2);
  applyConfig(cfg_);

  // Verify both MAC entries were added
  verifyStaticMacEntry(mac1, vlanId, portId1, true, true);
  verifyStaticMacEntry(mac2, vlanId, portId2, true, true);
}

TEST_F(StaticMacConfigTest, RemoveStaticMacs) {
  auto mac1 = folly::MacAddress("00:11:22:33:44:55");
  auto mac2 = folly::MacAddress("00:11:22:33:44:66");
  auto vlanId = VlanID(1);
  auto portId1 = PortID(1);
  auto portId2 = PortID(2);

  // Add multiple static MACs
  addStaticMacToConfig(mac1, vlanId, portId1);
  addStaticMacToConfig(mac2, vlanId, portId2);
  applyConfig(cfg_);

  // Verify both exist
  verifyStaticMacEntry(mac1, vlanId, portId1, true, true);
  verifyStaticMacEntry(mac2, vlanId, portId2, true, true);

  // Remove only one MAC
  removeStaticMacFromConfig(mac1, vlanId, portId1);
  applyConfig(cfg_);

  // Verify only mac1 was removed, mac2 still exists
  verifyStaticMacEntry(mac1, vlanId, portId1, false);
  verifyStaticMacEntry(mac2, vlanId, portId2, true, true);
}

TEST_F(StaticMacConfigTest, UpdateStaticMacPort) {
  auto mac = folly::MacAddress("00:11:22:33:44:55");
  auto vlanId = VlanID(1);
  auto oldPortId = PortID(1);
  auto newPortId = PortID(2);

  // Add static MAC with old port
  addStaticMacToConfig(mac, vlanId, oldPortId);
  applyConfig(cfg_);
  verifyStaticMacEntry(mac, vlanId, oldPortId, true, true);

  // Update to new port (remove old, add new)
  removeStaticMacFromConfig(mac, vlanId, oldPortId);
  addStaticMacToConfig(mac, vlanId, newPortId);
  applyConfig(cfg_);

  // Verify MAC now points to new port
  verifyStaticMacEntry(mac, vlanId, oldPortId, false);
  verifyStaticMacEntry(mac, vlanId, newPortId, true, true);
}

TEST_F(StaticMacConfigTest, StaticMacsAcrossVlans) {
  auto mac1 = folly::MacAddress("00:11:22:33:44:55");
  auto mac2 = folly::MacAddress("00:11:22:33:44:66");
  auto vlan1 = VlanID(1);
  auto vlan55 = VlanID(55);
  auto portId = PortID(1);

  // Add static MACs to different VLANs
  addStaticMacToConfig(mac1, vlan1, portId);
  addStaticMacToConfig(mac2, vlan55, portId);
  applyConfig(cfg_);

  // Verify both MAC entries exist in their respective VLANs
  verifyStaticMacEntry(mac1, vlan1, portId, true, true);
  verifyStaticMacEntry(mac2, vlan55, portId, true, true);

  // Remove MAC from vlan1 only
  removeStaticMacFromConfig(mac1, vlan1, portId);
  applyConfig(cfg_);

  // Verify only vlan1 MAC was removed
  verifyStaticMacEntry(mac1, vlan1, portId, false);
  verifyStaticMacEntry(mac2, vlan55, portId, true, true);
}

TEST_F(StaticMacConfigTest, EmptyStaticMacConfig) {
  auto mac = folly::MacAddress("00:11:22:33:44:55");
  auto vlanId = VlanID(1);
  auto portId = PortID(1);

  // Add static MAC first
  addStaticMacToConfig(mac, vlanId, portId);
  applyConfig(cfg_);
  verifyStaticMacEntry(mac, vlanId, portId, true, true);

  // Clear all static MACs from config
  cfg_.staticMacAddrs().reset();
  applyConfig(cfg_);

  // Verify all configured MAC entries were removed
  verifyStaticMacEntry(mac, vlanId, portId, false);
}

TEST_F(StaticMacConfigTest, InvalidVlanOrPortStaticMac) {
  auto mac1 = folly::MacAddress("00:11:22:33:44:55");
  auto mac2 = folly::MacAddress("00:11:22:33:44:66");

  auto invalidVlanId = VlanID(999); // Non-existent VLAN
  auto portId = PortID(1);
  addStaticMacToConfig(mac1, invalidVlanId, portId);
  EXPECT_THROW(applyConfig(cfg_), FbossError);
  cfg_.staticMacAddrs()->clear(); // Reset config

  auto vlanId = VlanID(1);
  auto invalidPortId = PortID(999); // Non-existent port
  addStaticMacToConfig(mac2, vlanId, invalidPortId);
  EXPECT_THROW(applyConfig(cfg_), FbossError);
  cfg_.staticMacAddrs()->clear(); // Reset config
}

TEST_F(StaticMacConfigTest, ApplyConfigAccountsStaticMacsOnce) {
  gflags::FlagSaver flagSaver;
  auto l2Entries = [&] { return getSw()->getResourceAccountant()->l2Entries_; };
  getSw()->applyConfig("base config", cfg_);
  const auto base = l2Entries();
  FLAGS_max_l2_entries = base + 4;

  // 1) Each added static MAC is accounted once
  addStaticMacs(1, 2);
  getSw()->applyConfig("2 static macs", cfg_);
  EXPECT_EQ(l2Entries(), base + 2);

  // 2) A rejected config leaves the accounting untouched
  addStaticMacs(3, 5);
  EXPECT_THROW(getSw()->applyConfig("5 static macs", cfg_), FbossError);
  EXPECT_EQ(l2Entries(), base + 2);

  // 3) A config at the limit is accepted
  cfg_.staticMacAddrs()->pop_back();
  getSw()->applyConfig("4 static macs", cfg_);
  EXPECT_EQ(l2Entries(), base + 4);

  // 4) Reapplying the same config changes nothing
  getSw()->applyConfig("4 static macs again", cfg_);
  EXPECT_EQ(l2Entries(), base + 4);

  // 5) Each removed static MAC is accounted once
  cfg_.staticMacAddrs()->resize(2);
  getSw()->applyConfig("2 static macs again", cfg_);
  EXPECT_EQ(l2Entries(), base + 2);
}

TEST_F(StaticMacConfigTest, IsValidStateUpdateDryRun) {
  gflags::FlagSaver flagSaver;
  auto l2Entries = [&] { return getSw()->getResourceAccountant()->l2Entries_; };
  getSw()->applyConfig("base config", cfg_);
  const auto base = l2Entries();
  FLAGS_max_l2_entries = base + 2;
  const auto oldState = getSw()->getState();
  auto platform = createMockPlatform();
  auto deltaToConfig = [&]() {
    return StateDelta(
        oldState, publishAndApplyConfig(oldState, &cfg_, platform.get()));
  };
  addStaticMacs(1, 2);
  const auto withinLimit = deltaToConfig();
  addStaticMacs(3, 3);
  const auto overLimit = deltaToConfig();

  // 1) A passing dry run leaves the accounting untouched, however often run
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(
        getSw()->isValidStateUpdateDryRun(withinLimit, getSw()->stats()));
  }
  EXPECT_EQ(l2Entries(), base);

  // 2) A failing dry run leaves the accounting untouched
  EXPECT_FALSE(getSw()->isValidStateUpdateDryRun(overLimit, getSw()->stats()));
  EXPECT_EQ(l2Entries(), base);

  // 3) The committing check records the same delta
  EXPECT_TRUE(getSw()->isValidStateUpdate(withinLimit, getSw()->stats()));
  EXPECT_EQ(l2Entries(), base + 2);
}
} // namespace facebook::fboss
