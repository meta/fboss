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
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <netinet/icmp6.h>

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace facebook::fboss {

template <typename AddrType>
struct IpAddrT {
  using AddrT = AddrType;
};

using TestTypes =
    ::testing::Types<IpAddrT<folly::IPAddressV4>, IpAddrT<folly::IPAddressV6>>;

template <typename IpAddrT>
class StaticL2ForNeighorObserverTest : public ::testing::Test {
 public:
  using Func = folly::Function<void()>;
  using StateUpdateFn = SwSwitch::StateUpdateFn;
  using AddrT = typename IpAddrT::AddrT;

  void SetUp() override {
    handle_ = createTestHandle(testStateAWithPortsUp());
    sw_ = handle_->getSw();
  }

  void TearDown() override {
    schedulePendingTestStateUpdates();
  }

  void updateState(folly::StringPiece name, StateUpdateFn func) {
    sw_->updateStateBlocking(name, func);
  }

  VlanID kVlan() const {
    return VlanID(1);
  }

  InterfaceID kInterfaceID() const {
    return InterfaceID(1);
  }

  PortID kPortID() const {
    return PortID(1);
  }

  PortID kPortID2() const {
    return PortID(2);
  }

  IPAddress kIp4Addr() const {
    return IPAddressV4("10.0.0.2");
  }
  IPAddress kIp4Addr2() const {
    return IPAddressV4("10.0.0.3");
  }

  IPAddress kIp6Addr() const {
    return IPAddressV6("2401:db00:2110:3001::0002");
  }
  IPAddress kIp6Addr2() const {
    return IPAddressV6("2401:db00:2110:3001::0003");
  }
  MacAddress kMacAddress() const {
    return MacAddress("01:02:03:04:05:06");
  }

  void resolveNeighbor(
      IPAddress ipAddress,
      MacAddress macAddress,
      std::optional<PortID> port = std::nullopt) {
    /*
     * Cause a neighbor entry to resolve by receiving appropriate ARP/NDP, and
     * assert if valid CLASSID is associated with the newly resolved neighbor.
     */
    if (ipAddress.isV4()) {
      sw_->getNeighborUpdater()->receivedArpMineForIntf(
          kInterfaceID(),
          ipAddress.asV4(),
          macAddress,
          PortDescriptor(port ? port.value() : this->kPortID()),
          ArpOpCode::ARP_OP_REPLY);

    } else {
      sw_->getNeighborUpdater()->receivedNdpMineForIntf(
          kInterfaceID(),
          ipAddress.asV6(),
          macAddress,
          PortDescriptor(port ? port.value() : this->kPortID()),
          ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
          ND_NA_FLAG_OVERRIDE | ND_NA_FLAG_SOLICITED);
    }

    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForStateUpdates(sw_);
  }

  void unresolveNeighbor(IPAddress ipAddress) {
    sw_->getNeighborUpdater()->flushEntryForIntf(kInterfaceID(), ipAddress);

    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
  }

  void resolve(
      IPAddress ipAddress,
      MacAddress macAddress,
      std::optional<PortID> port = std::nullopt) {
    if constexpr (std::is_same_v<AddrT, folly::MacAddress>) {
      resolveMac(macAddress, port);
    } else {
      this->resolveNeighbor(ipAddress, macAddress, port);
    }
  }

  void resolveMac(
      MacAddress macAddress,
      std::optional<PortID> port = std::nullopt) {
    auto l2Entry = L2Entry(
        macAddress,
        this->kVlan(),
        PortDescriptor(port ? port.value() : this->kPortID()),
        L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING);

    this->sw_->l2LearningUpdateReceived(
        l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);

    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  void unresolveMac(MacAddress macAddress) {
    auto l2Entry = L2Entry(
        macAddress,
        this->kVlan(),
        PortDescriptor(this->kPortID()),
        L2Entry::L2EntryType::L2_ENTRY_TYPE_VALIDATED);

    this->sw_->l2LearningUpdateReceived(
        l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_DELETE);

    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  IPAddress getIpAddress() {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4Addr());
    } else {
      ipAddress = IPAddress(this->kIp6Addr());
    }

    return ipAddress;
  }
  IPAddress getIpAddress2() {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4Addr2());
    } else {
      ipAddress = IPAddress(this->kIp6Addr2());
    }

    return ipAddress;
  }

  void bringPortDown(PortID portID) {
    this->sw_->linkStateChanged(portID, false, cfg::PortType::INTERFACE_PORT);

    waitForStateUpdates(this->sw_);
    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  // Hold the update thread, so the second update runs on the first one's state.
  void queueAfterRemoval(StateUpdateFn removeFn, Func queueFn) {
    runInUpdateEventBaseAndWait([&]() {
      sw_->updateState("remove from state", std::move(removeFn));
      queueFn();
    });
    waitForStateUpdates(sw_);
  }

  void queueEnsureStaticMac() {
    sw_->updateState(
        "ensure static MAC for nbr",
        [vlan = kVlan(),
         mac = kMacAddress()](const std::shared_ptr<SwitchState>& state) {
          return MacTableUtils::updateOrAddStaticEntryIfNbrExists(
              state, vlan, mac);
        });
  }

  size_t neighborCacheSize() {
    auto* updater = sw_->getNeighborUpdater();
    return updater->getArpCacheDataForIntf().get().size() +
        updater->getNdpCacheDataForIntf().get().size();
  }

  // The cache must have the entry, or the flush queues no update.
  void queueNeighborFlush() {
    EXPECT_EQ(neighborCacheSize(), 1);
    sw_->getNeighborUpdater()->portFlushEntries(PortDescriptor(kPortID()));
    sw_->getNeighborUpdater()->waitForPendingUpdates();
    EXPECT_EQ(neighborCacheSize(), 0);
  }

  void queueClassIDUpdate() {
    EXPECT_EQ(neighborCacheSize(), 1);
    auto ip = getIpAddress();
    auto classID = cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0;
    if (ip.isV4()) {
      sw_->getNeighborUpdater()->updateEntryClassID(
          kVlan(), ip.asV4(), classID);
    } else {
      sw_->getNeighborUpdater()->updateEntryClassID(
          kVlan(), ip.asV6(), classID);
    }
    sw_->getNeighborUpdater()->waitForPendingUpdates();
  }

  std::shared_ptr<SwitchState> removeInterface(
      const std::shared_ptr<SwitchState>& state,
      bool removeVlan) {
    auto newState = state->clone();
    newState->getInterfaces()->modify(&newState)->removeNode(kInterfaceID());
    auto vlans = newState->getVlans()->modify(&newState);
    if (removeVlan) {
      vlans->removeNode(kVlan());
    } else {
      vlans->getNode(kVlan())->modify(&newState)->setInterfaceID(
          InterfaceID(0));
    }
    return newState;
  }

  bool interfaceExists() const {
    return sw_->getState()->getInterfaces()->getNodeIf(kInterfaceID()) !=
        nullptr;
  }

 protected:
  void verifyMacEntryExists(
      MacEntryType type,
      std::optional<PortID> port = std::nullopt) const {
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
    auto macEntry = getMacEntry();
    ASSERT_NE(macEntry, nullptr);
    EXPECT_EQ(macEntry->getType(), type);
    if (port) {
      EXPECT_EQ(macEntry->getPort(), port);
    }
  }
  void verifyMacEntryDoesNotExist() const {
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
    EXPECT_EQ(getMacEntry(), nullptr);
  }

 private:
  auto getMacEntry() const {
    auto vlan = sw_->getState()->getVlans()->getNode(kVlan());
    return vlan->getMacTable()->getMacIf(kMacAddress());
  }
  void runInUpdateEventBaseAndWait(Func func) {
    auto* evb = sw_->getUpdateEvb();
    evb->runInFbossEventBaseThreadAndWait(std::move(func));
  }

  void runInUpdateEvbAndWaitAfterNeighborCachePropagation(Func func) {
    schedulePendingTestStateUpdates();
    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    runInUpdateEventBaseAndWait(std::move(func));
  }

  void schedulePendingTestStateUpdates() {
    runInUpdateEventBaseAndWait([]() {});
  }

  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_;
};

TYPED_TEST_SUITE(StaticL2ForNeighorObserverTest, TestTypes);

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    noStaticL2EntriesForUnResolvedNeighbor) {
  this->verifyMacEntryDoesNotExist();
}

TYPED_TEST(StaticL2ForNeighorObserverTest, staticL2EntriesForResolvedNeighbor) {
  this->verifyMacEntryDoesNotExist();
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    staticL2EntriesForUnresolvedToResolvedNeighbor) {
  this->verifyMacEntryDoesNotExist();
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->unresolveNeighbor(this->getIpAddress());
  this->verifyMacEntryDoesNotExist();
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(StaticL2ForNeighorObserverTest, dynamicMacEntryIfNoNeighbor) {
  this->resolveMac(this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::DYNAMIC_ENTRY);
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    dynamicToStaticL2MacEntryOnNeighborResolution) {
  this->resolveMac(this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::DYNAMIC_ENTRY);
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    staticL2MacEntryForMultipleNeighbors) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolve(this->getIpAddress2(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  // Unresolve one neighbor Static mac entry should continue to
  // exist
  this->unresolveNeighbor(this->getIpAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  // Unresolve second neighbor, now static MAC should go away
  this->unresolveNeighbor(this->getIpAddress2());
  this->verifyMacEntryDoesNotExist();
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    staticL2MacEntryForMultipleAddrFamilyNeighbors) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  auto secondAddr =
      this->getIpAddress().isV6() ? this->kIp4Addr() : this->kIp6Addr();
  this->resolve(secondAddr, this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  // Unresolve one neighbor Static mac entry should continue to
  // exist
  this->unresolveNeighbor(this->getIpAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  // Unresolve second neighbor, now static MAC should go away
  this->unresolveNeighbor(secondAddr);
  this->verifyMacEntryDoesNotExist();
}

TYPED_TEST(StaticL2ForNeighorObserverTest, bringPortDownSingleNeighbor) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->bringPortDown(this->kPortID());
  // Neighbor would get unreolved as a result of port down
  // taking down MAC entryy with it
  this->verifyMacEntryDoesNotExist();
}

TYPED_TEST(StaticL2ForNeighorObserverTest, bringPortDownMultipleNeighbors) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  auto secondAddr =
      this->getIpAddress().isV6() ? this->kIp4Addr() : this->kIp6Addr();
  this->resolve(secondAddr, this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->bringPortDown(this->kPortID());
  // All neighbor would get unreolved as a result of port down
  // taking down MAC entryy with it
  this->verifyMacEntryDoesNotExist();
}

TYPED_TEST(StaticL2ForNeighorObserverTest, learnMacPostNeighborResolution) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveMac(this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(StaticL2ForNeighorObserverTest, unrelatedPortDown) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->bringPortDown(this->kPortID2());
  // MAC entryy is unaffected, since the port down above did
  // not take away any neighbors
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(StaticL2ForNeighorObserverTest, ageMacWhileNeighborResolved) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveMac(this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->unresolveMac(this->kMacAddress());
  // MAC entry still exists due to the fact that a neighbor still
  // refers to it.
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(StaticL2ForNeighorObserverTest, macMovePostNeighborResolution) {
  // Since we create static entries for resolved neighbor, from there on
  // the mac entry is owned by the neighbor. So in the event MAC moves
  // to a different port make sure that we still defer to the neighbor
  // entry's port information. In practice, when a MAC moves a neighbor
  // resolution over the new port will follow soon and get things
  // to converge
  this->resolveMac(this->kMacAddress());
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->resolveMac(this->kMacAddress(), this->kPortID2());
  // Neighbor entry port ID trumps MAC learning event
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY, this->kPortID());
  // Neighbor entry move should update mac entry pointing to new port
  this->resolve(this->getIpAddress(), this->kMacAddress(), this->kPortID2());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY, this->kPortID2());
}

TYPED_TEST(StaticL2ForNeighorObserverTest, queuedStaticMacAfterVlanRemoved) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->queueAfterRemoval(
      [this](const auto& state) { return this->removeInterface(state, true); },
      [this]() { this->queueEnsureStaticMac(); });
  EXPECT_FALSE(this->interfaceExists());
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    queuedStaticMacAfterInterfaceRemoved) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->queueAfterRemoval(
      [this](const auto& state) { return this->removeInterface(state, false); },
      [this]() { this->queueEnsureStaticMac(); });
  EXPECT_FALSE(this->interfaceExists());
  // This records the present behavior: the static MAC of the neighbor stays.
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    queuedNeighborFlushAfterInterfaceRemoved) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacEntryExists(MacEntryType::STATIC_ENTRY);
  this->queueAfterRemoval(
      [this](const auto& state) { return this->removeInterface(state, false); },
      [this]() { this->queueNeighborFlush(); });
  EXPECT_FALSE(this->interfaceExists());
}

TYPED_TEST(
    StaticL2ForNeighorObserverTest,
    queuedClassIDUpdateAfterInterfaceRemoved) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->queueAfterRemoval(
      [this](const auto& state) { return this->removeInterface(state, false); },
      [this]() { this->queueClassIDUpdate(); });
  EXPECT_FALSE(this->interfaceExists());
}

} // namespace facebook::fboss
