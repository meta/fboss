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

#include <common/network/if/gen-cpp2/Address_types.h>
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibMySidUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV6.h>
#include <memory>

namespace facebook::fboss {

namespace {

constexpr uint32_t kEcmpWidth = 64;

std::shared_ptr<MySid> makeMySid(
    const std::string& sidPrefix,
    uint8_t prefixLen,
    MySidType type = MySidType::NODE_MICRO_SID) {
  state::MySidFields fields;
  fields.type() = type;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress(sidPrefix));
  thriftPrefix.prefixLength() = prefixLen;
  fields.mySid() = thriftPrefix;
  return std::make_shared<MySid>(fields);
}

RouteNextHopSet makeResolvedNhops(
    const std::vector<std::pair<std::string, InterfaceID>>& nhops,
    NextHopRole role = NextHopRole::PRIMARY,
    NextHopWeight weight = ECMP_WEIGHT) {
  RouteNextHopSet result;
  for (const auto& [addr, intfId] : nhops) {
    result.emplace(ResolvedNextHop(
        folly::IPAddress(addr),
        intfId,
        weight,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        role));
  }
  return result;
}

} // namespace

class RibMySidUpdaterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<NextHopIDManager>();
  }

  NextHopIDManager& manager() {
    return *manager_;
  }

  NextHopSetID allocUnresolvedSet(const RouteNextHopSet& nhops) {
    return manager()
        .getOrAllocRouteNextHopSetID(nhops)
        .nextHopIdSetIter->second.id;
  }

  std::shared_ptr<Route<folly::IPAddressV6>> makeResolvedV6Route(
      const folly::IPAddressV6& network,
      uint8_t mask,
      const RouteNextHopSet& nhops) {
    RoutePrefix<folly::IPAddressV6> prefix{network, mask};
    auto thrift = Route<folly::IPAddressV6>::makeThrift(prefix);
    auto route = std::make_shared<Route<folly::IPAddressV6>>(thrift);
    // Stamp resolved + normalized IDs as RibRouteUpdater::resolveOne does.
    RouteNextHopEntry entry(nhops, AdminDistance::EBGP);
    allocateRouteNextHopIds(manager_.get(), entry);
    route->setResolved(entry);
    route->publish();
    return route;
  }

  std::shared_ptr<Route<folly::IPAddressV6>> makeConnectedV6Route(
      const folly::IPAddressV6& network,
      uint8_t mask,
      InterfaceID intfId) {
    RoutePrefix<folly::IPAddressV6> prefix{network, mask};
    auto thrift = Route<folly::IPAddressV6>::makeThrift(prefix);
    auto route = std::make_shared<Route<folly::IPAddressV6>>(thrift);
    RouteNextHopSet nhops{
        ResolvedNextHop(folly::IPAddress(network), intfId, ECMP_WEIGHT)};
    RouteNextHopEntry entry(nhops, AdminDistance::DIRECTLY_CONNECTED);
    allocateRouteNextHopIds(manager_.get(), entry);
    route->setResolved(entry);
    route->setConnected();
    route->publish();
    return route;
  }

  std::unique_ptr<NextHopIDManager> manager_;
  IPv4NetworkToRouteMap v4Routes_;
  IPv6NetworkToRouteMap v6Routes_;
  MySidTable mySidTable_;
};

TEST_F(RibMySidUpdaterTest, noUnresolvedId_entrySkipped) {
  // MySid without unresolvedNextHopsId should not get a resolvedNextHopsId.
  auto mySid = makeMySid("fc00:100::1", 48);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  EXPECT_FALSE(mySidTable_.at(key)->getResolvedNextHopsId().has_value());
}

TEST_F(RibMySidUpdaterTest, nhopWithIntfId_resolvedSetIdAllocated) {
  // Nexthop with InterfaceID is already resolved → resolvedNextHopsId set.
  const auto nhops = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto unresolvedId = allocUnresolvedSet(nhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());
  EXPECT_TRUE(manager().getNextHopsIf(*resolvedId).has_value());
}

TEST_F(RibMySidUpdaterTest, nhopRefCountBumped_afterResolvingNhopWithIntfId) {
  // Each nexthop in the resolved set should have its ref count incremented.
  // Verified by checking the nexthop ID is present in idToNextHop after
  // resolve.
  const auto nhops = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto unresolvedId = allocUnresolvedSet(nhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());

  // Verify individual nexthop IDs are tracked (ref count > 0)
  const auto& nhIdSet = manager().getIdToNextHopIdSet().at(*resolvedId);
  const auto& idToNhop = manager().getIdToNextHop();
  EXPECT_FALSE(nhIdSet.empty());
  for (const auto& nhId : nhIdSet) {
    EXPECT_NE(idToNhop.find(nhId), idToNhop.end());
  }
  EXPECT_EQ(manager().getNextHopRefCount(*nhops.begin()), 2);
}

TEST_F(RibMySidUpdaterTest, gatewayNhopMatchingV6Route_resolvedSetIdAllocated) {
  // Gateway nexthop (no intfID) resolved via v6 route LPM lookup.
  const auto routeNhops = makeResolvedNhops(
      {{"fe80::1", InterfaceID(1)}, {"fe80::2", InterfaceID(2)}});
  auto v6Route =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8::"), 32, routeNhops);
  v6Routes_.insert(v6Route->prefix(), v6Route);

  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());
  const auto resolvedNhops = manager().getNextHopsIf(*resolvedId);
  ASSERT_TRUE(resolvedNhops.has_value());
  // Resolved set comes from normalizedNextHops(), so only verify size matches.
  EXPECT_EQ(resolvedNhops->size(), routeNhops.size());
}

TEST_F(RibMySidUpdaterTest, nextHopSetIdsChangeOnlyWhenResolvedNextHopsChange) {
  const folly::IPAddress gatewayAddr("2001:db8::1");
  const folly::CIDRNetwork relevantRoutePrefix{
      folly::IPAddress("2001:db8::"), 32};
  const folly::CIDRNetwork unrelatedRoutePrefix{
      folly::IPAddress("2001:db9::"), 32};
  const auto initialRouteNhops = makeResolvedNhops(
      {{"fe80::1", InterfaceID(1)}, {"fe80::2", InterfaceID(2)}});
  RibRouteUpdater routeUpdater(
      &v4Routes_, &v6Routes_, &manager(), nullptr, kEcmpWidth);
  auto updateRoute = [&routeUpdater](
                         const folly::CIDRNetwork& prefix,
                         const RouteNextHopSet& nhops) {
    routeUpdater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
        ClientID::OPENR,
        {{prefix, RouteNextHopEntry(nhops, AdminDistance::OPENR)}},
        {},
        false);
  };
  updateRoute(relevantRoutePrefix, initialRouteNhops);
  updateRoute(
      unrelatedRoutePrefix, makeResolvedNhops({{"fe80::10", InterfaceID(10)}}));

  const RouteNextHopSet primaryUnresolvedNhops{
      UnresolvedNextHop(gatewayAddr, ECMP_WEIGHT)};
  const RouteNextHopSet backupUnresolvedNhops{UnresolvedNextHop(
      gatewayAddr,
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      NextHopRole::BACKUP)};
  const auto primaryUnresolvedId = allocUnresolvedSet(primaryUnresolvedNhops);
  const auto backupUnresolvedId = allocUnresolvedSet(backupUnresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  mySid->setUnresolveNextHopsId(primaryUnresolvedId);
  mySid->setBackupUnresolveNextHopsId(backupUnresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto initialPrimaryResolvedId =
      mySidTable_.at(key)->getResolvedNextHopsId();
  const auto initialBackupResolvedId =
      mySidTable_.at(key)->getBackupResolvedNextHopsId();
  ASSERT_TRUE(initialPrimaryResolvedId.has_value());
  ASSERT_TRUE(initialBackupResolvedId.has_value());
  const auto initialResolvedMySid = mySidTable_.at(key);
  const auto expectBackupRole = [this](NextHopSetID id) {
    const auto& nextHops = manager().getNextHops(id);
    ASSERT_FALSE(nextHops.empty());
    for (const auto& nextHop : nextHops) {
      EXPECT_EQ(nextHop.role(), NextHopRole::BACKUP);
    }
  };
  expectBackupRole(*initialBackupResolvedId);

  updateRoute(
      unrelatedRoutePrefix, makeResolvedNhops({{"fe80::11", InterfaceID(11)}}));
  updater.resolve();

  const auto primaryResolvedIdAfterUnrelatedUpdate =
      mySidTable_.at(key)->getResolvedNextHopsId();
  const auto backupResolvedIdAfterUnrelatedUpdate =
      mySidTable_.at(key)->getBackupResolvedNextHopsId();
  ASSERT_TRUE(primaryResolvedIdAfterUnrelatedUpdate.has_value());
  ASSERT_TRUE(backupResolvedIdAfterUnrelatedUpdate.has_value());
  EXPECT_EQ(mySidTable_.at(key), initialResolvedMySid);
  EXPECT_EQ(*primaryResolvedIdAfterUnrelatedUpdate, *initialPrimaryResolvedId);
  EXPECT_EQ(*backupResolvedIdAfterUnrelatedUpdate, *initialBackupResolvedId);
  EXPECT_EQ(
      *mySidTable_.at(key)->getUnresolveNextHopsId(), primaryUnresolvedId);
  EXPECT_EQ(
      *mySidTable_.at(key)->getBackupUnresolveNextHopsId(), backupUnresolvedId);

  const auto updatedRouteNhops = makeResolvedNhops(
      {{"fe80::3", InterfaceID(3)}, {"fe80::4", InterfaceID(4)}});
  const auto expectedPrimaryResolvedNhops = makeResolvedNhops(
      {{"fe80::3", InterfaceID(3)}, {"fe80::4", InterfaceID(4)}},
      NextHopRole::PRIMARY,
      UCMP_DEFAULT_WEIGHT);
  const auto expectedBackupResolvedNhops = makeResolvedNhops(
      {{"fe80::3", InterfaceID(3)}, {"fe80::4", InterfaceID(4)}},
      NextHopRole::BACKUP,
      UCMP_DEFAULT_WEIGHT);
  updateRoute(relevantRoutePrefix, updatedRouteNhops);
  updater.resolve();

  const auto primaryResolvedIdAfterRelevantUpdate =
      mySidTable_.at(key)->getResolvedNextHopsId();
  const auto backupResolvedIdAfterRelevantUpdate =
      mySidTable_.at(key)->getBackupResolvedNextHopsId();
  ASSERT_TRUE(primaryResolvedIdAfterRelevantUpdate.has_value());
  ASSERT_TRUE(backupResolvedIdAfterRelevantUpdate.has_value());
  EXPECT_NE(mySidTable_.at(key), initialResolvedMySid);
  EXPECT_NE(*primaryResolvedIdAfterRelevantUpdate, *initialPrimaryResolvedId);
  EXPECT_NE(*backupResolvedIdAfterRelevantUpdate, *initialBackupResolvedId);
  EXPECT_EQ(
      manager().getNextHops(*primaryResolvedIdAfterRelevantUpdate),
      expectedPrimaryResolvedNhops);
  EXPECT_EQ(
      manager().getNextHops(*backupResolvedIdAfterRelevantUpdate),
      expectedBackupResolvedNhops);
  EXPECT_FALSE(manager().getNextHopsIf(*initialPrimaryResolvedId).has_value());
  EXPECT_FALSE(manager().getNextHopsIf(*initialBackupResolvedId).has_value());
  expectBackupRole(*backupResolvedIdAfterRelevantUpdate);
  EXPECT_EQ(
      *mySidTable_.at(key)->getUnresolveNextHopsId(), primaryUnresolvedId);
  EXPECT_EQ(
      *mySidTable_.at(key)->getBackupUnresolveNextHopsId(), backupUnresolvedId);
}

TEST_F(RibMySidUpdaterTest, gatewayNhopNoRouteMatch_noResolvedSetId) {
  // Gateway nexthop with no matching route → empty resolved set → no ID.
  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  EXPECT_FALSE(mySidTable_.at(key)->getResolvedNextHopsId().has_value());
}

TEST_F(
    RibMySidUpdaterTest,
    secondResolve_differentNhops_oldSetRetainedByRoute) {
  // After re-resolving to different nexthops the MySid releases its old
  // resolvedSetId and gets a new one. The old set is shared with route1's
  // normalizedResolvedNextHopSetID (recursive resolution reuses the route's fwd
  // nexthops), so it stays alive -- route1 still references it.
  const auto routeNhops1 = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto routeNhops2 = makeResolvedNhops({{"fe80::2", InterfaceID(2)}});
  auto v6Route1 =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8:1::"), 48, routeNhops1);
  auto v6Route2 =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8:2::"), 48, routeNhops2);
  v6Routes_.insert(v6Route1->prefix(), v6Route1);
  v6Routes_.insert(v6Route2->prefix(), v6Route2);

  const RouteNextHopSet unresolvedNhops1{
      UnresolvedNextHop(folly::IPAddress("2001:db8:1::1"), ECMP_WEIGHT)};
  const RouteNextHopSet unresolvedNhops2{
      UnresolvedNextHop(folly::IPAddress("2001:db8:2::1"), ECMP_WEIGHT)};
  const auto unresolvedId1 = allocUnresolvedSet(unresolvedNhops1);
  const auto unresolvedId2 = allocUnresolvedSet(unresolvedNhops2);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId1);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  // First resolve: allocates resolvedId pointing to routeNhops1.
  RibMySidUpdater updater1(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater1.resolve();

  const auto firstResolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(firstResolvedId.has_value());

  // Swap unresolvedId to point at the second route (simulating a route update).
  mySidTable_[key] = mySidTable_[key]->clone();
  mySidTable_[key]->setUnresolveNextHopsId(unresolvedId2);

  // Second resolve: old resolvedId decremented, new one allocated.
  RibMySidUpdater updater2(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater2.resolve();

  const auto secondResolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(secondResolvedId.has_value());
  EXPECT_NE(*firstResolvedId, *secondResolvedId);
  // The MySid released its reference to the old set, dropping the shared set's
  // ref count from 2 (route1 + MySid) to 1 (route1 alone) -- retained by
  // route1, not freed.
  const auto& oldNhIdSet = manager().getIdToNextHopIdSet().at(*firstResolvedId);
  EXPECT_EQ(manager().getNextHopIDSetRefCount(oldNhIdSet), 1);
  // New set ID should be alive.
  EXPECT_TRUE(manager().getNextHopsIf(*secondResolvedId).has_value());
}

TEST_F(
    RibMySidUpdaterTest,
    twoEntriesSameNhops_resolvedSetIdSharedAndRefCountIsThree) {
  // Two MySid entries with different gateway nexthops that both resolve via
  // the same route. They share one NextHopSetID; with the route also
  // referencing it, the ref count is 3.
  const auto sharedRouteNhops =
      makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  auto v6Route = makeResolvedV6Route(
      folly::IPAddressV6("2001:db8::"), 32, sharedRouteNhops);
  v6Routes_.insert(v6Route->prefix(), v6Route);

  const RouteNextHopSet unresolvedNhops1{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const RouteNextHopSet unresolvedNhops2{
      UnresolvedNextHop(folly::IPAddress("2001:db8::2"), ECMP_WEIGHT)};
  const auto unresolvedId1 = allocUnresolvedSet(unresolvedNhops1);
  const auto unresolvedId2 = allocUnresolvedSet(unresolvedNhops2);

  auto mySid1 = makeMySid("fc00:100::1", 48);
  mySid1->setUnresolveNextHopsId(unresolvedId1);
  const folly::CIDRNetworkV6 key1{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key1] = mySid1;

  auto mySid2 = makeMySid("fc00:200::1", 48);
  mySid2->setUnresolveNextHopsId(unresolvedId2);
  const folly::CIDRNetworkV6 key2{folly::IPAddressV6("fc00:200::1"), 48};
  mySidTable_[key2] = mySid2;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto id1 = mySidTable_.at(key1)->getResolvedNextHopsId();
  const auto id2 = mySidTable_.at(key2)->getResolvedNextHopsId();
  ASSERT_TRUE(id1.has_value());
  ASSERT_TRUE(id2.has_value());
  EXPECT_EQ(*id1, *id2);

  // Ref count of the shared set should be 3: the route's normalized ID plus
  // one for each of the two MySid entries (both resolve via the same route).
  const auto& nhIdSet = manager().getIdToNextHopIdSet().at(*id1);
  EXPECT_EQ(manager().getNextHopIDSetRefCount(nhIdSet), 3);
}

TEST_F(RibMySidUpdaterTest, resolveFiltered_onlyMatchingEntryResolved) {
  // Two MySid entries; only the one in the filter set should be resolved.
  const auto nhops1 = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto nhops2 = makeResolvedNhops({{"fe80::2", InterfaceID(2)}});
  const auto unresolvedId1 = allocUnresolvedSet(nhops1);
  const auto unresolvedId2 = allocUnresolvedSet(nhops2);

  const folly::CIDRNetworkV6 key1{folly::IPAddressV6("fc00:100::1"), 48};
  const folly::CIDRNetworkV6 key2{folly::IPAddressV6("fc00:200::1"), 48};

  auto mySid1 = makeMySid("fc00:100::1", 48);
  mySid1->setUnresolveNextHopsId(unresolvedId1);
  mySidTable_[key1] = mySid1;

  auto mySid2 = makeMySid("fc00:200::1", 48);
  mySid2->setUnresolveNextHopsId(unresolvedId2);
  mySidTable_[key2] = mySid2;

  const std::set<folly::CIDRNetwork> filter{
      {folly::IPAddress("fc00:100::1"), 48}};

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve(filter);

  EXPECT_TRUE(mySidTable_.at(key1)->getResolvedNextHopsId().has_value());
  EXPECT_FALSE(mySidTable_.at(key2)->getResolvedNextHopsId().has_value());
}

TEST_F(RibMySidUpdaterTest, resolveFiltered_cidrNotInTable_skipped) {
  // A CIDR in the filter that does not exist in the MySidTable should be
  // gracefully skipped (no crash, table unchanged).
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = makeMySid("fc00:100::1", 48);

  const std::set<folly::CIDRNetwork> filter{
      {folly::IPAddress("fc00:999::1"), 48}};

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve(filter);

  EXPECT_FALSE(mySidTable_.at(key)->getResolvedNextHopsId().has_value());
}

TEST_F(RibMySidUpdaterTest, resolveFiltered_entryWithNoUnresolvedId_skipped) {
  // An entry in the filter that has no unresolvedNextHopsId should be skipped.
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = makeMySid("fc00:100::1", 48); // no unresolvedId set

  const std::set<folly::CIDRNetwork> filter{
      {folly::IPAddress("fc00:100::1"), 48}};

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve(filter);

  EXPECT_FALSE(mySidTable_.at(key)->getResolvedNextHopsId().has_value());
}

TEST_F(
    RibMySidUpdaterTest,
    gatewayNhopMatchingConnectedRoute_resolvedWithGatewayAddr) {
  // Gateway nexthop resolved via a connected (interface) route. The resolved
  // next hop must use the original gateway address paired with the connected
  // route's interface — not the route's own forwarding next hop address.
  const InterfaceID connectedIntf{10};
  const folly::IPAddress gatewayAddr("2001:db8::1");

  auto connectedRoute =
      makeConnectedV6Route(folly::IPAddressV6("2001:db8::"), 32, connectedIntf);
  v6Routes_.insert(connectedRoute->prefix(), connectedRoute);

  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(gatewayAddr, ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());
  const auto resolvedNhops = manager().getNextHopsIf(*resolvedId);
  ASSERT_TRUE(resolvedNhops.has_value());
  ASSERT_EQ(resolvedNhops->size(), 1);

  // normalizedNextHops() converts ECMP_WEIGHT(0) to 1 for a single next hop.
  const RouteNextHopSet expected{
      ResolvedNextHop(gatewayAddr, connectedIntf, NextHopWeight(1))};
  EXPECT_EQ(*resolvedNhops, expected);
}

TEST_F(
    RibMySidUpdaterTest,
    primaryAndBackupNhopsResolveOverConnectedAndOpenrRoutes) {
  const InterfaceID connectedIntf{1};
  const InterfaceID openrIntf{2};
  const folly::IPAddress connectedGateway("2001:db8:1::20");
  const folly::IPAddress openrGateway("2001:db8:3::20");
  const folly::IPAddress openrNextHop("2001:db8:2::10");

  RibRouteUpdater routeUpdater(
      &v4Routes_, &v6Routes_, &manager(), nullptr, kEcmpWidth);
  const RouteNextHopSet connectedRouteNhops{ResolvedNextHop(
      folly::IPAddress("2001:db8:1::1"), connectedIntf, UCMP_DEFAULT_WEIGHT)};
  const RouteNextHopSet openrUnderlayRouteNhops{ResolvedNextHop(
      folly::IPAddress("2001:db8:2::1"), openrIntf, UCMP_DEFAULT_WEIGHT)};
  routeUpdater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{folly::IPAddress("2001:db8:1::"), 64},
           RouteNextHopEntry(
               connectedRouteNhops, AdminDistance::DIRECTLY_CONNECTED)},
          {{folly::IPAddress("2001:db8:2::"), 64},
           RouteNextHopEntry(
               openrUnderlayRouteNhops, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);
  const RouteNextHopSet openrRouteNhops{
      UnresolvedNextHop(openrNextHop, ECMP_WEIGHT)};
  routeUpdater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {{{folly::IPAddress("2001:db8:3::"), 64},
        RouteNextHopEntry(openrRouteNhops, AdminDistance::OPENR)}},
      {},
      false);

  const auto makeUnresolvedNextHop = [](const folly::IPAddress& address,
                                        NextHopRole role) {
    return UnresolvedNextHop(
        address,
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        role);
  };
  const RouteNextHopSet primaryUnresolvedNhops{
      makeUnresolvedNextHop(connectedGateway, NextHopRole::PRIMARY),
      makeUnresolvedNextHop(openrGateway, NextHopRole::PRIMARY)};
  const RouteNextHopSet backupUnresolvedNhops{
      makeUnresolvedNextHop(connectedGateway, NextHopRole::BACKUP),
      makeUnresolvedNextHop(openrGateway, NextHopRole::BACKUP)};
  const auto primaryUnresolvedId = allocUnresolvedSet(primaryUnresolvedNhops);
  const auto backupUnresolvedId = allocUnresolvedSet(backupUnresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48, MySidType::ADJACENCY_MICRO_SID);
  mySid->setUnresolveNextHopsId(primaryUnresolvedId);
  mySid->setBackupUnresolveNextHopsId(backupUnresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  const std::set<std::pair<folly::IPAddress, InterfaceID>> expectedNextHops{
      {connectedGateway, connectedIntf}, {openrNextHop, openrIntf}};
  const auto expectResolvedNextHops = [this, &expectedNextHops](
                                          std::optional<NextHopSetID> id,
                                          NextHopRole role) {
    ASSERT_TRUE(id.has_value());
    const auto nextHops = manager().getNextHops(*id);
    ASSERT_EQ(nextHops.size(), expectedNextHops.size());
    std::set<std::pair<folly::IPAddress, InterfaceID>> actualNextHops;
    for (const auto& nextHop : nextHops) {
      actualNextHops.emplace(nextHop.addr(), nextHop.intf());
      EXPECT_EQ(nextHop.role(), role);
    }
    EXPECT_EQ(actualNextHops, expectedNextHops);
  };
  expectResolvedNextHops(
      mySidTable_.at(key)->getResolvedNextHopsId(), NextHopRole::PRIMARY);
  expectResolvedNextHops(
      mySidTable_.at(key)->getBackupResolvedNextHopsId(), NextHopRole::BACKUP);
}

TEST_F(
    RibMySidUpdaterTest,
    multiVrf_nhopResolvedViaSecondVrf_resolvedSetIdAllocated) {
  // Gateway nexthop that has no match in the first VRF's route table but
  // matches a route in the second VRF. The updater should resolve via the
  // second VRF.
  IPv4NetworkToRouteMap v4Routes2;
  IPv6NetworkToRouteMap v6Routes2;

  const auto routeNhops = makeResolvedNhops(
      {{"fe80::1", InterfaceID(1)}, {"fe80::2", InterfaceID(2)}});
  auto v6Route =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8::"), 32, routeNhops);
  // Route lives only in the second VRF's table.
  v6Routes2.insert(v6Route->prefix(), v6Route);

  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  // Pass two VRFs: first has empty tables, second has the matching route.
  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}, {&v4Routes2, &v6Routes2}},
      &manager(),
      &mySidTable_,
      kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());
  const auto resolvedNhops = manager().getNextHopsIf(*resolvedId);
  ASSERT_TRUE(resolvedNhops.has_value());
  EXPECT_EQ(resolvedNhops->size(), routeNhops.size());
}

TEST_F(RibMySidUpdaterTest, multiVrf_nhopMatchInFirstVrf_firstVrfWins) {
  // When both VRFs have a matching route, the first VRF's route should be used.
  IPv4NetworkToRouteMap v4Routes2;
  IPv6NetworkToRouteMap v6Routes2;

  const auto nhops1 = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto nhops2 = makeResolvedNhops({{"fe80::2", InterfaceID(2)}});
  auto v6Route1 =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8::"), 32, nhops1);
  auto v6Route2 =
      makeResolvedV6Route(folly::IPAddressV6("2001:db8::"), 32, nhops2);
  v6Routes_.insert(v6Route1->prefix(), v6Route1);
  v6Routes2.insert(v6Route2->prefix(), v6Route2);

  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}, {&v4Routes2, &v6Routes2}},
      &manager(),
      &mySidTable_,
      kEcmpWidth);
  updater.resolve();

  const auto resolvedId = mySidTable_.at(key)->getResolvedNextHopsId();
  ASSERT_TRUE(resolvedId.has_value());
  const auto resolvedNhops = manager().getNextHopsIf(*resolvedId);
  ASSERT_TRUE(resolvedNhops.has_value());
  // Should resolve via the first VRF's route (fe80::1 on intf 1, not fe80::2).
  ASSERT_EQ(resolvedNhops->size(), 1);
  EXPECT_EQ(resolvedNhops->begin()->addr(), folly::IPAddress("fe80::1"));
}

TEST_F(RibMySidUpdaterTest, resolve_publishesEntryWithNoUnresolvedId) {
  // An entry with no unresolvedNextHopsId should still be published after
  // resolve() — matching the RouteUpdater pattern of publishing all processed
  // nodes regardless of whether they changed.
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = makeMySid("fc00:100::1", 48); // no unresolvedId

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  EXPECT_TRUE(mySidTable_.at(key)->isPublished());
}

TEST_F(RibMySidUpdaterTest, resolve_publishesEntryAfterResolvingNhops) {
  // An entry whose nexthops are resolved should be published after resolve().
  const auto nhops = makeResolvedNhops({{"fe80::1", InterfaceID(1)}});
  const auto unresolvedId = allocUnresolvedSet(nhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  EXPECT_TRUE(mySidTable_.at(key)->isPublished());
}

TEST_F(RibMySidUpdaterTest, resolve_publishesEntryWhenNhopCantBeResolved) {
  // An entry whose gateway nexthop has no matching route (empty resolved set)
  // should still be published after resolve().
  const RouteNextHopSet unresolvedNhops{
      UnresolvedNextHop(folly::IPAddress("2001:db8::1"), ECMP_WEIGHT)};
  const auto unresolvedId = allocUnresolvedSet(unresolvedNhops);

  auto mySid = makeMySid("fc00:100::1", 48);
  mySid->setUnresolveNextHopsId(unresolvedId);
  const folly::CIDRNetworkV6 key{folly::IPAddressV6("fc00:100::1"), 48};
  mySidTable_[key] = mySid;

  RibMySidUpdater updater(
      {{&v4Routes_, &v6Routes_}}, &manager(), &mySidTable_, kEcmpWidth);
  updater.resolve();

  EXPECT_FALSE(mySidTable_.at(key)->getResolvedNextHopsId().has_value());
  EXPECT_TRUE(mySidTable_.at(key)->isPublished());
}

} // namespace facebook::fboss
