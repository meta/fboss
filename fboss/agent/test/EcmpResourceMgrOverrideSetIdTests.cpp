/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gflags/gflags.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/OverrideSetIdManager.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/test/BaseEcmpResourceMgrMergeGroupsTests.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/EcmpResourceManagerTestUtils.h"

namespace facebook::fboss {

namespace {
IdToNextHopIdSetMapThriftType overrideIdMap(
    const std::shared_ptr<SwitchState>& state) {
  auto map = state->getFibsInfoMap()
                 ->cbegin()
                 ->second->getOverrideIdToNextHopIdSetMap();
  return map ? map->toThrift() : IdToNextHopIdSetMapThriftType{};
}

void assertDeltaChain(const std::vector<StateDelta>& deltas) {
  for (size_t i = 1; i < deltas.size(); ++i) {
    EXPECT_EQ(deltas[i - 1].newState(), deltas[i].oldState()) << "delta " << i;
  }
}

// Pre-upgrade state: inline overrides with no override ids or FibInfo map.
std::shared_ptr<SwitchState> withoutOverrideSetIds(
    const std::shared_ptr<SwitchState>& in) {
  auto state = in->clone();
  auto fib6 = fib(state);
  for (const auto& [_, route] : std::as_const(*cfib(in))) {
    if (!route->getForwardInfo().getOverrideNextHopSetID()) {
      continue;
    }
    RouteNextHopEntry fwd;
    fwd.fromThrift(route->getForwardInfo().toThrift());
    fwd.setOverrideNextHopSetID(std::nullopt);
    auto newRoute = route->clone();
    newRoute->setResolved(fwd);
    newRoute->publish();
    fib6->updateNode(newRoute);
  }
  for (const auto& [matcher, _] : std::as_const(*in->getFibsInfoMap())) {
    state->getFibsInfoMap()
        ->getNodeIf(matcher)
        ->modify(&state)
        ->setOverrideIdToNextHopIdSetMap(nullptr);
  }
  state->publish();
  return state;
}

} // namespace

class EcmpResourceMgrOverrideSetIdTest
    : public BaseEcmpResourceMgrMergeGroupsTest {
 public:
  void setupFlags() const override {
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ecmp_resource_percentage = 35;
  }

 protected:
  const EcmpResourceManager& swMgr() const {
    return *sw_->getEcmpResourceManager();
  }
  const std::unordered_map<NextHopSetID, NextHopIDSet>& swIds() const {
    return swMgr().getOverrideSetIdManager().getIdToMembers();
  }
  static std::optional<NextHopSetID> routeId(
      const std::shared_ptr<SwitchState>& state,
      const RoutePrefixV6& pfx) {
    auto route = cfib(state)->getRouteIf(pfx);
    EXPECT_NE(route, nullptr) << pfx.str();
    return route ? route->getForwardInfo().getOverrideNextHopSetID()
                 : std::nullopt;
  }
  std::optional<NextHopSetID> groupId(NextHopGroupId gid) const {
    auto info = swMgr().getMergeGroupConsolidationInfo(gid);
    return info ? info->overrideSetId : std::nullopt;
  }
  std::map<RoutePrefixV6, std::optional<NextHopSetID>> allRouteIds(
      const std::shared_ptr<SwitchState>& state) const {
    std::map<RoutePrefixV6, std::optional<NextHopSetID>> ids;
    for (const auto& [_, route] : std::as_const(*cfib(state))) {
      ids[route->prefix()] = route->getForwardInfo().getOverrideNextHopSetID();
    }
    return ids;
  }
  static bool hasOverrideIdMap(const std::shared_ptr<SwitchState>& state) {
    for (const auto& [_, fibInfo] : std::as_const(*state->getFibsInfoMap())) {
      auto map = fibInfo->getOverrideIdToNextHopIdSetMap();
      if (map && !map->toThrift().empty()) {
        return true;
      }
    }
    return false;
  }
  static std::set<NextHopIDSet> memberSets(
      const std::unordered_map<NextHopSetID, NextHopIDSet>& ids) {
    std::set<NextHopIDSet> sets;
    for (const auto& [_, members] : ids) {
      sets.insert(members);
    }
    return sets;
  }
  void assertSwIdsConsistent() const {
    assertOverrideSetIdsConsistency(swMgr(), sw_->getState());
    assertSwAndConsolidatorIdsMatch();
  }
  void assertSwAndConsolidatorIdsMatch() const {
    EXPECT_EQ(
        consolidator_->getOverrideSetIdManager().getIdToMembers(), swIds());
  }
  // Expects `merge` and its routes to share one id, and returns it.
  NextHopSetID assertOneIdFor(
      const EcmpResourceManager::NextHopGroupIds& merge) const {
    auto id = groupId(*merge.begin());
    EXPECT_TRUE(id.has_value()) << folly::join(", ", merge);
    for (auto gid : merge) {
      EXPECT_EQ(groupId(gid), id);
    }
    for (const auto& pfx : getPrefixesForGroups(merge)) {
      EXPECT_EQ(routeId(sw_->getState(), pfx), id) << pfx.str();
    }
    return id.value_or(NextHopSetID(0));
  }
};

TEST_F(EcmpResourceMgrOverrideSetIdTest, mergeAllocatesOneIdForAllMembers) {
  auto merge = swMgr().getOptimalMergeGroupSet();
  ASSERT_EQ(merge.size(), 2);
  auto deltas = addNextRoute();
  ASSERT_EQ(deltas.size(), 2);
  for (const auto& [pfx, id] : allRouteIds(deltas.front().oldState())) {
    EXPECT_FALSE(id.has_value()) << pfx.str();
  }
  auto id = assertOneIdFor(merge);
  EXPECT_TRUE(OverrideSetIdManager::isOverrideSetID(id));
  ASSERT_EQ(swIds().size(), 1);
  EXPECT_TRUE(swIds().contains(id));
  for (const auto& pfx : getPrefixesForGroups(merge)) {
    EXPECT_EQ(routeId(deltas.back().newState(), pfx), id) << pfx.str();
  }
  assertSwAndConsolidatorIdsMatch();
}

TEST_F(EcmpResourceMgrOverrideSetIdTest, newRouteJoiningMergeReusesId) {
  auto merge = swMgr().getOptimalMergeGroupSet();
  addNextRoute();
  auto id = assertOneIdFor(merge);
  auto newPfx = nextPrefix();
  addRoute(newPfx, getNextHops(*merge.begin()));
  EXPECT_EQ(routeId(sw_->getState(), newPfx), id);
  EXPECT_EQ(assertOneIdFor(merge), id);
  EXPECT_EQ(swIds().size(), 1);
  assertSwAndConsolidatorIdsMatch();
}

TEST_F(EcmpResourceMgrOverrideSetIdTest, remergeAllocatesFreshId) {
  auto merge = swMgr().getOptimalMergeGroupSet();
  auto newPfx = nextPrefix();
  auto newNhops = *nextNhopSets(1).begin();
  addRoute(newPfx, newNhops);
  auto oldId = assertOneIdFor(merge);
  rmRoute(newPfx);
  EXPECT_TRUE(swIds().empty());
  EXPECT_FALSE(hasOverrideIdMap(sw_->getState()));
  for (const auto& [pfx, id] : allRouteIds(sw_->getState())) {
    EXPECT_FALSE(id.has_value()) << pfx.str();
  }
  addRoute(newPfx, newNhops);
  auto newId = assertOneIdFor(merge);
  EXPECT_GT(newId, oldId);
  assertSwAndConsolidatorIdsMatch();
}

TEST_F(EcmpResourceMgrOverrideSetIdTest, failedUpdatesRollBackIds) {
  // Failed merge: no id is assigned.
  auto nhops = *nextNhopSets(1).begin();
  auto route = makeRoute(nextPrefix(), nhops)->clone();
  route->setResolved(RouteNextHopEntry(nhops, kDefaultAdminDistance));
  auto mergeState = state_->clone();
  fib(mergeState)->addNode(route);
  failUpdate(mergeState);
  EXPECT_TRUE(
      consolidator_->getOverrideSetIdManager().getIdToMembers().empty());
  assertOverrideSetIdsConsistency(*consolidator_, state_);

  // Failed unmerge: the merge keeps its members' id.
  auto merge = swMgr().getOptimalMergeGroupSet();
  auto newPfx = nextPrefix();
  addNextRoute();
  assertOneIdFor(merge);
  auto idsBefore = consolidator_->getOverrideSetIdManager().getIdToMembers();
  ASSERT_FALSE(idsBefore.empty());
  auto unmergeState = state_->clone();
  fib(unmergeState)->removeNode(newPfx.str());
  failUpdate(unmergeState);
  // failUpdate reverts sw_ by re-adding, so the id value may be fresh.
  EXPECT_EQ(
      memberSets(consolidator_->getOverrideSetIdManager().getIdToMembers()),
      memberSets(idsBefore));
  assertSwAndConsolidatorIdsMatch();
  assertOverrideSetIdsConsistency(*consolidator_, state_);
}

// A new merge pulls group A out of merge {A, B}: the new merge gets a fresh
// id, {A, B}'s id is freed and B's routes lose it.
TEST_F(EcmpResourceMgrOverrideSetIdTest, mergePullingInMergedGroupFreesOldId) {
  auto initialMerge = swMgr().getOptimalMergeGroupSet();
  ASSERT_EQ(initialMerge.size(), 2);
  addNextRoute();
  auto oldId = assertOneIdFor(initialMerge);
  auto groupA = *initialMerge.begin();
  auto groupB = *initialMerge.rbegin();
  auto groupANhops = swMgr().getGroupInfo(groupA)->getNhops();
  auto groupBPfxs = getPrefixesForGroups({groupB});
  auto routeOneNhops = groupANhops;
  routeOneNhops.insert(*makeNextHops(1, 1, 2).begin());
  auto routeTwoNhops = groupANhops;
  routeTwoNhops.insert(*makeNextHops(1, 1, 3).begin());
  auto fibSize = cfib(state_)->size();
  addRoutes({
      {makePrefix(fibSize + 100), routeOneNhops},
      {makePrefix(fibSize + 101), routeTwoNhops},
      {makePrefix(fibSize + 102), makeNextHops(kNumIntfs, 1, 7)},
  });
  EXPECT_FALSE(swMgr().getGroupInfo(groupB)->getMergedGroupInfoItr());
  for (const auto& pfx : groupBPfxs) {
    EXPECT_FALSE(routeId(sw_->getState(), pfx).has_value()) << pfx.str();
  }
  EXPECT_FALSE(swIds().contains(oldId));
  std::optional<NextHopSetID> newId;
  for (const auto& merge : swMgr().getMergedGroups()) {
    if (merge.contains(groupA)) {
      newId = assertOneIdFor(merge);
    }
  }
  ASSERT_TRUE(newId.has_value());
  EXPECT_GT(*newId, oldId);
  assertSwIdsConsistent();
}

TEST_F(
    EcmpResourceMgrOverrideSetIdTest,
    reclaimFreesOverrideSetIdsOnlyInLastDelta) {
  std::vector<RoutePrefixV6> addedPrefixes{nextPrefix()};
  addNextRoute();
  addedPrefixes.push_back(nextPrefix());
  addNextRoute();
  auto idsBefore = consolidator_->getOverrideSetIdManager().toThrift();
  ASSERT_EQ(idsBefore.size(), 2);
  auto deltas = rmRoutes(addedPrefixes);
  ASSERT_EQ(deltas.size(), 3);
  assertDeltaChain(deltas);
  for (size_t i = 0; i + 1 < deltas.size(); ++i) {
    EXPECT_EQ(overrideIdMap(deltas[i].newState()), idsBefore) << "delta " << i;
  }
  EXPECT_TRUE(overrideIdMap(deltas.back().newState()).empty());
  EXPECT_TRUE(swIds().empty());
}

TEST_F(EcmpResourceMgrOverrideSetIdTest, reconstructBackfillsOverrideSetIds) {
  addNextRoute();
  addNextRoute();
  auto preIdState = withoutOverrideSetIds(sw_->getState());
  ASSERT_FALSE(hasOverrideIdMap(preIdState));
  auto mgr = makeResourceMgr();
  auto deltas = mgr->reconstructFromSwitchState(preIdState);
  const auto& mgrIds = mgr->getOverrideSetIdManager().getIdToMembers();
  EXPECT_EQ(memberSets(mgrIds), memberSets(swIds()));
  // Every overridden route is stamped with one of the assigned ids.
  auto backfilled = deltas.back().newState();
  EXPECT_TRUE(hasOverrideIdMap(backfilled));
  auto stamped = 0;
  for (const auto& [_, route] : std::as_const(*cfib(backfilled))) {
    const auto& fwd = route->getForwardInfo();
    if (!fwd.hasOverrideNextHops()) {
      continue;
    }
    auto id = fwd.getOverrideNextHopSetID();
    ASSERT_TRUE(id.has_value()) << route->str();
    EXPECT_TRUE(mgrIds.contains(*id)) << route->str();
    ++stamped;
  }
  EXPECT_GT(stamped, 0);
  assertOverrideSetIdsConsistency(*mgr, backfilled);
}

// A merge forms, goes away and a new merge forms across three updates, each
// producing several deltas: every delta carries consistent ids, the first
// merge's id is freed only in the last delta, and the new merge gets a fresh
// id.
TEST_F(EcmpResourceMgrOverrideSetIdTest, mergeLifecycleAcrossUpdates) {
  auto firstMerge = swMgr().getOptimalMergeGroupSet();
  auto firstPfx = nextPrefix();
  auto deltas = addNextRoute();
  ASSERT_GT(deltas.size(), 1);
  assertDeltaChain(deltas);
  assertOverrideSetIdsInDeltas(*consolidator_, deltas);
  auto firstId = assertOneIdFor(firstMerge);

  deltas = rmRoute(firstPfx);
  ASSERT_GT(deltas.size(), 1);
  assertDeltaChain(deltas);
  assertOverrideSetIdsInDeltas(*consolidator_, deltas);
  for (size_t i = 0; i + 1 < deltas.size(); ++i) {
    EXPECT_TRUE(overrideIdMap(deltas[i].newState())
                    .contains(static_cast<int64_t>(firstId)))
        << "delta " << i;
  }
  EXPECT_FALSE(hasOverrideIdMap(deltas.back().newState()));
  EXPECT_TRUE(swIds().empty());

  auto secondMerge = swMgr().getOptimalMergeGroupSet();
  deltas = addRoute(nextPrefix(), *nextNhopSets(1).begin());
  ASSERT_GT(deltas.size(), 1);
  assertDeltaChain(deltas);
  assertOverrideSetIdsInDeltas(*consolidator_, deltas);
  EXPECT_GT(assertOneIdFor(secondMerge), firstId);
  assertSwIdsConsistent();
}

} // namespace facebook::fboss
