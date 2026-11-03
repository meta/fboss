/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/test/LabelForwardingUtils.h"

#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>

#include <gtest/gtest.h>

using namespace facebook::fboss;

using facebook::network::toBinaryAddress;
using folly::IPAddress;

namespace {
constexpr AdminDistance kBgpDistance = AdminDistance::EBGP;
constexpr ClientID kBgpClient = ClientID::BGPD;
constexpr AdminDistance kOpenrDistance = AdminDistance::OPENR;
constexpr ClientID kOpenrClient = ClientID::OPENR;
const RouterID kRid(0);
auto kPrefix1 = IPAddress::createNetwork("1::1/64");
auto kPrefix2 = IPAddress::createNetwork("2::2/64");

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

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

folly::CIDRNetworkV6 makeSidPrefix(const std::string& addr, uint8_t len) {
  return std::make_pair(folly::IPAddressV6(addr), len);
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

} // namespace

class FailSomeUpdates {
 public:
  explicit FailSomeUpdates(std::unordered_set<int> toFail)
      : toFail_(std::move(toFail)) {}
  StateDelta operator()(
      const SwitchIdScopeResolver* resolver,
      RouterID vrf,
      const IPv4NetworkToRouteMap& v4NetworkToRoute,
      const IPv6NetworkToRouteMap& v6NetworkToRoute,
      const LabelToRouteMap& labelToRoute,
      const NextHopIDManager* nextHopIDManager,
      const MySidTable& mySidTable,
      void* cookie,
      bool vrfDeleted) {
    if (toFail_.find(++cnt_) != toFail_.end()) {
      auto curSwitchStatePtr =
          static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
      (*curSwitchStatePtr)->publish();
      auto desiredState = *curSwitchStatePtr;
      ribToSwitchStateUpdate(
          resolver,
          vrf,
          v4NetworkToRoute,
          v6NetworkToRoute,
          labelToRoute,
          nextHopIDManager,
          mySidTable,
          static_cast<void*>(&desiredState),
          vrfDeleted);
      throw FbossHwUpdateError(desiredState, *curSwitchStatePtr);
    }
    return ribToSwitchStateUpdate(
        resolver,
        vrf,
        v4NetworkToRoute,
        v6NetworkToRoute,
        labelToRoute,
        nextHopIDManager,
        mySidTable,
        cookie,
        vrfDeleted);
  }

 private:
  int cnt_{0};
  std::unordered_set<int> toFail_;
};

class RibRollbackTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_mpls_rib = true;
    rib_.ensureVrf(kRid);
    switchState_ = makeSwitchState();
    auto origSwitchState = switchState_;
    switchState_->publish();
    rib_.update(
        scopeResolver(),
        kRid,
        kBgpClient,
        kBgpDistance,
        {makeDropUnicastRoute(kPrefix1)},
        {},
        false,
        "add only",
        ribToSwitchStateUpdate,
        &switchState_);
    EXPECT_NE(switchState_, origSwitchState);
    EXPECT_EQ(1, switchState_->getGeneration());
    auto oldSwitchState = switchState_;
    rib_.update(
        scopeResolver(),
        kRid,
        kBgpClient,
        kBgpDistance,
        util::getTestRoutes(0, 1),
        {},
        false,
        "add only",
        ribToSwitchStateUpdate,
        &switchState_);
    EXPECT_NE(switchState_, oldSwitchState);
    EXPECT_EQ(2, switchState_->getGeneration());
    assertRouteCount(0, 1, 1);
  }

  std::shared_ptr<SwitchState> makeSwitchState() const {
    auto switchState = std::make_shared<SwitchState>();
    auto intf = std::make_shared<Interface>(
        InterfaceID(1),
        kRid,
        std::optional<VlanID>(VlanID(1)),
        folly::StringPiece("intf1"),
        folly::MacAddress("00:00:00:00:00:01"),
        9000,
        false,
        false);
    auto intfs = std::make_shared<MultiSwitchInterfaceMap>();
    intfs->addNode(intf, scope());
    switchState->resetIntfs(intfs);
    return switchState;
  }

  void TearDown() override {
    switchState_->publish();
    auto curSwitchState = switchState_;
    // Unless there is mismatched state in RIB (i.e.
    // mismatched from FIB). A empty update should not
    // change switchState. Assert that.
    rib_.update(
        scopeResolver(),
        kRid,
        kBgpClient,
        kBgpDistance,
        std::vector<UnicastRoute>{},
        {},
        false,
        "empty update",
        ribToSwitchStateUpdate,
        &switchState_);
    EXPECT_EQ(curSwitchState, switchState_);

    rib_.update(
        scopeResolver(),
        kRid,
        kBgpClient,
        kBgpDistance,
        std::vector<MplsRoute>{},
        {},
        false,
        "empty update",
        ribToSwitchStateUpdate,
        &switchState_);
    EXPECT_EQ(curSwitchState, switchState_);
  }
  void assertRouteCount(int v4Expected, int v6Expected, int mplsExpected)
      const {
    auto [numV4, numV6] = switchState_->getFibsInfoMap()->getRouteCount();
    EXPECT_EQ(v6Expected, numV6);
    EXPECT_EQ(v4Expected, numV4);
    EXPECT_EQ(v6Expected + v4Expected, rib_.getRouteTableDetails(kRid).size());
    EXPECT_EQ(
        mplsExpected,
        switchState_->getLabelForwardingInformationBase()->numNodes());
    EXPECT_EQ(mplsExpected, rib_.getMplsRouteTableDetails().size());
  }

 protected:
  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(RibRollbackTest, rollbackFail) {
  FailSomeUpdates failUpdateAndRollback({1, 2});
  EXPECT_DEATH(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeDropUnicastRoute(kPrefix2)},
          {},
          false,
          "add only",
          failUpdateAndRollback,
          &switchState_),
      ".*");
}

TEST_F(RibRollbackTest, rollbackAdd) {
  // Fail route update. Rib should rollback to pre failed add state
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeDropUnicastRoute(kPrefix2)},
          {},
          false,
          "fail add",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackAddExisting) {
  // Fail route update. Rib should rollback to pre failed add state
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeDropUnicastRoute(kPrefix1)},
          {},
          false,
          "fail add",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackDel) {
  // Fail route update. Rib should rollback to pre failed add state
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {},
          {toIpPrefix(kPrefix1)},
          false,
          "fail del",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackDelNonExistent) {
  auto routeTableBeforeUpdate = rib_.getRouteTableDetails(kRid);
  // Noop update - prefix does not exist in rib
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {},
      {toIpPrefix(kPrefix2)}, // kPrefix2 does not exist in RIB
      false,
      "nonexistent prefix del",
      ribToSwitchStateUpdate,
      &switchState_);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeUpdate, rib_.getRouteTableDetails(kRid));
  // Failed update, still rib is left as is
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {},
          {toIpPrefix(kPrefix2)}, // kPrefix2 does not exist in RIB
          false,
          "fail del",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackAddAndDel) {
  // Fail route update. Rib should rollback to pre failed add state
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeDropUnicastRoute(kPrefix2)},
          {toIpPrefix(kPrefix1)},
          false,
          "fail add",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackDifferentClient) {
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kOpenrClient,
          kOpenrDistance,
          {makeDropUnicastRoute(kPrefix1)},
          {},
          false,
          "fail add",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackDifferentNexthops) {
  // Test rollback for route replacement
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeToCpuUnicastRoute(kPrefix1)},
          {},
          false,
          "fail add",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, syncFibRollbackExistingClient) {
  // Test rollback for sync fib
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {
              makeDropUnicastRoute(kPrefix1),
              makeDropUnicastRoute(kPrefix2),
          },
          {},
          true,
          "fail syncFib",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, syncFibRollbackNewClient) {
  // Test rollback for sync fib
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kOpenrClient,
          kOpenrDistance,
          {
              makeDropUnicastRoute(kPrefix1),
              makeDropUnicastRoute(kPrefix2),
          },
          {},
          true,
          "fail syncFib",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

TEST_F(RibRollbackTest, rollbackMpls) {
  // Fail route update. Rib should rollback to pre failed add state
  auto routeTableBeforeFailedUpdate = rib_.getRouteTableDetails(kRid);
  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          util::getTestRoutes(1, 1),
          {},
          false,
          "fail add mpls",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);
  assertRouteCount(0, 1, 1);
  EXPECT_EQ(routeTableBeforeFailedUpdate, rib_.getRouteTableDetails(kRid));
}

// Callback that injects MySid entries into the applied state on failure.
class FailWithMySidInAppliedState {
 public:
  explicit FailWithMySidInAppliedState(MySidTable mySidEntriesToInject)
      : mySidEntriesToInject_(std::move(mySidEntriesToInject)) {}

  StateDelta operator()(
      const SwitchIdScopeResolver* resolver,
      RouterID vrf,
      const IPv4NetworkToRouteMap& v4NetworkToRoute,
      const IPv6NetworkToRouteMap& v6NetworkToRoute,
      const LabelToRouteMap& labelToRoute,
      const NextHopIDManager* nextHopIDManager,
      const MySidTable& mySidTable,
      void* cookie,
      bool vrfDeleted) {
    auto curSwitchStatePtr = static_cast<std::shared_ptr<SwitchState>*>(cookie);
    (*curSwitchStatePtr)->publish();

    // Build the desired state normally
    auto desiredState = *curSwitchStatePtr;
    ribToSwitchStateUpdate(
        resolver,
        vrf,
        v4NetworkToRoute,
        v6NetworkToRoute,
        labelToRoute,
        nextHopIDManager,
        mySidTable,
        static_cast<void*>(&desiredState),
        vrfDeleted);

    // Build the applied state from the ORIGINAL state (not desiredState),
    // since in a partial HW update the FIB may not have been updated.
    // Inject MySid entries to simulate MySid being applied to HW.
    auto appliedState = (*curSwitchStatePtr)->clone();
    auto newMySids = std::make_shared<MultiSwitchMySidMap>();
    for (const auto& [cidr, mySid] : mySidEntriesToInject_) {
      newMySids->addNode(mySid, scope());
    }
    appliedState->resetMySids(newMySids);
    appliedState->publish();

    throw FbossHwUpdateError(desiredState, appliedState);
  }

 private:
  MySidTable mySidEntriesToInject_;
};

TEST(RibRollbackMySidTest, rollbackMySidReconstruction) {
  // Verify that MySidTable is reconstructed from the applied state
  // after a failed HW update.
  FLAGS_mpls_rib = true;
  RoutingInformationBase rib;
  rib.ensureVrf(kRid);
  auto switchState = std::make_shared<SwitchState>();
  switchState->publish();

  // Add an initial route
  rib.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeDropUnicastRoute(kPrefix1)},
      {},
      false,
      "setup",
      ribToSwitchStateUpdate,
      &switchState);

  // Verify mySidTable starts empty
  EXPECT_EQ(rib.getMySidTableCopy().size(), 0);

  // Create MySid entries to inject into the applied state
  auto sidPrefix = makeSidPrefix("fc00:100::1", 48);
  MySidTable mySidEntries;
  mySidEntries[sidPrefix] = makeMySid(sidPrefix);

  // The update fails and injects MySid entries into the applied state.
  // The rollback should reconstruct the RIB's mySidTable from the applied
  // state's MySidMap.
  FailWithMySidInAppliedState failWithMySid(mySidEntries);
  EXPECT_THROW(
      rib.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeDropUnicastRoute(kPrefix2)},
          {},
          false,
          "fail with mysid",
          failWithMySid,
          &switchState),
      FbossHwUpdateError);

  // Verify the mySidTable was reconstructed with the injected entries
  auto mySidTableCopy = rib.getMySidTableCopy();
  EXPECT_EQ(mySidTableCopy.size(), 1);
  EXPECT_NE(mySidTableCopy.find(sidPrefix), mySidTableCopy.end());
}

// Tests for unresolved-routes index populated by refreshUnresolvedIndex on
// the success branch of updateFib. Fixture's SetUp adds only resolved
// routes, so indexes start empty.

namespace {

const auto kV4Prefix = folly::CIDRNetwork{folly::IPAddressV4("10.0.0.0"), 24};
const auto kV4Prefix2 = folly::CIDRNetwork{folly::IPAddressV4("10.0.1.0"), 24};
const auto kV6PrefixUnresolved =
    folly::CIDRNetwork{folly::IPAddressV6("2001:db8::"), 32};
const folly::IPAddress kUnreachableV4Nhop{"192.168.1.1"};
const folly::IPAddress kUnreachableV6Nhop{"3000::1"};

folly::CIDRNetworkV4 toV4Key(const folly::CIDRNetwork& p) {
  return {p.first.asV4(), p.second};
}
folly::CIDRNetworkV6 toV6Key(const folly::CIDRNetwork& p) {
  return {p.first.asV6(), p.second};
}

} // namespace

// Add unresolved v4 + v6 routes and assert both indexes pick them up.
TEST_F(RibRollbackTest, indexAddsUnresolvedV4AndV6) {
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop}),
       makeUnicastRoute(kV6PrefixUnresolved, {kUnreachableV6Nhop})},
      {},
      false,
      "add unresolved v4+v6",
      ribToSwitchStateUpdate,
      &switchState_);

  auto v4 = rib_.getUnresolvedV4RoutesForTest(kRid);
  ASSERT_EQ(v4.size(), 1);
  auto v4It = v4.find(toV4Key(kV4Prefix));
  ASSERT_NE(v4It, v4.end());
  EXPECT_FALSE(v4It->second->isResolved());

  auto v6 = rib_.getUnresolvedV6RoutesForTest(kRid);
  ASSERT_EQ(v6.size(), 1);
  auto v6It = v6.find(toV6Key(kV6PrefixUnresolved));
  ASSERT_NE(v6It, v6.end());
  EXPECT_FALSE(v6It->second->isResolved());
}

// Resolved routes never enter the index; routes that do enter are published.
TEST_F(RibRollbackTest, indexSkipsResolvedAndPublishesUnresolved) {
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeDropUnicastRoute(kV4Prefix2),
       makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop})},
      {},
      false,
      "add drop + unresolved",
      ribToSwitchStateUpdate,
      &switchState_);

  auto index = rib_.getUnresolvedV4RoutesForTest(kRid);
  ASSERT_EQ(index.size(), 1);
  EXPECT_EQ(index.find(toV4Key(kV4Prefix2)), index.end());
  EXPECT_TRUE(index.at(toV4Key(kV4Prefix))->isPublished());
}

// An unrelated update keeps existing unresolved entries in the index; a
// route that transitions unresolved -> resolved is dropped.
TEST_F(RibRollbackTest, indexSurvivesUnrelatedUpdateAndDropsOnResolution) {
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop})},
      {},
      false,
      "add unresolved",
      ribToSwitchStateUpdate,
      &switchState_);
  ASSERT_EQ(rib_.getUnresolvedV4RoutesForTest(kRid).size(), 1);

  // Unrelated update on a different prefix: kV4Prefix stays in the index
  // (still unresolved).
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix2, {kUnreachableV4Nhop})},
      {},
      false,
      "add 2nd unresolved",
      ribToSwitchStateUpdate,
      &switchState_);
  auto mid = rib_.getUnresolvedV4RoutesForTest(kRid);
  ASSERT_EQ(mid.size(), 2);
  auto midIt = mid.find(toV4Key(kV4Prefix));
  ASSERT_NE(midIt, mid.end());
  EXPECT_FALSE(midIt->second->isResolved());

  // Replace kV4Prefix with a DROP route -> resolves -> drops from index.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeDropUnicastRoute(kV4Prefix)},
      {},
      false,
      "replace with drop",
      ribToSwitchStateUpdate,
      &switchState_);
  auto after = rib_.getUnresolvedV4RoutesForTest(kRid);
  EXPECT_EQ(after.find(toV4Key(kV4Prefix)), after.end());
  EXPECT_NE(after.find(toV4Key(kV4Prefix2)), after.end());
}

// Index correctly reflects add -> update (different nhops, still unresolved)
// -> delete lifecycle.
// Regression: a new unresolved route added by an update that fails HW
// programming must NOT survive rollback. Pre-fix, the rollback walked the
// post-failure RIB for unresolved routes and re-inserted them, incorrectly
// preserving the new entry.
TEST_F(RibRollbackTest, rollbackDropsAddedUnresolvedV4AndV6) {
  auto routeTableBefore = rib_.getRouteTableDetails(kRid);

  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop}),
           makeUnicastRoute(kV6PrefixUnresolved, {kUnreachableV6Nhop})},
          {},
          false,
          "fail add unresolved v4+v6",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);

  EXPECT_EQ(routeTableBefore, rib_.getRouteTableDetails(kRid));
  EXPECT_TRUE(rib_.getUnresolvedV4RoutesForTest(kRid).empty());
  EXPECT_TRUE(rib_.getUnresolvedV6RoutesForTest(kRid).empty());
}

// Regression: deleting an unresolved route in an update that fails HW must
// leave the route in the RIB after rollback (the delete never committed).
TEST_F(RibRollbackTest, rollbackRestoresUnresolvedAfterDeleteFailure) {
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop})},
      {},
      false,
      "add unresolved",
      ribToSwitchStateUpdate,
      &switchState_);
  ASSERT_EQ(rib_.getUnresolvedV4RoutesForTest(kRid).size(), 1);
  auto routeTableBeforeDelete = rib_.getRouteTableDetails(kRid);

  FailSomeUpdates failFirstUpdate({1});
  EXPECT_THROW(
      rib_.update(
          scopeResolver(),
          kRid,
          kBgpClient,
          kBgpDistance,
          {},
          {toIpPrefix(kV4Prefix)},
          false,
          "fail delete unresolved",
          failFirstUpdate,
          &switchState_),
      FbossHwUpdateError);

  EXPECT_EQ(routeTableBeforeDelete, rib_.getRouteTableDetails(kRid));
  EXPECT_EQ(rib_.getUnresolvedV4RoutesForTest(kRid).size(), 1);
}

// Regression: a failed update that transitions a route between resolved
// and unresolved must leave the route in its original state. Covers both
// directions: resolved -> unresolved (v4) and unresolved -> resolved (v6).
TEST_F(RibRollbackTest, rollbackRestoresOriginalStateAfterTransitionFailure) {
  // Sub-case A: resolved -> unresolved that fails.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeDropUnicastRoute(kV4Prefix)},
      {},
      false,
      "add resolved v4",
      ribToSwitchStateUpdate,
      &switchState_);
  auto stateAfterAddResolved = rib_.getRouteTableDetails(kRid);
  ASSERT_TRUE(rib_.getUnresolvedV4RoutesForTest(kRid).empty());

  {
    FailSomeUpdates failFirstUpdate({1});
    EXPECT_THROW(
        rib_.update(
            scopeResolver(),
            kRid,
            kBgpClient,
            kBgpDistance,
            {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop})},
            {},
            false,
            "fail resolved->unresolved",
            failFirstUpdate,
            &switchState_),
        FbossHwUpdateError);
    EXPECT_EQ(stateAfterAddResolved, rib_.getRouteTableDetails(kRid));
    EXPECT_TRUE(rib_.getUnresolvedV4RoutesForTest(kRid).empty());
  }

  // Sub-case B: unresolved -> resolved that fails.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV6PrefixUnresolved, {kUnreachableV6Nhop})},
      {},
      false,
      "add unresolved v6",
      ribToSwitchStateUpdate,
      &switchState_);
  auto stateBeforeTransition = rib_.getRouteTableDetails(kRid);
  ASSERT_EQ(rib_.getUnresolvedV6RoutesForTest(kRid).size(), 1);

  {
    FailSomeUpdates failFirstUpdate({1});
    EXPECT_THROW(
        rib_.update(
            scopeResolver(),
            kRid,
            kBgpClient,
            kBgpDistance,
            {makeDropUnicastRoute(kV6PrefixUnresolved)},
            {},
            false,
            "fail unresolved->resolved",
            failFirstUpdate,
            &switchState_),
        FbossHwUpdateError);
    EXPECT_EQ(stateBeforeTransition, rib_.getRouteTableDetails(kRid));
    EXPECT_EQ(rib_.getUnresolvedV6RoutesForTest(kRid).size(), 1);
  }
}

TEST_F(RibRollbackTest, indexHandlesAddUpdateDelete) {
  // Add: unresolved -> index has entry.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix, {kUnreachableV4Nhop})},
      {},
      false,
      "add",
      ribToSwitchStateUpdate,
      &switchState_);
  auto added = rib_.getUnresolvedV4RoutesForTest(kRid);
  ASSERT_EQ(added.size(), 1);
  auto* ptrAdded = added.at(toV4Key(kV4Prefix)).get();

  // Update: different unreachable nhop -> route cloned, still unresolved.
  // Index entry refreshes to the new shared_ptr.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {makeUnicastRoute(kV4Prefix, {folly::IPAddress("192.168.2.2")})},
      {},
      false,
      "update nhop",
      ribToSwitchStateUpdate,
      &switchState_);
  auto updated = rib_.getUnresolvedV4RoutesForTest(kRid);
  ASSERT_EQ(updated.size(), 1);
  EXPECT_NE(updated.at(toV4Key(kV4Prefix)).get(), ptrAdded);

  // Delete: index entry gone.
  rib_.update(
      scopeResolver(),
      kRid,
      kBgpClient,
      kBgpDistance,
      {},
      {toIpPrefix(kV4Prefix)},
      false,
      "delete",
      ribToSwitchStateUpdate,
      &switchState_);
  EXPECT_TRUE(rib_.getUnresolvedV4RoutesForTest(kRid).empty());
}
