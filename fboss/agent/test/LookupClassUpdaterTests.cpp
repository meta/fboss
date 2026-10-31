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
#include "fboss/agent/LookupClassUpdater.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace facebook::fboss {

template <typename AddrType, bool enableIntfNbrTable>
struct IpAddrAndEnableIntfNbrTableT {
  using AddrT = AddrType;
  static constexpr auto intfNbrTable = enableIntfNbrTable;
};

using TestTypes = ::testing::Types<
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, false>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, true>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV6, false>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV6, true>,
    IpAddrAndEnableIntfNbrTableT<folly::MacAddress, false>,
    IpAddrAndEnableIntfNbrTableT<folly::MacAddress, true>>;

template <typename IpAddrAndEnableIntfNbrTableT>
class LookupClassUpdaterTest : public ::testing::Test {
 public:
  using Func = folly::Function<void()>;
  using StateUpdateFn = SwSwitch::StateUpdateFn;
  using AddrT = typename IpAddrAndEnableIntfNbrTableT::AddrT;

  void SetUp() override {
    handle_ = createTestHandle(testStateAWithLookupClasses());
    sw_ = handle_->getSw();
  }

  void verifyStateUpdate(Func func) {
    runInUpdateEventBaseAndWait(std::move(func));
  }

  void verifyStateUpdateAfterNeighborCachePropagation(Func func) {
    // internally, calls through neighbor updater are proxied to the
    // NeighborCache thread, which can eventually end up scheduling a
    // new state update back on to the update evb. This helper waits
    // until the change has made it through all these proxy layers.
    runInUpdateEvbAndWaitAfterNeighborCachePropagation(std::move(func));
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

  AggregatePortID kAggregatePortID() const {
    return AggregatePortID(10);
  }

  IPAddressV4 kIp4Addr() const {
    return IPAddressV4("10.0.0.2");
  }

  IPAddressV6 kIp6Addr() const {
    return IPAddressV6("2401:db00:2110:3001::0002");
  }

  IPAddressV4 kIp4LinkLocalAddr() const {
    return IPAddressV4("169.254.0.1");
  }

  IPAddressV6 kIp6LinkLocalAddr() const {
    return IPAddressV6("fe80::302:03ff:fe04:0506");
  }
  IPAddressV4 kNonMacIp4LinkLocalAddr() const {
    return IPAddressV4("169.254.0.10");
  }

  IPAddressV6 kNonMacIp6LinkLocalAddr() const {
    return IPAddressV6("fe80::991a:885b:34ad:d94a");
  }

  IPAddressV4 kIp4Addr2() const {
    return IPAddressV4("10.0.0.3");
  }

  IPAddressV4 kIp4Addr3() const {
    return IPAddressV4("10.0.0.4");
  }

  IPAddressV4 kIp4AddrN(int n) const {
    return IPAddressV4("10.0.0." + std::to_string(n));
  }

  IPAddressV6 kIp6Addr2() const {
    return IPAddressV6("2401:db00:2110:3001::0003");
  }

  IPAddressV6 kIp6Addr3() const {
    return IPAddressV6("2401:db00:2110:3001::0004");
  }

  IPAddressV6 kIp6AddrN(int n) const {
    return IPAddressV6("2401:db00:2110:3001::" + std::to_string(n));
  }

  MacAddress kMacAddress() const {
    return MacAddress("01:02:03:04:05:06");
  }

  MacAddress kMacAddress2() const {
    return MacAddress("01:02:03:04:05:07");
  }

  MacAddress kMacAddressN(int n) const {
    return MacAddress(fmt::format("01:02:03:04:05:{:02X}", n));
  }

  std::string kVendorMacOui() const {
    return "01:02:03:00:00:00";
  }

  MacAddress kMetaMacAddress() const {
    return MacAddress("04:02:03:04:05:06");
  }

  std::string kMetaMacOui() const {
    return "04:02:03:00:00:00";
  }

  SwSwitch* getSw() const {
    return sw_;
  }

  void resolveNeighbor(
      IPAddress ipAddress,
      MacAddress macAddress,
      bool wait = true) {
    /*
     * Cause a neighbor entry to resolve by receiving appropriate ARP/NDP, and
     * assert if valid CLASSID is associated with the newly resolved neighbor.
     */
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      sw_->getNeighborUpdater()->receivedArpMineForIntf(
          kInterfaceID(),
          ipAddress.asV4(),
          macAddress,
          PortDescriptor(kPortID()),
          ArpOpCode::ARP_OP_REPLY);

    } else {
      sw_->getNeighborUpdater()->receivedNdpMineForIntf(
          kInterfaceID(),
          ipAddress.asV6(),
          macAddress,
          PortDescriptor(kPortID()),
          ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION,
          0);
    }
    if (wait) {
      sw_->getNeighborUpdater()->waitForPendingUpdates();
      waitForBackgroundThread(sw_);
      waitForStateUpdates(sw_);
      sw_->getNeighborUpdater()->waitForPendingUpdates();
      waitForStateUpdates(sw_);
    }
  }

  void unresolveNeighbor(IPAddress ipAddress) {
    sw_->getNeighborUpdater()->flushEntryForIntf(kInterfaceID(), ipAddress);

    sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(sw_);
    waitForStateUpdates(sw_);
  }

  void verifyNeighborClassIDHelper(
      folly::IPAddress ipAddress,
      std::optional<cfg ::AclLookupClass> ipClassID,
      std::optional<cfg ::AclLookupClass> macClassID) const {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto state = sw_->getState();

    auto verifyNeighbor = [this, ipClassID, macClassID, state](auto ipAddr) {
      std::shared_ptr<NeighborTableT> neighborTable;
      auto intf = state->getInterfaces()->getNode(kInterfaceID());
      neighborTable = intf->template getNeighborTable<NeighborTableT>();

      auto entry = neighborTable->getEntry(ipAddr);
      XLOG(DBG) << entry->str();
      EXPECT_EQ(entry->getClassID(), ipClassID);
      if (entry->isReachable() && !entry->getMac().isBroadcast()) {
        // We assume here that class ID of mac matches that of
        // neighbor. That's true for our tests, since for neighbor
        // entries we add a Mac entry in sequence. And since Mac
        // entries round robin over the same sequence of classIDs
        // the paired MAC entry gets a identical classID.
        verifyMacClassIDHelper(
            entry->getMac(), macClassID, MacEntryType::STATIC_ENTRY);
      }
    };
    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      verifyNeighbor(ipAddress.asV4());
    } else {
      verifyNeighbor(ipAddress.asV6());
    }
  }

  void resolve(IPAddress ipAddress, MacAddress macAddress) {
    if constexpr (std::is_same_v<AddrT, folly::MacAddress>) {
      resolveMac(macAddress);
    } else {
      this->resolveNeighbor(ipAddress, macAddress);
    }
  }

  void resolveMac(MacAddress macAddress, bool wait = true) {
    auto l2Entry = L2Entry(
        macAddress,
        this->kVlan(),
        PortDescriptor(this->kPortID()),
        L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING);

    this->sw_->l2LearningUpdateReceived(
        l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
    if (wait) {
      this->sw_->getNeighborUpdater()->waitForPendingUpdates();
      waitForBackgroundThread(this->sw_);
      waitForStateUpdates(this->sw_);
    }
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

  void verifyClassIDHelper(
      const folly::IPAddress& ipAddress,
      const folly::MacAddress& macAddress,
      std::optional<cfg::AclLookupClass> ipClassID,
      std::optional<cfg::AclLookupClass> macClassID) const {
    if constexpr (std::is_same_v<AddrT, folly::MacAddress>) {
      // Learned Mac entries always get added as dynamic entries
      verifyMacClassIDHelper(
          macAddress, macClassID, MacEntryType::DYNAMIC_ENTRY);
    } else {
      this->verifyNeighborClassIDHelper(ipAddress, ipClassID, macClassID);
    }
  }

  auto getMacEntry(folly::MacAddress mac) const {
    auto vlan = sw_->getState()->getVlans()->getNodeIf(this->kVlan());
    return vlan->getMacTable()->getMacIf(mac);
  }

  void verifyMacClassIDHelper(
      const folly::MacAddress& macAddress,
      std::optional<cfg::AclLookupClass> classID,
      MacEntryType type) const {
    auto entry = getMacEntry(macAddress);
    XLOG(DBG) << entry->str();
    EXPECT_EQ(entry->getClassID(), classID);
    EXPECT_EQ(entry->getType(), type);
  }

  IPAddress getIpAddress() const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4Addr());
    } else {
      ipAddress = IPAddress(this->kIp6Addr());
    }

    return ipAddress;
  }

  IPAddress getLinkLocalIpAddress() const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4LinkLocalAddr());
    } else {
      ipAddress = IPAddress(this->kIp6LinkLocalAddr());
    }
    return ipAddress;
  }

  IPAddress getNonMacLinkLocalIpAddress() const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kNonMacIp4LinkLocalAddr());
    } else {
      ipAddress = IPAddress(this->kNonMacIp6LinkLocalAddr());
    }
    return ipAddress;
  }

  std::vector<IPAddress> getLinkLocalIpAddresses() const {
    return {getLinkLocalIpAddress(), getNonMacLinkLocalIpAddress()};
  }

  void resolveLinkLocals(folly::MacAddress mac) {
    for (const auto& ip : getLinkLocalIpAddresses()) {
      this->resolve(ip, mac);
    }
  }
  std::optional<cfg::AclLookupClass> getExpectedLinkLocalClassID(
      cfg::AclLookupClass classID) const {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return classID;
    } else {
      // IPv6 Link local has noHostRoute set, so no classID
      return std::nullopt;
    }
  }

  IPAddress getIpAddress2() const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4Addr2());
    } else {
      ipAddress = IPAddress(this->kIp6Addr2());
    }

    return ipAddress;
  }

  IPAddress getIpAddress3() const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4Addr3());
    } else {
      ipAddress = IPAddress(this->kIp6Addr3());
    }

    return ipAddress;
  }

  IPAddress getIpAddressN(int n) const {
    IPAddress ipAddress;
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      ipAddress = IPAddress(this->kIp4AddrN(n));
    } else {
      ipAddress = IPAddress(this->kIp6AddrN(n));
    }

    return ipAddress;
  }

  void bringPortDown(PortID portID) const {
    this->sw_->linkStateChanged(portID, false, cfg::PortType::INTERFACE_PORT);

    waitForStateUpdates(this->sw_);
    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  void updateLookupClasses(
      const std::vector<cfg::AclLookupClass>& lookupClasses) {
    this->updateState(
        "Reset lookupclasses",
        [=, this](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          auto newPortMaps = newState->getPorts()->modify(&newState);

          for (auto portMap : std::as_const(*newPortMaps)) {
            for (auto port : std::as_const(*portMap.second)) {
              auto newPort = port.second->clone();
              newPort->setLookupClassesToDistributeTrafficOn(lookupClasses);
              newPortMaps->updateNode(
                  newPort, this->sw_->getScopeResolver()->scope(newPort));
            }
          }
          return newState;
        });

    waitForStateUpdates(this->sw_);
    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  void updateMacOuis(bool add = true) {
    this->updateState(
        "mac ouis", [=, this](const std::shared_ptr<SwitchState>& state) {
          auto newState = state->clone();
          auto matcher =
              HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
          auto switchSettings = newState->getSwitchSettings()
                                    ->getNode(matcher.matcherString())
                                    ->modify(&newState);
          if (add) {
            switchSettings->setVendorMacOuis({this->kVendorMacOui()});
            switchSettings->setMetaMacOuis({this->kMetaMacOui()});
          } else {
            switchSettings->setVendorMacOuis({});
            switchSettings->setMetaMacOuis({});
          }
          return newState;
        });
    // Changing the MAC OUIs makes LookupClassUpdater recompute neighbor class
    // IDs, and that half of the work is fanned out through the neighbor cache
    // evb rather than applied inline. waitForStateUpdates alone drains only the
    // update evb, so it returns while the neighbor entries still carry their
    // previous class IDs, and anything asserting on them reads stale values.
    // This is the same barrier updateLookupClasses uses above, for the same
    // reason.
    waitForStateUpdates(this->sw_);
    this->sw_->getNeighborUpdater()->waitForPendingUpdates();
    waitForBackgroundThread(this->sw_);
    waitForStateUpdates(this->sw_);
  }

  bool isBalanced(
      boost::container::flat_map<cfg::AclLookupClass, int>& classID2Cnt) {
    int minCnt = INT_MAX;
    int maxCnt = 0;
    for (auto it : classID2Cnt) {
      XLOG(DBG2) << "class id " << static_cast<int>(it.first) << " => cnt "
                 << it.second;
      minCnt = std::min(minCnt, it.second);
      maxCnt = std::max(maxCnt, it.second);
    }
    return maxCnt - minCnt <= 1;
  }

 protected:
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

using TestTypesNeighbor = ::testing::Types<
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, false>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, true>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV6, false>,
    IpAddrAndEnableIntfNbrTableT<folly::IPAddressV6, true>>;

TYPED_TEST_SUITE(LookupClassUpdaterTest, TestTypes);

TYPED_TEST(LookupClassUpdaterTest, VerifyClassID) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
  });

  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    for (const auto& ip : this->getLinkLocalIpAddresses()) {
      this->verifyClassIDHelper(
          ip,
          this->kMacAddress(),
          this->getExpectedLinkLocalClassID(
              cfg::AclLookupClass::
                  CLASS_QUEUE_PER_HOST_QUEUE_0) /* ipClassID */,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
    }
  });
}

TYPED_TEST(LookupClassUpdaterTest, VerifyClassIDPortDown) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());
  this->bringPortDown(this->kPortID());
  /*
   * On port down, ARP/NDP behavior differs from L2 entries:
   *  - ARP/NDP neighbors go to pending state, and classID is disassociated.
   *  - L2 entries should get pruned with neighbor dereference
   */
  this->verifyStateUpdate([=, this]() {
    if constexpr (std::is_same_v<
                      TypeParam,
                      IpAddrAndEnableIntfNbrTableT<folly::MacAddress, false>>) {
      this->verifyMacClassIDHelper(
          this->kMacAddress(),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          MacEntryType::DYNAMIC_ENTRY);
    } else if constexpr (
        std::is_same_v<
            TypeParam,
            IpAddrAndEnableIntfNbrTableT<folly::MacAddress, true>>) {
      this->verifyMacClassIDHelper(
          this->kMacAddress(),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          MacEntryType::DYNAMIC_ENTRY);
    } else {
      this->verifyClassIDHelper(
          this->getIpAddress(),
          this->kMacAddress(),
          std::nullopt /* ipClassID */,
          std::nullopt /* macClassID */);
      for (const auto& ip : this->getLinkLocalIpAddresses()) {
        this->verifyClassIDHelper(
            ip,
            this->kMacAddress(),
            std::nullopt /* ipClassID */,
            std::nullopt /* macClassID */);
        // Mac entry should get pruned with neighbor entry
        EXPECT_EQ(this->getMacEntry(this->kMacAddress()), nullptr);
      }
    }
  });
}

TYPED_TEST(LookupClassUpdaterTest, LookupClassesToNoLookupClasses) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());
  this->updateLookupClasses({});
  this->verifyClassIDHelper(
      this->getIpAddress(),
      this->kMacAddress(),
      std::nullopt /* ipClassID */,
      std::nullopt /* macClassID */);
  for (const auto& ip : this->getLinkLocalIpAddresses()) {
    this->verifyClassIDHelper(
        ip,
        this->kMacAddress(),
        std::nullopt /* ipClassID */,
        std::nullopt /* macClassID */);
  }
}

TYPED_TEST(LookupClassUpdaterTest, LookupClassesChange) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());
  this->updateLookupClasses(
      {cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3});
  this->verifyClassIDHelper(
      this->getIpAddress(),
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* macClassID */);
  for (const auto& ip : this->getLinkLocalIpAddresses()) {
    this->verifyClassIDHelper(
        ip,
        this->kMacAddress(),
        this->getExpectedLinkLocalClassID(
            cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3) /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* macClassID */);
  }
}

TYPED_TEST(LookupClassUpdaterTest, MacMove) {
  if constexpr (!std::is_same_v<TypeParam, folly::MacAddress>) {
    return;
  }

  this->resolve(this->getIpAddress(), this->kMacAddress());

  this->updateState(
      "Trigger MAC Move", [=, this](const std::shared_ptr<SwitchState>& state) {
        std::shared_ptr<SwitchState> newState{state};

        auto vlan = state->getVlans()->getNodeIf(this->kVlan()).get();
        auto* macTable = vlan->getMacTable().get();
        auto node = macTable->getMacIf(this->kMacAddress());

        EXPECT_NE(node, nullptr);

        macTable = macTable->modify(&vlan, &newState);

        macTable->removeEntry(this->kMacAddress());
        auto macEntry = std::make_shared<MacEntry>(
            this->kMacAddress(), PortDescriptor(this->kPortID2()));
        macTable->addEntry(macEntry);

        return newState;
      });

  waitForStateUpdates(this->sw_);
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  auto state = this->sw_->getState();

  auto vlan = state->getVlans()->getNodeIf(this->kVlan());
  auto* macTable = vlan->getMacTable().get();
  auto node = macTable->getMacIf(this->kMacAddress());

  EXPECT_NE(node, nullptr);
  EXPECT_EQ(node->getPort(), PortDescriptor(this->kPortID2()));
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::DYNAMIC_ENTRY);
}

/*
 * A MAC entry can sit on an aggregate port: learned there, or mirrored from a
 * neighbor resolved on a LAG by updateOrAddStaticEntryIfNbrExists(). Moving
 * such an entry to a physical port must not crash. Queue-per-host is supported
 * on physical ports only, so the move is skipped, but LookupClassUpdater must
 * not assume the old port was physical - phyPortID() CHECKs on that.
 */
TYPED_TEST(LookupClassUpdaterTest, MacMoveFromAggregatePort) {
  this->updateState(
      "Add MAC on aggregate port",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        std::shared_ptr<SwitchState> newState{state};

        auto vlan = state->getVlans()->getNodeIf(this->kVlan()).get();
        auto* macTable = vlan->getMacTable().get();

        EXPECT_EQ(macTable->getMacIf(this->kMacAddress()), nullptr);

        macTable = macTable->modify(&vlan, &newState);
        macTable->addEntry(
            std::make_shared<MacEntry>(
                this->kMacAddress(), PortDescriptor(this->kAggregatePortID())));

        return newState;
      });

  waitForStateUpdates(this->sw_);
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  // Entries on non-physical ports are not eligible for queue-per-host
  this->verifyMacClassIDHelper(
      this->kMacAddress(), std::nullopt, MacEntryType::DYNAMIC_ENTRY);

  this->updateState(
      "Trigger MAC Move off aggregate port",
      [=, this](const std::shared_ptr<SwitchState>& state) {
        std::shared_ptr<SwitchState> newState{state};

        auto vlan = state->getVlans()->getNodeIf(this->kVlan()).get();
        auto* macTable = vlan->getMacTable().get();
        auto node = macTable->getMacIf(this->kMacAddress());

        EXPECT_NE(node, nullptr);
        EXPECT_EQ(node->getPort(), PortDescriptor(this->kAggregatePortID()));

        macTable = macTable->modify(&vlan, &newState);

        macTable->removeEntry(this->kMacAddress());
        auto macEntry = std::make_shared<MacEntry>(
            this->kMacAddress(), PortDescriptor(this->kPortID2()));
        macTable->addEntry(macEntry);

        return newState;
      });

  waitForStateUpdates(this->sw_);
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  auto node = this->getMacEntry(this->kMacAddress());
  EXPECT_NE(node, nullptr);
  EXPECT_EQ(node->getPort(), PortDescriptor(this->kPortID2()));
  /*
   * No classID: processChanged() bails out for a non-physical old port (see
   * the TODO there), so the entry on the physical port is left alone. Update
   * this expectation if LAG to physical port moves ever get handled.
   */
  this->verifyMacClassIDHelper(
      this->kMacAddress(), std::nullopt, MacEntryType::DYNAMIC_ENTRY);

  this->verifyStateUpdate([=, this]() {
    EXPECT_EQ(
        this->sw_->getLookupClassUpdater()->getRefCnt(
            this->kPortID2(),
            this->kMacAddress(),
            this->kVlan(),
            cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
        0);
  });
}

/*
 * Tests that are valid for arp/ndp neighbors only and not for Mac addresses
 */
template <typename IpAddrAndEnableIntfNbrTableT>
class LookupClassUpdaterNeighborTest
    : public LookupClassUpdaterTest<IpAddrAndEnableIntfNbrTableT> {
 public:
  using AddrT = typename IpAddrAndEnableIntfNbrTableT::AddrT;

  void verifySameMacDifferentIpsHelper() {
    auto lookupClassUpdater = this->sw_->getLookupClassUpdater();

    this->verifyNeighborClassIDHelper(
        this->getIpAddress(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
    this->verifyNeighborClassIDHelper(
        this->getIpAddress2(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

    // Verify that refCnt is 3
    // 2 for ipAddress +  ipAddress2 and 1 for static MAC entry
    EXPECT_EQ(
        lookupClassUpdater->getRefCnt(
            this->kPortID(),
            this->kMacAddress(),
            this->kVlan(),
            cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
        3);
  }

  void verifyMultipleBlockedMacsHelper(
      const std::vector<folly::MacAddress>& macAddresses,
      std::optional<cfg::AclLookupClass> classID1,
      std::optional<cfg::AclLookupClass> classID2) {
    std::vector<std::pair<VlanID, folly::MacAddress>> macAddrsToBlock;
    for (const auto& macAddress : macAddresses) {
      macAddrsToBlock.push_back(std::make_pair(this->kVlan(), macAddress));
    }

    updateMacAddrsToBlock(this->getSw(), macAddrsToBlock);
    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      this->verifyClassIDHelper(
          this->getIpAddress(),
          this->kMacAddress(),
          classID1 /* ipClassID */,
          classID1 /* macClassID */);
    });
    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      this->verifyClassIDHelper(
          this->getIpAddress2(),
          this->kMacAddress2(),
          classID2 /* ipClassID */,
          classID2 /* macClassID */);
    });
  }

  void verifyNoNeighborEntryHelper() const {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto state = this->sw_->getState();

    std::shared_ptr<NeighborTableT> neighborTable;
    auto intf = state->getInterfaces()->getNode(this->kInterfaceID());
    neighborTable = intf->template getNeighborTable<NeighborTableT>();

    if constexpr (std::is_same<AddrT, folly::IPAddressV4>::value) {
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV4()), nullptr);
      for (const auto& ip : this->getLinkLocalIpAddresses()) {
        EXPECT_EQ(neighborTable->getEntryIf(ip.asV4()), nullptr);
      }
    } else {
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV6()), nullptr);
      for (const auto& ip : this->getLinkLocalIpAddresses()) {
        EXPECT_EQ(neighborTable->getEntryIf(ip.asV6()), nullptr);
      }
    }
  }
};

TYPED_TEST_SUITE(LookupClassUpdaterNeighborTest, TestTypesNeighbor);

TYPED_TEST(LookupClassUpdaterNeighborTest, VerifyMaxNumHostsPerQueue) {
  this->updateMacOuis();
  auto vendorMac = this->kMacAddress();
  auto metaMac = this->kMetaMacAddress();
  auto localAdministeredMac = MacAddress("02:02:03:04:05:06");
  auto outlierMac = MacAddress("08:02:03:04:05:06");
  // Meta VM MAC should not be counted by number of physical hosts
  this->resolve(this->getIpAddressN(1), metaMac);
  EXPECT_EQ(this->sw_->getLookupClassUpdater()->getMaxNumHostsPerQueue(), 0);
  // local Administered MAC  should not be counted either
  this->resolve(this->getIpAddressN(2), localAdministeredMac);
  EXPECT_EQ(this->sw_->getLookupClassUpdater()->getMaxNumHostsPerQueue(), 0);
  // vendor MAC should be counted
  this->resolve(this->getIpAddressN(3), vendorMac);
  EXPECT_EQ(this->sw_->getLookupClassUpdater()->getMaxNumHostsPerQueue(), 1);
  // unresolve should reset number to zero
  this->unresolveNeighbor(this->getIpAddressN(3));
  EXPECT_EQ(this->sw_->getLookupClassUpdater()->getMaxNumHostsPerQueue(), 0);
  // outlier MAC should also be counted
  this->resolve(this->getIpAddressN(4), outlierMac);
  EXPECT_EQ(this->sw_->getLookupClassUpdater()->getMaxNumHostsPerQueue(), 1);
}

TYPED_TEST(LookupClassUpdaterNeighborTest, VerifyQueuePerPhysicalHost) {
  auto verifyClassIDs =
      [this](IPAddress ip, MacAddress mac, cfg::AclLookupClass queue) {
        this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
          this->verifyClassIDHelper(
              ip, mac, queue /* ipClassID */, queue /* macClassID */);
        });
      };
  this->updateMacOuis();
  std::vector<MacAddress> hostMacs = {
      this->kMacAddressN(0),
      this->kMacAddressN(2),
      this->kMacAddressN(4),
      this->kMacAddressN(6)};
  auto oobMac = this->kMacAddressN(1);
  auto metaMac = this->kMetaMacAddress();
  auto localAdministeredMac = MacAddress("02:02:03:04:05:06");
  auto outlierMac = MacAddress("08:02:03:04:05:06");

  // physical host mac and oob mac are assigned to different queues
  this->resolve(this->getIpAddressN(3), hostMacs[3]);
  verifyClassIDs(
      this->getIpAddressN(3),
      hostMacs[3],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
  this->resolve(this->getIpAddressN(2), hostMacs[2]);
  verifyClassIDs(
      this->getIpAddressN(2),
      hostMacs[2],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
  this->resolve(this->getIpAddressN(4), oobMac);
  verifyClassIDs(
      this->getIpAddressN(4),
      oobMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4);
  this->resolve(this->getIpAddressN(1), hostMacs[1]);
  verifyClassIDs(
      this->getIpAddressN(1),
      hostMacs[1],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  this->resolve(this->getIpAddressN(0), hostMacs[0]);
  verifyClassIDs(
      this->getIpAddressN(0),
      hostMacs[0],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);

  // vm mac and outlier mac is assigned in old way (first queue with minimum
  // hosts assigned)
  this->resolve(this->getIpAddressN(5), metaMac);
  verifyClassIDs(
      this->getIpAddressN(5),
      metaMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
  this->resolve(this->getIpAddressN(6), localAdministeredMac);
  verifyClassIDs(
      this->getIpAddressN(6),
      localAdministeredMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  this->resolve(this->getIpAddressN(7), outlierMac);
  verifyClassIDs(
      this->getIpAddressN(7),
      outlierMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
}

TYPED_TEST(LookupClassUpdaterNeighborTest, VerifyMacOuiUpdate) {
  auto verifyClassIDs =
      [this](IPAddress ip, MacAddress mac, cfg::AclLookupClass queue) {
        this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
          this->verifyClassIDHelper(
              ip, mac, queue /* ipClassID */, queue /* macClassID */);
        });
      };
  std::vector<MacAddress> hostMacs = {
      this->kMacAddressN(0),
      this->kMacAddressN(2),
      this->kMacAddressN(4),
      this->kMacAddressN(6)};
  auto oobMac = this->kMacAddressN(1);
  auto metaMac = this->kMetaMacAddress();
  auto localAdministeredMac = MacAddress("02:02:03:04:05:06");
  auto outlierMac = MacAddress("08:02:03:04:05:06");

  // all macs are assigned in old way (first queue with minimum hosts
  // assigned)
  // physical host mac and oob mac
  this->resolve(this->getIpAddressN(3), hostMacs[3]);
  verifyClassIDs(
      this->getIpAddressN(3),
      hostMacs[3],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
  this->resolve(this->getIpAddressN(2), hostMacs[2]);
  verifyClassIDs(
      this->getIpAddressN(2),
      hostMacs[2],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  this->resolve(this->getIpAddressN(4), oobMac);
  verifyClassIDs(
      this->getIpAddressN(4),
      oobMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
  this->resolve(this->getIpAddressN(1), hostMacs[1]);
  verifyClassIDs(
      this->getIpAddressN(1),
      hostMacs[1],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
  this->resolve(this->getIpAddressN(0), hostMacs[0]);
  verifyClassIDs(
      this->getIpAddressN(0),
      hostMacs[0],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4);
  // vm mac and outlier mac
  this->resolve(this->getIpAddressN(5), metaMac);
  verifyClassIDs(
      this->getIpAddressN(5),
      metaMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
  this->resolve(this->getIpAddressN(6), localAdministeredMac);
  verifyClassIDs(
      this->getIpAddressN(6),
      localAdministeredMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  this->resolve(this->getIpAddressN(7), outlierMac);
  verifyClassIDs(
      this->getIpAddressN(7),
      outlierMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);

  // add mac ouis
  this->updateMacOuis();
  // physical host mac and oob mac are assigned to different queues
  verifyClassIDs(
      this->getIpAddressN(3),
      hostMacs[3],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);
  verifyClassIDs(
      this->getIpAddressN(2),
      hostMacs[2],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);
  verifyClassIDs(
      this->getIpAddressN(4),
      oobMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4);
  verifyClassIDs(
      this->getIpAddressN(1),
      hostMacs[1],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  verifyClassIDs(
      this->getIpAddressN(0),
      hostMacs[0],
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);

  // vm mac and outlier mac are not re-assigned
  verifyClassIDs(
      this->getIpAddressN(5),
      metaMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
  verifyClassIDs(
      this->getIpAddressN(6),
      localAdministeredMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
  verifyClassIDs(
      this->getIpAddressN(7),
      outlierMac,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2);

  // new vm mac should be assigned to next queue 3
  auto localAdministeredMac2 = MacAddress("02:02:03:04:05:07");
  this->resolve(this->getIpAddressN(8), localAdministeredMac2);
  verifyClassIDs(
      this->getIpAddressN(8),
      localAdministeredMac2,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3);

  // assign another vendor mac to queue 0 to make assignment imbalanced
  this->resolve(this->getIpAddressN(9), this->kMacAddressN(8));
  verifyClassIDs(
      this->getIpAddressN(9),
      this->kMacAddressN(8),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
  auto lookupClassUpdater = this->sw_->getLookupClassUpdater();
  auto& classID2Cnt =
      lookupClassUpdater->getPort2ClassIDAndCount()[this->kPortID()];
  EXPECT_FALSE(this->isBalanced(classID2Cnt));

  // remove mac ouis
  this->updateMacOuis(false);

  // all macs should be rebalanced in old way
  classID2Cnt = lookupClassUpdater->getPort2ClassIDAndCount()[this->kPortID()];
  EXPECT_TRUE(this->isBalanced(classID2Cnt));
}

TYPED_TEST(LookupClassUpdaterNeighborTest, VerifyRebalanceByMacOuiUpdate) {
  this->updateMacOuis();
  auto verifyClassIDs =
      [this](IPAddress ip, MacAddress mac, cfg::AclLookupClass queue) {
        this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
          this->verifyClassIDHelper(
              ip, mac, queue /* ipClassID */, queue /* macClassID */);
        });
      };
  // emulate imbalanced assignment by getting 5 macs all ending in 000b
  // and 4 macs all ending in 001b
  std::vector<MacAddress> macs = {
      this->kMacAddressN(0),
      this->kMacAddressN(8),
      this->kMacAddressN(0x10),
      this->kMacAddressN(0x18),
      this->kMacAddressN(0x20),
      this->kMacAddressN(1),
      this->kMacAddressN(9),
      this->kMacAddressN(0x11),
      this->kMacAddressN(0x19),
      this->kMacAddressN(0x21)};

  for (int i = 0; i < macs.size(); i++) {
    auto queueId = macs[i].u64HBO() % 2 == 0
        ? cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0
        : cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4;
    this->resolve(this->getIpAddressN(i), macs[i]);
    verifyClassIDs(this->getIpAddressN(i), macs[i], queueId);
  }
  this->updateMacOuis(false);
  auto lookupClassUpdater = this->sw_->getLookupClassUpdater();
  auto& classID2Cnt =
      lookupClassUpdater->getPort2ClassIDAndCount()[this->kPortID()];
  // macs should be balanced by old classID assignment when
  // queue_per_physical_host is disabled (empty vendorMacOuis)
  EXPECT_TRUE(this->isBalanced(classID2Cnt));
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    ResolveUnresolveResolveVerifyClassID) {
  auto verifyClassIDs = [this]() {
    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      this->verifyClassIDHelper(
          this->getIpAddress(),
          this->kMacAddress(),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
    });

    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      for (const auto& ip : this->getLinkLocalIpAddresses()) {
        this->verifyClassIDHelper(
            ip,
            this->kMacAddress(),
            this->getExpectedLinkLocalClassID(
                cfg::AclLookupClass::
                    CLASS_QUEUE_PER_HOST_QUEUE_0) /* ipClassID */,
            cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
      }
    });
  };

  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());

  verifyClassIDs();

  this->unresolveNeighbor(this->getIpAddress());
  this->unresolveNeighbor(this->getLinkLocalIpAddress());
  this->unresolveNeighbor(this->getNonMacLinkLocalIpAddress());

  auto state = this->sw_->getState();

  this->verifyNoNeighborEntryHelper();

  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolveLinkLocals(this->kMacAddress());

  verifyClassIDs();
}

TYPED_TEST(LookupClassUpdaterNeighborTest, StressNeighborEntryFlap) {
  auto verifyClassIDs = [this]() {
    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      this->verifyClassIDHelper(
          this->getIpAddress(),
          this->kMacAddress(),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
    });
  };

  this->resolve(this->getIpAddress(), this->kMacAddress());
  verifyClassIDs();

  // unresolve neighbor entry by moving it to pending state
  this->sw_->getNeighborUpdater()->portDown(PortDescriptor(this->kPortID()));
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  for (int i = 0; i < 1000; i++) {
    // 1. resolve neighbor entry by moving it back to reachable state
    // run neighbor entry state update
    // lookupClassUpdater do refCnt++, schedule set neighbor entry class id
    this->resolveNeighbor(this->getIpAddress(), this->kMacAddress(), false);

    // 2. unresolve neighbor entry by moving it to pending
    // run neighbor entry state update
    // lookupClassUpdater do refCnt--, schedule unset neighbor entry class id
    this->sw_->getNeighborUpdater()->portDown(PortDescriptor(this->kPortID()));

    // 3. resolve neighbor entry by moving it back to reachable state
    // run neighbor entry state update
    // run set neighbor entry class id from step 1
    // lookupClassUpdater do refCnt++, schedule set new neighbor entry class id
    this->resolveNeighbor(this->getIpAddress(), this->kMacAddress(), false);

    // 4. unresolve neighbor entry again by moving it to pending
    // run neighbor entry state update
    // run unset neighbor entry class id from 2
    // lookupClassUpdater do refCnt--, schedule unset neighbor entry class id
    this->sw_->getNeighborUpdater()->portDown(PortDescriptor(this->kPortID()));
    /* sleep override */
    usleep(100);
  }
  this->resolve(this->getIpAddress(), this->kMacAddress());
  verifyClassIDs();
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    NeighborEntryFlapWithBusyUpdateThread) {
  auto verifyClassIDs = [this]() {
    this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
      this->verifyClassIDHelper(
          this->getIpAddress(),
          this->kMacAddress(),
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
    });
  };

  this->resolve(this->getIpAddress(), this->kMacAddress());
  verifyClassIDs();

  // first unresolve neighbor entry by moving it to pending state
  this->sw_->getNeighborUpdater()->portDown(PortDescriptor(this->kPortID()));
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  // emulating neighbor entry flap by resolve followed up by quick unresolve
  this->resolveNeighbor(this->getIpAddress(), this->kMacAddress(), false);

  // unresolve neighbor entry by moving it to pending state
  this->sw_->getNeighborUpdater()->portDown(PortDescriptor(this->kPortID()));

  // in the meanwhile,emulate busy state update thread by keep doing state
  // snapshot
  for (int i = 0; i < 1000; i++) {
    std::shared_ptr<SwitchState> snapshot{nullptr};
    auto snapshotUpdate = [&snapshot](const std::shared_ptr<SwitchState>& state)
        -> std::shared_ptr<SwitchState> {
      snapshot = state;
      return nullptr;
    };
    this->sw_->updateStateNoCoalescing("snapshot update", snapshotUpdate);
    usleep(100);
  }

  this->unresolveMac(this->kMacAddress());

  this->resolve(this->getIpAddress(), this->kMacAddress());
  verifyClassIDs();
}

TYPED_TEST(LookupClassUpdaterNeighborTest, ClassIDRebalance) {
  int numNeighbors = 15;
  for (int i = 1; i <= numNeighbors; i++) {
    this->resolve(this->getIpAddressN(i), this->kMacAddressN(i));
  }
  // unresolve some neighbor to emulate imbalanced class id assignment
  std::set<int> removedNeighbors;
  for (int i = 1; i <= 2; i++) {
    for (int j = 0; j <= 2; j++) {
      int n = i + j * 5;
      removedNeighbors.insert(n);
      this->unresolveNeighbor(this->getIpAddressN(n));
    }
  }
  XLOG(DBG2) << "unbalanced class id assignment";
  auto lookupClassUpdater = this->sw_->getLookupClassUpdater();
  auto& classID2Cnt =
      lookupClassUpdater->getPort2ClassIDAndCount()[this->kPortID()];
  EXPECT_FALSE(this->isBalanced(classID2Cnt));

  // emulate flush all neighbor entry and re-learn to trigger rebalance
  this->sw_->getNeighborUpdater()->portFlushEntries(
      PortDescriptor(this->kPortID()));
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  for (int i = 1; i <= numNeighbors; i++) {
    if (removedNeighbors.find(i) == removedNeighbors.end()) {
      // this neighbor is resolved again aftre flush
      this->resolve(this->getIpAddressN(i), this->kMacAddressN(i));
    }
  }
  XLOG(DBG2) << "rebalanced class id assignment";
  EXPECT_TRUE(this->isBalanced(classID2Cnt));
}

TYPED_TEST(LookupClassUpdaterNeighborTest, VerifyClassIDSameMacDifferentIPs) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolve(this->getIpAddress2(), this->kMacAddress());

  // Two IPs with same MAC get same classID
  this->verifyStateUpdate(
      [=, this]() { this->verifySameMacDifferentIpsHelper(); });
}

TYPED_TEST(LookupClassUpdaterNeighborTest, ResolveUnresolveResolve) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolve(this->getIpAddress2(), this->kMacAddress());

  // Two IPs with same MAC get same classID
  this->verifyStateUpdate(
      [=, this]() { this->verifySameMacDifferentIpsHelper(); });

  this->unresolveNeighbor(this->getIpAddress());
  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto vlan = state->getVlans()->getNode(this->kVlan());
    auto intf = state->getInterfaces()->getNodeIf(this->kInterfaceID());

    if constexpr (
        std::is_same_v<
            TypeParam,
            IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, false>>) {
      auto neighborTable = vlan->getArpTable();
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV4()), nullptr);
    } else if constexpr (
        std::is_same_v<
            TypeParam,
            IpAddrAndEnableIntfNbrTableT<folly::IPAddressV6, false>>) {
      auto neighborTable = vlan->getNdpTable();
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV6()), nullptr);
    } else if constexpr (
        std::is_same_v<
            TypeParam,
            IpAddrAndEnableIntfNbrTableT<folly::IPAddressV4, true>>) {
      auto neighborTable = intf->getArpTable();
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV4()), nullptr);
    } else {
      auto neighborTable = intf->getNdpTable();
      EXPECT_EQ(
          neighborTable->getEntryIf(this->getIpAddress().asV6()), nullptr);
    }

    // Verify that refCnt is 2 =
    // 1 for ipAddress2 as ipAddress is unersolved + 1 for Static MAC
    auto lookupClassUpdater = this->sw_->getLookupClassUpdater();
    EXPECT_EQ(
        lookupClassUpdater->getRefCnt(
            this->kPortID(),
            this->kMacAddress(),
            this->kVlan(),
            cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
        2);
    // Assert that MAC entry still retains class ID and is of
    // type static (since one neighbor is still resolved)
    this->verifyMacClassIDHelper(
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
        MacEntryType::STATIC_ENTRY);
  });

  // Resolve the IP with same MAC, gets same classID as other IP with same MAC
  this->resolveNeighbor(this->getIpAddress(), this->kMacAddress());
  this->verifyStateUpdate(
      [=, this]() { this->verifySameMacDifferentIpsHelper(); });
}

// Verify that removing a Pending NDP/ARP entry that was never Reachable
// is handled cleanly by LookupClassUpdater on a QPH-enabled port.
TYPED_TEST(LookupClassUpdaterNeighborTest, RemovePendingNeighborDoesNotCrash) {
  using AddrT = typename TypeParam::AddrT;
  using NeighborTableT = std::conditional_t<
      std::is_same<AddrT, folly::IPAddressV4>::value,
      ArpTable,
      NdpTable>;

  auto pendingIp = this->getIpAddress();

  // Step 1: inject a Pending entry with the broadcast-MAC sentinel.
  this->updateState(
      "inject Pending neighbor",
      [this, pendingIp](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        auto intf = newState->getInterfaces()->getNode(this->kInterfaceID());
        auto neighborTable =
            intf->template getNeighborTable<NeighborTableT>()->modify(
                this->kInterfaceID(), &newState);
        if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
          neighborTable->addEntry(
              pendingIp.asV4(),
              folly::MacAddress::BROADCAST,
              PortDescriptor(this->kPortID()),
              this->kInterfaceID(),
              NeighborState::PENDING);
        } else {
          neighborTable->addEntry(
              pendingIp.asV6(),
              folly::MacAddress::BROADCAST,
              PortDescriptor(this->kPortID()),
              this->kInterfaceID(),
              NeighborState::PENDING);
        }
        return newState;
      });
  waitForStateUpdates(this->sw_);
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  // Step 2: remove the Pending entry. Pre-fix: this triggers CHECK_GT.
  this->updateState(
      "remove Pending neighbor",
      [this, pendingIp](const std::shared_ptr<SwitchState>& state) {
        auto newState = state->clone();
        auto intf = newState->getInterfaces()->getNode(this->kInterfaceID());
        auto neighborTable =
            intf->template getNeighborTable<NeighborTableT>()->modify(
                this->kInterfaceID(), &newState);
        if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
          neighborTable->removeEntry(pendingIp.asV4());
        } else {
          neighborTable->removeEntry(pendingIp.asV6());
        }
        return newState;
      });
  waitForStateUpdates(this->sw_);
  this->sw_->getNeighborUpdater()->waitForPendingUpdates();
  waitForBackgroundThread(this->sw_);
  waitForStateUpdates(this->sw_);

  this->verifyStateUpdate([=, this]() {
    auto state = this->sw_->getState();
    auto intf = state->getInterfaces()->getNode(this->kInterfaceID());
    auto neighborTable = intf->template getNeighborTable<NeighborTableT>();
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      EXPECT_EQ(neighborTable->getEntryIf(pendingIp.asV4()), nullptr);
    } else {
      EXPECT_EQ(neighborTable->getEntryIf(pendingIp.asV6()), nullptr);
    }
  });
}

TYPED_TEST(LookupClassUpdaterNeighborTest, staticL2EntriesForResolvedNeighbor) {
  this->resolveMac(this->kMacAddress());
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::DYNAMIC_ENTRY);
  this->resolve(this->getIpAddress(), this->kMacAddress());
  // Mac entry should transform to STATIC post neighbor resolution
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
}

TYPED_TEST(LookupClassUpdaterNeighborTest, NeighborMacChange) {
  // resolve neighbor
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

  // resolve neighbor to different MAC address
  this->resolve(this->getIpAddress(), this->kMacAddress2());
  this->verifyMacClassIDHelper(
      this->kMacAddress2(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* macClassID */);
}

// MAC addrs to block unit tests
TYPED_TEST(LookupClassUpdaterNeighborTest, BlockMacThenResolveNeighbor) {
  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress()}});
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
        cfg::AclLookupClass::CLASS_DROP /* macClassID */);
  });
}

TYPED_TEST(LookupClassUpdaterNeighborTest, ResolveNeighborThenBlockMac) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress()}});
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
        cfg::AclLookupClass::CLASS_DROP /* macClassID */);
  });
}

TYPED_TEST(LookupClassUpdaterNeighborTest, BlockThenUnblockMultipleMacs) {
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolve(this->getIpAddress2(), this->kMacAddress2());

  // mac1 unblocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper(
      {},
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);

  // mac1 blocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress()},
      cfg::AclLookupClass::CLASS_DROP,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);

  // mac1 blocked, mac2 blocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress(), this->kMacAddress2()},
      cfg::AclLookupClass::CLASS_DROP,
      cfg::AclLookupClass::CLASS_DROP);

  // mac1 unblocked, mac2 blocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress2()},
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      cfg::AclLookupClass::CLASS_DROP);

  // mac1 unblocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper(
      {},
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    BlockMacResolveNeighborResolveToUnblockedMac) {
  // resolve neighbor
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

  // block MAC
  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress()}});

  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_DROP,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
      cfg::AclLookupClass::CLASS_DROP /* macClassID */);

  // resolve neighbor to a different MAC address that is NOT blocked.
  // the Neighbor is no longer blocked.
  this->resolve(this->getIpAddress(), this->kMacAddress2());
  this->verifyMacClassIDHelper(
      this->kMacAddress2(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    BlockMacResolveNeighborResolveToAnotherBlockedMac) {
  // resolve neighbor
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

  // block MACs
  updateMacAddrsToBlock(
      this->getSw(),
      {{this->kVlan(), this->kMacAddress()},
       {this->kVlan(), this->kMacAddress2()}});

  this->verifyMacClassIDHelper(
      this->kMacAddress(),
      cfg::AclLookupClass::CLASS_DROP,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
      cfg::AclLookupClass::CLASS_DROP /* macClassID */);

  // resolve neighbor to a different MAC address that is also blocked.
  // the Neighbor remains blocked
  this->resolve(this->getIpAddress(), this->kMacAddress2());
  this->verifyMacClassIDHelper(
      this->kMacAddress2(),
      cfg::AclLookupClass::CLASS_DROP,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
      cfg::AclLookupClass::CLASS_DROP /* macClassID */);

  // unblock MAC that the neighbor no longer resolves to: neighbor remains
  // blocked
  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress2()}});
  this->verifyMacClassIDHelper(
      this->kMacAddress2(),
      cfg::AclLookupClass::CLASS_DROP,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
      cfg::AclLookupClass::CLASS_DROP /* macClassID */);

  // unblock MAC that the neighbor no longer resolves to: neighbor unblocked
  updateMacAddrsToBlock(this->getSw(), {});
  this->verifyMacClassIDHelper(
      this->kMacAddress2(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0,
      MacEntryType::STATIC_ENTRY);
  this->verifyNeighborClassIDHelper(
      this->getIpAddress(),
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
      cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    NoLookupClassBlockMacThenResolveNeighbor) {
  // No lookup classes
  this->updateLookupClasses({});

  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress()}});
  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
        cfg::AclLookupClass::CLASS_DROP /* macClassID */);
  });
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    NoLookupClassResolveNeighborThenBlockMac) {
  // No lookup classes
  this->updateLookupClasses({});

  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        std::nullopt /* ipClassID */,
        std::nullopt /* macClassID */);
  });

  updateMacAddrsToBlock(this->getSw(), {{this->kVlan(), this->kMacAddress()}});
  this->verifyStateUpdateAfterNeighborCachePropagation([=, this]() {
    this->verifyClassIDHelper(
        this->getIpAddress(),
        this->kMacAddress(),
        cfg::AclLookupClass::CLASS_DROP /* ipClassID */,
        cfg::AclLookupClass::CLASS_DROP /* macClassID */);
  });
}

TYPED_TEST(
    LookupClassUpdaterNeighborTest,
    NoLookupClassBlockThenUnblockMultipleMacs) {
  // No lookup classes
  this->updateLookupClasses({});

  this->resolve(this->getIpAddress(), this->kMacAddress());
  this->resolve(this->getIpAddress2(), this->kMacAddress2());

  // mac1 unblocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper({}, std::nullopt, std::nullopt);

  // mac1 blocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress()}, cfg::AclLookupClass::CLASS_DROP, std::nullopt);

  // mac1 blocked, mac2 blocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress(), this->kMacAddress2()},
      cfg::AclLookupClass::CLASS_DROP,
      cfg::AclLookupClass::CLASS_DROP);

  // mac1 unblocked, mac2 blocked
  this->verifyMultipleBlockedMacsHelper(
      {this->kMacAddress2()}, std::nullopt, cfg::AclLookupClass::CLASS_DROP);

  // mac1 unblocked, mac2 unblocked
  this->verifyMultipleBlockedMacsHelper({}, std::nullopt, std::nullopt);
}

template <typename IpAddrAndEnableIntfNbrTableT>
class LookupClassUpdaterWarmbootTest
    : public LookupClassUpdaterTest<IpAddrAndEnableIntfNbrTableT> {
 public:
  using AddrT = typename IpAddrAndEnableIntfNbrTableT::AddrT;

  void SetUp() override {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto newState = testStateAWithLookupClasses();

    std::shared_ptr<NeighborTableT> neighborTable;
    auto intf = newState->getInterfaces()->getNodeIf(this->kInterfaceID());
    neighborTable = intf->template getNeighborTable<NeighborTableT>();

    neighborTable->addEntry(NeighborEntryFields(
        this->getIpAddr(),
        this->kMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::PENDING));

    neighborTable->updateEntry(
        this->getIpAddr(),
        this->kMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::REACHABLE,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);

    this->handle_ = createTestHandle(newState);
    this->sw_ = this->handle_->getSw();
  }

  AddrT getIpAddr() {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return this->kIp4Addr();
    } else {
      return this->kIp6Addr();
    }
  }
};

TYPED_TEST_SUITE(LookupClassUpdaterWarmbootTest, TestTypesNeighbor);

/*
 * Initialize the SetUp() SwitchState to carry a neighbor with a classID.
 * LookupClassUpdater::initObserver should consume this to initialize its local
 * cache (this mimics warmboot).
 *
 * Verify if the neighbor indeed has the classID.
 * Resolve another neighbor with different MAC: it should get next classID.
 * Resolve another neighbor with same MAC as that of neighbor in SetUp().
 * Verify if it gets same classID as chosen SetUp(): classIDs are unique per
 * MAC.
 */
TYPED_TEST(LookupClassUpdaterWarmbootTest, VerifyClassID) {
  this->resolveNeighbor(this->getIpAddress2(), this->kMacAddress2());
  this->resolveNeighbor(this->getIpAddress3(), this->kMacAddress());

  this->verifyStateUpdate([=, this]() {
    this->verifyNeighborClassIDHelper(
        this->getIpAddress(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddress2(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddress3(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);
  });
}

template <typename IpAddrAndEnableIntfNbrTableT>
class LookupClassUpdaterWarmbootWithQueuePerPhysicalHostTest
    : public LookupClassUpdaterTest<IpAddrAndEnableIntfNbrTableT> {
 public:
  using AddrT = typename IpAddrAndEnableIntfNbrTableT::AddrT;

  void SetUp() override {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto newState = testStateAWithLookupClasses();

    auto vlanID = this->kVlan();
    auto vlan = newState->getVlans()->getNodeIf(vlanID);
    auto macTable = vlan->getMacTable();
    macTable->addEntry(
        std::make_shared<MacEntry>(
            this->kMacAddress(),
            PortDescriptor(this->kPortID()),
            std::optional<cfg::AclLookupClass>(
                cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
            MacEntryType::DYNAMIC_ENTRY));
    macTable->addEntry(
        std::make_shared<MacEntry>(
            this->kMetaMacAddress(),
            PortDescriptor(this->kPortID()),
            std::optional<cfg::AclLookupClass>(
                cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
            MacEntryType::DYNAMIC_ENTRY));

    std::shared_ptr<NeighborTableT> neighborTable;
    auto intf = newState->getInterfaces()->getNodeIf(this->kInterfaceID());
    neighborTable = intf->template getNeighborTable<NeighborTableT>();

    neighborTable->addEntry(NeighborEntryFields(
        this->getIpAddrN(2),
        this->kMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::PENDING));

    neighborTable->updateEntry(
        this->getIpAddrN(2),
        this->kMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::REACHABLE,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);

    neighborTable->addEntry(NeighborEntryFields(
        this->getIpAddrN(1),
        this->kMetaMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::PENDING));

    neighborTable->updateEntry(
        this->getIpAddrN(1),
        this->kMetaMacAddress(),
        PortDescriptor(this->kPortID()),
        this->kInterfaceID(),
        NeighborState::REACHABLE,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);

    auto matcher = HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
    newState->getSwitchSettings()
        ->getNode(matcher.matcherString())
        ->modify(&newState);

    this->handle_ = createTestHandle(newState);
    this->sw_ = this->handle_->getSw();
  }

  AddrT getIpAddrN(int n) {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return this->kIp4AddrN(n);
    } else {
      return this->kIp6AddrN(n);
    }
  }
};

TYPED_TEST_SUITE(
    LookupClassUpdaterWarmbootWithQueuePerPhysicalHostTest,
    TestTypesNeighbor);

TYPED_TEST(
    LookupClassUpdaterWarmbootWithQueuePerPhysicalHostTest,
    VerifyUpdateClassID) {
  // add mac ouis
  this->updateMacOuis();

  // meta Mac address should still be assigned to the first queue 0
  this->resolveNeighbor(this->getIpAddress2(), this->kMetaMacAddress());
  // vendor Mac address endin with 6 should be re-assigned to queue (6>>1)=3
  EXPECT_EQ(this->kMacAddress().u64HBO() & 0b111, 6);
  this->resolveNeighbor(this->getIpAddress3(), this->kMacAddress());

  this->verifyStateUpdate([=, this]() {
    this->verifyNeighborClassIDHelper(
        this->getIpAddress(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddress2(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddress3(),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* macClassID */);
  });
}

/*
 * This test verifies if the class id assignment is imblanaced before warmboot,
 * when the next warmboot without queue_per_physical_host (empty vendorMacOuis),
 * class id re-assignment logics should be triggered to re-balance mac->classId
 */
template <typename IpAddrAndEnableIntfNbrTableT>
class LookupClassUpdaterWarmbootRebalanceTest
    : public LookupClassUpdaterTest<IpAddrAndEnableIntfNbrTableT> {
 public:
  using AddrT = typename IpAddrAndEnableIntfNbrTableT::AddrT;

  void SetUp() override {
    using NeighborTableT = std::conditional_t<
        std::is_same<AddrT, folly::IPAddressV4>::value,
        ArpTable,
        NdpTable>;

    auto newState = testStateAWithLookupClasses();

    auto vlanID = this->kVlan();
    auto vlan = newState->getVlans()->getNodeIf(vlanID);
    auto port = newState->getPorts()->getNodeIf(this->kPortID());
    port->addVlan(vlanID, false, false);

    auto macTable = vlan->getMacTable();
    std::shared_ptr<NeighborTableT> neighborTable;
    auto intf = newState->getInterfaces()->getNodeIf(this->kInterfaceID());
    neighborTable = intf->template getNeighborTable<NeighborTableT>();

    // imbalanced assignment results from warmboot state
    for (int i = 0; i < 5; i++) {
      macTable->addEntry(
          std::make_shared<MacEntry>(
              this->kMacAddressN(i),
              PortDescriptor(this->kPortID()),
              std::optional<cfg::AclLookupClass>(
                  cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0),
              MacEntryType::DYNAMIC_ENTRY));

      neighborTable->addEntry(NeighborEntryFields(
          this->getIpAddrN(i),
          this->kMacAddressN(i),
          PortDescriptor(this->kPortID()),
          this->kInterfaceID(),
          NeighborState::PENDING));

      neighborTable->updateEntry(
          this->getIpAddrN(i),
          this->kMacAddressN(i),
          PortDescriptor(this->kPortID()),
          this->kInterfaceID(),
          NeighborState::REACHABLE,
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0);
    }

    auto matcher = HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
    auto switchSettings = newState->getSwitchSettings()
                              ->getNode(matcher.matcherString())
                              ->modify(&newState);

    switchSettings->setVendorMacOuis({this->kVendorMacOui()});
    switchSettings->setMetaMacOuis({this->kMetaMacOui()});

    this->handle_ = createTestHandle(newState);
    this->sw_ = this->handle_->getSw();
  }

  AddrT getIpAddrN(int n) {
    if constexpr (std::is_same_v<AddrT, folly::IPAddressV4>) {
      return this->kIp4AddrN(n);
    } else {
      return this->kIp6AddrN(n);
    }
  }
};

TYPED_TEST_SUITE(LookupClassUpdaterWarmbootRebalanceTest, TestTypesNeighbor);

TYPED_TEST(LookupClassUpdaterWarmbootRebalanceTest, VerifyRebalance) {
  // remove mac ouis
  this->updateMacOuis(false);

  this->verifyStateUpdate([=, this]() {
    this->verifyNeighborClassIDHelper(
        this->getIpAddrN(0),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddrN(1),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddrN(2),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_2 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddrN(3),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_3 /* macClassID */);

    this->verifyNeighborClassIDHelper(
        this->getIpAddrN(4),
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4 /* ipClassID */,
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_4 /* macClassID */);
  });
}

} // namespace facebook::fboss
