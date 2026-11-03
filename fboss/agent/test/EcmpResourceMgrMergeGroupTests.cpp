/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/test/BaseEcmpResourceMgrMergeGroupsTests.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/utils/EcmpResourceManagerTestUtils.h"

namespace facebook::fboss {

namespace {
std::vector<RoutePrefixV6> routePrefixes(
    const std::vector<std::shared_ptr<RouteV6>>& routes) {
  std::vector<RoutePrefixV6> prefixes;
  std::for_each(routes.begin(), routes.end(), [&prefixes](const auto& route) {
    prefixes.push_back(route->prefix());
  });
  return prefixes;
}

} // namespace
class EcmpResourceMgrMergeGroupTest
    : public BaseEcmpResourceMgrMergeGroupsTest {
 public:
  void setupFlags() const override {
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ecmp_resource_percentage = 35;
  }
  std::optional<cfg::SwitchingMode> getBackupEcmpSwitchingMode()
      const override {
    return std::nullopt;
  }
  void assertCost(
      const EcmpResourceManager::NextHopGroupIds& mergedGroups) const {
    auto consolidationInfo =
        sw_->getEcmpResourceManager()
            ->getMergeGroupConsolidationInfo(*mergedGroups.begin())
            .value();
    auto expectedPenalty = consolidationInfo.maxPenalty();
    for (auto gid : mergedGroups) {
      EXPECT_EQ(sw_->getEcmpResourceManager()->getCost(gid), expectedPenalty);
    }
  }
  void assertGroupsAreRemoved(
      const EcmpResourceManager::NextHopGroupIds& removedGroups) const {
    XLOG(DBG2) << " Asserting for removed groups: " << "["
               << folly::join(", ", removedGroups) << "]";
    const auto& nhops2Id = sw_->getEcmpResourceManager()->getNhopsToId();
    std::for_each(
        nhops2Id.begin(),
        nhops2Id.end(),
        [&removedGroups](const auto& nhopsAndGid) {
          EXPECT_FALSE(removedGroups.contains(nhopsAndGid.second));
        });
  }
};

// Base class add 5 groups, which is within in the
// Ecmp resource mgr limit.
TEST_F(EcmpResourceMgrMergeGroupTest, init) {}

TEST_F(EcmpResourceMgrMergeGroupTest, addRouteAboveEcmpLimit) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  assertCost(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, swapNhopsForToBeMergedGroupsAndOverflow) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  // Main motivation of swapping these nhops is to have the prefixes
  // come in reverse order than in addRouteAboveEcmpLimit during
  // state reconstruction, rollback passes.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  ASSERT_EQ(optimalMergeSet.size(), 2);
  ASSERT_EQ(overflowPrefixes.size(), 2);
  auto gid2Prefixes = sw_->getEcmpResourceManager()->getGidToPrefixes();
  XLOG(DBG2) << " Overflow gids : " << folly::join(",", optimalMergeSet);
  std::map<RoutePrefixV6, RouteNextHopSet> toUpdate;
  for (const auto& pfx : overflowPrefixes) {
    auto overflowPfxGrps = optimalMergeSet;
    auto pfxGid = sw_->getEcmpResourceManager()
                      ->getGroupInfo(RouterID(0), pfx.toCidrNetwork())
                      ->getID();
    XLOG(DBG2) << " For : " << pfx.str() << " got group: " << pfxGid;
    overflowPfxGrps.erase(pfxGid);
    auto otherGid = *overflowPfxGrps.begin();
    XLOG(DBG2) << "Getting nhops for : " << otherGid;
    auto otherGrpNhops = getNextHops(otherGid);
    toUpdate.insert({pfx, otherGrpNhops});
  }
  for (const auto& [pfx, nhops] : toUpdate) {
    updateRoute(pfx, nhops);
  }
  // Optimal merge set, overflow prefixes etc should remain the same
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  assertCost(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, addV4RouteAboveEcmpLimit) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto defaultNhops = defaultV4Nhops();
  auto newRoute = makeV4Route(makeV4Prefix(1), defaultNhops);
  auto newState = state_->clone();
  auto fib = fib4(newState);
  fib->addNode(newRoute->prefix().str(), newRoute);
  newState->publish();
  auto deltas = consolidate(newState);
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  assertCost(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, multiplePrefixesInEachMergeGrpMember) {
  for (const auto& [nhops, _] : sw_->getEcmpResourceManager()->getNhopsToId()) {
    auto deltas = addRoute(nextPrefix(), nhops);
    EXPECT_EQ(deltas.size(), 1);
  }
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 4);
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  assertCost(optimalMergeSet);
}

// Post spillover convert a route to single nhop. Now no spillover.
TEST_F(EcmpResourceMgrMergeGroupTest, overflowThenConvertPrefixToSingleNhop) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  auto pfxToMakeSingleHop = *getPrefixesWithoutOverrides().begin();
  deltas =
      updateRoute(pfxToMakeSingleHop, RouteNextHopSet{*defaultNhops().begin()});
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), {});
}

// Post spillover convert spilled over route to single nhop. Now no spillover.
TEST_F(
    EcmpResourceMgrMergeGroupTest,
    overflowThenConvertOverflowRouteToSingleNhop) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  auto pfxToMakeSingleHop = *overflowPrefixes.begin();
  deltas =
      updateRoute(pfxToMakeSingleHop, RouteNextHopSet{*defaultNhops().begin()});
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), {});
}

TEST_F(EcmpResourceMgrMergeGroupTest, incReferenceToMergedGroup) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto deltas = addNextRoute();
  EXPECT_EQ(deltas.size(), 2);
  auto gid = *optimalMergeSet.begin();
  auto beforeConsolidationInfo =
      sw_->getEcmpResourceManager()->getMergeGroupConsolidationInfo(gid);
  auto gidPfx = *sw_->getEcmpResourceManager()->getGidToPrefixes()[gid].begin();
  auto newPrefix = nextPrefix();
  deltas = addRoute(
      newPrefix,
      sw_->getEcmpResourceManager()
          ->getGroupInfo(gidPfx.first, gidPfx.second)
          ->getNhops());
  EXPECT_EQ(deltas.size(), 1);
  auto afterConsolidationInfo =
      sw_->getEcmpResourceManager()->getMergeGroupConsolidationInfo(gid);
  EXPECT_EQ(
      beforeConsolidationInfo->groupId2Penalty.size(),
      afterConsolidationInfo->groupId2Penalty.size());
  for (auto [group, beforePenalty] : beforeConsolidationInfo->groupId2Penalty) {
    if (group == gid) {
      EXPECT_EQ(
          afterConsolidationInfo->groupId2Penalty[group], 2 * beforePenalty);
    } else {
      EXPECT_EQ(afterConsolidationInfo->groupId2Penalty[group], beforePenalty);
    }
  }
}

TEST_F(EcmpResourceMgrMergeGroupTest, addRouteAboveEcmpLimitAndRemove) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto beforeConsolidationInfo =
      sw_->getEcmpResourceManager()->getCandidateMergeConsolidationInfo(
          *optimalMergeSet.begin());
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto newPrefix = nextPrefix();
  addNextRoute();
  assertMergedGroup(optimalMergeSet);
  auto deltas = rmRoute(newPrefix);
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), {});
  assertGroupsAreUnMerged(optimalMergeSet);
  auto afterConsolidationInfo =
      sw_->getEcmpResourceManager()->getCandidateMergeConsolidationInfo(
          *optimalMergeSet.begin());
  EXPECT_EQ(beforeConsolidationInfo, afterConsolidationInfo);
}

TEST_F(EcmpResourceMgrMergeGroupTest, rmRouteToUnmergeAndOverflowInSameUpdate) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto beforeConsolidationInfo =
      sw_->getEcmpResourceManager()->getCandidateMergeConsolidationInfo(
          *optimalMergeSet.begin());
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto newPrefix = nextPrefix();
  addNextRoute();
  assertMergedGroup(optimalMergeSet);
  // Remove one route from overflowPrefixes, that will cause
  // a unmerge. Now add a new route, which will force another merge.
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  fib6->removeNode(overflowPrefixes.begin()->str());
  auto newNhopSet = *nextNhopSets(1).begin();
  auto newRoute = makeRoute(nextPrefix(), newNhopSet)->clone();
  newRoute->setResolved(RouteNextHopEntry(newNhopSet, kDefaultAdminDistance));
  fib6->addNode(newRoute);
  newState->publish();
  auto deltas = consolidate(newState);
  // Umerge delta, merge delta, route add delta
  EXPECT_EQ(deltas.size(), 3);
  EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGroups().size(), 1);
  // New merge set should be selected, since the old one got unbundled
  // in this update.
  EXPECT_NE(
      optimalMergeSet,
      *sw_->getEcmpResourceManager()->getMergedGroups().begin());
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    updateRouteToUnmergeAndOverflowInSameUpdate) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto beforeConsolidationInfo =
      sw_->getEcmpResourceManager()->getCandidateMergeConsolidationInfo(
          *optimalMergeSet.begin());
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto newPrefix = nextPrefix();
  addNextRoute();
  assertMergedGroup(optimalMergeSet);
  // Update one route from overflowPrefixes, that will cause
  // a unmerge. Now add a new route, which will force another merge.
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto newNhopSet = *nextNhopSets(1).begin();
  auto updateRoute = makeRoute(*overflowPrefixes.begin(), newNhopSet)->clone();
  updateRoute->setResolved(
      RouteNextHopEntry(newNhopSet, kDefaultAdminDistance));
  fib6->updateNode(overflowPrefixes.begin()->str(), updateRoute);
  auto newRoute = makeRoute(nextPrefix(), newNhopSet)->clone();
  newRoute->setResolved(RouteNextHopEntry(newNhopSet, kDefaultAdminDistance));
  fib6->addNode(newRoute);
  newState->publish();
  auto deltas = consolidate(newState);
  // Umerge delta, merge delta, route add delta
  EXPECT_EQ(deltas.size(), 3);
  EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGroups().size(), 1);
  // New merge set should be selected, since the old one got unbundled
  // in this update.
  EXPECT_NE(
      optimalMergeSet,
      *sw_->getEcmpResourceManager()->getMergedGroups().begin());
}

// Update a route, which was earlier unmerged in the same update
TEST_F(EcmpResourceMgrMergeGroupTest, updateUnnmergedRouteInSameUpdate) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto beforeConsolidationInfo =
      sw_->getEcmpResourceManager()->getCandidateMergeConsolidationInfo(
          *optimalMergeSet.begin());
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto newPrefix = nextPrefix();
  addNextRoute();
  assertMergedGroup(optimalMergeSet);
  // Remove one route from just added route, which triggers a unmerge.
  // Update current merged route to new next hops
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  fib6->removeNode(newPrefix.str());
  auto newNhopSet = *nextNhopSets(1).begin();
  auto newRoute = fib6->getNode(overflowPrefixes.begin()->str())->clone();
  newRoute->setResolved(RouteNextHopEntry(newNhopSet, kDefaultAdminDistance));
  fib6->updateNode(newRoute);
  newState->publish();
  auto deltas = consolidate(newState);
  // Umerge delta, route update delta
  EXPECT_EQ(deltas.size(), 2);
  EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGroups().size(), 0);
  assertEndState(sw_->getState(), {});
}

// Update a route, which was earlier merged in the same update
TEST_F(EcmpResourceMgrMergeGroupTest, updateMergedRouteInSameUpdate) {
  auto startRoutes = getPostConfigResolvedRoutes(sw_->getState());
  for (const auto& [nhops, _] : sw_->getEcmpResourceManager()->getNhopsToId()) {
    auto deltas = addRoute(nextPrefix(), nhops);
    EXPECT_EQ(deltas.size(), 1);
  }
  rmRoutes(routePrefixes(startRoutes));
  assertEndState(sw_->getState(), {});
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto newNhops = *nextNhopSets(1).begin();
  auto overflowCausingRoute =
      makeRoute((*startRoutes.begin())->prefix(), newNhops);
  // This will cause prefixes from overflowPrefixes to be merged
  fib6->addNode(overflowCausingRoute->prefix().str(), overflowCausingRoute);
  // This update route will cause the just merged groups to get unmerged again.
  auto mergedRouteUpdated = makeRoute(*overflowPrefixes.begin(), newNhops);
  fib6->updateNode(std::move(mergedRouteUpdated));
  auto deltas = consolidate(newState);
  // Merge delta, add route + unmerge delta, route update
  EXPECT_EQ(deltas.size(), 3);
  // No routes should have override nhops
  assertEndState(sw_->getState(), {});
}

TEST_F(EcmpResourceMgrMergeGroupTest, reclaimPrioritizesGroupsWithHigherCost) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto deltas = addNextRoute();
  EXPECT_EQ(deltas.size(), 2);
  assertMergedGroup(optimalMergeSet);
  auto nextOptimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  deltas = addNextRoute();
  EXPECT_EQ(deltas.size(), 2);
  assertMergedGroup(optimalMergeSet);
  assertMergedGroup(nextOptimalMergeSet);
  // Remove one route to make space for reclaiming one of the
  // merged groups
  deltas = rmRoute(*getPrefixesWithoutOverrides().begin());
  EXPECT_EQ(deltas.size(), 2);
  // Assert that highest cost - nextOptimalMergeSet gets reclaimed,
  // while the optimalMergeSet remains merged. That is, merge
  // is done in order of lowest to highest cost, while reclaim is
  // done in the reverse order.
  assertMergedGroup(optimalMergeSet);
  assertGroupsAreUnMerged(nextOptimalMergeSet);
  // Remove one more prefix and assert that all merged groups are
  // reclaimed
  deltas = rmRoute(*getPrefixesWithoutOverrides().begin());
  EXPECT_EQ(deltas.size(), 2);
  assertGroupsAreUnMerged(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, reclaimMultipleMergeGroups) {
  // Cache prefixes to be affected by optimal merge grp selection.
  // We will later assert that these start pointing to merged groups.
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  std::vector<RoutePrefixV6> addedPrefixes;
  addedPrefixes.push_back(nextPrefix());
  auto deltas = addNextRoute();
  EXPECT_EQ(deltas.size(), 2);
  auto nextOptimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  addedPrefixes.push_back(nextPrefix());
  deltas = addNextRoute();
  EXPECT_EQ(deltas.size(), 2);
  assertMergedGroup(optimalMergeSet);
  assertMergedGroup(nextOptimalMergeSet);
  XLOG(DBG2) << " Added prefixes : " << folly::join(", ", addedPrefixes);
  std::for_each(
      addedPrefixes.begin(), addedPrefixes.end(), [this](const auto& pfx) {
        auto grpInfo = sw_->getEcmpResourceManager()->getGroupInfo(
            RouterID(0), pfx.toCidrNetwork());
        ASSERT_FALSE(grpInfo->hasOverrides());
      });
  XLOG(DBG2) << " Will delete " << folly::join(", ", addedPrefixes);
  deltas = rmRoutes(addedPrefixes);
  EXPECT_EQ(deltas.size(), 3);
  assertGroupsAreUnMerged(optimalMergeSet);
  assertGroupsAreUnMerged(nextOptimalMergeSet);
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    addRouteAboveEcmpLimitAndRemoveAllMergeGrpRefrences) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  addNextRoute();
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  for (auto pfx : overflowPrefixes) {
    rmRoute(pfx);
    // The first route remove should cause unmerge of the group
    assertEndState(sw_->getState(), {});
  }
  assertGroupsAreRemoved(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, checkPrimaryEcmpExhaustedEvents) {
  CounterCache counters(sw_);
  addNextRoute();
  counters.update();
  counters.checkDelta(
      SwitchStats::kCounterPrefix + "primary_ecmp_groups_exhausted_events.sum",
      1);
}
TEST_F(EcmpResourceMgrMergeGroupTest, removeAllMergeGrpRefrencesInOneUpdate) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  addNextRoute();
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  rmRoutes({overflowPrefixes.begin(), overflowPrefixes.end()});
  assertEndState(sw_->getState(), {});
  assertGroupsAreRemoved(optimalMergeSet);
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    removeAllMergeGrpRefrencesInOneUpdateMultipleRoutesToMergeSet) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  // Overflow and force a merge
  addNextRoute();
  auto addToMergeSet = nextPrefix();
  addRoute(addToMergeSet, getNextHops(*optimalMergeSet.begin()));
  overflowPrefixes.insert(addToMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 3);
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  rmRoutes({overflowPrefixes.begin(), overflowPrefixes.end()});
  assertEndState(sw_->getState(), {});
  assertGroupsAreRemoved(optimalMergeSet);
}

TEST_F(EcmpResourceMgrMergeGroupTest, exhaustAllPairwiseMerges) {
  auto nhopSets = nextNhopSets();
  std::set<RouteV6::Prefix> overflowPrefixes;
  std::vector<RouteV6::Prefix> addedPrefixes;
  auto removeRoutesAndCheck = [&addedPrefixes, this]() {
    // Now delete all the newly added prefixes. We should go back to
    // no overflow
    rmRoutes(addedPrefixes);
    assertEndState(sw_->getState(), {});
    // No merged groups
    EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGroups().size(), 0);
    EXPECT_EQ(getAllGroups(), getGroupsWithoutOverrides());
  };
  {
    // On the last route add we would have exhausted
    // all pairwise merges, and will now do a 3 group
    // merge
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto oldState = state_;
      auto newState = oldState->clone();
      auto fib6 = fib(newState);
      auto optimalMergeSet =
          sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
      auto prefixesForMergeSet = getPrefixesForGroups(optimalMergeSet);
      overflowPrefixes.insert(
          prefixesForMergeSet.begin(), prefixesForMergeSet.end());
      addedPrefixes.push_back(nextPrefix());
      auto route = makeRoute(addedPrefixes.back(), nhopSets[i]);
      fib6->addNode(route);
      auto deltas = consolidate(newState);
      EXPECT_EQ(deltas.size(), 2);
    }
    assertEndState(sw_->getState(), overflowPrefixes);
    auto allMergedGroups = sw_->getEcmpResourceManager()->getMergedGroups();
    EXPECT_EQ(
        std::count_if(
            allMergedGroups.begin(),
            allMergedGroups.end(),
            [](const auto& gids) { return gids.size() > 2; }),
        1);
  }
  removeRoutesAndCheck();
  {
    // Now add all the previously added routes in one shot. It should
    // result in the same end state as before
    auto oldState = state_;
    auto newState = oldState->clone();
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto fib6 = fib(newState);
      auto route = makeRoute(addedPrefixes[i], nhopSets[i]);
      fib6->addNode(route);
    }
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), addedPrefixes.size() + 1);
    assertEndState(sw_->getState(), overflowPrefixes);
    auto allMergedGroups = sw_->getEcmpResourceManager()->getMergedGroups();
    EXPECT_EQ(
        std::count_if(
            allMergedGroups.begin(),
            allMergedGroups.end(),
            [](const auto& gids) { return gids.size() > 2; }),
        1);
  }
  removeRoutesAndCheck();
}

TEST_F(EcmpResourceMgrMergeGroupTest, addRoutesAboveEcmpLimitAndSyncFibReplay) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  std::set<RouteV6::Prefix> overflowPrefixes;
  auto replayRoutes =
      [this, &overflowPrefixes](const std::set<RouteV6::Prefix>& toReplay) {
        auto newerState = state_->clone();
        auto fib6 = fib(newerState);
        for (const auto& origRoute : getPostConfigResolvedRoutes(state_)) {
          if (toReplay.size() && !toReplay.contains(origRoute->prefix())) {
            continue;
          }
          auto route = fib6->getRouteIf(origRoute->prefix())->clone();
          route->publish();
          fib6->updateNode(route);
        }
        auto deltas2 = consolidate(newerState);
        EXPECT_EQ(deltas2.size(), 1);
        assertEndState(sw_->getState(), overflowPrefixes);
      };
  {
    // On the last route add we would have exhausted
    // all pairwise merges, and will now do a 3 group
    // merge
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto oldState = state_;
      auto newState = oldState->clone();
      auto fib6 = fib(newState);
      auto optimalMergeSet =
          sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
      auto prefixesForMergeSet = getPrefixesForGroups(optimalMergeSet);
      overflowPrefixes.insert(
          prefixesForMergeSet.begin(), prefixesForMergeSet.end());
      auto route = makeRoute(nextPrefix(), nhopSets[i]);
      fib6->addNode(route);
      auto deltas = consolidate(newState);
      EXPECT_EQ(deltas.size(), 2);
    }
    assertEndState(sw_->getState(), overflowPrefixes);
  }
  {
    // Replay state with new pointers for the overflow routes,
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned overflow routes, "
               << "should get identical result";
    replayRoutes(overflowPrefixes);
  }
  {
    // Replay state with new pointers for all routes
    // should just return with the same prefixes marked for overflow
    XLOG(INFO) << " Replaying state with cloned non-overflow routes, "
               << "should get identical result";
    replayRoutes({});
  }
}

TEST_F(EcmpResourceMgrMergeGroupTest, updateAllRoutesToNewNhopsAboveEcmpLimit) {
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
    /*
     * Each prefix update triggers a merge or reclaim. Last update triggers
     * a update and reclaim.
     */
    EXPECT_EQ(deltas.size(), getPostConfigResolvedRoutes(oldState).size() + 1);
    assertEndState(newState, {});
  }
}

TEST_F(EcmpResourceMgrMergeGroupTest, overflowRoutesReconstructInReverseOrder) {
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
    R0 -> G6
    R1 -> G7
    R2 -> G8
    R3 -> G9
    R4 -> G10
    R5 -> G1
    R6 -> G2
    R7 -> G3
    R8 -> G4
    R9 -> G5
  */
  {
    // Add new routes. These will trigger mergess
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
    EXPECT_EQ(deltas.size(), startPrefixes.size() + 1);
  }
}

TEST_F(EcmpResourceMgrMergeGroupTest, reclaimOnReconstructionFromSwitchState) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto routesBefore = getPostConfigResolvedRoutes(sw_->getState()).size();
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto oldState = state_;
    auto newState = oldState->clone();
    auto fib6 = fib(newState);
    auto optimalMergeSet =
        sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
    auto prefixesForMergeSet = getPrefixesForGroups(optimalMergeSet);
    overflowPrefixes.insert(
        prefixesForMergeSet.begin(), prefixesForMergeSet.end());
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    fib6->addNode(route);
    auto deltas = consolidate(newState);
    EXPECT_EQ(deltas.size(), 2);
  }
  assertEndState(sw_->getState(), overflowPrefixes);
  auto higherEcmpGroupLimit = cfib(state_)->size() +
      FLAGS_ecmp_resource_manager_make_before_break_buffer;
  XLOG(INFO) << " Creating new ERM with higher ECMP limit of : "
             << higherEcmpGroupLimit << " all overflow should get reclaimed";
  auto newConsolidator = makeResourceMgrWithEcmpLimit(higherEcmpGroupLimit);
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

TEST_F(EcmpResourceMgrMergeGroupTest, newGroupPointsToExistingMergeSet) {
  // remove 2 routes to make space for new unmerged groups.
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  auto nextNhops = *nextNhopSets(1).begin();
  CHECK(nextNhops.size());
  auto toAppend =
      makeNextHops(kNumIntfs, 1 /*numNhopsPerIntf*/, 1 /*startOffset*/);
  auto commonNextHops = nextNhops;
  commonNextHops.insert(toAppend.begin(), toAppend.end());
  ASSERT_EQ(commonNextHops.size(), nextNhops.size() + toAppend.size());
  auto routeOneNhops = commonNextHops;
  routeOneNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 2 /*startOffset*/).begin());
  auto routeTwoNhops = commonNextHops;
  routeTwoNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 3 /*startOffset*/).begin());
  std::set<RoutePrefixV6> addedPrefixes;
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeOneNhops);
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeTwoNhops);
  auto toBeMergedPrefixes = getPrefixesForGroups(
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet());
  EXPECT_EQ(toBeMergedPrefixes, addedPrefixes);
  addRoute(nextPrefix(), nextNhops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  auto prefixToAdd = nextPrefix();
  toBeMergedPrefixes.insert(prefixToAdd);
  addRoute(prefixToAdd, commonNextHops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    newGroupPointsToExistingMergeSetThenRemoveOne) {
  // remove 2 routes to make space for new unmerged groups.
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  auto nextNhops = *nextNhopSets(1).begin();
  CHECK(nextNhops.size());
  auto toAppend =
      makeNextHops(kNumIntfs, 1 /*numNhopsPerIntf*/, 1 /*startOffset*/);
  auto commonNextHops = nextNhops;
  commonNextHops.insert(toAppend.begin(), toAppend.end());
  ASSERT_EQ(commonNextHops.size(), nextNhops.size() + toAppend.size());
  auto routeOneNhops = commonNextHops;
  routeOneNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 2 /*startOffset*/).begin());
  auto routeTwoNhops = commonNextHops;
  routeTwoNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 3 /*startOffset*/).begin());
  std::set<RoutePrefixV6> addedPrefixes;
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeOneNhops);
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeTwoNhops);
  auto toBeMergedPrefixes = getPrefixesForGroups(
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet());
  EXPECT_EQ(toBeMergedPrefixes, addedPrefixes);
  addRoute(nextPrefix(), nextNhops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  auto prefixToAdd = nextPrefix();
  toBeMergedPrefixes.insert(prefixToAdd);
  addRoute(prefixToAdd, commonNextHops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  rmRoute(prefixToAdd);
  toBeMergedPrefixes.erase(prefixToAdd);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    newGroupPointsToExistingMergeSetThenRemoveAllMerged) {
  // remove 2 routes to make space for new unmerged groups.
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  auto nextNhops = *nextNhopSets(1).begin();
  CHECK(nextNhops.size());
  auto toAppend =
      makeNextHops(kNumIntfs, 1 /*numNhopsPerIntf*/, 1 /*startOffset*/);
  auto commonNextHops = nextNhops;
  commonNextHops.insert(toAppend.begin(), toAppend.end());
  ASSERT_EQ(commonNextHops.size(), nextNhops.size() + toAppend.size());
  auto routeOneNhops = commonNextHops;
  routeOneNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 2 /*startOffset*/).begin());
  auto routeTwoNhops = commonNextHops;
  routeTwoNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 3 /*startOffset*/).begin());
  std::set<RoutePrefixV6> addedPrefixes;
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeOneNhops);
  addedPrefixes.insert(nextPrefix());
  addRoute(nextPrefix(), routeTwoNhops);
  auto toBeMergedPrefixes = getPrefixesForGroups(
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet());
  EXPECT_EQ(toBeMergedPrefixes, addedPrefixes);
  addRoute(nextPrefix(), nextNhops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  auto prefixToAdd = nextPrefix();
  toBeMergedPrefixes.insert(prefixToAdd);
  addRoute(prefixToAdd, commonNextHops);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  rmRoute(prefixToAdd);
  toBeMergedPrefixes.erase(prefixToAdd);
  assertEndState(sw_->getState(), toBeMergedPrefixes);
  rmRoute(*toBeMergedPrefixes.begin());
  assertEndState(sw_->getState(), {});
}

TEST_F(EcmpResourceMgrMergeGroupTest, updateRouteToTriggerUnmerge) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  addNextRoute();
  assertEndState(sw_->getState(), overflowPrefixes);
  assertMergedGroup(optimalMergeSet);
  auto unmergedGid = *sw_->getEcmpResourceManager()->getUnMergedGids().begin();
  // Move one of the merged routes to a unmerged
  updateRoute(
      *overflowPrefixes.begin(),
      sw_->getEcmpResourceManager()->getGroupInfo(unmergedGid)->getNhops());
  // The first route remove should cause unmerge of the group
  assertEndState(sw_->getState(), {});
  EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGids().size(), 0);
  EXPECT_EQ(getAllGroups(), getGroupsWithoutOverrides());
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    addNewRouteWithSameNhopsAsTheMergeItTriggersInSingleUpdate) {
  // remove 2 routes to make space for new unmerged groups.
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  auto nextNhops = *nextNhopSets(1).begin();
  CHECK(nextNhops.size());
  auto toAppend =
      makeNextHops(kNumIntfs, 1 /*numNhopsPerIntf*/, 1 /*startOffset*/);
  auto commonNextHops = nextNhops;
  commonNextHops.insert(toAppend.begin(), toAppend.end());
  ASSERT_EQ(commonNextHops.size(), nextNhops.size() + toAppend.size());
  auto routeOneNhops = commonNextHops;
  routeOneNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 2 /*startOffset*/).begin());
  auto routeTwoNhops = commonNextHops;
  routeTwoNhops.insert(
      *makeNextHops(1, 1 /*numNhopsPerIntf*/, 3 /*startOffset*/).begin());
  std::map<RoutePrefixV6, RouteNextHopSet> toAddRoute2Nhops{
      {makePrefix(cfib(state_)->size()), routeOneNhops},
      {makePrefix(cfib(state_)->size() + 1), routeTwoNhops},
      {makePrefix(cfib(state_)->size() + 2), commonNextHops}};

  addRoutes(toAddRoute2Nhops);
  std::set<RoutePrefixV6> addedPrefixes;
  std::for_each(
      toAddRoute2Nhops.begin(),
      toAddRoute2Nhops.end(),
      [&addedPrefixes](const auto& pfxAndNhops) {
        addedPrefixes.insert(pfxAndNhops.first);
      });
  assertEndState(sw_->getState(), addedPrefixes);
  auto mergedGroups = sw_->getEcmpResourceManager()->getMergedGroups();
  EXPECT_EQ(mergedGroups.size(), 1);
  // gids for routeOneNhops, routeTwoNhops and commonNextHops
  EXPECT_EQ(mergedGroups.begin()->size(), 3);
}

TEST_F(EcmpResourceMgrMergeGroupTest, mergeThenUnmerge) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto mergedPfxOne = *overflowPrefixes.begin();
  auto mergedPfxTwo = *(++overflowPrefixes.begin());
  std::map<RoutePrefixV6, RouteNextHopSet> origPfxToNhops{
      {mergedPfxOne,
       sw_->getEcmpResourceManager()
           ->getGroupInfo(RouterID(0), mergedPfxOne.toCidrNetwork())
           ->getNhops()},
      {mergedPfxTwo,
       sw_->getEcmpResourceManager()
           ->getGroupInfo(RouterID(0), mergedPfxTwo.toCidrNetwork())
           ->getNhops()}};

  auto mergeTriggerPfx = nextPrefix();
  auto triggerMerge = [&]() {
    addRoute(mergeTriggerPfx, *nextNhopSets(1).begin());
    assertEndState(sw_->getState(), overflowPrefixes);
    assertMergedGroup(optimalMergeSet);
  };
  auto restoreToOrigState = [&]() {
    rmRoute(mergeTriggerPfx);
    updateRoute(mergedPfxOne, origPfxToNhops[mergedPfxOne]);
    updateRoute(mergedPfxTwo, origPfxToNhops[mergedPfxTwo]);
    assertEndState(sw_->getState(), {});
    EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGids().size(), 0);
    EXPECT_EQ(getAllGroups(), getGroupsWithoutOverrides());
  };
  {
    triggerMerge();
    // Migrate routeOne to routeTwo's nhops. This should trigger a unmerge
    updateRoute(mergedPfxOne, origPfxToNhops[mergedPfxTwo]);
    assertEndState(sw_->getState(), {});
    EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGids().size(), 0);
    EXPECT_EQ(getAllGroups(), getGroupsWithoutOverrides());
    restoreToOrigState();
  };
  // Do the reverse now.
  // Migrate routeTwo to routeOne's nhops. This should trigger a unmerge
  {
    triggerMerge();
    // Migrate routeOne to routeTwo's nhops. This should trigger a unmerge
    updateRoute(mergedPfxTwo, origPfxToNhops[mergedPfxOne]);
    assertEndState(sw_->getState(), {});
    EXPECT_EQ(sw_->getEcmpResourceManager()->getMergedGids().size(), 0);
    EXPECT_EQ(getAllGroups(), getGroupsWithoutOverrides());
    restoreToOrigState();
  };
}

// Regression test for the dangling `mergedGroupInfoItr` bug in
// `mergeGroupAndMigratePrefixes` (SEV S666090), N=1 leftover case.
//
// We have a pre-existing 2-group merge {groupA, groupB}. A subsequent
// route update forms a new merge that pulls in groupA (but not groupB)
// and erases the original {groupA, groupB} entry. Pre-fix, groupB was
// left holding a stale iterator into the erased entry — the next walk
// of groupB would dereference freed memory and crash the agent.
//
// Pre-fix: fails via EXPECT_FALSE on groupB's iterator, or via an ASan
// heap-use-after-free on the next per-route bookkeeping walk.
// Post-fix: groupB's iterator is cleared during the merge cleanup.
TEST_F(
    EcmpResourceMgrMergeGroupTest,
    mergeGroupAndMigratePrefixesLeavesStaleItrOnOrphanedGroup) {
  // Phase 1: trigger the initial 2-member merge {groupA, groupB}.
  auto initialMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  ASSERT_EQ(initialMergeSet.size(), 2);
  addNextRoute();
  assertMergedGroup(initialMergeSet);

  auto groupA = *initialMergeSet.begin();
  auto groupB = *(++initialMergeSet.begin());
  auto groupANhops =
      sw_->getEcmpResourceManager()->getGroupInfo(groupA)->getNhops();

  // Do NOT rmRoute — freeing capacity triggers the reclaim path which
  // correctly clears iterators and hides the bug.

  // Phase 2: construct routeOne / routeTwo so that the merge they would
  // form pulls in groupA, making (routeOne, routeTwo) the cheapest
  // candidate at the next overflow.
  auto routeOneNhops = groupANhops;
  routeOneNhops.insert(*makeNextHops(1, 1, 2).begin());
  auto routeTwoNhops = groupANhops;
  routeTwoNhops.insert(*makeNextHops(1, 1, 3).begin());

  // Phase 3: add routeOne + routeTwo + filler in one batch. Filler tips
  // us over the limit and triggers a merge that pulls in groupA,
  // erases {groupA, groupB}, and (pre-fix) leaves groupB stranded.
  auto fibSize = cfib(state_)->size();
  addRoutes({
      {makePrefix(fibSize + 100), routeOneNhops},
      {makePrefix(fibSize + 101), routeTwoNhops},
      {makePrefix(fibSize + 102), makeNextHops(kNumIntfs, 1, 7)},
  });

  // Phase 4: with the fix, groupB's iterator was cleared during Part 1.
  auto groupBInfo = sw_->getEcmpResourceManager()->getGroupInfo(groupB);
  ASSERT_NE(groupBInfo, nullptr);
  EXPECT_FALSE(groupBInfo->getMergedGroupInfoItr().has_value())
      << "groupB (" << groupB
      << ") leftover after {groupA, groupB} erase; iterator should be cleared";

  // nhop count: manager's ecmp member count matches the nhops programmed in
  // the FIB (covers the leftover restore's member accounting).
  assertResourceMgrCorrectness(*sw_->getEcmpResourceManager(), sw_->getState());
}

TEST_F(
    EcmpResourceMgrMergeGroupTest,
    triggerMergeThenShrinkOverflowedMemberGroup) {
  auto optimalMergeSet =
      sw_->getEcmpResourceManager()->getOptimalMergeGroupSet();
  auto overflowPrefixes = getPrefixesForGroups(optimalMergeSet);
  EXPECT_EQ(overflowPrefixes.size(), 2);
  auto deltas = addNextRoute();
  // Route delta + merge delta
  EXPECT_EQ(deltas.size(), 2);
  assertEndState(sw_->getState(), overflowPrefixes);
  auto mergedNhops =
      sw_->getEcmpResourceManager()
          ->getMergeGroupConsolidationInfo(*optimalMergeSet.begin())
          ->mergedNhops;
  RouteNextHopSet shrunkNhops;
  for (auto i = 0; i < 2; ++i) {
    shrunkNhops.insert(*mergedNhops.begin());
    mergedNhops.erase(*mergedNhops.begin());
  }
  updateRoute(*overflowPrefixes.begin(), shrunkNhops);
}

// Regression test for the same dangling `mergedGroupInfoItr` bug, N=2
// leftover case (the N=1 test exercises only one orphaned group; this
// one exercises the cleanup loop with two).
//
// We have a pre-existing 3-group merge {A, B, C}. A subsequent route
// update forms a new merge that pulls in B (but not A or C) and erases
// the original {A, B, C} entry. Pre-fix, BOTH A and C were left holding
// stale iterators into the erased entry.
//
// Pre-fix: fails via EXPECT_FALSE on A's or C's iterator, or via an ASan
// heap-use-after-free during Phase 3's route processing.
// Post-fix: both A's and C's iterators are cleared during the merge
// cleanup loop.
TEST_F(
    EcmpResourceMgrMergeGroupTest,
    mergeGroupAndMigratePrefixesLeavesStaleItrsOnMultipleOrphanedGroups) {
  // Phase 1: build the 3-group merge {A, B, C}. Free two slots and add
  // B, C, A in one batch — when A is processed last it triggers overflow,
  // and the cheapest candidate (B, C) gets extended to pull A in,
  // forming the 3-group merge in a single shot.
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());

  // A: 20 fresh nhops disjoint from the defaults. B, C: A + one unique
  // extra each, so intersection(B, C) == A.
  auto aNhops = makeNextHops(kNumIntfs, 1, 5);
  auto bNhops = aNhops;
  bNhops.insert(*makeNextHops(1, 1, 6).begin());
  auto cNhops = aNhops;
  cNhops.insert(*makeNextHops(1, 1, 7).begin());

  addRoutes({
      {makePrefix(100), bNhops},
      {makePrefix(101), cNhops},
      {makePrefix(102), aNhops},
  });

  auto mergedGroupsAfterPhase1 =
      sw_->getEcmpResourceManager()->getMergedGroups();
  ASSERT_EQ(mergedGroupsAfterPhase1.size(), 1);
  ASSERT_EQ(mergedGroupsAfterPhase1[0].size(), 3);

  const auto& nhopsToId = sw_->getEcmpResourceManager()->getNhopsToId();
  auto aGid = nhopsToId.find(aNhops)->second;
  auto bGid = nhopsToId.find(bNhops)->second;
  auto cGid = nhopsToId.find(cNhops)->second;
  auto threeMergeSet = mergedGroupsAfterPhase1[0];

  // Phase 2: stage D and E (designed so the merge they trigger will pull
  // in B) below the soft limit, so the (D, E) candidate exists when the
  // Phase 3 trigger fires.
  auto dNhops = bNhops;
  dNhops.insert(*makeNextHops(1, 1, 8).begin());
  auto eNhops = bNhops;
  eNhops.insert(*makeNextHops(1, 1, 9).begin());

  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  addRoutes({
      {makePrefix(200), dNhops},
      {makePrefix(201), eNhops},
  });

  // Bump primary back to the soft limit so Phase 3's update sees overflow.
  addRoute(makePrefix(300), makeNextHops(kNumIntfs, 1, 15));

  // Phase 3: trigger the (D, E) merge via an UPDATE (not a new add) so
  // the route delete and add cancel out in the primary count — otherwise
  // the N=2-leftover accounting would push us over the soft limit and
  // trip a DCHECK. The merge pulls in B and erases {A, B, C}, leaving
  // A and C as leftovers.
  updateRoute(
      (*getPostConfigResolvedRoutes(state_).begin())->prefix(),
      makeNextHops(kNumIntfs, 1, 20));

  // With the fix, Part 1 cleared both leftover iterators. Without it, an
  // ASan run hits a heap-use-after-free during the update above; in a
  // non-ASan run these EXPECT_FALSEs would still fail.
  auto aInfo = sw_->getEcmpResourceManager()->getGroupInfo(aGid);
  auto cInfo = sw_->getEcmpResourceManager()->getGroupInfo(cGid);
  ASSERT_NE(aInfo, nullptr);
  ASSERT_NE(cInfo, nullptr);
  EXPECT_FALSE(aInfo->getMergedGroupInfoItr().has_value())
      << "A (gid " << aGid << ") leftover iterator should be cleared";
  EXPECT_FALSE(cInfo->getMergedGroupInfoItr().has_value())
      << "C (gid " << cGid << ") leftover iterator should be cleared";

  // Sanity: original {A, B, C} merge erased, new {B, D, E} merge present.
  auto mergedGroupsAfter = sw_->getEcmpResourceManager()->getMergedGroups();
  EXPECT_TRUE(
      std::find(
          mergedGroupsAfter.begin(), mergedGroupsAfter.end(), threeMergeSet) ==
      mergedGroupsAfter.end());
  auto dGid = nhopsToId.find(dNhops)->second;
  auto eGid = nhopsToId.find(eNhops)->second;
  bool foundNewMerge = std::any_of(
      mergedGroupsAfter.begin(),
      mergedGroupsAfter.end(),
      [&](const auto& mset) {
        return mset.contains(bGid) && mset.contains(dGid) &&
            mset.contains(eGid);
      });
  EXPECT_TRUE(foundNewMerge);

  // nhop count: manager's ecmp member count matches the nhops programmed in
  // the FIB (covers the leftover restore's member accounting).
  assertResourceMgrCorrectness(*sw_->getEcmpResourceManager(), sw_->getState());
}

// Option-2 behavior: when a merge loses one member to a new merge but 2+
// members survive, the survivors are kept as a single smaller merged group
// (not split into standalones). Same shape as the N=2 orphan test, but the
// trigger is an add (not a net-neutral update): keeping the survivors merged
// is slot-neutral, so we land at the soft limit and reclaim does NOT dissolve
// the survivor merge — letting us assert it stays merged.
TEST_F(
    EcmpResourceMgrMergeGroupTest,
    mergeGroupAndMigratePrefixesKeepsSurvivingMembersMerged) {
  // Phase 1: build the 3-group merge {A, B, C} (intersection(B, C) == A).
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  auto aNhops = makeNextHops(kNumIntfs, 1, 5);
  auto bNhops = aNhops;
  bNhops.insert(*makeNextHops(1, 1, 6).begin());
  auto cNhops = aNhops;
  cNhops.insert(*makeNextHops(1, 1, 7).begin());
  addRoutes({
      {makePrefix(100), bNhops},
      {makePrefix(101), cNhops},
      {makePrefix(102), aNhops},
  });
  ASSERT_EQ(sw_->getEcmpResourceManager()->getMergedGroups().size(), 1);

  const auto& nhopsToId = sw_->getEcmpResourceManager()->getNhopsToId();
  auto aGid = nhopsToId.find(aNhops)->second;
  auto bGid = nhopsToId.find(bNhops)->second;
  auto cGid = nhopsToId.find(cNhops)->second;

  // Phase 2: stage D, E so the merge they trigger pulls in B.
  auto dNhops = bNhops;
  dNhops.insert(*makeNextHops(1, 1, 8).begin());
  auto eNhops = bNhops;
  eNhops.insert(*makeNextHops(1, 1, 9).begin());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  rmRoute((*getPostConfigResolvedRoutes(state_).begin())->prefix());
  addRoutes({
      {makePrefix(200), dNhops},
      {makePrefix(201), eNhops},
  });
  // Bump back to the soft limit so the Phase 3 add triggers overflow.
  addRoute(makePrefix(300), makeNextHops(kNumIntfs, 1, 15));

  // Phase 3: add a filler that tips us over. The optimal (D, E) merge pulls in
  // B and erases {A, B, C}, leaving A and C as survivors kept merged.
  addRoute(makePrefix(400), makeNextHops(kNumIntfs, 1, 20));

  auto mgr = sw_->getEcmpResourceManager();
  auto aInfo = mgr->getGroupInfo(aGid);
  auto cInfo = mgr->getGroupInfo(cGid);
  ASSERT_NE(aInfo, nullptr);
  ASSERT_NE(cInfo, nullptr);
  // A and C are kept merged: both point at the same {A, C} merge entry.
  ASSERT_TRUE(aInfo->getMergedGroupInfoItr().has_value());
  ASSERT_TRUE(cInfo->getMergedGroupInfoItr().has_value());
  EcmpResourceManager::NextHopGroupIds acMergeSet{aGid, cGid};
  EXPECT_EQ((*aInfo->getMergedGroupInfoItr())->first, acMergeSet);
  EXPECT_EQ(*aInfo->getMergedGroupInfoItr(), *cInfo->getMergedGroupInfoItr());

  // nhop count: the {A, C} merge programs intersection(A, C) == A's nhops.
  auto acMergeInfo = mgr->getMergeGroupConsolidationInfo(aGid);
  ASSERT_TRUE(acMergeInfo.has_value());
  EXPECT_EQ(acMergeInfo->mergedNhops.size(), aNhops.size());

  // Original {A, B, C} is gone; the new {B, D, E} merge is present.
  auto dGid = nhopsToId.find(dNhops)->second;
  auto eGid = nhopsToId.find(eNhops)->second;
  auto mergedGroups = mgr->getMergedGroups();
  EXPECT_TRUE(
      std::any_of(
          mergedGroups.begin(), mergedGroups.end(), [&](const auto& mset) {
            return mset.contains(bGid) && mset.contains(dGid) &&
                mset.contains(eGid);
          }));

  // nhop count: manager's ecmp member count matches the FIB.
  assertResourceMgrCorrectness(*mgr, sw_->getState());
}

TEST_F(EcmpResourceMgrMergeGroupTest, reconstructRebuildsOverrideSetIds) {
  addNextRoute();
  addNextRoute();
  addNextRoute();
  const auto& idToMembers =
      sw_->getEcmpResourceManager()->getOverrideSetIdManager().getIdToMembers();
  ASSERT_EQ(idToMembers.size(), 3);
  auto newConsolidator = makeResourceMgr();
  newConsolidator->reconstructFromSwitchState(sw_->getState());
  EXPECT_EQ(
      newConsolidator->getOverrideSetIdManager().getIdToMembers(), idToMembers);
}
} // namespace facebook::fboss
