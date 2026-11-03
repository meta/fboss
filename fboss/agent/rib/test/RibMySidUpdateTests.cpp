// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/MySidConfigUtils.h"
#include "fboss/agent/rib/MySidMapUpdater.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <gtest/gtest.h>

#include <set>

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

namespace {

const RouterID kRid(0);
const std::string kSrv6Tunnel0{"srv6Tunnel0"};

std::map<int64_t, cfg::SwitchInfo> getTestSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> map;
  cfg::SwitchInfo info{};
  info.switchType() = cfg::SwitchType::NPU;
  info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  info.switchIndex() = 0;
  map.emplace(0, info);
  return map;
}

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kSwitchIdScopeResolver(
      getTestSwitchInfo());
  return &kSwitchIdScopeResolver;
}

folly::CIDRNetworkV6 makeSidPrefix(const std::string& addr, uint8_t len) {
  return std::make_pair(folly::IPAddressV6(addr), len);
}

MySidEntry makeMySidEntry(
    const std::string& addr,
    uint8_t len,
    MySidType type = MySidType::DECAPSULATE_AND_LOOKUP) {
  MySidEntry entry;
  entry.type() = type;
  facebook::network::thrift::IPPrefix prefix;
  prefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(addr));
  prefix.prefixLength() = len;
  entry.mySid() = prefix;
  return entry;
}

MySidEntry makeMySidEntryWithNextHops(
    const std::string& addr,
    uint8_t len,
    const std::vector<std::string>& nextHopAddrs,
    MySidType type = MySidType::NODE_MICRO_SID) {
  MySidEntry entry = makeMySidEntry(addr, len, type);
  std::vector<NextHopThrift> nextHops;
  for (const auto& nhAddr : nextHopAddrs) {
    NextHopThrift nh;
    nh.address() =
        facebook::network::toBinaryAddress(folly::IPAddressV6(nhAddr));
    nextHops.push_back(std::move(nh));
  }
  entry.nextHops() = std::move(nextHops);
  return entry;
}

RouteNextHopSet makeUnresolvedNextHops(
    const std::vector<std::string>& nextHopAddrs) {
  RouteNextHopSet nextHops;
  for (const auto& nhAddr : nextHopAddrs) {
    nextHops.emplace(UnresolvedNextHop(folly::IPAddress(nhAddr), 1));
  }
  return nextHops;
}

RouteNextHopSet makeBackupNextHops(
    const std::vector<std::string>& nextHopAddrs) {
  std::vector<NextHopThrift> nextHops;
  for (const auto& nextHopAddr : nextHopAddrs) {
    NextHopThrift nextHop;
    nextHop.address() =
        facebook::network::toBinaryAddress(folly::IPAddress(nextHopAddr));
    nextHop.role() = NextHopRole::BACKUP;
    nextHops.push_back(std::move(nextHop));
  }
  return util::toRouteNextHopSet(nextHops, true);
}

RouteNextHopSet makeResolvedBackupNextHops(
    const std::vector<std::string>& nextHopAddrs,
    InterfaceID interfaceId) {
  RouteNextHopSet nextHops;
  for (const auto& nextHopAddr : nextHopAddrs) {
    nextHops.emplace(ResolvedNextHop(
        folly::IPAddress(nextHopAddr),
        interfaceId,
        NextHopWeight(1),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        NextHopRole::BACKUP));
  }
  return nextHops;
}

NextHopThrift makeSrv6NextHop(const std::string& nextHopAddr) {
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(nextHopAddr));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  return nhop;
}

RouteNextHopSet makeSrv6NextHops(const std::vector<std::string>& nextHopAddrs) {
  std::vector<NextHopThrift> nextHops;
  for (const auto& nextHopAddr : nextHopAddrs) {
    nextHops.push_back(makeSrv6NextHop(nextHopAddr));
  }
  return util::toRouteNextHopSet(nextHops, true);
}

NextHopThrift makeLinkLocalNextHop(
    const std::string& nextHopAddr,
    const std::string& ifName) {
  NextHopThrift nhop;
  auto addr =
      facebook::network::toBinaryAddress(folly::IPAddressV6(nextHopAddr));
  addr.ifName() = ifName;
  nhop.address() = std::move(addr);
  return nhop;
}

UnicastRoute makeOpenRRoute(
    const std::string& prefixAddr,
    uint8_t prefixLen,
    const std::vector<std::pair<std::string, std::string>>& nextHops) {
  UnicastRoute route;
  IpPrefix routePrefix;
  routePrefix.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(prefixAddr));
  routePrefix.prefixLength() = prefixLen;
  route.dest() = routePrefix;
  for (const auto& [nextHopAddr, ifName] : nextHops) {
    route.nextHops()->push_back(makeLinkLocalNextHop(nextHopAddr, ifName));
  }
  return route;
}

// Route whose next hops are plain gateways, recursively resolved by the RIB
// over a connected route. Unlike makeOpenRRoute the resulting next hops are
// not link-local, so they stay valid in a SwitchState with no Interfaces.
UnicastRoute makeGatewayRoute(
    const std::string& prefixAddr,
    uint8_t prefixLen,
    const std::vector<std::string>& gateways) {
  UnicastRoute route;
  IpPrefix routePrefix;
  routePrefix.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6(prefixAddr));
  routePrefix.prefixLength() = prefixLen;
  route.dest() = routePrefix;
  for (const auto& gateway : gateways) {
    NextHopThrift nextHop;
    nextHop.address() =
        facebook::network::toBinaryAddress(folly::IPAddress(gateway));
    route.nextHops()->push_back(std::move(nextHop));
  }
  return route;
}

std::set<folly::IPAddress> getNextHopAddrs(const RouteNextHopSet& nextHops) {
  std::set<folly::IPAddress> addrs;
  for (const auto& nextHop : nextHops) {
    addrs.insert(nextHop.addr());
  }
  return addrs;
}

IpPrefix toIpPrefix(const std::string& addr, uint8_t len) {
  IpPrefix prefix;
  prefix.ip() = facebook::network::toBinaryAddress(folly::IPAddressV6(addr));
  prefix.prefixLength() = len;
  return prefix;
}

// Callback that uses MySidMapUpdater to apply RIB MySid to SwitchState.
StateDelta mySidToSwitchStateUpdate(
    const SwitchIdScopeResolver* resolver,
    const NextHopIDManager* /*nextHopIDManager*/,
    const MySidTable& mySidTable,
    void* cookie) {
  auto switchState =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
  auto oldState = *switchState;

  MySidMapUpdater updater(resolver, mySidTable);
  auto newState = updater(*switchState);
  newState->publish();
  *switchState = newState;
  return StateDelta(oldState, newState);
}

StateDelta noOpRibToSwitchStateUpdate(
    const SwitchIdScopeResolver* /*resolver*/,
    RouterID /*vrf*/,
    const IPv4NetworkToRouteMap& /*v4NetworkToRoute*/,
    const IPv6NetworkToRouteMap& /*v6NetworkToRoute*/,
    const LabelToRouteMap& /*labelToRoute*/,
    const NextHopIDManager* /*nextHopIDManager*/,
    const MySidTable& /*mySidTable*/,
    void* cookie,
    bool /*vrfDeleted*/) {
  auto switchState =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
  return StateDelta(*switchState, *switchState);
}

void addNamedNextHopGroup(
    RoutingInformationBase& rib,
    const std::string& name,
    const RouteNextHopSet& nextHops,
    std::shared_ptr<SwitchState>* switchState) {
  rib.addOrUpdateNamedNextHopGroups(
      scopeResolver(),
      {{name, nextHops}},
      noOpRibToSwitchStateUpdate,
      switchState);
}

// Callback that always fails via FbossHwUpdateError.
// Uses MySidMapUpdater to build the desired state.
// Applied state is the original state (no MySid changes applied to HW).
class FailMySidUpdate {
 public:
  StateDelta operator()(
      const SwitchIdScopeResolver* resolver,
      const NextHopIDManager* /*nextHopIDManager*/,
      const MySidTable& mySidTable,
      void* cookie) {
    auto switchState =
        static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
    (*switchState)->publish();

    // Build the desired state using MySidMapUpdater
    MySidMapUpdater updater(resolver, mySidTable);
    auto desiredState = updater(*switchState);
    desiredState->publish();

    // Applied state is the original state (no MySid changes applied)
    throw FbossHwUpdateError(desiredState, *switchState);
  }
};

// Callback that fails but injects specific MySid entries into the applied
// state, simulating a partial HW update where some MySids were programmed.
class FailWithMySidInAppliedState {
 public:
  explicit FailWithMySidInAppliedState(MySidTable mySidEntriesToInject)
      : mySidEntriesToInject_(std::move(mySidEntriesToInject)) {}

  StateDelta operator()(
      const SwitchIdScopeResolver* resolver,
      const NextHopIDManager* /*nextHopIDManager*/,
      const MySidTable& mySidTable,
      void* cookie) {
    auto switchState =
        static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
    (*switchState)->publish();

    // Build the desired state using MySidMapUpdater
    MySidMapUpdater updater(resolver, mySidTable);
    auto desiredState = updater(*switchState);
    desiredState->publish();

    // Build the applied state from original state with injected MySids
    auto appliedState = (*switchState)->clone();
    auto newMySids = std::make_shared<MultiSwitchMySidMap>();
    for (const auto& [cidr, mySid] : mySidEntriesToInject_) {
      newMySids->addNode(
          mySid, HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}});
    }
    appliedState->resetMySids(newMySids);
    appliedState->publish();

    throw FbossHwUpdateError(desiredState, appliedState);
  }

 private:
  MySidTable mySidEntriesToInject_;
};

// Locator with nonzero second 16-bit group so the resulting SID address
// has an unambiguous IPv6 string form (e.g., "3001:db8:100::" rather
// than the IPv6-compacted "fc00:0:100::" for an all-zero second group).
constexpr folly::StringPiece kTestLocatorPrefix = "3001:db8::/32";

cfg::MySidConfig makeDecapMySidConfig(int16_t functionId) {
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::MySidEntryConfig entry;
  entry.set_decap(cfg::DecapMySidConfig{});
  mySidConfig.entries()->emplace(functionId, std::move(entry));
  return mySidConfig;
}

cfg::MySidConfig makeNodeMySidConfig(
    int16_t functionId,
    const std::string& nodeAddress) {
  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::NodeMySidConfig node;
  node.nodeAddress() = nodeAddress;
  cfg::MySidEntryConfig entry;
  entry.set_node(std::move(node));
  mySidConfig.entries()->emplace(functionId, std::move(entry));
  return mySidConfig;
}

std::shared_ptr<MySid> makeMySid(
    const folly::CIDRNetworkV6& prefix,
    MySidType type = MySidType::DECAPSULATE_AND_LOOKUP) {
  state::MySidFields fields;
  fields.type() = type;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(prefix.first);
  thriftPrefix.prefixLength() = prefix.second;
  fields.mySid() = thriftPrefix;
  auto mySid = std::make_shared<MySid>(fields);
  mySid->setUnresolveNextHopsId(std::nullopt);
  mySid->setResolvedNextHopsId(std::nullopt);
  return mySid;
}

// Callback that uses RibToSwitchStateUpdater with UPDATE_MYSID, so that
// SwitchStateNextHopIdUpdater runs and populates FibInfo id2NextHopSet maps.
StateDelta mySidToSwitchStateUpdateViaRibUpdater(
    const SwitchIdScopeResolver* resolver,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie) {
  auto switchState =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
  auto oldState = *switchState;

  IPv4NetworkToRouteMap emptyV4;
  IPv6NetworkToRouteMap emptyV6;
  LabelToRouteMap emptyLabel;
  RibToSwitchStateUpdater updater(
      resolver,
      RouterID(0), // don't care
      emptyV4,
      emptyV6,
      emptyLabel,
      nextHopIDManager,
      mySidTable,
      RibToSwitchStateUpdater::UPDATE_MYSID);

  auto newState = updater(*switchState);
  newState->publish();
  *switchState = newState;
  return StateDelta(oldState, newState);
}

// Route-update counterpart of mySidToSwitchStateUpdateViaRibUpdater. A route
// update re-resolves MySid next hops, so the MySid map and the FibInfo
// id2NextHopSet maps have to be refreshed from the post-update RIB for the
// re-resolution to be observable in SwitchState.
StateDelta routeToSwitchStateUpdateViaRibUpdater(
    const SwitchIdScopeResolver* resolver,
    RouterID vrf,
    const IPv4NetworkToRouteMap& v4NetworkToRoute,
    const IPv6NetworkToRouteMap& v6NetworkToRoute,
    const LabelToRouteMap& labelToRoute,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie,
    bool /*vrfDeleted*/) {
  auto switchState =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
  auto oldState = *switchState;

  RibToSwitchStateUpdater updater(
      resolver,
      vrf,
      v4NetworkToRoute,
      v6NetworkToRoute,
      labelToRoute,
      nextHopIDManager,
      mySidTable,
      RibToSwitchStateUpdater::UPDATE_MYSID);

  auto newState = updater(*switchState);
  newState->publish();
  *switchState = newState;
  return StateDelta(oldState, newState);
}

using NextHopAddrs = std::set<std::pair<folly::IPAddress, InterfaceID>>;

// (address, interface) of every next hop in `id`, which must all be resolved
// and carry `role`.
NextHopAddrs getNextHopsForRole(
    const NextHopIDManager& manager,
    NextHopSetID id,
    NextHopRole role) {
  NextHopAddrs nextHops;
  for (const auto& nextHop : manager.getNextHops(id)) {
    EXPECT_EQ(nextHop.role(), role);
    EXPECT_TRUE(nextHop.isResolved());
    if (nextHop.isResolved()) {
      nextHops.emplace(nextHop.addr(), nextHop.intf());
    }
  }
  return nextHops;
}

NextHopAddrs getBackupNextHops(
    const NextHopIDManager& manager,
    NextHopSetID id) {
  return getNextHopsForRole(manager, id, NextHopRole::BACKUP);
}

} // namespace

class RibMySidCrudTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib_.ensureVrf(kRid);
    switchState_ = std::make_shared<SwitchState>();
    switchState_->publish();
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

class RibMySidValidationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib_.ensureVrf(kRid);
    switchState_ = std::make_shared<SwitchState>();
    switchState_->publish();
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

class RibMySidSwitchStateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib_.ensureVrf(kRid);
    switchState_ = std::make_shared<SwitchState>();
    switchState_->publish();
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

class RibMySidRollbackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib_.ensureVrf(kRid);
    switchState_ = std::make_shared<SwitchState>();
    switchState_->publish();
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(RibMySidCrudTest, addMySidEntry) {
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);

  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48),
  };
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  auto prefix = makeSidPrefix("fc00:100::1", 48);
  EXPECT_NE(mySidTable.find(prefix), mySidTable.end());
  EXPECT_EQ(*mySidTable[prefix].type(), MySidType::DECAPSULATE_AND_LOOKUP);

  // Verify switchState reflects the MySid
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_EQ(
      switchState_->getMySids()->getNodeIf("fc00:100::1/48")->getType(),
      MySidType::DECAPSULATE_AND_LOOKUP);
}

TEST_F(RibMySidCrudTest, addMultipleMySidEntries) {
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48),
      makeMySidEntry("fc00:200::1", 64),
  };
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add multiple mysids",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 2);
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());

  // Verify switchState reflects both MySids
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST_F(RibMySidCrudTest, deleteMySidEntry) {
  // Add first
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48),
      makeMySidEntry("fc00:200::1", 64),
  };
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysids",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 2);

  // Delete one
  std::vector<IpPrefix> toDelete = {toIpPrefix("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      toDelete,
      "delete mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());

  // Verify switchState: deleted entry gone, remaining entry present
  EXPECT_EQ(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST_F(RibMySidCrudTest, deleteNonExistentMySidEntry) {
  // Add one entry
  std::vector<MySidEntry> toAdd = {makeMySidEntry("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  auto mySidTableBefore = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTableBefore.size(), 1);

  // Delete a non-existent entry — should be a no-op for that prefix
  std::vector<IpPrefix> toDelete = {toIpPrefix("fc00:999::1", 48)};
  rib_.update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      toDelete,
      "delete nonexistent mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable is unchanged
  auto mySidTableAfter = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTableBefore, mySidTableAfter);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
}

TEST_F(RibMySidCrudTest, addAndDeleteInSameUpdate) {
  // Add initial entry
  std::vector<MySidEntry> toAdd = {makeMySidEntry("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);

  // Add a new entry and delete the old one in the same update
  std::vector<MySidEntry> toAdd2 = {makeMySidEntry("fc00:200::1", 64)};
  std::vector<IpPrefix> toDelete = {toIpPrefix("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd2,
      toDelete,
      "add and delete mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());

  // Verify switchState reflects the swap
  EXPECT_EQ(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST_F(RibMySidCrudTest, replaceMySidEntry) {
  // Add entry
  std::vector<MySidEntry> toAdd = {makeMySidEntry("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);

  // Add same prefix again (replace)
  std::vector<MySidEntry> toAdd2 = {makeMySidEntry("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd2,
      {},
      "replace mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify RIB mySidTable
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());

  // Verify switchState still has the entry
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
}

TEST_F(RibMySidValidationTest, rejectNonDecapsulateType) {
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48, MySidType::NODE_MICRO_SID),
  };
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          toAdd,
          {},
          "add unsupported mysid type",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  // Verify RIB and switchState remain empty
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

TEST_F(RibMySidValidationTest, rejectEntryWithNextHops) {
  auto entry = makeMySidEntry("fc00:100::1", 48);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2::2"));
  entry.nextHops() = {nhop};

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "add mysid with nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  // Verify RIB and switchState remain empty
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

TEST_F(RibMySidValidationTest, rejectBothNextHopsAndNamedNextHops) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::NODE_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2::2"));
  entry.nextHops() = {nhop};
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "add mysid with both nexthops and named nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

TEST_F(RibMySidValidationTest, rejectDecapsulateTypeWithNamedNextHops) {
  auto entry = makeMySidEntry("fc00:100::1", 48);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "add decap mysid with named nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

TEST_F(RibMySidValidationTest, rejectNamedNextHopsWithPolicyName) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.policyName() = "policy1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "mysid with policyName in namedNextHops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, rejectBindingSidWithoutNextHopsOrNamedNhg) {
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID),
  };
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          toAdd,
          {},
          "binding sid without nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

TEST_F(RibMySidValidationTest, acceptBindingSidWithNextHops) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop};

  rib_.update(
      scopeResolver(),
      {entry},
      {},
      "binding sid with nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto mySidTable = rib_.getMySidTableCopy();
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  EXPECT_TRUE(mySidTable.at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidValidationTest, rejectBindingSidWithoutSidList) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop};

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "binding sid without sid list",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, rejectBindingSidWithoutTunnelId) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  entry.nextHops() = {nhop};

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "binding sid without tunnel id",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, rejectBindingSidWithWrongTunnelType) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::IP_IN_IP_DECAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop};

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "binding sid with wrong tunnel type",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, acceptBindingSidWithNamedNhg) {
  addNamedNextHopGroup(
      rib_, "group1", makeSrv6NextHops({"2001:db8::1"}), &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  rib_.update(
      scopeResolver(),
      {entry},
      {},
      "binding sid with named nhg",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto mySidTable = rib_.getMySidTableCopy();
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  EXPECT_TRUE(mySidTable.at(prefix).unresolveNextHopsId().has_value());

  const auto manager = rib_.getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(manager->getMySidsForNamedNhg("group1").count(prefix), 1);
}

TEST_F(RibMySidValidationTest, rejectBindingSidWithInvalidNamedNhgNextHops) {
  addNamedNextHopGroup(
      rib_, "group1", makeUnresolvedNextHops({"2001:db8::1"}), &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "binding sid with invalid named nhg nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, namedNhgMySidMappingUpdatedOnAddUpdateDelete) {
  addNamedNextHopGroup(
      rib_, "group1", makeSrv6NextHops({"2001:db8::1"}), &switchState_);
  addNamedNextHopGroup(
      rib_, "group2", makeSrv6NextHops({"2001:db8::2"}), &switchState_);

  auto inlineEntry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  inlineEntry.nextHops() = {makeSrv6NextHop("2001:db8::3")};
  rib_.update(
      scopeResolver(),
      {inlineEntry},
      {},
      "add unrelated inline binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group1"));
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group2"));

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  auto group1Entry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination group1;
  group1.nextHopGroup() = "group1";
  group1Entry.namedNextHops() = group1;

  rib_.update(
      scopeResolver(),
      {group1Entry},
      {},
      "binding sid with named nhg",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(
      rib_.getNextHopIDManagerCopy()->getMySidsForNamedNhg("group1").count(
          prefix),
      1);
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group2"));

  auto group2Entry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination group2;
  group2.nextHopGroup() = "group2";
  group2Entry.namedNextHops() = group2;

  rib_.update(
      scopeResolver(),
      {group2Entry},
      {},
      "replace binding sid named nhg",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group1"));
  EXPECT_EQ(
      rib_.getNextHopIDManagerCopy()->getMySidsForNamedNhg("group2").count(
          prefix),
      1);

  rib_.update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      {toIpPrefix("fc00:100::1", 48)},
      "delete binding sid with named nhg",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group1"));
  EXPECT_FALSE(rib_.getNextHopIDManagerCopy()->hasMySidsForNamedNhg("group2"));
}

TEST_F(RibMySidValidationTest, acceptNodeSidWithNamedNhg) {
  addNamedNextHopGroup(
      rib_, "group1", makeUnresolvedNextHops({"2001:db8::1"}), &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::NODE_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  rib_.update(
      scopeResolver(),
      {entry},
      {},
      "node sid with named nhg",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);
}

TEST_F(RibMySidValidationTest, rejectNodeSidWithMissingNamedNhg) {
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::NODE_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "node sid with missing named nhg",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(
    RibMySidValidationTest,
    rejectBatchWithMissingNamedNhgDoesNotMutateRibOrInlineNextHopRefs) {
  auto inlineEntry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  inlineEntry.nextHops() = {makeSrv6NextHop("2001:db8::1")};
  const auto inlineNextHops =
      util::toRouteNextHopSet(*inlineEntry.nextHops(), true);
  const auto beforeSetId =
      rib_.getNextHopIDManagerCopy()->lookupRouteNextHopSetID(inlineNextHops);

  auto namedEntry =
      makeMySidEntry("fc00:200::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "missing_group";
  namedEntry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {inlineEntry, namedEntry},
          {},
          "binding sid batch with missing named nhg",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  const auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 0);
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:200::1", 48)), mySidTable.end());
  EXPECT_EQ(
      rib_.getNextHopIDManagerCopy()->lookupRouteNextHopSetID(inlineNextHops),
      beforeSetId);
}

TEST_F(RibMySidValidationTest, rejectAdjacencySidWithNamedNhg) {
  auto entry =
      makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "adjacency sid with named nhg",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, rejectAdjacencySidWithMultipleNextHops) {
  auto entry =
      makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  NextHopThrift nhop1;
  nhop1.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  NextHopThrift nhop2;
  nhop2.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::2"));
  entry.nextHops() = {nhop1, nhop2};

  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {entry},
          {},
          "adjacency sid with multiple nexthops",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidValidationTest, acceptAdjacencySidWithSingleNextHop) {
  auto entry =
      makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  entry.nextHops() = {nhop};

  rib_.update(
      scopeResolver(),
      {entry},
      {},
      "adjacency sid with single nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);
}

TEST_F(RibMySidValidationTest, rejectFrrProtectionForMissingMySid) {
  const folly::CIDRNetwork prefix{folly::IPAddress("fc00:100::1"), 48};
  const MySidFrrProtectionUpdate update{prefix, {}};

  EXPECT_THROW(
      rib_.updateMySidFrrProtection(
          scopeResolver(),
          {update},
          {},
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);
  EXPECT_THROW(
      rib_.updateMySidFrrProtection(
          scopeResolver(),
          {},
          {prefix},
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);
}

TEST_F(RibMySidValidationTest, rejectFrrProtectionForNonAdjacencyMySid) {
  rib_.update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add node mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  const folly::CIDRNetwork prefix{folly::IPAddress("fc00:100::1"), 48};
  const MySidFrrProtectionUpdate update{prefix, {}};

  EXPECT_THROW(
      rib_.updateMySidFrrProtection(
          scopeResolver(),
          {update},
          {},
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);
  EXPECT_THROW(
      rib_.updateMySidFrrProtection(
          scopeResolver(),
          {},
          {prefix},
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);
}

TEST_F(
    RibMySidValidationTest,
    invalidEntryInBatchDoesNotPartiallyMutateMySidTable) {
  // Verify that if any entry in toAdd is invalid, mySidFromEntry throws before
  // updateRibMySids is called, leaving mySidTable completely unmodified.
  // Without the pre-build fix, the valid entry (first in the vector) would be
  // written to mySidTable before the invalid entry throws. With the fix,
  // neither entry is written.

  // Add an initial valid entry so the table is non-empty going in
  rib_.update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48)},
      {},
      "add initial mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);

  // Batch: valid entry first, then invalid (NODE_MICRO_SID with no nexthops)
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:200::1", 64), // valid
      makeMySidEntry(
          "fc00:300::1", 48, MySidType::NODE_MICRO_SID), // invalid: no nexthops
  };
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          toAdd,
          {},
          "batch with invalid entry",
          mySidToSwitchStateUpdate,
          &switchState_),
      FbossError);

  // mySidTable must contain only the original entry — the valid entry from
  // the failed batch must not have been inserted
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());
}

TEST_F(RibMySidSwitchStateTest, switchStateUpdatedOnAdd) {
  auto origState = switchState_;

  std::vector<MySidEntry> toAdd = {makeMySidEntry("fc00:100::1", 48)};
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Switch state should have changed
  EXPECT_NE(switchState_, origState);
  // MySid should be in switch state with correct type
  auto mySidNode = switchState_->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(mySidNode, nullptr);
  EXPECT_EQ(mySidNode->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
}

TEST_F(RibMySidSwitchStateTest, switchStateUpdatedOnDelete) {
  // Add two entries
  std::vector<MySidEntry> toAdd = {
      makeMySidEntry("fc00:100::1", 48),
      makeMySidEntry("fc00:200::1", 64),
  };
  rib_.update(
      scopeResolver(),
      toAdd,
      {},
      "add mysids",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 2);

  auto stateBeforeDelete = switchState_;

  // Delete one
  rib_.update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      {toIpPrefix("fc00:100::1", 48)},
      "delete mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Switch state should have changed
  EXPECT_NE(switchState_, stateBeforeDelete);
  // Only one MySid should remain in switch state
  EXPECT_EQ(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);
}

TEST_F(RibMySidRollbackTest, rollbackOnFailedUpdate) {
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);

  // Try to add a MySid entry but fail the HW update
  std::vector<MySidEntry> toAdd = {makeMySidEntry("fc00:100::1", 48)};
  FailMySidUpdate failUpdate;
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          toAdd,
          {},
          "fail mysid update",
          failUpdate,
          &switchState_),
      FbossHwUpdateError);

  // After rollback, mySidTable should be reconstructed from applied state
  // (which is the original empty state), so it should remain empty
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidRollbackTest, rollbackPreservesAppliedStateMySids) {
  // Verify that after a failed update, the RIB mySidTable is reconstructed
  // from the applied state's MySidMap (which may contain MySid entries
  // that were partially programmed to HW).

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);

  // Inject a MySid entry into the applied state during failure
  auto injectedPrefix = makeSidPrefix("fc00:999::1", 48);
  MySidTable injectedMySids;
  injectedMySids[injectedPrefix] = makeMySid(injectedPrefix);

  FailWithMySidInAppliedState failWithInjected(injectedMySids);
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {makeMySidEntry("fc00:100::1", 48)},
          {},
          "fail with injected mysid",
          failWithInjected,
          &switchState_),
      FbossHwUpdateError);

  // RIB mySidTable should be reconstructed from applied state,
  // which has the injected entry (not the entry we tried to add)
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_NE(mySidTable.find(injectedPrefix), mySidTable.end());
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
}

TEST_F(RibMySidRollbackTest, rollbackWithPreExistingEntries) {
  // Verify rollback when the RIB already has MySid entries.
  // After a failed add, the RIB should reflect the applied state's MySids,
  // not the pre-update RIB state.

  // Successfully add an initial MySid entry
  rib_.update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48)},
      {},
      "add initial mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 1);
  EXPECT_NE(switchState_->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);

  // Now fail an update that tries to add a second entry.
  // The applied state contains the original entry (fc00:100::1/48).
  FailMySidUpdate failUpdate;
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          {makeMySidEntry("fc00:200::1", 64)},
          {},
          "fail second add",
          failUpdate,
          &switchState_),
      FbossHwUpdateError);

  // RIB mySidTable should be reconstructed from applied state,
  // which still has only the original entry
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_EQ(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());
}

TEST_F(RibMySidRollbackTest, rollbackDeleteRestoresEntries) {
  // Verify rollback when a delete fails.
  // The applied state still has the entry we tried to delete.

  // Successfully add two MySid entries
  rib_.update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48), makeMySidEntry("fc00:200::1", 64)},
      {},
      "add mysids",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_.getMySidTableCopy().size(), 2);

  // Fail an update that tries to delete one entry.
  // The applied state still has both entries.
  FailMySidUpdate failUpdate;
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          std::vector<MySidEntry>{},
          {toIpPrefix("fc00:100::1", 48)},
          "fail delete",
          failUpdate,
          &switchState_),
      FbossHwUpdateError);

  // RIB mySidTable should be reconstructed from applied state,
  // which still has both entries (delete was not applied to HW)
  auto mySidTable = rib_.getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 2);
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:100::1", 48)), mySidTable.end());
  EXPECT_NE(
      mySidTable.find(makeSidPrefix("fc00:200::1", 64)), mySidTable.end());
}

TEST_F(RibMySidSwitchStateTest, updaterClonesPublishedState) {
  // Verify that MySidMapUpdater clones the state before mutating it.
  // A previous bug had MySidMapUpdater returning a shared_ptr copy (not a
  // clone), which mutated the already-published input state and caused an
  // assertion failure.

  // Build a RIB MySidTable with one entry
  MySidTable mySidTable;
  auto prefix = makeSidPrefix("fc00:100::1", 48);
  mySidTable[prefix] = makeMySid(prefix);

  MySidMapUpdater updater(scopeResolver(), mySidTable);
  auto newState = updater(switchState_);

  // Returned state must be a different object (cloned, not aliased)
  EXPECT_NE(newState.get(), switchState_.get());
  // Original published state must not have been mutated
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
  // New state should have the entry
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
}

TEST_F(RibMySidCrudTest, emptyUpdate) {
  auto origState = switchState_;

  // Empty update should be a no-op
  rib_.update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      std::vector<IpPrefix>{},
      "empty mysid update",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_EQ(rib_.getMySidTableCopy().size(), 0);
  // switchState should not change on empty update
  EXPECT_EQ(switchState_->getMySids()->numNodes(), 0);
}

class RibMySidNextHopTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    rib_ = std::make_unique<RoutingInformationBase>();
    switchState_ = std::make_shared<SwitchState>();
    switchState_->publish();
    rib_->ensureVrf(kRid);
  }

  void TearDown() override {
    FLAGS_enable_nexthop_id_manager = false;
  }

  std::optional<int64_t> getUnresolveId(
      const folly::CIDRNetworkV6& prefix) const {
    const auto table = rib_->getMySidTableCopy();
    auto it = table.find(prefix);
    if (it == table.end()) {
      return std::nullopt;
    }
    if (auto v = it->second.unresolveNextHopsId()) {
      return *v;
    }
    return std::nullopt;
  }

  std::vector<MySidWithNextHops> makePair(
      const std::string& addr,
      uint8_t len,
      MySidType type = MySidType::DECAPSULATE_AND_LOOKUP,
      RouteNextHopSet nhops = {}) {
    auto mySid = makeMySid(makeSidPrefix(addr, len), type);
    mySid->setClientId(ClientID::STATIC_ROUTE);
    return {{std::move(mySid), std::move(nhops), std::nullopt}};
  }

  std::unique_ptr<RoutingInformationBase> rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(RibMySidNextHopTest, newEntryWithNextHopsAllocatesNextHopSetId) {
  // Adding a MySid entry with next hops should allocate a NextHopSetID.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto mySidTable = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  EXPECT_TRUE(mySidTable.at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, replaceMySidWithSameNextHopsReusesNextHopSetId) {
  // Replacing a MySid entry with identical next hops should reuse the same ID.
  const std::vector<std::string> nextHops = {"2001:db8::1"};
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, nextHops)},
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterFirst = rib_->getMySidTableCopy();
  const auto idAfterFirstAdd =
      tableAfterFirst.at(makeSidPrefix("fc00:100::1", 48))
          .unresolveNextHopsId();
  ASSERT_TRUE(idAfterFirstAdd.has_value());

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, nextHops)},
      {},
      "re-add mysid same nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterSecond = rib_->getMySidTableCopy();
  const auto idAfterSecondAdd =
      tableAfterSecond.at(makeSidPrefix("fc00:100::1", 48))
          .unresolveNextHopsId();
  EXPECT_EQ(idAfterFirstAdd, idAfterSecondAdd);
}

TEST_F(
    RibMySidNextHopTest,
    replaceMySidWithDifferentNextHopsUpdatesNextHopSetId) {
  // Replacing a MySid entry with different next hops should allocate a new ID.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterFirst = rib_->getMySidTableCopy();
  const auto idAfterFirstAdd =
      tableAfterFirst.at(makeSidPrefix("fc00:100::1", 48))
          .unresolveNextHopsId();
  ASSERT_TRUE(idAfterFirstAdd.has_value());

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops(
          "fc00:100::1", 48, {"2001:db8::3", "2001:db8::4"})},
      {},
      "re-add mysid different nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterSecond = rib_->getMySidTableCopy();
  const auto idAfterSecondAdd =
      tableAfterSecond.at(makeSidPrefix("fc00:100::1", 48))
          .unresolveNextHopsId();
  ASSERT_TRUE(idAfterSecondAdd.has_value());
  EXPECT_NE(idAfterFirstAdd, idAfterSecondAdd);
}

TEST_F(RibMySidNextHopTest, replaceMySidWithNoNextHopsClearsNextHopSetId) {
  // Replacing a MySid entry with an entry that has no next hops should clear
  // unresolveNextHopsId and release the old ID without crash.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_TRUE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());

  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP)},
      {},
      "replace mysid with no nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, deleteMySidWithNextHopsReleasesNextHopSetId) {
  // Deleting a MySid entry that has next hops should release the ID.
  // Verified by re-adding the same entry after deletion — no crash expected.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_TRUE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());

  rib_->update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      {toIpPrefix("fc00:100::1", 48)},
      "delete mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_EQ(rib_->getMySidTableCopy().count(prefix), 0);

  // Re-add after delete should succeed and allocate a fresh ID.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "re-add mysid after delete",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_TRUE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(
    RibMySidNextHopTest,
    updateFibFailureRestoresMySidTableAndNextHopIdManager) {
  // Add entry A with nexthops — succeeds, allocates ID_A in the manager
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid A",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefixA = makeSidPrefix("fc00:100::1", 48);

  // Fail adding entry B — FailMySidUpdate uses the current switchState_ (with
  // A) as the applied state, so rollback should restore to A-only state
  FailMySidUpdate failUpdate;
  EXPECT_THROW(
      rib_->update(
          scopeResolver(),
          {makeMySidEntryWithNextHops("fc00:200::1", 64, {"2001:db8::2"})},
          {},
          "fail adding mysid B",
          failUpdate,
          &switchState_),
      FbossHwUpdateError);

  // mySidTable should be restored to only contain A
  const auto mySidTable = rib_->getMySidTableCopy();
  EXPECT_EQ(mySidTable.size(), 1);
  EXPECT_NE(mySidTable.find(prefixA), mySidTable.end());

  // A subsequent successful add of B confirms the manager fully recovered
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:200::1", 64, {"2001:db8::2"})},
      {},
      "add mysid B after recovery",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefixB = makeSidPrefix("fc00:200::1", 64);
  const auto tableAfter = rib_->getMySidTableCopy();
  EXPECT_EQ(tableAfter.size(), 2);
  const auto idBOpt = tableAfter.at(prefixB).unresolveNextHopsId();
  EXPECT_TRUE(idBOpt.has_value());
}

TEST_F(
    RibMySidNextHopTest,
    addMySidWithGatewayNextHop_noRoutes_resolvedSetIdNotSet) {
  // Gateway nexthop (no interface ID) with no matching VRF 0 route → resolve
  // finds nothing, so resolvedNextHopsId should remain unset.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with unresolvable gateway nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());
}

TEST_F(
    RibMySidNextHopTest,
    addMySidWithGatewayNextHop_withMatchingRoute_resolvedSetIdSet) {
  // Inject a resolved interface route covering the gateway nexthop's address
  // via reconfigure, then add the MySid entry → resolvedNextHopsId is set.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with resolvable gateway nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  EXPECT_TRUE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, namedNhgMySidResolvesLikeInlineNextHops) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  const auto nextHops = makeSrv6NextHops({"2001:db8::1"});
  addNamedNextHopGroup(*rib_, "group1", nextHops, &switchState_);

  auto inlineEntry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  inlineEntry.nextHops() = {makeSrv6NextHop("2001:db8::1")};

  auto namedEntry =
      makeMySidEntry("fc00:200::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  namedEntry.namedNextHops() = named;

  rib_->update(
      scopeResolver(),
      {inlineEntry, namedEntry},
      {},
      "add inline and named nhg binding sids",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto inlinePrefix = makeSidPrefix("fc00:100::1", 48);
  const auto namedPrefix = makeSidPrefix("fc00:200::1", 48);
  const auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_NE(mySidTable.find(inlinePrefix), mySidTable.end());
  ASSERT_NE(mySidTable.find(namedPrefix), mySidTable.end());

  const auto inlineResolvedId =
      mySidTable.at(inlinePrefix).resolvedNextHopsId();
  const auto namedResolvedId = mySidTable.at(namedPrefix).resolvedNextHopsId();
  ASSERT_TRUE(inlineResolvedId.has_value());
  ASSERT_TRUE(namedResolvedId.has_value());

  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto inlineResolvedNextHops =
      manager->getNextHops(NextHopSetID(*inlineResolvedId));
  const auto namedResolvedNextHops =
      manager->getNextHops(NextHopSetID(*namedResolvedId));
  EXPECT_EQ(inlineResolvedNextHops, namedResolvedNextHops);
  ASSERT_EQ(namedResolvedNextHops.size(), 1);

  const auto& resolvedNextHop = *namedResolvedNextHops.begin();
  EXPECT_EQ(resolvedNextHop.addr(), folly::IPAddress("2001:db8::1"));
  ASSERT_TRUE(resolvedNextHop.intfID().has_value());
  EXPECT_EQ(*resolvedNextHop.intfID(), InterfaceID(1));
  const auto segmentList = resolvedNextHop.srv6SegmentList();
  EXPECT_EQ(segmentList.size(), 1);
  EXPECT_EQ(segmentList[0], folly::IPAddressV6("2001:db8::10"));
  ASSERT_TRUE(resolvedNextHop.tunnelType().has_value());
  EXPECT_EQ(*resolvedNextHop.tunnelType(), TunnelType::SRV6_ENCAP);
  ASSERT_TRUE(resolvedNextHop.tunnelId().has_value());
  EXPECT_EQ(*resolvedNextHop.tunnelId(), kSrv6Tunnel0);
}

TEST_F(RibMySidNextHopTest, namedNhgBindingSidReresolvesWhenRouteChanges) {
  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      {makeOpenRRoute(
          "2001:db8::", 32, {{"fe80::1", "fboss1"}, {"fe80::2", "fboss2"}})},
      std::vector<IpPrefix>{},
      false,
      "add openr route",
      noopFibUpdate,
      &switchState_);

  addNamedNextHopGroup(
      *rib_, "group1", makeSrv6NextHops({"2001:db8::1"}), &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add named nhg binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto initialMySid = rib_->getMySidTableCopy().at(prefix);
  const auto initialResolvedId = initialMySid.resolvedNextHopsId();
  ASSERT_TRUE(initialResolvedId.has_value());
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto initialResolvedNextHops =
      manager->getNextHops(NextHopSetID(*initialResolvedId));
  ASSERT_EQ(initialResolvedNextHops.size(), 2);
  for (const auto& nhop : initialResolvedNextHops) {
    ASSERT_TRUE(nhop.intfID().has_value());
  }
  EXPECT_EQ(
      getNextHopAddrs(initialResolvedNextHops),
      std::set<folly::IPAddress>(
          {folly::IPAddress("fe80::1"), folly::IPAddress("fe80::2")}));

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      {makeOpenRRoute(
          "2001:db8::", 32, {{"fe80::3", "fboss3"}, {"fe80::4", "fboss4"}})},
      std::vector<IpPrefix>{},
      false,
      "update openr route nexthops",
      noopFibUpdate,
      &switchState_);

  const auto updatedMySid = rib_->getMySidTableCopy().at(prefix);
  const auto updatedResolvedId = updatedMySid.resolvedNextHopsId();
  ASSERT_TRUE(updatedResolvedId.has_value());
  EXPECT_NE(updatedResolvedId, initialResolvedId);

  manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto updatedResolvedNextHops =
      manager->getNextHops(NextHopSetID(*updatedResolvedId));
  ASSERT_EQ(updatedResolvedNextHops.size(), 2);

  for (const auto& nhop : updatedResolvedNextHops) {
    ASSERT_TRUE(nhop.intfID().has_value());
    const auto segmentList = nhop.srv6SegmentList();
    EXPECT_EQ(segmentList.size(), 1);
    EXPECT_EQ(segmentList[0], folly::IPAddressV6("2001:db8::10"));
    ASSERT_TRUE(nhop.tunnelType().has_value());
    EXPECT_EQ(*nhop.tunnelType(), TunnelType::SRV6_ENCAP);
    ASSERT_TRUE(nhop.tunnelId().has_value());
    EXPECT_EQ(*nhop.tunnelId(), kSrv6Tunnel0);
  }
  EXPECT_EQ(
      getNextHopAddrs(updatedResolvedNextHops),
      std::set<folly::IPAddress>(
          {folly::IPAddress("fe80::3"), folly::IPAddress("fe80::4")}));
}

TEST_F(RibMySidNextHopTest, updateNamedNhgReresolvesBindingSid) {
  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      {makeOpenRRoute(
           "2001:db8::", 32, {{"fe80::1", "fboss1"}, {"fe80::2", "fboss2"}}),
       makeOpenRRoute(
           "2001:db9::", 32, {{"fe80::3", "fboss3"}, {"fe80::4", "fboss4"}})},
      std::vector<IpPrefix>{},
      false,
      "add openr routes",
      noopFibUpdate,
      &switchState_);

  addNamedNextHopGroup(
      *rib_, "group1", makeSrv6NextHops({"2001:db8::1"}), &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add named nhg binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto initialMySid = rib_->getMySidTableCopy().at(prefix);
  const auto initialUnresolvedId = initialMySid.unresolveNextHopsId();
  const auto initialResolvedId = initialMySid.resolvedNextHopsId();
  ASSERT_TRUE(initialUnresolvedId.has_value());
  ASSERT_TRUE(initialResolvedId.has_value());

  addNamedNextHopGroup(
      *rib_, "group1", makeSrv6NextHops({"2001:db9::1"}), &switchState_);

  const auto updatedMySid = rib_->getMySidTableCopy().at(prefix);
  const auto updatedUnresolvedId = updatedMySid.unresolveNextHopsId();
  const auto updatedResolvedId = updatedMySid.resolvedNextHopsId();
  ASSERT_TRUE(updatedUnresolvedId.has_value());
  ASSERT_TRUE(updatedResolvedId.has_value());
  EXPECT_NE(updatedUnresolvedId, initialUnresolvedId);
  EXPECT_NE(updatedResolvedId, initialResolvedId);

  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      getNextHopAddrs(manager->getNextHops(NextHopSetID(*updatedUnresolvedId))),
      std::set<folly::IPAddress>{folly::IPAddress("2001:db9::1")});

  const auto updatedResolvedNextHops =
      manager->getNextHops(NextHopSetID(*updatedResolvedId));
  ASSERT_EQ(updatedResolvedNextHops.size(), 2);
  EXPECT_EQ(
      getNextHopAddrs(updatedResolvedNextHops),
      std::set<folly::IPAddress>(
          {folly::IPAddress("fe80::3"), folly::IPAddress("fe80::4")}));
  for (const auto& nhop : updatedResolvedNextHops) {
    ASSERT_TRUE(nhop.intfID().has_value());
    const auto segmentList = nhop.srv6SegmentList();
    EXPECT_EQ(segmentList.size(), 1);
    EXPECT_EQ(segmentList[0], folly::IPAddressV6("2001:db8::10"));
    ASSERT_TRUE(nhop.tunnelType().has_value());
    EXPECT_EQ(*nhop.tunnelType(), TunnelType::SRV6_ENCAP);
    ASSERT_TRUE(nhop.tunnelId().has_value());
    EXPECT_EQ(*nhop.tunnelId(), kSrv6Tunnel0);
  }
}

TEST_F(
    RibMySidNextHopTest,
    invalidNamedNhgUpdateDoesNotMutateBindingSidOrGroup) {
  const auto originalNextHops = makeSrv6NextHops({"2001:db8::1"});
  addNamedNextHopGroup(*rib_, "group1", originalNextHops, &switchState_);

  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NamedRouteDestination named;
  named.nextHopGroup() = "group1";
  entry.namedNextHops() = named;

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add named nhg binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto beforeMySidTable = rib_->getMySidTableCopy();
  const auto beforeManager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(beforeManager, nullptr);
  EXPECT_EQ(beforeManager->getNextHopsForName("group1"), originalNextHops);

  EXPECT_THROW(
      addNamedNextHopGroup(
          *rib_,
          "group1",
          makeUnresolvedNextHops({"2001:db8::2"}),
          &switchState_),
      FbossError);

  EXPECT_EQ(rib_->getMySidTableCopy(), beforeMySidTable);
  const auto afterManager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(afterManager, nullptr);
  EXPECT_EQ(afterManager->getNextHopsForName("group1"), originalNextHops);
  EXPECT_EQ(
      afterManager->getMySidsForNamedNhg("group1"),
      beforeManager->getMySidsForNamedNhg("group1"));
}

// Replacing next hops on a MySid entry should:
// 1. Deallocate the old NextHopSetID (getNextHopsIf returns nullopt)
// 2. Keep the new NextHopSetID alive (getNextHopsIf returns non-null)
TEST_F(
    RibMySidNextHopTest,
    replaceNextHopsDecrementsOldAndIncrementsNewRefCount) {
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops(
          "fc00:100::1", 48, {"2001:db8::1", "2001:db8::2"})},
      {},
      "add mysid with nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterFirst = rib_->getMySidTableCopy();
  const auto oldIdOpt = tableAfterFirst.at(makeSidPrefix("fc00:100::1", 48))
                            .unresolveNextHopsId();
  ASSERT_TRUE(oldIdOpt.has_value());
  const auto oldId = NextHopSetID(*oldIdOpt);

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops(
          "fc00:100::1", 48, {"2001:db8::3", "2001:db8::4"})},
      {},
      "replace mysid with different nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto tableAfterSecond = rib_->getMySidTableCopy();
  const auto newIdOpt = tableAfterSecond.at(makeSidPrefix("fc00:100::1", 48))
                            .unresolveNextHopsId();
  ASSERT_TRUE(newIdOpt.has_value());
  const auto newId = NextHopSetID(*newIdOpt);

  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  // Old next hop set should have been deallocated (refcount dropped to 0)
  EXPECT_FALSE(manager->getNextHopsIf(oldId).has_value());
  // New next hop set should still be allocated
  EXPECT_TRUE(manager->getNextHopsIf(newId).has_value());
}

// Replacing a uN MySid (with both unresolved and resolved next-hop IDs)
// with a decap MySid (no next hops) must decrement the manager's
// reference for BOTH the old unresolveNextHopsId and the old
// resolvedNextHopsId.
//
// The resolvedId for a route-resolved gateway is shared with the matching
// route's own next-hop set, so the SetID entry stays alive after the MySid
// release (held by the route). We pin the decrement contract via the
// per-NextHop refcount: each MySid set contributes +1 to each NextHop in
// its set, so after replacing the MySid the resolved NextHop's refcount
// must drop by 1. Without the fix the MySid's reference is never
// released, the NextHop refcount stays flat — that's the leak.
TEST_F(
    RibMySidNextHopTest,
    replaceNodeMySidWithDecapReleasesBothUnresolvedAndResolvedIds) {
  // Inject an interface route that the gateway nexthop will resolve via.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  // Add a NODE_MICRO_SID with a gateway next hop that resolves through the
  // interface route — this populates BOTH unresolveNextHopsId and
  // resolvedNextHopsId on the entry.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add uN mysid with resolvable gateway nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto entryBefore = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(entryBefore.unresolveNextHopsId().has_value());
  ASSERT_TRUE(entryBefore.resolvedNextHopsId().has_value())
      << "Test setup: NODE_MICRO_SID gateway should have resolved through "
         "the interface route";

  // Capture per-NextHop refcounts before the replace. The interface route
  // contributes 1 ref to the resolved nexthop; the MySid's resolved set
  // contributes a second ref. The unresolved gateway nexthop is held only
  // by the MySid's unresolved set.
  const ResolvedNextHop resolvedNextHop{
      folly::IPAddress("2001:db8::1"), InterfaceID(1), NextHopWeight(1)};
  const UnresolvedNextHop unresolvedNextHop{
      folly::IPAddress("2001:db8::1"), ECMP_WEIGHT};
  uint32_t resolvedRefBefore = 0;
  uint32_t unresolvedRefBefore = 0;
  {
    auto manager = rib_->getNextHopIDManagerCopy();
    ASSERT_NE(manager, nullptr);
    resolvedRefBefore = manager->getNextHopRefCount(resolvedNextHop);
    unresolvedRefBefore = manager->getNextHopRefCount(unresolvedNextHop);
    EXPECT_GE(resolvedRefBefore, 2u)
        << "Test setup: resolved NextHop should be shared by route + MySid";
    EXPECT_GE(unresolvedRefBefore, 1u)
        << "Test setup: unresolved gateway NextHop should be held by MySid";
  }

  // Replace with a DECAP entry (no next hops at all). The fix releases
  // both of the old MySid's NextHop set references.
  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48, MySidType::DECAPSULATE_AND_LOOKUP)},
      {},
      "replace with decap mysid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // The new entry should have neither id set.
  const auto entryAfter = rib_->getMySidTableCopy().at(prefix);
  EXPECT_FALSE(entryAfter.unresolveNextHopsId().has_value());
  EXPECT_FALSE(entryAfter.resolvedNextHopsId().has_value());

  // Both NextHop refcounts must have decreased by exactly one. Without
  // the fix, resolvedRefAfter would equal resolvedRefBefore — the leak.
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(manager->getNextHopRefCount(resolvedNextHop), resolvedRefBefore - 1)
      << "Resolved NextHop refcount didn't drop on MySid replace — "
         "resolvedNextHopsId leak persists";
  EXPECT_EQ(
      manager->getNextHopRefCount(unresolvedNextHop), unresolvedRefBefore - 1)
      << "Unresolved NextHop refcount didn't drop on MySid replace";
}

// Test fixture that sets up a SwitchState with a FibInfo node so that
// SwitchStateNextHopIdUpdater (called via RibToSwitchStateUpdater with
// UPDATE_MYSID) can populate FibInfo's id2NextHopSet maps.
class RibMySidFibInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    rib_ = std::make_unique<RoutingInformationBase>();
    switchState_ = std::make_shared<SwitchState>();
    // Add a FibInfo node so SwitchStateNextHopIdUpdater has a node to write to.
    auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    fibsInfoMap->addNode("id=0", std::make_shared<FibInfo>());
    switchState_->resetFibsInfoMap(fibsInfoMap);
    switchState_->publish();
    rib_->ensureVrf(kRid);
  }

  std::shared_ptr<FibInfo> getFibInfo() {
    return switchState_->getFibsInfoMap()->getNodeIf("id=0");
  }

  // Returns the IdToNextHopIdSetMap from the FibInfo node in the SwitchState.
  std::shared_ptr<IdToNextHopIdSetMap> getIdToNextHopIdSetMap() {
    auto fibInfo = getFibInfo();
    return fibInfo ? fibInfo->getIdToNextHopIdSetMap() : nullptr;
  }

  // Connected route that every gateway used by the FRR tests below
  // ultimately resolves over.
  void addInterfaceRoute() {
    RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
    interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
        InterfaceID(1), folly::IPAddress("2001:db8::ffff")};
    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {} /* staticMySids */,
        noopFibUpdate,
        &switchState_);
  }

  void addAdjacencyMySid(const std::string& addr, uint8_t len) {
    rib_->update(
        scopeResolver(),
        {makeMySidEntry(addr, len, MySidType::ADJACENCY_MICRO_SID)},
        {},
        "add adjacency mysid",
        mySidToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  void addAdjacencyMySidWithNextHops(
      const std::string& addr,
      uint8_t len,
      const std::vector<std::string>& nextHopAddrs) {
    rib_->update(
        scopeResolver(),
        {makeMySidEntryWithNextHops(
            addr, len, nextHopAddrs, MySidType::ADJACENCY_MICRO_SID)},
        {},
        "add adjacency mysid with next hops",
        mySidToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  void updateGatewayRoute(
      const std::string& prefixAddr,
      uint8_t prefixLen,
      const std::vector<std::string>& gateways) {
    rib_->update(
        scopeResolver(),
        kRid,
        ClientID::OPENR,
        AdminDistance::OPENR,
        {makeGatewayRoute(prefixAddr, prefixLen, gateways)},
        std::vector<IpPrefix>{},
        false,
        "update gateway route",
        routeToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  void deleteGatewayRoute(const std::string& prefixAddr, uint8_t prefixLen) {
    rib_->update(
        scopeResolver(),
        kRid,
        ClientID::OPENR,
        AdminDistance::OPENR,
        std::vector<UnicastRoute>{},
        {toIpPrefix(prefixAddr, prefixLen)},
        false,
        "delete gateway route",
        routeToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  // Backup next hops SwitchState currently has programmed for `mySidId`, as
  // (address, interface) pairs. Empty when the MySid carries no resolved
  // backup next hop set.
  NextHopAddrs getSwitchStateBackupNextHops(const std::string& mySidId) {
    const auto stateMySid = switchState_->getMySids()->getNodeIf(mySidId);
    EXPECT_NE(stateMySid, nullptr);
    if (!stateMySid) {
      return {};
    }
    const auto resolvedId = stateMySid->getBackupResolvedNextHopsId();
    if (!resolvedId.has_value()) {
      return {};
    }
    NextHopAddrs nextHops;
    for (const auto& nextHop : getFibInfo()->resolveNextHopSetFromId(
             static_cast<NextHopSetId>(*resolvedId))) {
      EXPECT_EQ(nextHop.role(), NextHopRole::BACKUP);
      EXPECT_TRUE(nextHop.isResolved());
      if (nextHop.isResolved()) {
        nextHops.emplace(nextHop.addr(), nextHop.intf());
      }
    }
    return nextHops;
  }

  std::unique_ptr<RoutingInformationBase> rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(RibMySidFibInfoTest, mySidNextHopSetIdReflectedInFibInfo) {
  // Adding a MySid entry with next hops allocates a NextHopSetID.  Verify that
  // the allocated ID is written into FibInfo->id2NextHopIdSet by
  // SwitchStateNextHopIdUpdater (called through RibToSwitchStateUpdater with
  // UPDATE_MYSID).
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with nexthops",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  const auto unresolvedId = mySidTable.at(prefix).unresolveNextHopsId();
  ASSERT_TRUE(unresolvedId.has_value());

  // The allocated NextHopSetID must appear in FibInfo's id2NextHopIdSet.
  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(*unresolvedId), nullptr)
      << "MySid unresolvedNextHopsId not found in FibInfo id2NextHopIdSet";
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsReflectedInSwitchState) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::ffff")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID)},
      {},
      "add adjacency mysid",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto firstBackupNextHops =
      makeBackupNextHops({"2001:db8::1", "2001:db8::2"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, firstBackupNextHops}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_TRUE(mySidTable.at(prefix).backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(mySidTable.at(prefix).backupResolvedNextHopsId().has_value());
  const auto firstBackupId = *mySidTable.at(prefix).backupUnresolveNextHopsId();
  const auto firstResolvedBackupId =
      *mySidTable.at(prefix).backupResolvedNextHopsId();
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(firstBackupId)), firstBackupNextHops);
  const auto firstResolvedBackupNextHops = makeResolvedBackupNextHops(
      {"2001:db8::1", "2001:db8::2"}, InterfaceID(1));
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(firstResolvedBackupId)),
      firstResolvedBackupNextHops);

  auto stateMySid = switchState_->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_EQ(
      stateMySid->getBackupUnresolveNextHopsId(), NextHopSetID(firstBackupId));
  EXPECT_EQ(
      stateMySid->getBackupResolvedNextHopsId(),
      NextHopSetID(firstResolvedBackupId));
  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(firstBackupId), nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(firstResolvedBackupId), nullptr);
  const auto firstSwitchStateBackupNextHops =
      getFibInfo()->resolveNextHopSetFromId(
          static_cast<NextHopSetId>(firstBackupId));
  EXPECT_EQ(
      RouteNextHopSet(
          firstSwitchStateBackupNextHops.begin(),
          firstSwitchStateBackupNextHops.end()),
      firstBackupNextHops);
  const auto firstSwitchStateResolvedBackupNextHops =
      getFibInfo()->resolveNextHopSetFromId(
          static_cast<NextHopSetId>(firstResolvedBackupId));
  EXPECT_EQ(
      RouteNextHopSet(
          firstSwitchStateResolvedBackupNextHops.begin(),
          firstSwitchStateResolvedBackupNextHops.end()),
      firstResolvedBackupNextHops);

  const auto secondBackupNextHops = makeBackupNextHops({"2001:db8::3"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, secondBackupNextHops}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  mySidTable = rib_->getMySidTableCopy();
  ASSERT_TRUE(mySidTable.at(prefix).backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(mySidTable.at(prefix).backupResolvedNextHopsId().has_value());
  const auto secondBackupId =
      *mySidTable.at(prefix).backupUnresolveNextHopsId();
  const auto secondResolvedBackupId =
      *mySidTable.at(prefix).backupResolvedNextHopsId();
  EXPECT_NE(firstBackupId, secondBackupId);
  EXPECT_NE(firstResolvedBackupId, secondResolvedBackupId);
  manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(firstBackupId)).has_value());
  EXPECT_FALSE(
      manager->getNextHopsIf(NextHopSetID(firstResolvedBackupId)).has_value());
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(secondBackupId)), secondBackupNextHops);
  const auto secondResolvedBackupNextHops =
      makeResolvedBackupNextHops({"2001:db8::3"}, InterfaceID(1));
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(secondResolvedBackupId)),
      secondResolvedBackupNextHops);

  stateMySid = switchState_->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_EQ(
      stateMySid->getBackupUnresolveNextHopsId(), NextHopSetID(secondBackupId));
  EXPECT_EQ(
      stateMySid->getBackupResolvedNextHopsId(),
      NextHopSetID(secondResolvedBackupId));
  idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(firstBackupId), nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(firstResolvedBackupId), nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(secondBackupId), nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(secondResolvedBackupId), nullptr);
  const auto secondSwitchStateBackupNextHops =
      getFibInfo()->resolveNextHopSetFromId(
          static_cast<NextHopSetId>(secondBackupId));
  EXPECT_EQ(
      RouteNextHopSet(
          secondSwitchStateBackupNextHops.begin(),
          secondSwitchStateBackupNextHops.end()),
      secondBackupNextHops);
  const auto secondSwitchStateResolvedBackupNextHops =
      getFibInfo()->resolveNextHopSetFromId(
          static_cast<NextHopSetId>(secondResolvedBackupId));
  EXPECT_EQ(
      RouteNextHopSet(
          secondSwitchStateResolvedBackupNextHops.begin(),
          secondSwitchStateResolvedBackupNextHops.end()),
      secondResolvedBackupNextHops);
}

TEST_F(RibMySidFibInfoTest, deleteMySidFrrClearsBackupNextHops) {
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID)},
      {},
      "add adjacency mysid",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto backupNextHops = makeBackupNextHops({"2001:db8::1"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, backupNextHops}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);
  const auto mySidTableWithBackup = rib_->getMySidTableCopy();
  ASSERT_TRUE(
      mySidTableWithBackup.at(prefix).backupUnresolveNextHopsId().has_value());
  const auto backupId =
      *mySidTableWithBackup.at(prefix).backupUnresolveNextHopsId();

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {},
      {prefix},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  EXPECT_FALSE(mySidTable.at(prefix).backupUnresolveNextHopsId().has_value());
  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(backupId)).has_value());

  const auto stateMySid =
      switchState_->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_FALSE(stateMySid->getBackupUnresolveNextHopsId().has_value());
  const auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(backupId), nullptr);
}

TEST_F(RibMySidFibInfoTest, deleteMySidFrrClearsResolvedBackupNextHops) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::ffff")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID)},
      {},
      "add adjacency mysid",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, makeBackupNextHops({"2001:db8::1"})}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto mySidWithBackup = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidWithBackup.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(mySidWithBackup.backupResolvedNextHopsId().has_value());
  const auto backupId = *mySidWithBackup.backupUnresolveNextHopsId();
  const auto resolvedBackupId = *mySidWithBackup.backupResolvedNextHopsId();

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {},
      {prefix},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto mySid = rib_->getMySidTableCopy().at(prefix);
  EXPECT_FALSE(mySid.backupUnresolveNextHopsId().has_value());
  EXPECT_FALSE(mySid.backupResolvedNextHopsId().has_value());

  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(backupId)).has_value());
  EXPECT_FALSE(
      manager->getNextHopsIf(NextHopSetID(resolvedBackupId)).has_value());

  const auto stateMySid =
      switchState_->getMySids()->getNodeIf("fc00:100::1/48");
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_FALSE(stateMySid->getBackupUnresolveNextHopsId().has_value());
  EXPECT_FALSE(stateMySid->getBackupResolvedNextHopsId().has_value());

  const auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(backupId), nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(resolvedBackupId), nullptr);
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsFollowRouteLifecycle) {
  // Backup next hops programmed before any covering route exists must reach
  // SwitchState unresolved, then track the route through resolution,
  // re-resolution and withdrawal with no further FRR API call.
  const std::string kMySidId{"fc00:100::1/48"};
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  addAdjacencyMySid("fc00:100::1", 48);

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, makeBackupNextHops({"2001:dba::1"})}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  // No route covers the backup gateway yet.
  auto mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupUnresolveNextHopsId().has_value());
  EXPECT_FALSE(mySid.backupResolvedNextHopsId().has_value());
  const auto unresolvedId = *mySid.backupUnresolveNextHopsId();
  auto stateMySid = switchState_->getMySids()->getNodeIf(kMySidId);
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_EQ(
      stateMySid->getBackupUnresolveNextHopsId(), NextHopSetID(unresolvedId));
  EXPECT_FALSE(stateMySid->getBackupResolvedNextHopsId().has_value());
  EXPECT_TRUE(getSwitchStateBackupNextHops(kMySidId).empty());

  // A route covering the gateway resolves the backup next hops.
  updateGatewayRoute("2001:dba::", 32, {"2001:db8::1"});

  mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto resolvedId = *mySid.backupResolvedNextHopsId();
  const NextHopAddrs viaFirstGateway{
      {folly::IPAddress("2001:db8::1"), InterfaceID(1)}};
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      getBackupNextHops(*manager, NextHopSetID(resolvedId)), viaFirstGateway);
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), viaFirstGateway);
  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(resolvedId), nullptr);

  // Re-pointing the route re-resolves the backup next hops.
  updateGatewayRoute("2001:dba::", 32, {"2001:db8::2"});

  mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto reresolvedId = *mySid.backupResolvedNextHopsId();
  EXPECT_NE(reresolvedId, resolvedId);
  const NextHopAddrs viaSecondGateway{
      {folly::IPAddress("2001:db8::2"), InterfaceID(1)}};
  manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      getBackupNextHops(*manager, NextHopSetID(reresolvedId)),
      viaSecondGateway);
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), viaSecondGateway);
  idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(resolvedId), nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(reresolvedId), nullptr);

  // Withdrawing the route unresolves the backup next hops again. The
  // requested (unresolved) set is untouched -- only resolution changed.
  deleteGatewayRoute("2001:dba::", 32);

  mySid = rib_->getMySidTableCopy().at(prefix);
  EXPECT_EQ(mySid.backupUnresolveNextHopsId(), unresolvedId);
  EXPECT_FALSE(mySid.backupResolvedNextHopsId().has_value());
  manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(reresolvedId)).has_value());
  stateMySid = switchState_->getMySids()->getNodeIf(kMySidId);
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_FALSE(stateMySid->getBackupResolvedNextHopsId().has_value());
  EXPECT_TRUE(getSwitchStateBackupNextHops(kMySidId).empty());
  idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(reresolvedId), nullptr);
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsResolvedByPreexistingRoute) {
  // When the covering route is already in the RIB, updateMySidFrrProtection
  // resolves the backup next hops inline rather than leaving them for a later
  // route update.
  const std::string kMySidId{"fc00:100::1/48"};
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  updateGatewayRoute("2001:dba::", 32, {"2001:db8::1"});
  addAdjacencyMySid("fc00:100::1", 48);

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, makeBackupNextHops({"2001:dba::1"})}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto resolvedId = *mySid.backupResolvedNextHopsId();
  const NextHopAddrs expected{
      {folly::IPAddress("2001:db8::1"), InterfaceID(1)}};
  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(getBackupNextHops(*manager, NextHopSetID(resolvedId)), expected);

  const auto stateMySid = switchState_->getMySids()->getNodeIf(kMySidId);
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_EQ(
      stateMySid->getBackupResolvedNextHopsId(), NextHopSetID(resolvedId));
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), expected);
  const auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(resolvedId), nullptr);
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupInstallLeavesResolvedPrimaryIntact) {
  // Installing FRR protection re-resolves the MySid's primary next hops too,
  // since RibMySidUpdater has no notion that only the backup changed. That
  // must be a no-op: a new primary resolvedNextHopsId would clone the MySid,
  // and the resulting SwitchState delta tears the entry -- and the primary
  // path it is meant to protect -- out of hardware and rebuilds it.
  const std::string kMySidId{"fc00:100::1/48"};
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  updateGatewayRoute("2001:dba::", 32, {"2001:db8::1"});
  addAdjacencyMySidWithNextHops("fc00:100::1", 48, {"2001:db8::2"});

  const auto beforeMySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(beforeMySid.resolvedNextHopsId().has_value());
  EXPECT_FALSE(beforeMySid.backupResolvedNextHopsId().has_value());
  const auto primaryId = *beforeMySid.resolvedNextHopsId();
  const NextHopAddrs primaryNextHops{
      {folly::IPAddress("2001:db8::2"), InterfaceID(1)}};
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  ASSERT_EQ(
      getNextHopsForRole(
          *manager, NextHopSetID(primaryId), NextHopRole::PRIMARY),
      primaryNextHops);

  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, makeBackupNextHops({"2001:dba::1"})}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto afterMySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(afterMySid.resolvedNextHopsId().has_value());
  EXPECT_EQ(*afterMySid.resolvedNextHopsId(), primaryId);
  manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      getNextHopsForRole(
          *manager, NextHopSetID(primaryId), NextHopRole::PRIMARY),
      primaryNextHops);

  // The backup was installed, so this is not a no-op overall -- only the
  // primary is untouched.
  ASSERT_TRUE(afterMySid.backupResolvedNextHopsId().has_value());
  EXPECT_NE(*afterMySid.backupResolvedNextHopsId(), primaryId);
  const NextHopAddrs backupNextHops{
      {folly::IPAddress("2001:db8::1"), InterfaceID(1)}};
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), backupNextHops);

  const auto stateMySid = switchState_->getMySids()->getNodeIf(kMySidId);
  ASSERT_NE(stateMySid, nullptr);
  EXPECT_EQ(stateMySid->getResolvedNextHopsId(), NextHopSetID(primaryId));
  const auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(primaryId), nullptr);
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsSurvivePrimaryRebind) {
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  addAdjacencyMySidWithNextHops("fc00:100::1", 48, {"2001:db8::1"});

  const auto backupNextHops = makeBackupNextHops({"2001:db8::2"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, backupNextHops}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto before = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(before.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(before.backupResolvedNextHopsId().has_value());
  const auto backupUnresolvedId = *before.backupUnresolveNextHopsId();
  const auto backupResolvedId = *before.backupResolvedNextHopsId();

  // Mirror MySidNeighborObserver::queueResolve(): preserve FRR backup IDs
  // while replacing the primary neighbor binding.
  auto reboundMySid = std::make_shared<MySid>(before);
  reboundMySid->setUnresolveNextHopsId(std::nullopt);
  reboundMySid->setResolvedNextHopsId(std::nullopt);
  rib_->update(
      scopeResolver(),
      std::vector<MySidWithNextHops>{
          {std::move(reboundMySid),
           makeUnresolvedNextHops({"2001:db8::3"}),
           std::nullopt}},
      {},
      {},
      "rebind adjacency mysid primary",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto after = rib_->getMySidTableCopy().at(prefix);
  EXPECT_EQ(after.backupUnresolveNextHopsId(), backupUnresolvedId);
  EXPECT_EQ(after.backupResolvedNextHopsId(), backupResolvedId);
  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(backupUnresolvedId)), backupNextHops);
  EXPECT_TRUE(
      manager->getNextHopsIf(NextHopSetID(backupResolvedId)).has_value());
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsSurvivePrimaryUnresolve) {
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  addAdjacencyMySidWithNextHops("fc00:100::1", 48, {"2001:db8::1"});

  const auto backupNextHops = makeBackupNextHops({"2001:db8::2"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, backupNextHops}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto before = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(before.unresolveNextHopsId().has_value());
  ASSERT_TRUE(before.resolvedNextHopsId().has_value());
  ASSERT_TRUE(before.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(before.backupResolvedNextHopsId().has_value());
  const auto backupUnresolvedId = *before.backupUnresolveNextHopsId();
  const auto backupResolvedId = *before.backupResolvedNextHopsId();

  rib_->update(
      scopeResolver(),
      {},
      {{prefix, folly::IPAddress("2001:db8::1")}},
      {},
      "unresolve adjacency mysid primary",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto after = rib_->getMySidTableCopy().at(prefix);
  EXPECT_FALSE(after.unresolveNextHopsId().has_value());
  EXPECT_FALSE(after.resolvedNextHopsId().has_value());
  EXPECT_EQ(after.backupUnresolveNextHopsId(), backupUnresolvedId);
  EXPECT_EQ(after.backupResolvedNextHopsId(), backupResolvedId);
  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(backupUnresolvedId)), backupNextHops);
  EXPECT_TRUE(
      manager->getNextHopsIf(NextHopSetID(backupResolvedId)).has_value());
}

TEST_F(RibMySidFibInfoTest, mySidFrrBackupNextHopsTrackPartialResolution) {
  // With two backup gateways the resolved set holds exactly those gateways
  // that currently have a covering route, and follows routes appearing and
  // being withdrawn.
  const std::string kMySidId{"fc00:100::1/48"};
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  addInterfaceRoute();
  updateGatewayRoute("2001:dba::", 32, {"2001:db8::1"});
  addAdjacencyMySid("fc00:100::1", 48);

  // Only 2001:dba::1 is routable; 2001:dbb::1 has no covering route.
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, makeBackupNextHops({"2001:dba::1", "2001:dbb::1"})}},
      {},
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const NextHopAddrs viaFirstGateway{
      {folly::IPAddress("2001:db8::1"), InterfaceID(1)}};
  auto mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto partiallyResolvedId = *mySid.backupResolvedNextHopsId();
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), viaFirstGateway);

  // The second gateway becomes routable -- both backup next hops resolve.
  updateGatewayRoute("2001:dbb::", 32, {"2001:db8::2"});

  const NextHopAddrs viaBothGateways{
      {folly::IPAddress("2001:db8::1"), InterfaceID(1)},
      {folly::IPAddress("2001:db8::2"), InterfaceID(1)}};
  mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto fullyResolvedId = *mySid.backupResolvedNextHopsId();
  EXPECT_NE(fullyResolvedId, partiallyResolvedId);
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), viaBothGateways);

  // Withdrawing the first route drops that gateway back out of the set.
  deleteGatewayRoute("2001:dba::", 32);

  const NextHopAddrs viaSecondGateway{
      {folly::IPAddress("2001:db8::2"), InterfaceID(1)}};
  mySid = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySid.backupResolvedNextHopsId().has_value());
  const auto reducedId = *mySid.backupResolvedNextHopsId();
  EXPECT_NE(reducedId, fullyResolvedId);
  EXPECT_EQ(getSwitchStateBackupNextHops(kMySidId), viaSecondGateway);
  const auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(fullyResolvedId), nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(reducedId), nullptr);
}

TEST_F(RibMySidFibInfoTest, deleteMySidClearsFibInfoNextHopSetId) {
  // After deleting a MySid entry, its NextHopSetID should be removed from
  // FibInfo->id2NextHopIdSet.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto tableAfterAdd = rib_->getMySidTableCopy();
  ASSERT_NE(tableAfterAdd.find(prefix), tableAfterAdd.end());
  const auto idAfterAdd = tableAfterAdd.at(prefix).unresolveNextHopsId();
  ASSERT_TRUE(idAfterAdd.has_value());

  rib_->update(
      scopeResolver(),
      std::vector<MySidEntry>{},
      {toIpPrefix("fc00:100::1", 48)},
      "delete mysid",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  // After deletion the NextHopSetID must be absent from FibInfo.
  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->getNextHopIdSetIf(*idAfterAdd), nullptr)
      << "MySid NextHopSetId should have been removed from FibInfo after delete";
}

TEST_F(RibMySidFibInfoTest, mySidOnlyNextHopSetIdsInFibInfo) {
  // With no routes present, only MySid-allocated NextHopSetIDs should appear
  // in FibInfo id2NextHopIdSet (i.e. they are not shared with any route).
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops(
          "fc00:100::1", 48, {"2001:db8::1", "2001:db8::2"})},
      {},
      "add mysid with two nexthops",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  const auto unresolvedId = mySidTable.at(prefix).unresolveNextHopsId();
  ASSERT_TRUE(unresolvedId.has_value());

  // FibInfo should have exactly one NextHopSetID entry — the MySid's unresolved
  // set — confirming it is not shared with any route.
  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_EQ(idSetMap->size(), 1u);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(*unresolvedId), nullptr);
}

TEST_F(RibMySidFibInfoTest, resolvedNextHopSetIdReflectedInFibInfo) {
  // When a MySid entry's gateway nexthop can be resolved via an interface
  // route, both unresolvedNextHopsId and resolvedNextHopsId should appear in
  // FibInfo->id2NextHopIdSet.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with resolvable gateway nexthop",
      mySidToSwitchStateUpdateViaRibUpdater,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto mySidTable = rib_->getMySidTableCopy();
  ASSERT_NE(mySidTable.find(prefix), mySidTable.end());
  const auto unresolvedId = mySidTable.at(prefix).unresolveNextHopsId();
  const auto resolvedId = mySidTable.at(prefix).resolvedNextHopsId();
  ASSERT_TRUE(unresolvedId.has_value());
  ASSERT_TRUE(resolvedId.has_value());

  auto idSetMap = getIdToNextHopIdSetMap();
  ASSERT_NE(idSetMap, nullptr);
  EXPECT_NE(idSetMap->getNextHopIdSetIf(*unresolvedId), nullptr)
      << "MySid unresolvedNextHopsId not found in FibInfo id2NextHopIdSet";
  EXPECT_NE(idSetMap->getNextHopIdSetIf(*resolvedId), nullptr)
      << "MySid resolvedNextHopsId not found in FibInfo id2NextHopIdSet";
}

TEST_F(RibMySidNextHopTest, routeUpdateResolvesMySidNextHops) {
  // Add a MySid entry whose nexthop can't be resolved yet (no route exists).
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with unresolvable nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_TRUE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());
  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());

  // Add an interface route covering the MySid nexthop address.
  // This route update must trigger re-resolution of MySid entries.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 64}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::2")};
  rib_->updateRemoteInterfaceRoutes(
      scopeResolver(), interfaceRoutes, {}, noopFibUpdate, &switchState_);

  EXPECT_TRUE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, routeDeleteUnresolvesMySidNextHops) {
  // Add an interface route and a MySid whose nexthop resolves through it.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 64}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::2")};
  rib_->updateRemoteInterfaceRoutes(
      scopeResolver(), interfaceRoutes, {}, noopFibUpdate, &switchState_);

  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("fc00:100::1", 48, {"2001:db8::1"})},
      {},
      "add mysid with resolvable nexthop",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  ASSERT_TRUE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());

  // Delete the interface route. MySid nexthop should become unresolved.
  boost::container::flat_map<
      RouterID,
      std::vector<std::pair<folly::CIDRNetwork, InterfaceID>>>
      toDel;
  toDel[kRid].emplace_back(
      folly::CIDRNetwork{folly::IPAddress("2001:db8::"), 64}, InterfaceID(1));
  rib_->updateRemoteInterfaceRoutes(
      scopeResolver(), {}, toDel, noopFibUpdate, &switchState_);

  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).resolvedNextHopsId().has_value());
}

// Tests for MySid programming via reconfigure() — ensures config-driven
// MySid entries passed through reconfigure reach the RIB mySidTable.

TEST_F(RibMySidNextHopTest, reconfigureAddsMySidToRibTable) {
  auto mySidConfig = makeDecapMySidConfig(0x100);

  rib_->reconfigure(
      scopeResolver(),
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_EQ(table.at(prefix).type().value(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_EQ(table.at(prefix).clientId().value(), ClientID::STATIC_ROUTE);
}

TEST_F(RibMySidNextHopTest, reconfigureDeletesMySidFromRibTable) {
  // Keep kRid alive across both reconfigure calls — production callers
  // always pass at least one VRF in configRouterIDToInterfaceRoutes.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  auto mySidConfig = makeDecapMySidConfig(0x100);
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);
  {
    const auto tableAfterAdd = rib_->getMySidTableCopy();
    ASSERT_NE(
        tableAfterAdd.find(makeSidPrefix("3001:db8:100::", 48)),
        tableAfterAdd.end());
  }

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  const auto tableAfterDelete = rib_->getMySidTableCopy();
  EXPECT_EQ(
      tableAfterDelete.find(makeSidPrefix("3001:db8:100::", 48)),
      tableAfterDelete.end());
}

TEST_F(RibMySidNextHopTest, reconfigureMySidWithNextHopsAllocatesId) {
  auto mySidConfig = makeNodeMySidConfig(0x100, "2001:db8::1");

  rib_->reconfigure(
      scopeResolver(),
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_TRUE(table.at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, reconfigureMySidAndRoutesTogetherBothInstalled) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("10.0.0.0"), 24}] = {
      InterfaceID(1), folly::IPAddress("10.0.0.1")};

  auto mySidConfig = makeDecapMySidConfig(0x100);

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_EQ(table.at(prefix).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_NE(
      rib_->longestMatch<folly::IPAddressV4>(
          folly::IPAddressV4("10.0.0.1"), kRid),
      nullptr);
}

TEST_F(RibMySidNextHopTest, reconfigureNodeMySidWithRouteResolved) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::1")};

  auto mySidConfig = makeNodeMySidConfig(0x100, "2001:db8::1");

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_TRUE(table.at(prefix).unresolveNextHopsId().has_value());
  // Route is present, so MySid should be resolved via LPM
  EXPECT_TRUE(table.at(prefix).resolvedNextHopsId().has_value());
}

// Non-STATIC_ROUTE MySid entries (default ClientID = TE_AGENT) must not be
// removed by a reconfigure() that supplies an empty mySidConfig.
TEST_F(RibMySidNextHopTest, reconfigurePreservesTeAgentMySid) {
  // Add an entry via the RPC path (clientId defaults to TE_AGENT).
  rib_->update(
      scopeResolver(),
      {makeMySidEntry("fc00:200::1", 48)},
      {},
      "add te_agent mysid",
      mySidToSwitchStateUpdate,
      &switchState_);
  ASSERT_NE(
      rib_->getMySidTableCopy().find(makeSidPrefix("fc00:200::1", 48)),
      rib_->getMySidTableCopy().end());

  // reconfigure with no mySidConfig should clear STATIC_ROUTE only — leave
  // the TE_AGENT entry intact.
  rib_->reconfigure(
      scopeResolver(),
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  EXPECT_NE(table.find(makeSidPrefix("fc00:200::1", 48)), table.end());
}

// Reconfigure with the same prefix but a different MySid type — second
// reconfigure must overwrite the first entry, not coexist or skip.
TEST_F(RibMySidNextHopTest, reconfigureUpdatesMySidType) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  // First reconfigure: install DECAP entry.
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(makeDecapMySidConfig(0x100), {}),
      noopFibUpdate,
      &switchState_);
  {
    const auto table = rib_->getMySidTableCopy();
    const auto prefix = makeSidPrefix("3001:db8:100::", 48);
    ASSERT_NE(table.find(prefix), table.end());
    EXPECT_EQ(
        table.at(prefix).type().value(), MySidType::DECAPSULATE_AND_LOOKUP);
    EXPECT_FALSE(table.at(prefix).unresolveNextHopsId().has_value());
  }

  // Second reconfigure: same prefix (functionId 0x100) but NODE type with
  // a next hop. Must replace the DECAP entry and allocate a new nhopId.
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(makeNodeMySidConfig(0x100, "2001:db8::1"), {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_EQ(table.at(prefix).type().value(), MySidType::NODE_MICRO_SID);
  EXPECT_EQ(table.at(prefix).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_TRUE(table.at(prefix).unresolveNextHopsId().has_value());
}

// Reconfigure with multiple STATIC entries, then with a subset — only the
// removed prefix is gone; the kept prefix survives.
TEST_F(RibMySidNextHopTest, reconfigurePartiallyDeletesMySid) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  // Install two DECAP entries.
  cfg::MySidConfig twoEntries;
  twoEntries.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::MySidEntryConfig decap;
  decap.set_decap(cfg::DecapMySidConfig{});
  twoEntries.entries()->emplace(0x100, decap);
  twoEntries.entries()->emplace(0x200, decap);

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(twoEntries, {}),
      noopFibUpdate,
      &switchState_);
  {
    const auto table = rib_->getMySidTableCopy();
    EXPECT_NE(table.find(makeSidPrefix("3001:db8:100::", 48)), table.end());
    EXPECT_NE(table.find(makeSidPrefix("3001:db8:200::", 48)), table.end());
  }

  // Reconfigure with only one of the two entries — the dropped one must
  // disappear, the kept one must remain.
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(makeDecapMySidConfig(0x100), {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:100::", 48)), table.end());
  EXPECT_EQ(table.find(makeSidPrefix("3001:db8:200::", 48)), table.end());
}

// Reconfigure with multiple MySid entries of mixed types in a single call —
// every entry should land with the correct type and clientId.
TEST_F(RibMySidNextHopTest, reconfigureMultipleMySidEntries) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  cfg::MySidConfig multiConfig;
  multiConfig.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::MySidEntryConfig decap;
  decap.set_decap(cfg::DecapMySidConfig{});
  multiConfig.entries()->emplace(0x100, decap);
  multiConfig.entries()->emplace(0x200, decap);

  cfg::NodeMySidConfig node;
  node.nodeAddress() = "2001:db8::1";
  cfg::MySidEntryConfig nodeEntry;
  nodeEntry.set_node(std::move(node));
  multiConfig.entries()->emplace(0x300, std::move(nodeEntry));

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(multiConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto p100 = makeSidPrefix("3001:db8:100::", 48);
  const auto p200 = makeSidPrefix("3001:db8:200::", 48);
  const auto p300 = makeSidPrefix("3001:db8:300::", 48);
  ASSERT_NE(table.find(p100), table.end());
  ASSERT_NE(table.find(p200), table.end());
  ASSERT_NE(table.find(p300), table.end());
  EXPECT_EQ(table.at(p100).type().value(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_EQ(table.at(p200).type().value(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_EQ(table.at(p300).type().value(), MySidType::NODE_MICRO_SID);
  EXPECT_EQ(table.at(p100).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_EQ(table.at(p200).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_EQ(table.at(p300).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_FALSE(table.at(p100).unresolveNextHopsId().has_value());
  EXPECT_FALSE(table.at(p200).unresolveNextHopsId().has_value());
  EXPECT_TRUE(table.at(p300).unresolveNextHopsId().has_value());
}

// Re-applying an identical mySidConfig must NOT churn next-hop IDs: the
// existing entry should be left in place (same unresolveNextHopsId) and
// no new id should be allocated for the same nhop set. This is the
// no-op-replace contract that prevents unnecessary HW deltas + nhop-id
// waste on every reconfigure.
TEST_F(RibMySidNextHopTest, reconfigureIdenticalMySidIsNoOp) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  auto mySidConfig = makeNodeMySidConfig(0x100, "2001:db8::1");

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  const auto idBefore = getUnresolveId(prefix);
  ASSERT_TRUE(idBefore.has_value());

  // Re-apply the exact same config. The id must be preserved.
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(mySidConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto idAfter = getUnresolveId(prefix);
  ASSERT_TRUE(idAfter.has_value());
  EXPECT_EQ(*idAfter, *idBefore)
      << "Identical reconfigure churned the unresolveNextHopsId — "
         "applyStaticMySids should leave unchanged entries alone";
}

// Mixed reconfigure: changing only one of several entries should churn
// only that one. The unchanged entry's id stays put; the changed entry's
// id is reallocated.
TEST_F(RibMySidNextHopTest, reconfigureOnlyChangedMySidChurnsId) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  // Install two NODE entries.
  cfg::MySidConfig firstConfig;
  firstConfig.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::NodeMySidConfig nodeA;
  nodeA.nodeAddress() = "2001:db8::1";
  cfg::MySidEntryConfig entryA;
  entryA.set_node(nodeA);
  firstConfig.entries()->emplace(0x100, entryA);

  cfg::NodeMySidConfig nodeB;
  nodeB.nodeAddress() = "2001:db8::2";
  cfg::MySidEntryConfig entryB;
  entryB.set_node(nodeB);
  firstConfig.entries()->emplace(0x200, entryB);

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(firstConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto p100 = makeSidPrefix("3001:db8:100::", 48);
  const auto p200 = makeSidPrefix("3001:db8:200::", 48);
  const auto idAOldOpt = getUnresolveId(p100);
  const auto idBOldOpt = getUnresolveId(p200);
  ASSERT_TRUE(idAOldOpt.has_value());
  ASSERT_TRUE(idBOldOpt.has_value());

  // Re-apply with entry A unchanged but entry B's nodeAddress flipped.
  cfg::MySidConfig secondConfig;
  secondConfig.locatorPrefix() = kTestLocatorPrefix.str();
  secondConfig.entries()->emplace(0x100, entryA);
  cfg::NodeMySidConfig nodeBNew;
  nodeBNew.nodeAddress() = "2001:db8::99";
  cfg::MySidEntryConfig entryBNew;
  entryBNew.set_node(nodeBNew);
  secondConfig.entries()->emplace(0x200, entryBNew);

  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(secondConfig, {}),
      noopFibUpdate,
      &switchState_);

  const auto idANewOpt = getUnresolveId(p100);
  const auto idBNewOpt = getUnresolveId(p200);
  ASSERT_TRUE(idANewOpt.has_value());
  ASSERT_TRUE(idBNewOpt.has_value());
  // A is unchanged → same id.
  EXPECT_EQ(*idANewOpt, *idAOldOpt);
  // B changed → its old id is gone (released) and a new id allocated.
  EXPECT_NE(*idBNewOpt, *idBOldOpt);
  // Old id for B should have been deallocated from the manager.
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(*idBOldOpt)).has_value())
      << "Old next-hop set for changed entry B was not released";
}

// Identical DECAP reconfigure (no next hops) must hit the "both empty"
// branch of mySidEntryUnchanged and be a no-op — covers the 0-nhop fast
// path that the NODE-only no-op test does not exercise.
TEST_F(RibMySidNextHopTest, reconfigureIdenticalDecapMySidIsNoOp) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};
  auto mySidConfig = makeDecapMySidConfig(0x100);
  auto applyOnce = [&]() {
    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        convertMySidConfig(mySidConfig, {}),
        noopFibUpdate,
        &switchState_);
  };
  applyOnce();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  const auto table1 = rib_->getMySidTableCopy();
  ASSERT_NE(table1.find(prefix), table1.end());
  const auto type1 = table1.at(prefix).type().value();

  applyOnce();
  const auto table2 = rib_->getMySidTableCopy();
  ASSERT_NE(table2.find(prefix), table2.end());
  EXPECT_EQ(table2.at(prefix).type().value(), type1);
  EXPECT_FALSE(table2.at(prefix).unresolveNextHopsId().has_value());
}

// Reconfigure that flips isV6 on the same prefix must churn the entry,
// not be skipped — covers the isV6 branch of mySidEntryUnchanged.
TEST_F(RibMySidNextHopTest, reconfigureChangedIsV6ChurnsEntry) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};
  auto mkConfigWithIsV6 = [&](bool isV6) {
    cfg::MySidConfig cfg;
    cfg.locatorPrefix() = kTestLocatorPrefix.str();
    cfg::AdjacencyMySidConfig adj;
    adj.portName() = "Port-Channel301";
    adj.isV6() = isV6;
    cfg::MySidEntryConfig entry;
    entry.set_adjacency(std::move(adj));
    cfg.entries()->emplace(0x100, std::move(entry));
    return cfg;
  };
  // portName -> InterfaceID map: convertMySidConfig requires it for adjacency.
  std::unordered_map<std::string, InterfaceID> portMap{
      {"Port-Channel301", InterfaceID(301)}};
  auto applyWithIsV6 = [&](bool isV6) {
    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        convertMySidConfig(mkConfigWithIsV6(isV6), portMap),
        noopFibUpdate,
        &switchState_);
  };
  applyWithIsV6(true);
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  ASSERT_NE(
      rib_->getMySidTableCopy().find(prefix), rib_->getMySidTableCopy().end());
  EXPECT_TRUE(rib_->getMySidTableCopy().at(prefix).isV6().value_or(false));

  applyWithIsV6(false);
  EXPECT_FALSE(rib_->getMySidTableCopy().at(prefix).isV6().value_or(true))
      << "Changing isV6 should churn the entry; smart diff should NOT skip";
}

// Reconfigure that flips adjacencyInterfaceId on the same prefix must
// churn the entry — covers the adjacencyInterfaceId branch.
TEST_F(RibMySidNextHopTest, reconfigureChangedAdjacencyInterfaceIdChurnsEntry) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};
  auto mkConfigWithPort = [&](const std::string& portName) {
    cfg::MySidConfig cfg;
    cfg.locatorPrefix() = kTestLocatorPrefix.str();
    cfg::AdjacencyMySidConfig adj;
    adj.portName() = portName;
    adj.isV6() = true;
    cfg::MySidEntryConfig entry;
    entry.set_adjacency(std::move(adj));
    cfg.entries()->emplace(0x100, std::move(entry));
    return cfg;
  };
  std::unordered_map<std::string, InterfaceID> portMap{
      {"Port-Channel301", InterfaceID(301)},
      {"Port-Channel302", InterfaceID(302)}};
  auto applyWithPort = [&](const std::string& portName) {
    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        convertMySidConfig(mkConfigWithPort(portName), portMap),
        noopFibUpdate,
        &switchState_);
  };
  applyWithPort("Port-Channel301");
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  EXPECT_EQ(
      rib_->getMySidTableCopy().at(prefix).adjacencyInterfaceId().value_or(0),
      301);

  applyWithPort("Port-Channel302");
  EXPECT_EQ(
      rib_->getMySidTableCopy().at(prefix).adjacencyInterfaceId().value_or(0),
      302)
      << "Changing adjacencyInterfaceId should churn the entry";
}

TEST_F(
    RibMySidNextHopTest,
    reconfigureAdjacencyMySidPreservesFrrBackupNextHops) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
      InterfaceID(1), folly::IPAddress("2001:db8::ffff")};

  cfg::MySidConfig mySidConfig;
  mySidConfig.locatorPrefix() = kTestLocatorPrefix.str();
  cfg::AdjacencyMySidConfig adjacency;
  adjacency.portName() = "Port-Channel301";
  adjacency.isV6() = true;
  cfg::MySidEntryConfig entry;
  entry.set_adjacency(std::move(adjacency));
  mySidConfig.entries()->emplace(0x100, std::move(entry));
  const std::unordered_map<std::string, InterfaceID> portMap{
      {"Port-Channel301", InterfaceID(301)}};
  const auto staticMySids = convertMySidConfig(mySidConfig, portMap);
  auto reconfigure = [&]() {
    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        staticMySids,
        noopFibUpdate,
        &switchState_);
  };

  reconfigure();
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  const auto primaryNextHops = makeUnresolvedNextHops({"2001:db8::1"});
  auto reboundMySid =
      std::make_shared<MySid>(rib_->getMySidTableCopy().at(prefix));
  rib_->update(
      scopeResolver(),
      std::vector<MySidWithNextHops>{
          {std::move(reboundMySid), primaryNextHops, std::nullopt}},
      {},
      {},
      "resolve adjacency mysid primary",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto backupNextHops = makeBackupNextHops({"2001:db8::2"});
  rib_->updateMySidFrrProtection(
      scopeResolver(),
      {{prefix, backupNextHops}},
      {},
      mySidToSwitchStateUpdate,
      &switchState_);
  const auto before = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(before.unresolveNextHopsId().has_value());
  ASSERT_TRUE(before.resolvedNextHopsId().has_value());
  ASSERT_TRUE(before.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(before.backupResolvedNextHopsId().has_value());
  const auto primaryUnresolvedId = *before.unresolveNextHopsId();
  const auto primaryResolvedId = *before.resolvedNextHopsId();
  const auto backupUnresolvedId = *before.backupUnresolveNextHopsId();
  const auto backupResolvedId = *before.backupResolvedNextHopsId();

  // Same-config reconciliation must preserve the dynamic primary and backup
  // bindings.
  reconfigure();

  const auto after = rib_->getMySidTableCopy().at(prefix);
  EXPECT_EQ(after.unresolveNextHopsId(), primaryUnresolvedId);
  EXPECT_EQ(after.resolvedNextHopsId(), primaryResolvedId);
  EXPECT_EQ(after.backupUnresolveNextHopsId(), backupUnresolvedId);
  EXPECT_EQ(after.backupResolvedNextHopsId(), backupResolvedId);
  const auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(primaryUnresolvedId)), primaryNextHops);
  EXPECT_TRUE(
      manager->getNextHopsIf(NextHopSetID(primaryResolvedId)).has_value());
  EXPECT_EQ(
      manager->getNextHops(NextHopSetID(backupUnresolvedId)), backupNextHops);
  EXPECT_TRUE(
      manager->getNextHopsIf(NextHopSetID(backupResolvedId)).has_value());
}

// Pass 2's "non-STATIC_ROUTE entry at same CIDR" overwrite path. Existing
// reconfigurePreservesTeAgentMySid uses a different prefix and so does
// not exercise this branch. Here we put a TE_AGENT entry (with nhops) at
// the exact prefix the next config reconfigure targets, and verify:
//   - the old TE_AGENT entry's nhop id is released (no leak)
//   - the new entry has STATIC_ROUTE clientId
TEST_F(RibMySidNextHopTest, reconfigureOverwritesTeAgentAtSamePrefix) {
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid] = {};

  // 1. Install a TE_AGENT (default clientId) entry with nhops at the prefix
  //    that the next reconfigure will target.
  rib_->update(
      scopeResolver(),
      {makeMySidEntryWithNextHops("3001:db8:100::", 48, {"2001:db8::1"})},
      {},
      "seed te_agent at config-target prefix",
      mySidToSwitchStateUpdate,
      &switchState_);
  const auto prefix = makeSidPrefix("3001:db8:100::", 48);
  const auto teAgentIdOpt = getUnresolveId(prefix);
  ASSERT_TRUE(teAgentIdOpt.has_value())
      << "Test setup: TE_AGENT entry should have an unresolveNextHopsId";

  // 2. Reconfigure with a STATIC_ROUTE DECAP entry at the same prefix —
  //    Pass 2 in applyStaticMySids should release the TE_AGENT entry's id
  //    and install the DECAP entry.
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      convertMySidConfig(makeDecapMySidConfig(0x100), {}),
      noopFibUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  ASSERT_NE(table.find(prefix), table.end());
  EXPECT_EQ(table.at(prefix).clientId().value(), ClientID::STATIC_ROUTE);
  EXPECT_EQ(table.at(prefix).type().value(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_FALSE(table.at(prefix).unresolveNextHopsId().has_value());

  // The TE_AGENT entry's old id must have been released (refcount 1 → 0).
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  EXPECT_FALSE(manager->getNextHopsIf(NextHopSetID(*teAgentIdOpt)).has_value())
      << "Pass 2 silently leaked the overwritten TE_AGENT entry's nhop id";
}

TEST_F(RibMySidNextHopTest, bindingSidResolvesOverOpenrRouteRetainsSrv6Fields) {
  // Step 1: Inject interface routes for the link-local subnet so link-local
  // nexthops are valid.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("fe80::"), 64}] = {
      InterfaceID(1), folly::IPAddress("fe80::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  // Step 2: Add an OpenR route for 2001:db8::/32 with a link-local nexthop.
  // This simulates how OpenR programs routes — the nexthop is link-local
  // with an interface scope.
  UnicastRoute openrRoute;
  IpPrefix routePrefix;
  routePrefix.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::"));
  routePrefix.prefixLength() = 32;
  openrRoute.dest() = routePrefix;
  NextHopThrift llNhop;
  auto llAddr =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::1"));
  llAddr.ifName() = "fboss1";
  llNhop.address() = llAddr;
  openrRoute.nextHops() = {llNhop};

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      std::vector<UnicastRoute>{openrRoute},
      std::vector<IpPrefix>{},
      false,
      "add openr route with link-local nexthop",
      noopFibUpdate,
      &switchState_);

  // Step 3: Add a BINDING_MICRO_SID with a gateway nexthop carrying SRV6
  // fields. The gateway 2001:db8::1 resolves recursively through the OpenR
  // route above, whose link-local nexthop fe80::1 is already resolved.
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10")),
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::20"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop};

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add binding sid with srv6 nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify both unresolved and resolved IDs are set.
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  const auto mySidEntry = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidEntry.unresolveNextHopsId().has_value());
  ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());

  // Verify the resolved nexthop is the link-local address from the OpenR
  // route but retains the SRV6 fields from the binding SID's original nexthop.
  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto resolvedNhops =
      manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
  ASSERT_TRUE(resolvedNhops.has_value());
  ASSERT_EQ(resolvedNhops->size(), 1);

  const auto& resolvedNh = *resolvedNhops->begin();
  EXPECT_TRUE(resolvedNh.intfID().has_value());
  EXPECT_EQ(resolvedNh.addr(), folly::IPAddress("fe80::1"));

  const std::vector<folly::IPAddressV6> expectedSidList = {
      folly::IPAddressV6("2001:db8::10"), folly::IPAddressV6("2001:db8::20")};
  EXPECT_EQ(resolvedNh.srv6SegmentList(), expectedSidList);
  ASSERT_TRUE(resolvedNh.tunnelType().has_value());
  EXPECT_EQ(*resolvedNh.tunnelType(), TunnelType::SRV6_ENCAP);
  ASSERT_TRUE(resolvedNh.tunnelId().has_value());
  EXPECT_EQ(*resolvedNh.tunnelId(), kSrv6Tunnel0);
}

TEST_F(
    RibMySidNextHopTest,
    bindingSidTwoNextHopsEachResolveOverTwoLinkLocalNextHops) {
  // Set up interface routes for two link-local subnets on two interfaces.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("fe80::"), 64}] = {
      InterfaceID(1), folly::IPAddress("fe80::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  // Add an OpenR route for 2001:db8:1::/48 with two link-local nexthops.
  UnicastRoute openrRoute1;
  IpPrefix prefix1;
  prefix1.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:1::"));
  prefix1.prefixLength() = 48;
  openrRoute1.dest() = prefix1;
  NextHopThrift llNhop1a;
  auto llAddr1a =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::1"));
  llAddr1a.ifName() = "fboss1";
  llNhop1a.address() = llAddr1a;
  NextHopThrift llNhop1b;
  auto llAddr1b =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::2"));
  llAddr1b.ifName() = "fboss2";
  llNhop1b.address() = llAddr1b;
  openrRoute1.nextHops() = {llNhop1a, llNhop1b};

  // Add an OpenR route for 2001:db8:2::/48 with two link-local nexthops.
  UnicastRoute openrRoute2;
  IpPrefix prefix2;
  prefix2.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:2::"));
  prefix2.prefixLength() = 48;
  openrRoute2.dest() = prefix2;
  NextHopThrift llNhop2a;
  auto llAddr2a =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::3"));
  llAddr2a.ifName() = "fboss3";
  llNhop2a.address() = llAddr2a;
  NextHopThrift llNhop2b;
  auto llAddr2b =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::4"));
  llAddr2b.ifName() = "fboss4";
  llNhop2b.address() = llAddr2b;
  openrRoute2.nextHops() = {llNhop2a, llNhop2b};

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      std::vector<UnicastRoute>{openrRoute1, openrRoute2},
      std::vector<IpPrefix>{},
      false,
      "add openr routes with link-local nexthops",
      noopFibUpdate,
      &switchState_);

  // Add a BINDING_MICRO_SID with two gateway nexthops, each with its own
  // SID list and tunnel ID. Each gateway resolves through one of the OpenR
  // routes above, each of which has 2 link-local nexthops → 4 total.
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop1;
  nhop1.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:1::1"));
  nhop1.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop1.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop1.tunnelId() = kSrv6Tunnel0;
  NextHopThrift nhop2;
  nhop2.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:2::1"));
  nhop2.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::20"))};
  nhop2.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop2.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop1, nhop2};

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add binding sid with two srv6 nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto sidPrefix = makeSidPrefix("fc00:100::1", 48);
  const auto mySidEntry = rib_->getMySidTableCopy().at(sidPrefix);
  ASSERT_TRUE(mySidEntry.unresolveNextHopsId().has_value());
  ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());

  auto manager = rib_->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  const auto resolvedNhops =
      manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
  ASSERT_TRUE(resolvedNhops.has_value());
  ASSERT_EQ(resolvedNhops->size(), 4);

  // Build expected: each of the 2 original nexthops fans out to 2 link-local
  // nexthops, each retaining the parent's SRV6 fields.
  const std::vector<folly::IPAddressV6> sidList1 = {
      folly::IPAddressV6("2001:db8::10")};
  const std::vector<folly::IPAddressV6> sidList2 = {
      folly::IPAddressV6("2001:db8::20")};

  std::map<
      folly::IPAddress,
      std::pair<std::vector<folly::IPAddressV6>, std::string>>
      expectedByAddr;
  expectedByAddr[folly::IPAddress("fe80::1")] = {sidList1, kSrv6Tunnel0};
  expectedByAddr[folly::IPAddress("fe80::2")] = {sidList1, kSrv6Tunnel0};
  expectedByAddr[folly::IPAddress("fe80::3")] = {sidList2, kSrv6Tunnel0};
  expectedByAddr[folly::IPAddress("fe80::4")] = {sidList2, kSrv6Tunnel0};

  for (const auto& nh : *resolvedNhops) {
    EXPECT_TRUE(nh.intfID().has_value());
    auto it = expectedByAddr.find(nh.addr());
    ASSERT_NE(it, expectedByAddr.end()) << "Unexpected nexthop: " << nh.addr();
    EXPECT_EQ(nh.srv6SegmentList(), it->second.first);
    ASSERT_TRUE(nh.tunnelType().has_value());
    EXPECT_EQ(*nh.tunnelType(), TunnelType::SRV6_ENCAP);
    ASSERT_TRUE(nh.tunnelId().has_value());
    EXPECT_EQ(*nh.tunnelId(), it->second.second);
    expectedByAddr.erase(it);
  }
  EXPECT_TRUE(expectedByAddr.empty());
}

TEST_F(RibMySidNextHopTest, bindingSidReResolvesWhenOpenrRouteNextHopsChange) {
  // Step 1: Interface routes for link-local subnets.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("fe80::"), 64}] = {
      InterfaceID(1), folly::IPAddress("fe80::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  // Step 2: OpenR route for 2001:db8::/32 with two link-local nexthops.
  UnicastRoute openrRoute;
  IpPrefix routePrefix;
  routePrefix.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::"));
  routePrefix.prefixLength() = 32;
  openrRoute.dest() = routePrefix;
  NextHopThrift llNhop1;
  auto llAddr1 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::1"));
  llAddr1.ifName() = "fboss1";
  llNhop1.address() = llAddr1;
  NextHopThrift llNhop2;
  auto llAddr2 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::2"));
  llAddr2.ifName() = "fboss2";
  llNhop2.address() = llAddr2;
  openrRoute.nextHops() = {llNhop1, llNhop2};

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      std::vector<UnicastRoute>{openrRoute},
      std::vector<IpPrefix>{},
      false,
      "add openr route",
      noopFibUpdate,
      &switchState_);

  // Step 3: Add a BINDING_MICRO_SID with a gateway nexthop carrying SRV6
  // fields. Resolves through the OpenR route → 2 link-local nexthops.
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop;
  nhop.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::1"));
  nhop.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop};

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify initial resolution: 2 nexthops (fe80::1, fe80::2).
  const auto prefix = makeSidPrefix("fc00:100::1", 48);
  {
    const auto mySidEntry = rib_->getMySidTableCopy().at(prefix);
    ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());
    auto manager = rib_->getNextHopIDManagerCopy();
    const auto resolved =
        manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
    ASSERT_TRUE(resolved.has_value());
    ASSERT_EQ(resolved->size(), 2);
    std::set<folly::IPAddress> addrs;
    for (const auto& nh : *resolved) {
      addrs.insert(nh.addr());
    }
    EXPECT_TRUE(addrs.count(folly::IPAddress("fe80::1")));
    EXPECT_TRUE(addrs.count(folly::IPAddress("fe80::2")));
  }

  // Step 4: Update the OpenR route — change link-local nexthops to different
  // addresses on different interfaces.
  UnicastRoute updatedRoute;
  updatedRoute.dest() = routePrefix;
  NextHopThrift llNhop3;
  auto llAddr3 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::3"));
  llAddr3.ifName() = "fboss3";
  llNhop3.address() = llAddr3;
  NextHopThrift llNhop4;
  auto llAddr4 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::4"));
  llAddr4.ifName() = "fboss4";
  llNhop4.address() = llAddr4;
  updatedRoute.nextHops() = {llNhop3, llNhop4};

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      std::vector<UnicastRoute>{updatedRoute},
      std::vector<IpPrefix>{},
      false,
      "update openr route nexthops",
      noopFibUpdate,
      &switchState_);

  // Verify re-resolution: nexthops should now be fe80::3 and fe80::4,
  // still retaining the SRV6 fields from the binding SID's original nexthop.
  const auto mySidEntry = rib_->getMySidTableCopy().at(prefix);
  ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());

  auto manager = rib_->getNextHopIDManagerCopy();
  const auto resolvedNhops =
      manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
  ASSERT_TRUE(resolvedNhops.has_value());
  ASSERT_EQ(resolvedNhops->size(), 2);

  const std::vector<folly::IPAddressV6> expectedSidList = {
      folly::IPAddressV6("2001:db8::10")};
  for (const auto& nh : *resolvedNhops) {
    EXPECT_TRUE(nh.intfID().has_value());
    EXPECT_TRUE(
        nh.addr() == folly::IPAddress("fe80::3") ||
        nh.addr() == folly::IPAddress("fe80::4"));
    EXPECT_EQ(nh.srv6SegmentList(), expectedSidList);
    ASSERT_TRUE(nh.tunnelType().has_value());
    EXPECT_EQ(*nh.tunnelType(), TunnelType::SRV6_ENCAP);
    ASSERT_TRUE(nh.tunnelId().has_value());
    EXPECT_EQ(*nh.tunnelId(), kSrv6Tunnel0);
  }
}

TEST_F(RibMySidNextHopTest, bindingSidUpdatedToNewNextHopsResolvesRecursively) {
  // Step 1: Interface routes for link-local subnets.
  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
  interfaceRoutes[kRid][{folly::IPAddress("fe80::"), 64}] = {
      InterfaceID(1), folly::IPAddress("fe80::1")};
  rib_->reconfigure(
      scopeResolver(),
      interfaceRoutes,
      {},
      {},
      {},
      {},
      {},
      {},
      {},
      {} /* staticMySids */,
      noopFibUpdate,
      &switchState_);

  // Step 2: Two OpenR routes with link-local nexthops.
  UnicastRoute openrRoute1;
  IpPrefix prefix1;
  prefix1.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:1::"));
  prefix1.prefixLength() = 48;
  openrRoute1.dest() = prefix1;
  NextHopThrift llNhop1;
  auto llAddr1 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::1"));
  llAddr1.ifName() = "fboss1";
  llNhop1.address() = llAddr1;
  openrRoute1.nextHops() = {llNhop1};

  UnicastRoute openrRoute2;
  IpPrefix prefix2;
  prefix2.ip() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:2::"));
  prefix2.prefixLength() = 48;
  openrRoute2.dest() = prefix2;
  NextHopThrift llNhop2;
  auto llAddr2 =
      facebook::network::toBinaryAddress(folly::IPAddressV6("fe80::2"));
  llAddr2.ifName() = "fboss2";
  llNhop2.address() = llAddr2;
  openrRoute2.nextHops() = {llNhop2};

  rib_->update(
      scopeResolver(),
      kRid,
      ClientID::OPENR,
      AdminDistance::OPENR,
      std::vector<UnicastRoute>{openrRoute1, openrRoute2},
      std::vector<IpPrefix>{},
      false,
      "add openr routes",
      noopFibUpdate,
      &switchState_);

  // Step 3: Add BINDING_MICRO_SID pointing to 2001:db8:1::1 with SRV6 fields.
  auto entry = makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop1;
  nhop1.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:1::1"));
  nhop1.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::10"))};
  nhop1.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop1.tunnelId() = kSrv6Tunnel0;
  entry.nextHops() = {nhop1};

  rib_->update(
      scopeResolver(),
      {entry},
      {},
      "add binding sid",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify initial resolution: resolves to fe80::1.
  const auto sidPrefix = makeSidPrefix("fc00:100::1", 48);
  {
    const auto mySidEntry = rib_->getMySidTableCopy().at(sidPrefix);
    ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());
    auto manager = rib_->getNextHopIDManagerCopy();
    const auto resolved =
        manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
    ASSERT_TRUE(resolved.has_value());
    ASSERT_EQ(resolved->size(), 1);
    EXPECT_EQ(resolved->begin()->addr(), folly::IPAddress("fe80::1"));
    EXPECT_EQ(*resolved->begin()->tunnelId(), kSrv6Tunnel0);
  }

  // Step 4: Update the binding SID to point to a different gateway with
  // different SRV6 fields. The new gateway resolves through openrRoute2.
  auto updatedEntry =
      makeMySidEntry("fc00:100::1", 48, MySidType::BINDING_MICRO_SID);
  NextHopThrift nhop2;
  nhop2.address() =
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8:2::1"));
  nhop2.srv6SegmentList() = {
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::20")),
      facebook::network::toBinaryAddress(folly::IPAddressV6("2001:db8::30"))};
  nhop2.tunnelType() = TunnelType::SRV6_ENCAP;
  nhop2.tunnelId() = kSrv6Tunnel0;
  updatedEntry.nextHops() = {nhop2};

  rib_->update(
      scopeResolver(),
      {updatedEntry},
      {},
      "update binding sid nexthops",
      mySidToSwitchStateUpdate,
      &switchState_);

  // Verify re-resolution: now resolves to fe80::2 with the new SRV6 fields.
  const auto mySidEntry = rib_->getMySidTableCopy().at(sidPrefix);
  ASSERT_TRUE(mySidEntry.resolvedNextHopsId().has_value());

  auto manager = rib_->getNextHopIDManagerCopy();
  const auto resolvedNhops =
      manager->getNextHopsIf(NextHopSetID(*mySidEntry.resolvedNextHopsId()));
  ASSERT_TRUE(resolvedNhops.has_value());
  ASSERT_EQ(resolvedNhops->size(), 1);

  const auto& resolvedNh = *resolvedNhops->begin();
  EXPECT_TRUE(resolvedNh.intfID().has_value());
  EXPECT_EQ(resolvedNh.addr(), folly::IPAddress("fe80::2"));

  const std::vector<folly::IPAddressV6> expectedSidList = {
      folly::IPAddressV6("2001:db8::20"), folly::IPAddressV6("2001:db8::30")};
  EXPECT_EQ(resolvedNh.srv6SegmentList(), expectedSidList);
  ASSERT_TRUE(resolvedNh.tunnelType().has_value());
  EXPECT_EQ(*resolvedNh.tunnelType(), TunnelType::SRV6_ENCAP);
  ASSERT_TRUE(resolvedNh.tunnelId().has_value());
  EXPECT_EQ(*resolvedNh.tunnelId(), kSrv6Tunnel0);
}

TEST_F(RibMySidNextHopTest, syncUpdateAddsEntry) {
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48),
      {} /* toUnresolveIfMatch */,
      {} /* toDelete */,
      "syncAdd",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  EXPECT_EQ(table.size(), 1);
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:1::", 48)), table.end());
}

TEST_F(RibMySidNextHopTest, syncUpdateAllocatesNextHopSetId) {
  RouteNextHopSet nhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48, MySidType::NODE_MICRO_SID, std::move(nhops)),
      {} /* toUnresolveIfMatch */,
      {},
      "syncAddWithNhops",
      mySidToSwitchStateUpdate,
      &switchState_);

  const auto table = rib_->getMySidTableCopy();
  const auto p = makeSidPrefix("3001:db8:1::", 48);
  ASSERT_NE(table.find(p), table.end());
  EXPECT_TRUE(table.at(p).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, syncUpdateDeletesEntry) {
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48),
      {} /* toUnresolveIfMatch */,
      {},
      "add",
      mySidToSwitchStateUpdate,
      &switchState_);
  ASSERT_EQ(rib_->getMySidTableCopy().size(), 1);

  rib_->update(
      scopeResolver(),
      std::vector<MySidWithNextHops>{},
      {} /* toUnresolveIfMatch */,
      {toIpPrefix("3001:db8:1::", 48)},
      "delete",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_->getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidNextHopTest, syncUpdatePropagatesException) {
  // Sync update must rethrow exceptions raised on the rib event-base thread —
  // captured via std::exception_ptr since folly's runInFbossEventBaseThread-
  // AndWait does not propagate.
  FailMySidUpdate failUpdate;
  EXPECT_THROW(
      rib_->update(
          scopeResolver(),
          makePair("3001:db8:1::", 48),
          {} /* toUnresolveIfMatch */,
          {},
          "syncFail",
          failUpdate,
          &switchState_),
      FbossHwUpdateError);
}

TEST_F(RibMySidNextHopTest, asyncUpdateAddsEntryAfterDrain) {
  rib_->updateAsync(
      scopeResolver(),
      makePair("3001:db8:1::", 48),
      {} /* toUnresolveIfMatch */,
      {},
      "asyncAdd",
      mySidToSwitchStateUpdate,
      &switchState_);

  // updateAsync is fire-and-forget — drain the rib event base before
  // observing state.
  rib_->waitForRibUpdates();

  const auto table = rib_->getMySidTableCopy();
  EXPECT_EQ(table.size(), 1);
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:1::", 48)), table.end());
}

TEST_F(RibMySidNextHopTest, asyncUpdateDeletesEntryAfterDrain) {
  // Seed an entry synchronously
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48),
      {} /* toUnresolveIfMatch */,
      {},
      "syncAdd",
      mySidToSwitchStateUpdate,
      &switchState_);
  ASSERT_EQ(rib_->getMySidTableCopy().size(), 1);

  rib_->updateAsync(
      scopeResolver(),
      std::vector<MySidWithNextHops>{},
      {} /* toUnresolveIfMatch */,
      {toIpPrefix("3001:db8:1::", 48)},
      "asyncDelete",
      mySidToSwitchStateUpdate,
      &switchState_);
  rib_->waitForRibUpdates();

  EXPECT_EQ(rib_->getMySidTableCopy().size(), 0);
}

// Tests for the toUnresolveIfMatch update path used by the
// MySidNeighborObserver when a neighbor goes down. RIB only clears the
// MySid's unresolved+resolved ids when the materialized unresolved set
// actually contains the removed neighbor's IP — observer can't do this
// match itself because it only sees a NextHopSetID.

TEST_F(RibMySidNextHopTest, unresolveIfMatchClearsWhenIpMatches) {
  const folly::IPAddress neigh1("2001:db8::1");
  RouteNextHopSet nhops{UnresolvedNextHop(neigh1, ECMP_WEIGHT)};
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48, MySidType::NODE_MICRO_SID, std::move(nhops)),
      {} /* toUnresolveIfMatch */,
      {},
      "seedWithNhops",
      mySidToSwitchStateUpdate,
      &switchState_);
  const auto prefix = makeSidPrefix("3001:db8:1::", 48);
  ASSERT_TRUE(rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId());

  rib_->update(
      scopeResolver(),
      {} /* toAdd */,
      {{prefix, neigh1}} /* toUnresolveIfMatch */,
      {} /* toDelete */,
      "neighborDown",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, unresolveIfMatchSkipsWhenIpDoesNotMatch) {
  // Materialize the unresolveNextHopsId into a value — the field_ref
  // returned by unresolveNextHopsId() points into the temporary table,
  // so it can't outlive a single statement.
  auto materializeId =
      [&](const folly::CIDRNetworkV6& p) -> std::optional<int64_t> {
    const auto t = rib_->getMySidTableCopy();
    auto it = t.find(p);
    if (it == t.end()) {
      return std::nullopt;
    }
    if (auto v = it->second.unresolveNextHopsId()) {
      return *v;
    }
    return std::nullopt;
  };

  const folly::IPAddress neigh1("2001:db8::1");
  const folly::IPAddress neigh2("2001:db8::2");
  RouteNextHopSet nhops{UnresolvedNextHop(neigh1, ECMP_WEIGHT)};
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48, MySidType::NODE_MICRO_SID, std::move(nhops)),
      {} /* toUnresolveIfMatch */,
      {},
      "seedWithNhops",
      mySidToSwitchStateUpdate,
      &switchState_);
  const auto prefix = makeSidPrefix("3001:db8:1::", 48);
  const auto idBefore = materializeId(prefix);
  ASSERT_TRUE(idBefore.has_value());

  // Removed IP doesn't match — RIB no-ops.
  rib_->update(
      scopeResolver(),
      {} /* toAdd */,
      {{prefix, neigh2}} /* toUnresolveIfMatch */,
      {},
      "neighborDownNonMatch",
      mySidToSwitchStateUpdate,
      &switchState_);

  EXPECT_EQ(materializeId(prefix), idBefore);
}

TEST_F(RibMySidNextHopTest, unresolveIfMatchUnknownPrefixIsNoOp) {
  // No entries at all — match-and-clear must not crash.
  const folly::IPAddress neigh1("2001:db8::1");
  rib_->update(
      scopeResolver(),
      {},
      {{makeSidPrefix("3001:db8:42::", 48), neigh1}} /* toUnresolveIfMatch */,
      {},
      "neighborDownUnknownPrefix",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_EQ(rib_->getMySidTableCopy().size(), 0);
}

TEST_F(RibMySidNextHopTest, unresolveIfMatchOnEntryWithNoUnresolvedIdIsNoOp) {
  // DECAP entry has no unresolveNextHopsId. Match-and-clear must skip.
  rib_->update(
      scopeResolver(),
      makePair("3001:db8:1::", 48, MySidType::DECAPSULATE_AND_LOOKUP),
      {} /* toUnresolveIfMatch */,
      {},
      "seedDecap",
      mySidToSwitchStateUpdate,
      &switchState_);
  const auto prefix = makeSidPrefix("3001:db8:1::", 48);
  ASSERT_FALSE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());

  rib_->update(
      scopeResolver(),
      {},
      {{prefix, folly::IPAddress("2001:db8::1")}} /* toUnresolveIfMatch */,
      {},
      "neighborDownNoUnresolved",
      mySidToSwitchStateUpdate,
      &switchState_);
  EXPECT_FALSE(
      rib_->getMySidTableCopy().at(prefix).unresolveNextHopsId().has_value());
}

TEST_F(RibMySidNextHopTest, asyncUpdatesAreSerialized) {
  // Multiple async updates run sequentially on the same event-base thread and
  // are observable after a single drain.
  for (int i = 1; i <= 3; ++i) {
    rib_->updateAsync(
        scopeResolver(),
        makePair(fmt::format("3001:db8:{}::", i), 48),
        {} /* toUnresolveIfMatch */,
        {},
        fmt::format("asyncAdd{}", i),
        mySidToSwitchStateUpdate,
        &switchState_);
  }
  rib_->waitForRibUpdates();

  const auto table = rib_->getMySidTableCopy();
  EXPECT_EQ(table.size(), 3);
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:1::", 48)), table.end());
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:2::", 48)), table.end());
  EXPECT_NE(table.find(makeSidPrefix("3001:db8:3::", 48)), table.end());
}

class RibConfigAdjacencyMySidTest : public RibMySidFibInfoTest {
 protected:
  static constexpr int16_t kFunctionId = 0x100;
  static constexpr uint32_t kEcmpWidth = 64;
  const folly::CIDRNetworkV6 kPrefix = makeSidPrefix("3001:db8:100::", 48);
  const std::string kPrefixStr{"3001:db8:100::/48"};
  const std::unordered_map<std::string, InterfaceID> kPortMap{
      {"Port-Channel301", InterfaceID(301)},
      {"Port-Channel302", InterfaceID(302)}};

  static cfg::MySidConfig makeAdjacencyConfig(const std::string& portName) {
    cfg::MySidConfig mySidConfig;
    mySidConfig.locatorPrefix() = kTestLocatorPrefix.str();
    cfg::AdjacencyMySidConfig adjacency;
    adjacency.portName() = portName;
    adjacency.isV6() = true;
    cfg::MySidEntryConfig entry;
    entry.set_adjacency(std::move(adjacency));
    mySidConfig.entries()->emplace(kFunctionId, std::move(entry));
    return mySidConfig;
  }

  void reconfigure(
      RoutingInformationBase* rib,
      const std::optional<cfg::MySidConfig>& mySidConfig) {
    RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
    interfaceRoutes[kRid][{folly::IPAddress("2001:db8::"), 32}] = {
        InterfaceID(1), folly::IPAddress("2001:db8::ffff")};
    rib->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        mySidConfig ? convertMySidConfig(*mySidConfig, kPortMap)
                    : std::vector<MySidWithNextHops>{},
        routeToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  void bindToNeighbor(InterfaceID intf) {
    auto mySid = switchState_->getMySids()->getNodeIf(kPrefixStr)->clone();
    mySid->setUnresolveNextHopsId(std::nullopt);
    mySid->setResolvedNextHopsId(std::nullopt);
    RouteNextHopSet nextHops{
        ResolvedNextHop(folly::IPAddress("2401:db00::2"), intf, ECMP_WEIGHT)};
    rib_->update(
        scopeResolver(),
        {{std::move(mySid), std::move(nextHops), std::nullopt}},
        {} /* toUnresolveIfMatch */,
        {} /* toDelete */,
        "bind uA to neighbor",
        mySidToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  void addFrrBackup() {
    rib_->updateMySidFrrProtection(
        scopeResolver(),
        {{{folly::IPAddress("3001:db8:100::"), 48},
          makeBackupNextHops({"2001:db8::1", "2001:db8::2"})}},
        {},
        mySidToSwitchStateUpdateViaRibUpdater,
        &switchState_);
  }

  state::MySidFields getRibEntry(const RoutingInformationBase& rib) const {
    return rib.getMySidTableCopy().at(kPrefix);
  }

  bool isNextHopSetAllocated(int64_t id) const {
    return rib_->getNextHopIDManagerCopy()
        ->getNextHopsIf(NextHopSetID(id))
        .has_value();
  }
};

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureIdenticalBoundAdjacencyMySidIsNoOp) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  const auto boundEntry = getRibEntry(*rib_);
  ASSERT_TRUE(boundEntry.unresolveNextHopsId().has_value());
  const auto stateMySid = switchState_->getMySids()->getNodeIf(kPrefixStr);

  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));

  EXPECT_EQ(getRibEntry(*rib_), boundEntry);
  EXPECT_EQ(switchState_->getMySids()->getNodeIf(kPrefixStr), stateMySid);
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    warmbootReconfigureKeepsBoundAdjacencyMySid) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));

  auto warmbootRib = RoutingInformationBase::fromThrift(
      rib_->warmBootState(),
      switchState_->getFibsInfoMap(),
      switchState_->getLabelForwardingInformationBase(),
      switchState_->getMySids(),
      kEcmpWidth,
      nullptr);
  const auto restoredEntry = getRibEntry(*warmbootRib);
  ASSERT_TRUE(restoredEntry.unresolveNextHopsId().has_value());
  const auto stateMySid = switchState_->getMySids()->getNodeIf(kPrefixStr);

  reconfigure(warmbootRib.get(), makeAdjacencyConfig("Port-Channel301"));

  EXPECT_EQ(getRibEntry(*warmbootRib), restoredEntry);
  EXPECT_EQ(switchState_->getMySids()->getNodeIf(kPrefixStr), stateMySid);
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureIdenticalKeepsAdjacencyFrrBackup) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  addFrrBackup();
  const auto protectedEntry = getRibEntry(*rib_);
  ASSERT_TRUE(protectedEntry.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(protectedEntry.backupResolvedNextHopsId().has_value());

  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));

  EXPECT_EQ(getRibEntry(*rib_), protectedEntry);
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureBoundAdjacencyInterfaceChangeDropsBinding) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  const auto boundId = *getRibEntry(*rib_).unresolveNextHopsId();

  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel302"));

  const auto entry = getRibEntry(*rib_);
  EXPECT_EQ(entry.adjacencyInterfaceId().value_or(0), 302);
  EXPECT_FALSE(entry.unresolveNextHopsId().has_value());
  EXPECT_FALSE(entry.resolvedNextHopsId().has_value());
  EXPECT_FALSE(isNextHopSetAllocated(boundId));
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureBoundAdjacencyToDecapDropsBinding) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  const auto boundId = *getRibEntry(*rib_).unresolveNextHopsId();

  reconfigure(rib_.get(), makeDecapMySidConfig(kFunctionId));

  const auto entry = getRibEntry(*rib_);
  EXPECT_EQ(*entry.type(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_FALSE(entry.unresolveNextHopsId().has_value());
  EXPECT_FALSE(isNextHopSetAllocated(boundId));
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureRemovingBoundAdjacencyReleasesIds) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  const auto boundId = *getRibEntry(*rib_).unresolveNextHopsId();

  reconfigure(rib_.get(), std::nullopt);

  EXPECT_EQ(rib_->getMySidTableCopy().count(kPrefix), 0);
  EXPECT_EQ(switchState_->getMySids()->getNodeIf(kPrefixStr), nullptr);
  EXPECT_FALSE(isNextHopSetAllocated(boundId));
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureRemovingAdjacencyWithFrrBackupReleasesBackupIds) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  addFrrBackup();
  const auto entry = getRibEntry(*rib_);
  ASSERT_TRUE(entry.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(entry.backupResolvedNextHopsId().has_value());

  reconfigure(rib_.get(), std::nullopt);

  EXPECT_FALSE(isNextHopSetAllocated(*entry.backupUnresolveNextHopsId()));
  EXPECT_FALSE(isNextHopSetAllocated(*entry.backupResolvedNextHopsId()));
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureAdjacencyInterfaceChangeReleasesFrrBackupIds) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  addFrrBackup();
  const auto oldEntry = getRibEntry(*rib_);
  ASSERT_TRUE(oldEntry.unresolveNextHopsId().has_value());
  ASSERT_TRUE(oldEntry.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(oldEntry.backupResolvedNextHopsId().has_value());

  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel302"));

  const auto entry = getRibEntry(*rib_);
  EXPECT_EQ(entry.adjacencyInterfaceId().value_or(0), 302);
  EXPECT_FALSE(entry.backupUnresolveNextHopsId().has_value());
  EXPECT_FALSE(entry.backupResolvedNextHopsId().has_value());
  EXPECT_FALSE(isNextHopSetAllocated(*oldEntry.unresolveNextHopsId()));
  EXPECT_FALSE(isNextHopSetAllocated(*oldEntry.backupUnresolveNextHopsId()));
  EXPECT_FALSE(isNextHopSetAllocated(*oldEntry.backupResolvedNextHopsId()));
}

TEST_F(
    RibConfigAdjacencyMySidTest,
    reconfigureAdjacencyToDecapReleasesFrrBackupIds) {
  reconfigure(rib_.get(), makeAdjacencyConfig("Port-Channel301"));
  bindToNeighbor(InterfaceID(301));
  addFrrBackup();
  const auto oldEntry = getRibEntry(*rib_);
  ASSERT_TRUE(oldEntry.backupUnresolveNextHopsId().has_value());
  ASSERT_TRUE(oldEntry.backupResolvedNextHopsId().has_value());

  reconfigure(rib_.get(), makeDecapMySidConfig(kFunctionId));

  const auto entry = getRibEntry(*rib_);
  EXPECT_EQ(*entry.type(), MySidType::DECAPSULATE_AND_LOOKUP);
  EXPECT_FALSE(entry.backupUnresolveNextHopsId().has_value());
  EXPECT_FALSE(entry.backupResolvedNextHopsId().has_value());
  EXPECT_FALSE(isNextHopSetAllocated(*oldEntry.backupUnresolveNextHopsId()));
  EXPECT_FALSE(isNextHopSetAllocated(*oldEntry.backupResolvedNextHopsId()));
}

} // namespace facebook::fboss
