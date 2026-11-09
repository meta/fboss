/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/test/BaseEcmpResourceManagerTest.h"

namespace facebook::fboss {

class NextHopIdAllocatorTest : public BaseEcmpResourceManagerTest {
 public:
  void assertRouteUsageCount(
      EcmpResourceManager::NextHopGroupId groupId,
      int usageCount) const {
    EXPECT_EQ(consolidator_->getRouteUsageCount(groupId), usageCount);
    EXPECT_EQ(consolidator_->getCost(groupId), usageCount);
  }
};

TEST_F(NextHopIdAllocatorTest, init) {
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  auto id = *getNhopId(defaultNhops());
  EXPECT_EQ(id, 1);
  // All routes point to same nhop group
  assertRouteUsageCount(id, numPostConfigResolvedRoutes(state_));
}

TEST_F(NextHopIdAllocatorTest, addRouteSameNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  fib6->addNode(makeRoute(nextPrefix(), defaultNhops()));
  EXPECT_EQ(fib6->size(), routesBefore + 1);
  consolidate(newState);
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  auto id = *getNhopId(defaultNhops());
  EXPECT_EQ(id, 1);
  // All routes point to same nhop group
  assertRouteUsageCount(id, numPostConfigResolvedRoutes(state_));
}

TEST_F(NextHopIdAllocatorTest, addRouteNewNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  fib6->addNode(makeRoute(nextPrefix(), newNhops));
  EXPECT_EQ(fib6->size(), routesBefore + 1);
  consolidate(newState);
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 2);
  auto idDefaultNhops = *getNhopId(defaultNhops());
  EXPECT_EQ(idDefaultNhops, 1);
  // All but one routes point to same nhop group
  assertRouteUsageCount(
      idDefaultNhops, numPostConfigResolvedRoutes(state_) - 1);
  auto idNewNhops = *getNhopId(newNhops);
  EXPECT_EQ(idNewNhops, 2);
  // One route points to new nhop group
  assertRouteUsageCount(idNewNhops, 1);
}

TEST_F(NextHopIdAllocatorTest, protectionGroupIsNotTracked) {
  const auto prefix = nextPrefix();
  const auto protectionNhops = withBackupNextHops(defaultNhops());
  const auto groupsBefore = consolidator_->getNhopsToId().size();
  const auto countsBefore = consolidator_->getPrimaryEcmpAndMemberCounts();

  addRoute(prefix, protectionNhops);

  EXPECT_EQ(consolidator_->getNhopsToId().size(), groupsBefore);
  EXPECT_EQ(consolidator_->getPrimaryEcmpAndMemberCounts(), countsBefore);
  EXPECT_FALSE(getNhopId(protectionNhops).has_value());
  EXPECT_EQ(
      consolidator_->getGroupInfo(RouterID(0), prefix.toCidrNetwork()),
      nullptr);
  const auto route = cfib(state_)->getRouteIf(prefix);
  ASSERT_NE(route, nullptr);
  EXPECT_EQ(
      facebook::fboss::getNextHops(state_, route->getForwardInfo()),
      protectionNhops);
  EXPECT_FALSE(route->getForwardInfo().hasOverrideSwitchingModeOrNhops());

  rmRoute(prefix);
  EXPECT_EQ(consolidator_->getNhopsToId().size(), groupsBefore);
}

TEST_F(NextHopIdAllocatorTest, singleNextHopToProtectionGroup) {
  const auto prefix = nextPrefix();
  const auto protectionNhops = withBackupNextHops(defaultNhops());
  auto singleNhop = defaultNhops();
  singleNhop.erase(std::next(singleNhop.begin()), singleNhop.end());
  addRoute(prefix, singleNhop);
  const auto groupsBefore = consolidator_->getNhopsToId().size();
  const auto countsBefore = consolidator_->getPrimaryEcmpAndMemberCounts();

  updateRoute(prefix, protectionNhops);

  EXPECT_EQ(consolidator_->getNhopsToId().size(), groupsBefore);
  EXPECT_EQ(consolidator_->getPrimaryEcmpAndMemberCounts(), countsBefore);
  EXPECT_FALSE(getNhopId(protectionNhops).has_value());
  EXPECT_EQ(
      consolidator_->getGroupInfo(RouterID(0), prefix.toCidrNetwork()),
      nullptr);
  const auto route = cfib(state_)->getRouteIf(prefix);
  ASSERT_NE(route, nullptr);
  EXPECT_EQ(
      facebook::fboss::getNextHops(state_, route->getForwardInfo()),
      protectionNhops);
}

TEST_F(NextHopIdAllocatorTest, protectionGroupUnresolveAndRestore) {
  const auto prefix = nextPrefix();
  const auto protectionNhops = withBackupNextHops(defaultNhops());
  addRoute(prefix, protectionNhops);

  auto unresolvedState = state_->clone();
  const auto route = cfib(state_)->getRouteIf(prefix);
  ASSERT_NE(route, nullptr);
  auto unresolvedRoute = route->clone();
  unresolvedRoute->clearForward();
  fib(unresolvedState)->updateNode(unresolvedRoute);
  consolidate(unresolvedState);

  EXPECT_FALSE(getNhopId(protectionNhops).has_value());
  EXPECT_EQ(
      consolidator_->getGroupInfo(RouterID(0), prefix.toCidrNetwork()),
      nullptr);

  auto resolvedState = state_->clone();
  fib(resolvedState)->updateNode(makeRoute(prefix, protectionNhops));
  consolidate(resolvedState);

  EXPECT_FALSE(getNhopId(protectionNhops).has_value());
  EXPECT_EQ(
      consolidator_->getGroupInfo(RouterID(0), prefix.toCidrNetwork()),
      nullptr);
  const auto restoredRoute = cfib(state_)->getRouteIf(prefix);
  ASSERT_NE(restoredRoute, nullptr);
  EXPECT_TRUE(restoredRoute->isResolved());
  EXPECT_EQ(
      facebook::fboss::getNextHops(state_, restoredRoute->getForwardInfo()),
      protectionNhops);
  EXPECT_FALSE(
      restoredRoute->getForwardInfo().hasOverrideSwitchingModeOrNhops());
}

TEST_F(NextHopIdAllocatorTest, addRemoveRouteNewNhopsUnresolved) {
  auto newState = state_->clone();
  const auto& nhops2Id = consolidator_->getNhopsToId();
  auto groupId = *getNhopId(defaultNhops());
  EXPECT_EQ(groupId, 1);
  EXPECT_EQ(nhops2Id.size(), 1);
  EXPECT_EQ(nhops2Id.find(defaultNhops())->second, groupId);
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  auto newRoute = makeRoute(nextPrefix(), newNhops)->clone();
  newRoute->clearForward();
  {
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    // All routes point to same nhop group
    auto postConfigRoutesBefore = numPostConfigResolvedRoutes(newState);
    assertRouteUsageCount(groupId, postConfigRoutesBefore);
    fib6->addNode(newRoute);
    EXPECT_EQ(fib6->size(), routesBefore + 1);
    consolidate(newState);
    // New nhops don't get a id, since no resolved routes point to it
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    // All routes point to same nhop group, new route is unresolved
    assertRouteUsageCount(groupId, postConfigRoutesBefore);
  }
  {
    auto newerState = newState->clone();
    auto fib6 = fib(newerState);
    auto routesBefore = fib6->size();
    // All resolved routes point to same nhop group
    assertRouteUsageCount(groupId, numPostConfigResolvedRoutes(newState));
    fib6->removeNode(newRoute);
    EXPECT_EQ(fib6->size(), routesBefore - 1);
    consolidate(newerState);
    const auto& updatedNhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(updatedNhops2Id.size(), 1);
    EXPECT_EQ(*getNhopId(defaultNhops()), groupId);
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    // All resolved routes point to same nhop group
    assertRouteUsageCount(groupId, numPostConfigResolvedRoutes(state_));
  }
}

TEST_F(NextHopIdAllocatorTest, updateRouteNhops) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  fib6->updateNode(makeRoute(makePrefix(0), newNhops));
  EXPECT_EQ(fib6->size(), routesBefore);
  consolidate(newState);
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 2);
  auto defaultNhopsId = *getNhopId(defaultNhops());
  auto newNhopsId = *getNhopId(newNhops);
  EXPECT_EQ(defaultNhopsId, 1);
  EXPECT_EQ(newNhopsId, 2);
  // All but one route point to defaultNhops
  assertRouteUsageCount(
      defaultNhopsId, numPostConfigResolvedRoutes(state_) - 1);
  // One route points to new nhop group
  assertRouteUsageCount(newNhopsId, 1);
}

TEST_F(NextHopIdAllocatorTest, updateAllRoutes) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  for (const auto& route6 : getPostConfigResolvedRoutes(state_)) {
    auto newRoute = route6->clone();
    newRoute->setResolved(RouteNextHopEntry(newNhops, kDefaultAdminDistance));
    fib6->updateNode(newRoute);
  }
  EXPECT_EQ(fib6->size(), routesBefore);
  consolidate(newState);
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  EXPECT_FALSE(getNhopId(defaultNhops()).has_value());
  auto newNhopsId = *getNhopId(newNhops);
  EXPECT_EQ(newNhopsId, 2);
  // All routes route point to newNhopsId
  assertRouteUsageCount(newNhopsId, numPostConfigResolvedRoutes(state_));
}

TEST_F(NextHopIdAllocatorTest, updateRouteNhopsMultipleTimes) {
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  auto newerNhops = newNhops;
  newerNhops.erase(newerNhops.begin());

  {
    // Update first route to new nhops
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    fib6->updateNode(makeRoute(makePrefix(0), newNhops));
    EXPECT_EQ(fib6->size(), routesBefore);
    consolidate(newState);
    const auto& nhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 2);
    auto defaultNhopsId = *getNhopId(defaultNhops());
    auto newNhopsId = *getNhopId(newNhops);
    EXPECT_EQ(defaultNhopsId, 1);
    EXPECT_EQ(newNhopsId, 2);
    // All but one route point to defaultNhops
    assertRouteUsageCount(
        defaultNhopsId, numPostConfigResolvedRoutes(state_) - 1);
    assertRouteUsageCount(newNhopsId, 1);
  }
  {
    // Update first route to newer nhops
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    fib6->updateNode(makeRoute(makePrefix(0), newerNhops));
    EXPECT_EQ(fib6->size(), routesBefore);
    consolidate(newState);
    const auto& nhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 2);
    auto defaultNhopsId = *getNhopId(defaultNhops());
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    auto newerNhopsId = *getNhopId(newerNhops);
    EXPECT_EQ(defaultNhopsId, 1);
    // Since we freed up newNhops, its ID should get reused
    EXPECT_EQ(newerNhopsId, 2);
    // All but one route point to defaultNhops
    assertRouteUsageCount(
        defaultNhopsId, numPostConfigResolvedRoutes(state_) - 1);
    assertRouteUsageCount(newerNhopsId, 1);
  }
  {
    // Update first route back to default nhops
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    fib6->updateNode(makeRoute(makePrefix(0), defaultNhops()));
    EXPECT_EQ(fib6->size(), routesBefore);
    consolidate(newState);
    const auto& nhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 1);
    auto defaultNhopsId = *getNhopId(defaultNhops());
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    EXPECT_FALSE(getNhopId(newerNhops).has_value());
    EXPECT_EQ(defaultNhopsId, 1);
    // All routes point to defaultNhops
    assertRouteUsageCount(defaultNhopsId, numPostConfigResolvedRoutes(state_));
  }
}

TEST_F(NextHopIdAllocatorTest, updateAllRouteNhops) {
  RouteNextHopSet curNhops = defaultNhops();
  {
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto resolvedRoutesBefore = numPostConfigResolvedRoutes(newState);
    // Previously nhop group Id set was {1} and we updated all 5 routes. But
    // as we assign new nhops, and remove {1}, that ID would get reused.
    std::set<NextHopGroupId> expectedNhopIds{1, 2, 3, 4, 5};
    for (auto i = 0; i < resolvedRoutesBefore; ++i) {
      curNhops.erase(curNhops.begin());
      CHECK(curNhops.size());
      fib6->updateNode(makeRoute(makePrefix(i), curNhops));
    }
    consolidate(newState);
    EXPECT_EQ(getNhopGroupIds(), expectedNhopIds);
  }
  {
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto resolvedRoutesBefore = numPostConfigResolvedRoutes(newState);
    // Previously nhop group Id set was {1, 2, 3, 4, 5}
    // and we update all 10 routes. But since IDs get recirculated
    // as they are freed, we should still get the same gids
    // allocated
    std::set<NextHopGroupId> expectedNhopIds{1, 2, 3, 4, 5};
    for (auto i = 0; i < resolvedRoutesBefore; ++i) {
      curNhops.erase(curNhops.begin());
      CHECK(curNhops.size());
      fib6->updateNode(makeRoute(makePrefix(i), curNhops));
    }
    consolidate(newState);
    EXPECT_EQ(getNhopGroupIds(), expectedNhopIds);
  }
}

TEST_F(NextHopIdAllocatorTest, updateRouteToUnresolved) {
  auto defaultNhopsId = *getNhopId(defaultNhops());
  EXPECT_EQ(defaultNhopsId, 1);
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto updatedRoute = fib6->exactMatch(makePrefix(0))->clone();
  updatedRoute->clearForward();
  auto resolvedRoutesBefore = numPostConfigResolvedRoutes(newState);
  // All routes point to defaultNhops
  assertRouteUsageCount(defaultNhopsId, resolvedRoutesBefore);
  fib6->updateNode(updatedRoute);
  EXPECT_EQ(fib6->size(), routesBefore);
  consolidate(newState);
  const auto& nhops2Id = consolidator_->getNhopsToId();
  EXPECT_EQ(nhops2Id.size(), 1);
  EXPECT_EQ(*getNhopId(defaultNhops()), defaultNhopsId);
  // All but newly unresolved route point to defaultNhops
  assertRouteUsageCount(defaultNhopsId, resolvedRoutesBefore - 1);
}

TEST_F(NextHopIdAllocatorTest, updateAndDeleteRoute) {
  auto newNhops = defaultNhops();
  newNhops.erase(newNhops.begin());
  {
    // Update first route to new nhops
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();
    auto resolvedRoutesBefore = numPostConfigResolvedRoutes(newState);
    fib6->updateNode(makeRoute(makePrefix(0), newNhops));
    EXPECT_EQ(fib6->size(), routesBefore);
    consolidate(newState);
    const auto& nhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 2);
    auto defaultNhopsId = *getNhopId(defaultNhops());
    auto newNhopsId = *getNhopId(newNhops);
    EXPECT_EQ(defaultNhopsId, 1);
    EXPECT_EQ(newNhopsId, 2);
    // All but one route point to defaultNhops
    assertRouteUsageCount(defaultNhopsId, resolvedRoutesBefore - 1);
    assertRouteUsageCount(newNhopsId, 1);
  }
  {
    // delete route
    auto newState = state_->clone();
    auto fib6 = fib(newState);
    auto routesBefore = fib6->size();

    fib6->removeNode(makeRoute(makePrefix(0), newNhops));
    EXPECT_EQ(fib6->size(), routesBefore - 1);
    consolidate(newState);
    const auto& nhops2Id = consolidator_->getNhopsToId();
    EXPECT_EQ(nhops2Id.size(), 1);
    auto defaultNhopsId = *getNhopId(defaultNhops());
    EXPECT_FALSE(getNhopId(newNhops).has_value());
    EXPECT_EQ(defaultNhopsId, 1);
    assertRouteUsageCount(defaultNhopsId, numPostConfigResolvedRoutes(state_));
  }
}

TEST_F(NextHopIdAllocatorTest, deleteRoute) {
  // delete route
  const auto& nhops2Id = consolidator_->getNhopsToId();
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  auto defaultNhopsId = *getNhopId(defaultNhops());
  EXPECT_EQ(defaultNhopsId, 1);
  assertRouteUsageCount(defaultNhopsId, numPostConfigResolvedRoutes(state_));
  fib6->removeNode(makeRoute(makePrefix(0), defaultNhops()));
  EXPECT_EQ(fib6->size(), routesBefore - 1);
  consolidate(newState);
  EXPECT_EQ(nhops2Id.size(), 1);
  assertRouteUsageCount(defaultNhopsId, numPostConfigResolvedRoutes(state_));
}

TEST_F(NextHopIdAllocatorTest, deleteAllRoute) {
  // delete route
  const auto& nhops2Id = consolidator_->getNhopsToId();
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto defaultNhopsId = *getNhopId(defaultNhops());
  assertRouteUsageCount(defaultNhopsId, numPostConfigResolvedRoutes(state_));
  fib6->clear();
  EXPECT_EQ(fib6->size(), 0);
  consolidate(newState);
  EXPECT_EQ(nhops2Id.size(), 0);
  EXPECT_FALSE(getNhopId(defaultNhops()).has_value());
}

TEST_F(NextHopIdAllocatorTest, addRouteAndSetBackupSwitchingMode) {
  auto newState = state_->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  fib6->addNode(makeRoute(nextPrefix(), defaultNhops()));
  EXPECT_EQ(fib6->size(), routesBefore + 1);
  EXPECT_EQ(
      consolidator_->getBackupEcmpSwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  // Set backup switching mode
  auto newFlowletSwitchingConfig =
      newState->getFlowletSwitchingConfig()->modify(&newState);
  newFlowletSwitchingConfig->setBackupSwitchingMode(
      cfg::SwitchingMode::FIXED_ASSIGNMENT);
  auto deltas = consolidate(newState);
  EXPECT_EQ(deltas.size(), 1);
  EXPECT_TRUE(consolidator_->getBackupEcmpSwitchingMode().has_value());
}

} // namespace facebook::fboss
