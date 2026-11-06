/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/test/BaseEcmpResourceManagerTest.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"

namespace facebook::fboss {

class EcmpBackupGroupTypeTest : public BaseEcmpResourceManagerTest {
 public:
  std::shared_ptr<EcmpResourceManager> makeResourceMgr() const override {
    static constexpr auto kEcmpGroupHwLimit = 7;
    return makeResourceMgrWithEcmpLimit(kEcmpGroupHwLimit);
  }
  static constexpr auto kNumStartRoutes = 5;

  int numStartRoutes() const override {
    return kNumStartRoutes;
  }
  std::vector<RouteNextHopSet> defaultNhopSets() const {
    std::vector<RouteNextHopSet> defaultNhopGroups;
    auto beginNhops = defaultNhops();
    for (auto i = 0; i < numStartRoutes(); ++i) {
      defaultNhopGroups.push_back(beginNhops);
      beginNhops.erase(beginNhops.begin());
      CHECK_GT(beginNhops.size(), 1);
    }
    return defaultNhopGroups;
  }

  std::vector<RouteNextHopSet> nextNhopSets(int numSets = kNumStartRoutes) {
    auto nhopsStart = defaultNhopSets().back();
    std::vector<RouteNextHopSet> nhopsTo;
    for (auto i = 0; i < numSets; ++i) {
      nhopsStart.erase(nhopsStart.begin());
      CHECK_GT(nhopsStart.size(), 1);
      nhopsTo.push_back(nhopsStart);
    }
    return nhopsTo;
  }

  void SetUp() override {
    BaseEcmpResourceManagerTest::SetUp();
    XLOG(DBG2) << "EcmpResourceMgrBackupGrpTest SetUp";
    CHECK(state_->isPublished());
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto newNhops = defaultNhopSets();
    CHECK_EQ(getPostConfigResolvedRoutes(newState).size(), newNhops.size());
    auto idx = 0;
    for (const auto& route : getPostConfigResolvedRoutes(state_)) {
      auto newRoute = route->clone();
      newRoute->setResolved(
          RouteNextHopEntry(newNhops[idx++], kDefaultAdminDistance));
      newRoute->publish();
      fib6->updateNode(newRoute);
    }
    newState->publish();
    consolidate(newState);
    assertEndState(newState, {});
    XLOG(DBG2) << "EcmpResourceMgrBackupGrpTest SetUp done";
  }

  // Adds one route per new nhop group past the ECMP limit, in nextNhopSets()
  // order, so each lands in its own backup group.
  std::vector<RouteV6::Prefix> addBackupGroupRoutes() {
    auto nhopSets = nextNhopSets();
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = getPostConfigResolvedRoutes(newState).size();
    std::vector<RouteV6::Prefix> prefixes;
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
      prefixes.push_back(route->prefix());
      fib6->addNode(route);
    }
    consolidate(newState);
    assertEndState(newState, {prefixes.begin(), prefixes.end()});
    return prefixes;
  }

  // Re-pushes pfx carrying staleMode, as a rib that has not yet synced a
  // backup switching mode change does, and returns the mode ERM programs.
  std::optional<cfg::SwitchingMode> pushStaleBackupMode(
      const RouteV6::Prefix& pfx,
      cfg::SwitchingMode staleMode) {
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto staleRoute = fib6->exactMatch(pfx)->clone();
    const auto& fwd = staleRoute->getForwardInfo();
    RouteNextHopEntry staleFwd(
        fwd.getNextHopSet(),
        fwd.getAdminDistance(),
        fwd.getCounterID(),
        fwd.getClassID(),
        std::optional<cfg::SwitchingMode>(staleMode),
        fwd.getOverrideNextHops(),
        fwd.getNormalizedResolvedNextHopSetID(),
        fwd.getResolvedNextHopSetID());
    staleRoute->setResolved(staleFwd);
    staleRoute->publish();
    fib6->updateNode(staleRoute);
    newState->publish();
    auto deltas = consolidator_->consolidate(StateDelta(state_, newState));
    consolidator_->updateDone();
    return cfib(deltas.back().newState())
        ->exactMatch(pfx)
        ->getForwardInfo()
        .getOverrideEcmpSwitchingMode();
  }
  static constexpr auto kStaleBackupMode = cfg::SwitchingMode::FIXED_ASSIGNMENT;
};

TEST_F(EcmpBackupGroupTypeTest, addSingleNhopRoutesBelowEcmpLimit) {
  // add new routes pointing to single nhops, no limit is thus breached
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  auto nhopSet = defaultNhops();
  auto nhopItr = nhopSet.begin();
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route =
        makeRoute(makePrefix(routesBefore + i), RouteNextHopSet{*nhopItr});
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, addRemoveSingleNhopRoutesBelowEcmpLimit) {
  std::set<RouteV6::Prefix> singleNhopPrefixes;
  // add new routes pointing to single nhops, no limit is thus breached
  {
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto routesBefore = getPostConfigResolvedRoutes(newState).size();
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto route =
          makeRoute(makePrefix(routesBefore + i), RouteNextHopSet{*nhopItr});
      singleNhopPrefixes.insert(route->prefix());
      fib6->addNode(route);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    // Remove single nhop routes.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    for (const auto& pfx : singleNhopPrefixes) {
      auto route = fib6->getRouteIf(pfx);
      fib6->removeNode(route);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}
// Convert routes to single nhop and back to mnhops. No spillover before
// and after
TEST_F(EcmpBackupGroupTypeTest, updateRouteBelowEcmpLimitToSingleNhop) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  {
    // Update all routes to a single NHOP. No limit is breached before
    // or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Update all routes back to mnhops. No limit breached before or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = defaultNhopSets();
    auto nhopSetItr = nhopSets.begin();
    for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(
          RouteNextHopEntry(*nhopSetItr, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRoutesSingleNhopToSingleNhop) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  {
    // Update all routes to a single NHOP. No limit is breached before
    // or after.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.begin();
    for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Update all routes to different single nhops, no limit is breached
    // before or after
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto nhopItr = nhopSet.rbegin();
    for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
}
// Convert spillover routes to single nhop. Now no spillover.
TEST_F(EcmpBackupGroupTypeTest, updateRouteAboveEcmpLimitToSingleNhop) {
  std::set<RouteV6::Prefix> addedPrefixes;
  {
    // Update a route pointing to new nhops. ECMP limit is breached during
    // update due to make before break. Then the reclaim step notices
    // a freed up primary ECMP group and reclaims it back. So in the
    // end no prefixes have back up ecmp group override set.
    auto nhopSets = nextNhopSets();
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto newRoute = (*getPostConfigResolvedRoutes(oldState).begin())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(*nhopSets.begin(), kDefaultAdminDistance));
    addedPrefixes.insert(newRoute->prefix());
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    // Route delta + reclaim delta
    EXPECT_EQ(deltas.size(), 2);
    assertEndState(newState, {});
  }
  {
    // Update the overflow route to single nhop. Should no longer
    // be of backupGroupType, since single nhop groups don't contribute
    // to ecmp groups
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSet = defaultNhops();
    auto newRoute = (*getPostConfigResolvedRoutes(oldState).begin())->clone();
    newRoute->setResolved(RouteNextHopEntry(
        RouteNextHopSet{*nhopSet.begin()}, kDefaultAdminDistance));
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    // Single delta, no spillover expected
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesBelowEcmpLimit) {
  // add new routes pointing to existing nhops. No limit is thus breached.
  auto nhopSets = defaultNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesAboveEcmpLimit) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, overflowPrefixes);
}

TEST_F(EcmpBackupGroupTypeTest, managedAndProtectionRoutesInSameDelta) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  const auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  const auto managedPrefix = makePrefix(routesBefore);
  const auto protectionPrefix = makePrefix(routesBefore + 1);
  const auto managedNhops = nextNhopSets(1).front();
  const auto protectionNhops = withBackupNextHops(defaultNhops());
  fib6->addNode(makeRoute(managedPrefix, managedNhops));
  fib6->addNode(makeRoute(protectionPrefix, protectionNhops));

  const auto deltas = consolidate(newState);

  ASSERT_FALSE(deltas.empty());
  assertEndState(newState, {managedPrefix});
  const auto protectionRoute = cfib(state_)->getRouteIf(protectionPrefix);
  ASSERT_NE(protectionRoute, nullptr);
  EXPECT_EQ(
      facebook::fboss::getNextHops(state_, protectionRoute->getForwardInfo()),
      protectionNhops);
  EXPECT_FALSE(
      protectionRoute->getForwardInfo().hasOverrideSwitchingModeOrNhops());
  EXPECT_EQ(
      consolidator_->getGroupInfo(
          RouterID(0), protectionPrefix.toCidrNetwork()),
      nullptr);
}

TEST_F(EcmpBackupGroupTypeTest, addRoutesAboveEcmpLimitAndSyncFibReplay) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(oldState).size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, overflowPrefixes);
  {
    // Replay state with new pointers for the overflow routes,
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (const auto& pfx : overflowPrefixes) {
      auto route = fib6->getRouteIf(pfx)->clone();
      // Clear any overrides. This test mimics routes
      // coming over thrift (say on FibSync) which would
      // come w/o any overrides set.
      route->setResolved(RouteNextHopEntry(
          facebook::fboss::getNextHops(state_, route->getForwardInfo()),
          kDefaultAdminDistance));
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newState);
    EXPECT_EQ(deltas2.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Replay state with new pointers for non overflow routes,
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned non-overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto route = fib6->getRouteIf(makePrefix(i))->clone();
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newerState);
    EXPECT_EQ(deltas2.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    // Replay state with new pointers for all routes
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned non-overflow routes, "
               << "should get identical result";
    auto newerState = state_->clone();
    fib6 = fib(newerState);
    for (const auto& origRoute : getPostConfigResolvedRoutes(state_)) {
      auto route = fib6->getRouteIf(origRoute->prefix())->clone();
      route->publish();
      fib6->updateNode(route);
    }
    auto deltas2 = consolidate(newerState);
    EXPECT_EQ(deltas2.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRoutesToSingleNhopGroups) {
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto nhopSet = defaultNhops();
  auto nhopItr = nhopSet.begin();
  for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
    auto newRoute = fib6->getRouteIf(route->prefix())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(RouteNextHopSet{*nhopItr++}, kDefaultAdminDistance));
    fib6->updateNode(newRoute);
  }
  auto deltas = consolidate(newState);
  // Single delta, no spillover expected
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateRouteBelowEcmpLimit) {
  // Update a route pointing to new nhops. But stay below ECMP limit
  {
    // Remove one route so we go below ECMP limit
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto rmRoute = (*getPostConfigResolvedRoutes(newState).begin())->clone();
    fib6->removeNode(rmRoute);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto newRoute = (*getPostConfigResolvedRoutes(newState).begin())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(*nextNhopSets().begin(), kDefaultAdminDistance));
    fib6->updateNode(newRoute);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateRouteAboveEcmpLimit) {
  // Update a route pointing to new nhops. ECMP limit is breached during
  // update due to make before break. Then the reclaim step notices
  // a freed up primary ECMP group and reclaims it back. So in the
  // end no prefixes have back up ecmp group override set.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto newRoute = (*getPostConfigResolvedRoutes(oldState).begin())->clone();
  std::set<RouteV6::Prefix> overflowPrefixes;
  newRoute->setResolved(
      RouteNextHopEntry(*nhopSets.begin(), kDefaultAdminDistance));
  fib6->updateNode(newRoute);
  auto deltas = consolidate(newState);
  // Initial update + overflow + reclaim
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesOneRouteAboveEcmpLimit) {
  // Update a route pointing to new nhops. ECMP limit is breached.
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  std::set<RouteV6::Prefix> overflowPrefixes;
  auto nhopSets = nextNhopSets();
  ASSERT_EQ(nhopSets.size(), getPostConfigResolvedRoutes(oldState).size());
  auto idx = 0;
  for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
    auto newRoute = fib6->getRouteIf(route->prefix())->clone();
    newRoute->setResolved(
        RouteNextHopEntry(nhopSets[idx++], kDefaultAdminDistance));
    fib6->updateNode(newRoute);
  }
  auto deltas = consolidate(newState);
  // 1 delta for routes + 1 delta for reclaim
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(newState, {});
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesAllRoutesAboveEcmpLimit) {
  std::set<RouteV6::Prefix> startPrefixes;
  for (const auto& route : getPostConfigResolvedRoutes(state_)) {
    startPrefixes.insert(route->prefix());
  }
  {
    // Add routes pointing to existing nhops. ECMP limit is not breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto existingNhopSets = defaultNhopSets();
    auto routesBefore = getPostConfigResolvedRoutes(oldState).size();
    for (auto i = 0; i < routesBefore; ++i) {
      auto newRoute =
          makeRoute(makePrefix(routesBefore + i), existingNhopSets[i]);
      fib6->addNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  /*
   * Routes before
   * R1, R5 - G1
   * R2, R6 - G2
   * R3, R7 - G3
   * R4, R8 - G4
   * R0, R9 - G5
   * Routes after
   * R1 - G6 (bkup)
   * R2 - G7 (bkup)
   * R3 - G8 (bkup)
   * R4 - G9 (bkup)
   * R5 - G1
   * R6 - G2
   * R7 - G3
   * R8 - G4
   * R9 - G5
   * R0 - G10 (bkup)
   *
   */
  {
    // Update 5 routes pointing to new nhops. ECMP limit is breached as
    // 2 routes pointing to new nhops get updated one by one. Later when
    // both routes get updated, one ecmp group gets removed. Later reclaim
    // step comes a moves all groups back to primary ecmp group type
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    std::set<RouteV6::Prefix> overflowPrefixes;
    auto nhopSets = nextNhopSets();
    auto idx = 0;
    for (const auto& prefix : startPrefixes) {
      auto newRoute = fib6->getRouteIf(prefix)->clone();
      newRoute->setResolved(
          RouteNextHopEntry(nhopSets[idx++], kDefaultAdminDistance));
      overflowPrefixes.insert(newRoute->prefix());
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    // Route delta, no reclaima
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
}

TEST_F(EcmpBackupGroupTypeTest, updateAllRoutesToNewNhopsAboveEcmpLimit) {
  {
    // Add routes pointing to existing nhops. ECMP limit is not breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto existingNhopSets = defaultNhopSets();
    auto routesBefore = getPostConfigResolvedRoutes(oldState).size();
    for (auto i = 0; i < routesBefore; ++i) {
      auto newRoute =
          makeRoute(makePrefix(routesBefore + i), existingNhopSets[i]);
      fib6->addNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  {
    /*
     * Initial state
     * R1, R5 -> G1
     * R2, R6 -> G2
     * R3, R7 -> G3
     * R4, R8 -> G4
     * R0, R9 -> G5
     * New state
     * R1 -> G6
     * R2 -> G7
     * R3 -> G8
     * R4 -> G9
     * R5 -> G10
     * R6 -> G10
     * R7 -> G9
     * R8 -> G8
     * R9 -> G7
     * R0 -> G6
     */
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    auto idx = 0;
    bool idxDirectionFwd = true;
    for (const auto& route : getPostConfigResolvedRoutes(oldState)) {
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(RouteNextHopEntry(
          nhopSets[idxDirectionFwd ? idx++ : idx--], kDefaultAdminDistance));
      fib6->updateNode(newRoute);
      if (idx == nhopSets.size()) {
        idxDirectionFwd = false;
        --idx;
      }
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 2);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpBackupGroupTypeTest, reclaimPrioritizesECMPWithMoreRoutes) {
  std::set<RouteV6::Prefix> overflowPrefixes;
  size_t routesStart = getPostConfigResolvedRoutes(state_).size();
  {
    /*
     * Initial state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * New state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * R5 -> G6 (backup group type)
     * R6 -> G7 (backup group type)
     */
    // Add 2 routes pointing to new nhops. ECMP limit is breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    ASSERT_EQ(nhopSets.size(), getPostConfigResolvedRoutes(newState).size());
    for (auto i = 0; i < 2; ++i) {
      auto route = makeRoute(makePrefix(routesStart + i), nhopSets[i]);
      overflowPrefixes.insert(route->prefix());
      fib6->addNode(route);
      auto newRoute = fib6->getRouteIf(route->prefix())->clone();
      newRoute->setResolved(
          RouteNextHopEntry(nhopSets[i], kDefaultAdminDistance));
      fib6->updateNode(newRoute);
    }
    auto deltas = consolidate(newState);
    // Routes delta - no reclaim
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, overflowPrefixes);
  }
  {
    /*
     * Old state
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G5
     * R5 -> G6 (backup group type)
     * R6 -> G7 (backup group type)
     * New State
     * R1 -> G1
     * R2 -> G2
     * R3 -> G3
     * R4 -> G4
     * R0 -> G7
     * R5 -> G6 (backup group type)
     * R6 -> G7
     * Notice that the group with 2 routes pointing to it is prioritized for
     * reclaim (over the group with one route pointing to it)
     */
    // Add 2 routes pointing to new nhops. ECMP limit is breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto nhopSets = nextNhopSets();
    auto prefixFrom =
        makePrefix(getPostConfigResolvedRoutes(newState).size() - 1);
    auto nhopsFrom = facebook::fboss::getNextHops(
        state_, fib6->getRouteIf(prefixFrom)->getForwardInfo());
    auto updateRoute = fib6->getRouteIf(makePrefix(0))->clone();
    updateRoute->setResolved(
        RouteNextHopEntry(nhopsFrom, kDefaultAdminDistance));
    fib6->updateNode(updateRoute);
    auto deltas = consolidate(newState);
    // Routes delta + reclaim
    EXPECT_EQ(deltas.size(), 2);
    // prefixFrom should now longer point to backupGroupType since it
    // will be reclaimed as one ECMP group gets freed due to update
    // of R0
    overflowPrefixes.erase(prefixFrom);
    assertEndState(newState, overflowPrefixes);
  }
}

// Backup switching mode change tests
TEST_F(EcmpBackupGroupTypeTest, changeSwitchingModeNoBackupEcmpRoutes) {
  // add new routes pointing to existing nhops. No limit is thus breached.
  auto oldState = state_;
  auto newState = oldState->clone();
  auto newFlowletSwitchingConfig =
      newState->getFlowletSwitchingConfig()->modify(&newState);
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  EXPECT_EQ(deltas.back(), StateDelta(oldState, newState));
  // No overflow
  assertEndState(newState, {});
  EXPECT_EQ(
      state_->getFlowletSwitchingConfig()->getBackupSwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_EQ(
      *consolidator_->getBackupEcmpSwitchingMode(),
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
}

TEST_F(EcmpBackupGroupTypeTest, overflowAndSwitchingModeChange) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  // Change backup ecmp switching mode
  auto newFlowletSwitchingConfig =
      newState->getFlowletSwitchingConfig()->modify(&newState);
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, overflowPrefixes);
}

TEST_F(EcmpBackupGroupTypeTest, overflowRoutesAndThenSwitchingModeChange) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, overflowPrefixes);
  auto newerState = newState->clone();
  // Change backup ecmp switching mode
  auto newFlowletSwitchingConfig =
      newerState->getFlowletSwitchingConfig()->modify(&newerState);
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  auto deltas2 = consolidate(newerState);
  EXPECT_EQ(deltas2.size(), 1);
  assertEndState(newerState, overflowPrefixes);
}

TEST_F(EcmpBackupGroupTypeTest, staleBackupModeRestampedWhenGroupRecreated) {
  ASSERT_NE(consolidator_->getBackupEcmpSwitchingMode(), kStaleBackupMode);
  // Each backup group has a single route, so the update frees and recreates
  // the group.
  auto prefixes = addBackupGroupRoutes();

  EXPECT_EQ(
      pushStaleBackupMode(prefixes.front(), kStaleBackupMode),
      consolidator_->getBackupEcmpSwitchingMode());
}

TEST_F(EcmpBackupGroupTypeTest, staleBackupModeRestampedInExistingGroup) {
  ASSERT_NE(consolidator_->getBackupEcmpSwitchingMode(), kStaleBackupMode);
  auto prefixes = addBackupGroupRoutes();
  // A second route keeps the backup group alive across the stale update.
  auto newState = state_->clone();
  auto sharedPrefix = makePrefix(getPostConfigResolvedRoutes(newState).size());
  fib(newState)->addNode(makeRoute(sharedPrefix, nextNhopSets().front()));
  consolidate(newState);
  std::set<RouteV6::Prefix> backupPrefixes(prefixes.begin(), prefixes.end());
  backupPrefixes.insert(sharedPrefix);
  assertEndState(newState, backupPrefixes);

  EXPECT_EQ(
      pushStaleBackupMode(prefixes.front(), kStaleBackupMode),
      consolidator_->getBackupEcmpSwitchingMode());
}

TEST_F(EcmpBackupGroupTypeTest, resetBackupSwitchingModeProhibited) {
  // Reset switching config mode from one value to null. Expect throw
  auto oldState = state_;
  auto newState = oldState->clone();
  auto switchSettings = newState->getSwitchSettings()
                            ->getNode(hwMatcher().matcherString())
                            ->modify(&newState);
  switchSettings->setFlowletSwitchingConfig(nullptr);
  auto newConsolidator = makeResourceMgr();
  newConsolidator->reconstructFromSwitchState(oldState);
  newConsolidator->updateDone();
  EXPECT_THROW(
      newConsolidator->consolidate(StateDelta(oldState, newState)), FbossError);
}

TEST_F(EcmpBackupGroupTypeTest, changeSwitchingModeAndFailUpdate) {
  // add new routes pointing to existing nhops. No limit is thus breached.
  auto oldState = state_;
  auto newState = oldState->clone();
  auto newFlowletSwitchingConfig =
      newState->getFlowletSwitchingConfig()->modify(&newState);
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  EXPECT_THROW(failUpdate(newState), FbossError);
}

// Replay state tests

TEST_F(EcmpBackupGroupTypeTest, overflowRoutesInReverseOrderOfReplay) {
  std::set<RouteV6::Prefix> startPrefixes;
  for (const auto& route : getPostConfigResolvedRoutes(state_)) {
    startPrefixes.insert(route->prefix());
  }
  // clear all routes
  {
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    fib6->clear();
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  /*
  State before
  - No routes
  State after
    R5 -> G1
    R6 -> G2
    R7 -> G3
    R8 -> G4
    R9 -> G5
  */
  {
    // Add routes pointing to default nhop sets ECMP limit is not breached.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto defaultNhops = defaultNhopSets();
    for (auto i = 0; i < startPrefixes.size(); ++i) {
      auto newRoute =
          makeRoute(makePrefix(startPrefixes.size() + i), defaultNhops[i]);
      fib6->addNode(newRoute);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, {});
  }
  /*
  State before
    R5 -> G1
    R6 -> G2
    R7 -> G3
    R8 -> G4
    R9 -> G5
  State after
    R0 -> G6 (bkup)
    R1 -> G7 (bkup)
    R2 -> G8 (bkup)
    R3 -> G9 (bkup)
    R4 -> G10 (bkup)
    R5 -> G1
    R6 -> G2
    R7 -> G3
    R8 -> G4
    R9 -> G5
  */
  {
    // Add new routes. These will go to backup nhop groups.
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto newNhops = nextNhopSets();
    auto idx = 0;
    for (const auto& pfx : startPrefixes) {
      auto newRoute = makeRoute(pfx, newNhops[idx++]);
      fib6->addNode(newRoute);
    }
    // BaseEcmpResourceManagerTest::consolidate does a replay
    // of newState and asserts that same set of prefixes gets
    // marked for backup ecmp group. Even though had we played
    // the current start from start routes from R5-R9 would have
    // ended up with backup ecmp group type nhops
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 1);
    assertEndState(newState, startPrefixes);
  }
}

TEST_F(EcmpBackupGroupTypeTest, reclaimOnReplay) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  assertEndState(newState, overflowPrefixes);
  auto newConsolidator = makeResourceMgrWithEcmpLimit(
      cfib(state_)->size() +
      FLAGS_ecmp_resource_manager_make_before_break_buffer);
  auto replayDeltas = newConsolidator->reconstructFromSwitchState(state_);
  ASSERT_EQ(replayDeltas.size(), 1);
  // No overflow, since we increased the limit to cover all the prefixes
  assertTargetState(
      replayDeltas.back().newState(),
      state_,
      {},
      newConsolidator.get(),
      false /*checkStats*/);
}

TEST_F(EcmpBackupGroupTypeTest, checkPrimaryEcmpExhaustedEvents) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  CounterCache counters(sw_);
  auto nhopSets = nextNhopSets();
  auto oldState = state_;
  auto newState = oldState->clone();
  auto fib6 = fib(newState);
  auto routesBefore = getPostConfigResolvedRoutes(newState).size();
  auto route = makeRoute(makePrefix(routesBefore), *nhopSets.begin());
  fib6->addNode(route);
  consolidate(newState);
  counters.update();
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "primary_ecmp_groups_exhausted_events.sum",
      1);
}

// Shared base for the virtual-ARS fixtures, which build states by hand and feed
// them to a standalone EcmpResourceManager. Stamps NextHop IDs on each state
// before consolidate (mirrors EcmpResourceManagerDsfScaleTest) so the
// consolidator can resolve nexthops under FLAGS_resolve_nexthops_from_id.
class EcmpVirtualArsTestBase : public BaseEcmpResourceManagerTest {
 public:
  void SetUp() override {
    BaseEcmpResourceManagerTest::SetUp();
    if (FLAGS_enable_nexthop_id_manager) {
      nextHopIDManager_ = std::make_unique<NextHopIDManager>();
    }
  }

  std::vector<StateDelta> consolidateWithIds(
      EcmpResourceManager& mgr,
      const std::shared_ptr<SwitchState>& curState,
      std::shared_ptr<SwitchState> nextState) {
    assignNextHopIdsToAllRoutes(nextHopIDManager_.get(), nextState);
    nextState->publish();
    return mgr.consolidate(StateDelta(curState, nextState));
  }

  std::unique_ptr<NextHopIDManager> nextHopIDManager_;
};

class EcmpVirtualArsGroupTest : public EcmpVirtualArsTestBase {
 public:
  static constexpr uint32_t kMaxVirtualGroups = 5; // effective: 5 - 2 = 3
  static constexpr int32_t kMinWidthForVirtual = 2;
  static constexpr int32_t kMaxVirtualGroupWidth = 16;
  static constexpr int32_t kMaxEcmpWidth = 16;
  static constexpr uint32_t kMaxHwEcmpGroups = 100;

  int numStartRoutes() const override {
    return 0;
  }
  std::shared_ptr<EcmpResourceManager> makeResourceMgr() const override {
    return std::make_shared<EcmpResourceManager>(
        kMaxHwEcmpGroups,
        cfg::SwitchingMode::PER_PACKET_RANDOM,
        std::make_optional<uint32_t>(kMaxVirtualGroups),
        std::make_optional<int32_t>(kMinWidthForVirtual),
        std::make_optional<int32_t>(kMaxVirtualGroupWidth),
        std::make_optional<int32_t>(kMaxEcmpWidth));
  }
};

TEST_F(EcmpVirtualArsGroupTest, virtualLimitTriggersBackupOverride) {
  // Pin the make-before-break buffer so other tests can't shift our limit.
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto baseNhops = defaultNhops();
  std::vector<RouteNextHopSet> distinctWideNhops;
  for (int i = 0; i < 5; i++) {
    distinctWideNhops.push_back(baseNhops);
    baseNhops.erase(baseNhops.begin());
    CHECK_GT(baseNhops.size(), 1);
  }

  auto mgr = makeResourceMgr();
  ASSERT_EQ(
      mgr->getBackupEcmpSwitchingMode(), cfg::SwitchingMode::PER_PACKET_RANDOM);

  // Use mgr->consolidate directly; the SwSwitch consolidator lacks virtual
  // config.
  auto curState = state_;
  std::vector<RouteV6::Prefix> prefixes;
  for (int i = 0; i < 5; i++) {
    auto pfx = makePrefix(i);
    prefixes.push_back(pfx);
    auto nextState = curState->clone();
    auto fib6 = fib(nextState);
    fib6->addNode(pfx.str(), makeRoute(pfx, distinctWideNhops[i]));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }

  // Effective limit = kMaxVirtualGroups - MBB = 3; routes 4 and 5 go to backup.
  int virtualCount = 0;
  int backupCount = 0;
  for (const auto& pfx : prefixes) {
    auto route = cfib(curState)->getRouteIf(pfx);
    ASSERT_NE(route, nullptr) << " missing route for " << pfx.str();
    auto overrideMode = route->getForwardInfo().getOverrideEcmpSwitchingMode();
    if (overrideMode.has_value()) {
      EXPECT_EQ(*overrideMode, cfg::SwitchingMode::PER_PACKET_RANDOM);
      ++backupCount;
    } else {
      ++virtualCount;
    }
  }
  EXPECT_EQ(virtualCount, 3);
  EXPECT_EQ(backupCount, 2);

  // All groups pay per-nhop cost: 3 virtual (20+19+18) + 2 backup (17+16).
  auto [primary, virtualGroups, members] = mgr->getPrimaryEcmpAndMemberCounts();
  EXPECT_EQ(virtualGroups, 3u);
  EXPECT_EQ(primary, 0u);
  EXPECT_EQ(members, 20u + 19u + 18u + 17u + 16u);
}

TEST_F(EcmpVirtualArsGroupTest, reclaimBackupVirtualGroupCounterConsistency) {
  // Verify add → reclaim member accounting is symmetric: reclaimBackupGroups
  // subtracts numNhops posted by the backup-add path.
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto baseNhops = defaultNhops();
  std::vector<RouteNextHopSet> distinctWideNhops;
  for (int i = 0; i < 5; i++) {
    distinctWideNhops.push_back(baseNhops);
    baseNhops.erase(baseNhops.begin());
    CHECK_GT(baseNhops.size(), 1);
  }

  auto mgr = makeResourceMgr();

  // Phase 1: add 5 wide routes → 3 virtual + 2 backup.
  auto curState = state_;
  std::vector<RouteV6::Prefix> prefixes;
  for (int i = 0; i < 5; i++) {
    auto pfx = makePrefix(i);
    prefixes.push_back(pfx);
    auto nextState = curState->clone();
    fib(nextState)->addNode(pfx.str(), makeRoute(pfx, distinctWideNhops[i]));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }
  {
    auto [primary, virtualGroups, members] =
        mgr->getPrimaryEcmpAndMemberCounts();
    ASSERT_EQ(virtualGroups, 3u);
    ASSERT_GT(members, 0u); // backup virtual groups must contribute members
  }

  // Phase 2: delete one virtual route, freeing a slot for reclaim.
  {
    auto nextState = curState->clone();
    fib(nextState)->removeNode(prefixes[0].str());
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }

  int virtualCount = 0;
  int backupCount = 0;
  for (size_t i = 1; i < prefixes.size(); i++) {
    auto route = cfib(curState)->getRouteIf(prefixes[i]);
    ASSERT_NE(route, nullptr);
    if (route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
      ++backupCount;
    } else {
      ++virtualCount;
    }
  }
  EXPECT_EQ(virtualCount, 3);
  EXPECT_EQ(backupCount, 1);

  auto [primary, virtualGroups, members] = mgr->getPrimaryEcmpAndMemberCounts();
  EXPECT_EQ(virtualGroups, 3u);
  EXPECT_GT(members, 0u);
  EXPECT_LT(members, 1000u); // guard against uint32_t underflow

  // Phase 3: drain all virtual routes; verify counters fully unwind.
  for (size_t i = 1; i < prefixes.size(); i++) {
    auto route = cfib(curState)->getRouteIf(prefixes[i]);
    if (!route ||
        route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
      continue; // skip backup routes; drain virtual only
    }
    auto nextState = curState->clone();
    fib(nextState)->removeNode(prefixes[i].str());
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }
  auto [primaryAfter, virtualAfter, membersAfter] =
      mgr->getPrimaryEcmpAndMemberCounts();
  EXPECT_EQ(virtualAfter, 0u);
  EXPECT_EQ(primaryAfter, 0u);
  EXPECT_EQ(membersAfter, 0u);
}

TEST_F(EcmpVirtualArsGroupTest, directDeleteBackupGroupDecrementsMembers) {
  // routeDeleted must decrement ecmpMemberCnt for backup virtual
  // groups even when reclaim doesn't run.
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto baseNhops = defaultNhops();
  std::vector<RouteNextHopSet> distinctWideNhops;
  for (int i = 0; i < 5; i++) {
    distinctWideNhops.push_back(baseNhops);
    baseNhops.erase(baseNhops.begin());
    CHECK_GT(baseNhops.size(), 1);
  }

  auto mgr = makeResourceMgr();

  auto curState = state_;
  std::vector<RouteV6::Prefix> prefixes;
  for (int i = 0; i < 5; i++) {
    auto pfx = makePrefix(i);
    prefixes.push_back(pfx);
    auto nextState = curState->clone();
    fib(nextState)->addNode(pfx.str(), makeRoute(pfx, distinctWideNhops[i]));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }

  {
    auto [p, v, m] = mgr->getPrimaryEcmpAndMemberCounts();
    ASSERT_EQ(v, 3u);
    // All 5 groups (virtual + backup) contribute member cost.
    ASSERT_EQ(
        m,
        distinctWideNhops[0].size() + distinctWideNhops[1].size() +
            distinctWideNhops[2].size() + distinctWideNhops[3].size() +
            distinctWideNhops[4].size());
  }

  // Delete backup route directly; no virtual slot freed so reclaim doesn't run.
  auto nextState = curState->clone();
  fib(nextState)->removeNode(prefixes[3].str());
  auto deltas = consolidateWithIds(*mgr, curState, nextState);
  ASSERT_FALSE(deltas.empty());

  auto [p2, v2, m2] = mgr->getPrimaryEcmpAndMemberCounts();
  EXPECT_EQ(v2, 3u);
  EXPECT_EQ(p2, 0u);
  // Backup group at index 3 removed; remaining 4 groups still contribute.
  EXPECT_EQ(
      m2,
      distinctWideNhops[0].size() + distinctWideNhops[1].size() +
          distinctWideNhops[2].size() + distinctWideNhops[4].size());
}

TEST_F(EcmpVirtualArsGroupTest, typeAwareReclaimRespectsVirtualHeadroom) {
  // Freeing one virtual slot must reclaim exactly one backup virtual group,
  // not both (canReclaimVirtual exhausted after the first reclaim).
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto baseNhops = defaultNhops();
  std::vector<RouteNextHopSet> distinctWideNhops;
  for (int i = 0; i < 5; i++) {
    distinctWideNhops.push_back(baseNhops);
    baseNhops.erase(baseNhops.begin());
    CHECK_GT(baseNhops.size(), 1);
  }

  auto mgr = makeResourceMgr();

  // Phase 1: 5 wide routes -> 3 virtual + 2 backup.
  auto curState = state_;
  std::vector<RouteV6::Prefix> prefixes;
  for (int i = 0; i < 5; i++) {
    auto pfx = makePrefix(i);
    prefixes.push_back(pfx);
    auto nextState = curState->clone();
    fib(nextState)->addNode(pfx.str(), makeRoute(pfx, distinctWideNhops[i]));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }
  {
    int virtualCount = 0;
    int backupCount = 0;
    for (const auto& pfx : prefixes) {
      auto route = cfib(curState)->getRouteIf(pfx);
      ASSERT_NE(route, nullptr);
      if (route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
        ++backupCount;
      } else {
        ++virtualCount;
      }
    }
    ASSERT_EQ(virtualCount, 3);
    ASSERT_EQ(backupCount, 2);
  }

  // Phase 2: delete one virtual route → canReclaimVirtual = 1.
  auto nextState = curState->clone();
  fib(nextState)->removeNode(prefixes[0].str());
  auto deltas = consolidateWithIds(*mgr, curState, nextState);
  ASSERT_FALSE(deltas.empty());
  curState = deltas.back().newState();

  int virtualCount = 0;
  int backupCount = 0;
  for (size_t i = 1; i < prefixes.size(); i++) {
    auto route = cfib(curState)->getRouteIf(prefixes[i]);
    ASSERT_NE(route, nullptr);
    if (route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
      EXPECT_EQ(
          *route->getForwardInfo().getOverrideEcmpSwitchingMode(),
          cfg::SwitchingMode::PER_PACKET_RANDOM);
      ++backupCount;
    } else {
      ++virtualCount;
    }
  }
  EXPECT_EQ(virtualCount, 3);
  EXPECT_EQ(backupCount, 1);
}

class EcmpVirtualArsMixedReclaimTest : public EcmpVirtualArsTestBase {
 public:
  static constexpr uint32_t kMaxVirtualGroups = 4; // effective: 4 - 2 = 2
  static constexpr int32_t kMinWidthForVirtual = 8; // width 2..7 = non-virtual
  static constexpr int32_t kMaxVirtualGroupWidth = 16;
  static constexpr int32_t kMaxEcmpWidth = 16;
  static constexpr uint32_t kMaxHwEcmpGroups = 5; // effective: 5 - 2 = 3

  int numStartRoutes() const override {
    return 0;
  }
  std::shared_ptr<EcmpResourceManager> makeResourceMgr() const override {
    return std::make_shared<EcmpResourceManager>(
        kMaxHwEcmpGroups,
        cfg::SwitchingMode::PER_PACKET_RANDOM,
        std::make_optional<uint32_t>(kMaxVirtualGroups),
        std::make_optional<int32_t>(kMinWidthForVirtual),
        std::make_optional<int32_t>(kMaxVirtualGroupWidth),
        std::make_optional<int32_t>(kMaxEcmpWidth));
  }
};

TEST_F(
    EcmpVirtualArsMixedReclaimTest,
    perTypeFilterIsSoleGate_NotPreTruncatedByCost) {
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto allNhops = defaultNhops();
  ASSERT_GE(allNhops.size(), 12u)
      << " test needs at least 12 distinct nhops; defaultNhops gave "
      << allNhops.size();

  std::vector<RouteNextHopSet> wideNhopSets;
  for (int i = 0; i < 3; i++) {
    RouteNextHopSet wide;
    auto it = std::next(allNhops.begin(), i);
    for (int j = 0; j < kMinWidthForVirtual && it != allNhops.end();
         j++, ++it) {
      wide.insert(*it);
    }
    ASSERT_GE(wide.size(), static_cast<size_t>(kMinWidthForVirtual));
    wideNhopSets.push_back(wide);
  }
  // Narrow sets (width 2) from the tail to avoid overlap with wide sets.
  std::vector<RouteNextHopSet> narrowNhopSets;
  {
    auto rit = allNhops.rbegin();
    for (int i = 0; i < 3; i++) {
      RouteNextHopSet narrow;
      narrow.insert(*rit++);
      narrow.insert(*rit++);
      ASSERT_LT(narrow.size(), static_cast<size_t>(kMinWidthForVirtual));
      narrowNhopSets.push_back(narrow);
    }
  }

  auto mgr = makeResourceMgr();
  auto curState = state_;
  auto addRouteAt = [&](int prefixIdx, const RouteNextHopSet& nhops) {
    auto pfx = makePrefix(prefixIdx);
    auto nextState = curState->clone();
    fib(nextState)->addNode(pfx.str(), makeRoute(pfx, nhops));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  };
  auto isBackup = [&](int prefixIdx) {
    auto route = cfib(curState)->getRouteIf(makePrefix(prefixIdx));
    return route &&
        route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value();
  };

  // Phase 1: 2 distinct wide routes -> virtual pool full at cap 2.
  addRouteAt(0, wideNhopSets[0]);
  addRouteAt(1, wideNhopSets[1]);

  // Phase 2: 3 routes sharing wideNhopSets[2] → virtual overflow, group cost=3.
  addRouteAt(2, wideNhopSets[2]);
  addRouteAt(3, wideNhopSets[2]);
  addRouteAt(4, wideNhopSets[2]);
  ASSERT_TRUE(isBackup(2));
  ASSERT_TRUE(isBackup(3));
  ASSERT_TRUE(isBackup(4));

  // Phase 3: narrow routes — 2 fit in primary, 3rd overflows to backup (cost
  // 1).
  addRouteAt(5, narrowNhopSets[0]);
  addRouteAt(6, narrowNhopSets[1]);
  addRouteAt(7, narrowNhopSets[2]);
  ASSERT_FALSE(isBackup(5));
  ASSERT_FALSE(isBackup(6));
  ASSERT_TRUE(isBackup(7))
      << " narrow-3 must overflow primary into backup for the repro";

  // Phase 4: delete narrow-1 → canReclaimNonVirtual=1, canReclaimVirtual=0.
  {
    auto pfx = makePrefix(5);
    auto nextState = curState->clone();
    fib(nextState)->removeNode(pfx.str());
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }

  EXPECT_FALSE(isBackup(7))
      << " narrow-3 should have been reclaimed back to primary; pre-fix "
         "the cost-ordered resize starved it because the higher-cost "
         "virtual backup group truncated it out before the per-type "
         "filter ran.";
  EXPECT_TRUE(isBackup(2));
  EXPECT_TRUE(isBackup(3));
  EXPECT_TRUE(isBackup(4));
  EXPECT_FALSE(isBackup(6));
}

TEST_F(
    EcmpVirtualArsMixedReclaimTest,
    reclaimExhaustedBothVirtualAndNonVirtualBudgets) {
  // When canReclaimNonVirtual=1 and canReclaimVirtual=1 simultaneously,
  // reclaimEcmpGroups must promote exactly one backup from each type —
  // neither budget starves the other.
  //
  // maxPrimaryEcmpGroups = kMaxHwEcmpGroups(5) - MBB(2) - collective(1) = 2
  // maxVirtualEcmpGroups = kMaxVirtualGroups(4) - MBB(2) = 2
  FLAGS_ecmp_resource_manager_make_before_break_buffer = 2;

  auto allNhops = defaultNhops();
  ASSERT_GE(allNhops.size(), 12u);

  std::vector<RouteNextHopSet> wideNhopSets;
  for (int i = 0; i < 3; i++) {
    RouteNextHopSet wide;
    auto it = std::next(allNhops.begin(), i);
    for (int j = 0; j < kMinWidthForVirtual && it != allNhops.end();
         j++, ++it) {
      wide.insert(*it);
    }
    ASSERT_EQ(wide.size(), static_cast<size_t>(kMinWidthForVirtual));
    wideNhopSets.push_back(wide);
  }
  std::vector<RouteNextHopSet> narrowNhopSets;
  {
    auto rit = allNhops.rbegin();
    for (int i = 0; i < 3; i++) {
      RouteNextHopSet narrow;
      narrow.insert(*rit++);
      narrow.insert(*rit++);
      ASSERT_LT(narrow.size(), static_cast<size_t>(kMinWidthForVirtual));
      narrowNhopSets.push_back(narrow);
    }
  }

  auto mgr = makeResourceMgr();
  auto curState = state_;
  auto addRouteAt = [&](int prefixIdx, const RouteNextHopSet& nhops) {
    auto pfx = makePrefix(prefixIdx);
    auto nextState = curState->clone();
    fib(nextState)->addNode(pfx.str(), makeRoute(pfx, nhops));
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  };
  auto isBackup = [&](int prefixIdx) {
    auto route = cfib(curState)->getRouteIf(makePrefix(prefixIdx));
    return route &&
        route->getForwardInfo().getOverrideEcmpSwitchingMode().has_value();
  };

  // Phase 1: fill non-virtual primary pool (cap 2) with 2 narrow routes.
  addRouteAt(0, narrowNhopSets[0]);
  addRouteAt(1, narrowNhopSets[1]);
  ASSERT_FALSE(isBackup(0));
  ASSERT_FALSE(isBackup(1));

  // Phase 2: fill virtual primary pool (cap 2) with 2 wide routes.
  addRouteAt(2, wideNhopSets[0]);
  addRouteAt(3, wideNhopSets[1]);
  ASSERT_FALSE(isBackup(2));
  ASSERT_FALSE(isBackup(3));

  // Phase 3: 3rd narrow → non-virtual pool full → goes to backup.
  addRouteAt(4, narrowNhopSets[2]);
  ASSERT_TRUE(isBackup(4));

  // Phase 4: 3rd wide → virtual pool full → goes to backup.
  addRouteAt(5, wideNhopSets[2]);
  ASSERT_TRUE(isBackup(5));

  // Phase 5: delete one primary of each type in a single update.
  // After: primaryEcmpGroupsCnt=1, virtualEcmpGroupsCnt=1
  //        → canReclaimNonVirtual=1, canReclaimVirtual=1.
  {
    auto nextState = curState->clone();
    fib(nextState)->removeNode(makePrefix(0).str()); // narrow primary
    fib(nextState)->removeNode(makePrefix(2).str()); // virtual primary
    auto deltas = consolidateWithIds(*mgr, curState, nextState);
    ASSERT_FALSE(deltas.empty());
    curState = deltas.back().newState();
  }

  // Both backups must be promoted — neither budget starves the other.
  EXPECT_FALSE(isBackup(4))
      << "non-virtual backup must be reclaimed to primary";
  EXPECT_FALSE(isBackup(5)) << "virtual backup must be reclaimed to primary";
}

// ERM primary cap (DLB groups) forces demotion to backup ECMP-spray, then
// the total post-demotion ECMP group count breaches the ResourceAccountant
// cap and the next update is rejected.
class DlbOverflowsAccountantTest : public BaseEcmpResourceManagerTest {
 public:
  // MockAsic getMaxArsGroups()==7; floor(7*50/100)=3 → ERM primary cap.
  static constexpr auto kExpectedMaxArsGroups = 7;
  // MockAsic getMaxEcmpGroups()==20; floor(20*25/100)=5 → accountant cap.
  static constexpr auto kExpectedMaxEcmpGroups = 20;
  // # routes that fit the accountant cap exactly (3 primary + 2 backup).
  static constexpr auto kAccountantEcmpCap = 5;
  // One more than the accountant cap — triggers ResourceAccountant rejection.
  static constexpr auto kRoutesAboveCap = kAccountantEcmpCap + 1;

  int numStartRoutes() const override {
    return 0;
  }

  void setupFlags() const override {
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_flowletSwitchingEnable = true;
    FLAGS_dlbResourceCheckEnable = true;
    FLAGS_ecmp_resource_manager_make_before_break_buffer = 0;
    FLAGS_ars_resource_percentage = 50;
    FLAGS_ecmp_resource_percentage = 25;
  }

  // Build `n` distinct ECMP nhop sets by repeatedly trimming the front of
  // defaultNhops() — mirrors the pattern at `defaultNhopSets` (line 32) /
  // `nextNhopSets` (line 42) on `EcmpBackupGroupTypeTest`.
  std::vector<RouteNextHopSet> distinctNhopSets(int n) const {
    std::vector<RouteNextHopSet> sets;
    auto nhops = defaultNhops();
    for (auto i = 0; i < n; ++i) {
      sets.push_back(nhops);
      nhops.erase(nhops.begin());
      CHECK_GT(nhops.size(), 1);
    }
    return sets;
  }

  // The base TearDown's `assertReplayIsNoOp` re-adds routes via thrift and
  // asserts the state is identical to `state_`. We update via the Thrift
  // handler directly (not consolidate()), so `state_` stays at the SetUp
  // snapshot. Sync `state_` to the live SwSwitch state so the base TearDown's
  // warmboot/RIB/ERM-correctness invariants still run.
  void TearDown() override {
    state_ = sw_->getState();
    BaseEcmpResourceManagerTest::TearDown();
  }
};

TEST_F(DlbOverflowsAccountantTest, DemoteThenReject) {
  // Guard the math behind the cap choices — if MockAsic ever changes these
  // limits, the test's per-FLAG percentages below will silently miscount.
  auto asic = *sw_->getHwAsicTable()->getL3Asics().begin();
  ASSERT_TRUE(asic->getMaxArsGroups().has_value());
  ASSERT_TRUE(asic->getMaxEcmpGroups().has_value());
  EXPECT_EQ(*asic->getMaxArsGroups(), kExpectedMaxArsGroups);
  EXPECT_EQ(*asic->getMaxEcmpGroups(), kExpectedMaxEcmpGroups);

  // Build kRoutesAboveCap distinct ECMP nhop sets (one per route).
  auto nhopSets = distinctNhopSets(kRoutesAboveCap);

  // Phase 1 — add kAccountantEcmpCap routes. ERM primary cap (3) forces 2
  // groups to backup ECMP-spray. Total ECMP groups = cap, update lands.
  std::vector<RouteV6::Prefix> phase1Prefixes;
  {
    auto newState = sw_->getState()->clone();
    auto fib6 = fib(newState);
    for (int i = 0; i < kAccountantEcmpCap; ++i) {
      auto pfx = makePrefix(i);
      phase1Prefixes.push_back(pfx);
      fib6->addNode(makeRoute(pfx, nhopSets[i]));
    }
    updateRoutes(newState);
  }
  EXPECT_EQ(
      getPostConfigResolvedRoutes(sw_->getState()).size(), kAccountantEcmpCap);

  int backupCount = 0;
  for (const auto& pfx : phase1Prefixes) {
    auto grpInfo = sw_->getEcmpResourceManager()->getGroupInfo(
        RouterID(0), pfx.toCidrNetwork());
    ASSERT_NE(grpInfo, nullptr);
    if (grpInfo->isBackupEcmpGroupType()) {
      ++backupCount;
      auto mode = cfib(sw_->getState())
                      ->getRouteIf(pfx)
                      ->getForwardInfo()
                      .getOverrideEcmpSwitchingMode();
      ASSERT_TRUE(mode.has_value());
      EXPECT_EQ(*mode, cfg::SwitchingMode::PER_PACKET_RANDOM);
    }
  }
  EXPECT_EQ(backupCount, 2);
  EXPECT_EQ(sw_->stats()->getBackupEcmpGroupsCount(), backupCount);

  // Phase 2 — add one more route, putting total ECMP groups one above the
  // accountant cap. ResourceAccountant rejects → SwSwitch state stays
  // unchanged. updateStateWithHwFailureProtection throws FbossHwUpdateError
  // when the applied state can't match the desired one — including the
  // validation rejection path; otherwise SwSwitch FATALs on unprotected
  // rejection.
  auto preState = sw_->getState();
  auto extraPfx = makePrefix(kAccountantEcmpCap);
  auto extraNhops = nhopSets[kAccountantEcmpCap];
  EXPECT_THROW(
      sw_->updateStateWithHwFailureProtection(
          "DlbOverflowsAccountantTest extra route",
          [&](const std::shared_ptr<SwitchState>& in) {
            auto out = in->clone();
            fib(out)->addNode(makeRoute(extraPfx, extraNhops));
            // Stamp NextHop IDs + FibInfo so the ResourceAccountant's
            // ID-aware normalized-nexthop read resolves under
            // FLAGS_resolve_nexthops_from_id -- this raw protected update
            // bypasses updateRoutes()'s stamping.
            return withNextHopIds(out);
          }),
      FbossHwUpdateError);
  EXPECT_EQ(sw_->getState(), preState);
  EXPECT_EQ(
      getPostConfigResolvedRoutes(sw_->getState()).size(), kAccountantEcmpCap);
}

} // namespace facebook::fboss
