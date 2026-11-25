/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/core.h>
#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/ResourceAccountant.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

class ResourceAccountantTest : public ResourceAccountantTestBase {
 public:
  void SetUp() override {
    ResourceAccountantTestBase::SetUp();
    makeResourceAccountant();
  }
  void makeResourceAccountant() {
    resourceAccountant_ = std::make_unique<ResourceAccountant>(
        asicTable_.get(), scopeResolver_.get());
  }

  std::unique_ptr<ResourceAccountant> resourceAccountant_;
};

// This test verifies that routeAndEcmpStateChangedImpl correctly handles
// resolved and unresolved routes.
// 1. Add resolved and unresolved routes - only resolved are tracked
// 2. Delete resolved and unresolved routes - only resolved affect usage
// 3. Change resolved routes to unresolved - remove from tracking
// 4. Change unresolved routes to resolved - add to tracking
TEST_F(ResourceAccountantTest, resolvedAndUnresolvedRoutes) {
  FLAGS_enable_route_resource_protection = true;

  // Create nexthops for resolved routes
  RouteNextHopSet ecmpNexthops;
  ecmpNexthops.insert(
      ResolvedNextHop(folly::IPAddress("1.1.1.1"), InterfaceID(1), ecmpWeight));
  ecmpNexthops.insert(
      ResolvedNextHop(folly::IPAddress("1.1.1.2"), InterfaceID(2), ecmpWeight));
  RouteNextHopEntry ecmpNextHopEntry =
      RouteNextHopEntry(ecmpNexthops, AdminDistance::EBGP);

  auto initialEcmpMemberUsage =
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage();
  auto initialEcmpGroupCount =
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpGroupUsage();
  auto initialRouteUsage =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();

  // Helper to build SwitchState with V6 routes
  auto makeSwitchStateWithV6Routes =
      [this](const std::vector<std::shared_ptr<RouteV6>>& routes) {
        auto state = std::make_shared<SwitchState>();
        auto fibContainer =
            std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));
        ForwardingInformationBaseV6 fibV6;

        for (const auto& route : routes) {
          route->publish();
          fibV6.addNode(route);
        }

        fibContainer->setFib(fibV6.clone());

        // Create ForwardingInformationBaseMap and add the container
        auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
        fibsMap->updateForwardingInformationBaseContainer(fibContainer);

        HwSwitchMatcher scope(std::unordered_set<SwitchID>({SwitchID(0)}));
        auto fibInfo = std::make_shared<FibInfo>();
        fibInfo->ref<switch_state_tags::fibsMap>() = fibsMap;

        // Populate ID maps for ID-based resolution
        auto id2Nhop = std::make_shared<IdToNextHopMap>();
        for (const auto& [id, nhopEntry] :
             nextHopIDManager_->getIdToNextHop()) {
          id2Nhop->addNextHop(id, nhopEntry.nextHop.toThrift());
        }
        auto id2NhopSetIds = std::make_shared<IdToNextHopIdSetMap>();
        for (const auto& [id, nhopIdSet] :
             nextHopIDManager_->getIdToNextHopIdSet()) {
          std::set<int64_t> nhopIds;
          for (const auto& nhopId : nhopIdSet) {
            nhopIds.insert(static_cast<int64_t>(nhopId));
          }
          id2NhopSetIds->addNextHopIdSet(id, nhopIds);
        }
        fibInfo->setIdToNextHopMap(id2Nhop);
        fibInfo->setIdToNextHopIdSetMap(id2NhopSetIds);

        auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
        fibInfoMap->updateFibInfo(fibInfo, scope);
        state->resetFibsInfoMap(fibInfoMap);
        return state;
      };

  // Test 1: Add resolved and unresolved routes
  // Only resolved routes should increase usage
  auto resolvedRoute1 = makeV6Route(
      {folly::IPAddressV6("100::1"), 128}, ecmpNextHopEntry, ecmpNexthops);
  auto resolvedRoute2 = makeV6Route(
      {folly::IPAddressV6("100::2"), 128}, ecmpNextHopEntry, ecmpNexthops);

  // Create unresolved route
  auto unresolvedRoute1 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("200::1"), 128}));

  auto oldState1 = makeSwitchStateWithV6Routes({});
  auto newState1 = makeSwitchStateWithV6Routes(
      {resolvedRoute1, resolvedRoute2, unresolvedRoute1});
  StateDelta delta1(oldState1, newState1);

  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta1));

  // Only 2 resolved routes should be counted
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      initialRouteUsage + 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpGroupUsage(),
      initialEcmpGroupCount + 1);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage(),
      initialEcmpMemberUsage + 2);

  // Test 2: Delete resolved and unresolved routes
  // Only resolved route deletions should decrease usage
  auto oldState2 = newState1;
  auto newState2 = makeSwitchStateWithV6Routes({});
  StateDelta delta2(oldState2, newState2);

  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta2));

  // Should go back to initial state
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      initialRouteUsage);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpGroupUsage(),
      initialEcmpGroupCount);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage(),
      initialEcmpMemberUsage);

  // Test 3: Change resolved routes to unresolved
  // First add resolved routes
  auto oldState3a = makeSwitchStateWithV6Routes({});
  auto newState3a =
      makeSwitchStateWithV6Routes({resolvedRoute1, resolvedRoute2});
  StateDelta delta3a(oldState3a, newState3a);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta3a));

  auto usageBeforeUnresolve =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();
  auto ecmpUsageBeforeUnresolve =
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage();

  // Change to unresolved (same prefixes, but unresolved)
  auto unresolvedRoute2 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("100::1"), 128}));
  auto unresolvedRoute3 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("100::2"), 128}));

  auto oldState3b = newState3a;
  auto newState3b =
      makeSwitchStateWithV6Routes({unresolvedRoute2, unresolvedRoute3});
  StateDelta delta3b(oldState3b, newState3b);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta3b));

  // Usage should decrease when routes become unresolved
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      usageBeforeUnresolve - 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage(),
      ecmpUsageBeforeUnresolve - 2);

  // Test 4: Change unresolved routes to resolved
  auto usageBeforeResolve =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();
  auto ecmpUsageBeforeResolve =
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage();

  auto oldState4 = newState3b;
  auto newState4 =
      makeSwitchStateWithV6Routes({resolvedRoute1, resolvedRoute2});
  StateDelta delta4(oldState4, newState4);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta4));

  // Usage should increase when routes become resolved
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      usageBeforeResolve + 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpResourceAccountant_.getEcmpMemberUsage(),
      ecmpUsageBeforeResolve + 2);
}

TEST_F(ResourceAccountantTest, mySidResourceExceeded) {
  FLAGS_enable_mysid_resource_protection = true;

  // MockAsic supports 8 MySID entries, 75% = 6 allowed
  HwSwitchMatcher mySidScope(std::unordered_set<SwitchID>{SwitchID(10)});

  auto makeMySidEntry = [](const std::string& addr, uint8_t len) {
    state::MySidFields fields;
    fields.type() = MySidType::NODE_MICRO_SID;
    facebook::network::thrift::IPPrefix thriftPrefix;
    thriftPrefix.prefixAddress() =
        facebook::network::toBinaryAddress(folly::IPAddress(addr));
    thriftPrefix.prefixLength() = len;
    fields.mySid() = thriftPrefix;
    return std::make_shared<MySid>(fields);
  };

  // Add 6 entries (exactly 75% of 8) — should be accepted
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();

  auto stateWith6 = oldState->clone();
  auto mySids6 = stateWith6->getMySids()->modify(&stateWith6);
  for (int i = 0; i < 6; ++i) {
    mySids6->addNode(
        makeMySidEntry(fmt::format("fc00:{:x}::1", i + 1), 48), mySidScope);
  }
  stateWith6->publish();

  StateDelta delta6(oldState, stateWith6);
  EXPECT_TRUE(this->resourceAccountant_->isValidUpdate(delta6));

  // Add 1 more entry (7/8 = 87.5%, exceeds 75%) — should be rejected
  auto stateWith7 = stateWith6->clone();
  auto mySids7 = stateWith7->getMySids()->modify(&stateWith7);
  mySids7->addNode(makeMySidEntry("fc00:7::1", 48), mySidScope);
  stateWith7->publish();

  StateDelta delta7(stateWith6, stateWith7);
  EXPECT_FALSE(this->resourceAccountant_->isValidUpdate(delta7));
}

} // namespace facebook::fboss
