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
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/EcmpResourceManager.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/FileBasedWarmbootUtils.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/BaseEcmpResourceManagerTest.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

namespace facebook::fboss {

class EcmpResourceManagerRibFibTest : public ::testing::Test {
 public:
  static constexpr auto kNumIntfs = 20;
  void SetUp() override {
    FLAGS_enable_ecmp_resource_manager = true;
    FLAGS_ecmp_resource_percentage = 100;
    FLAGS_ars_resource_percentage = 100;
    FLAGS_flowletSwitchingEnable = true;
    FLAGS_dlbResourceCheckEnable = false;
    auto cfg = onePortPerIntfConfig(kNumIntfs);
    handle_ = createTestHandle(&cfg);
    sw_ = handle_->getSw();
    if (FLAGS_enable_nexthop_id_manager) {
      nextHopIDManager_ = std::make_unique<NextHopIDManager>();
    }
    ASSERT_NE(sw_->getEcmpResourceManager(), nullptr);
    // Taken from mock asic
    EXPECT_EQ(sw_->getEcmpResourceManager()->getMaxPrimaryEcmpGroups(), 5);
    // Backup ecmp group type will com from default flowlet confg
    EXPECT_EQ(
        *sw_->getEcmpResourceManager()->getBackupEcmpSwitchingMode(),
        *cfg.flowletSwitchingConfig()->backupSwitchingMode());

    // Assert rib/fib sync after config application
    assertRibFibEquivalence();
    // Add 5 routes with distinct nhops, should not go
    // over DLB limits
    auto newNhops = defaultNhopSets();
    auto newState = sw_->getState()->clone();
    auto fib6 = fib(newState);
    for (auto i = 0; i < numStartRoutes(); ++i) {
      auto newRoute = makeRoute(makePrefix(i), RouteNextHopSet(newNhops[i]));
      fib6->addNode(newRoute);
    }
    updateRoutes(newState);
  }
  void assertRibFibEquivalence() const {
    waitForStateUpdates(sw_);
    for (const auto& [_, route] : std::as_const(*cfib(sw_->getState()))) {
      auto ribRoute =
          sw_->getRib()->longestMatch(route->prefix().network(), RouterID(0));
      ASSERT_NE(ribRoute, nullptr);
      // TODO - check why are the pointers different even though the
      // forwarding info matches. This is true with or w/o consolidator
      EXPECT_EQ(ribRoute->getForwardInfo(), route->getForwardInfo());
    }
  }
  RouteNextHopSet defaultNhops() const {
    return makeNextHops(kNumIntfs);
  }
  int numStartRoutes() const {
    return 5;
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

  std::vector<RouteNextHopSet> nextNhopSets(int numSets = 5) {
    auto nhopsStart = defaultNhopSets().back();
    std::vector<RouteNextHopSet> nhopsTo;
    for (auto i = 0; i < numSets; ++i) {
      nhopsStart.erase(nhopsStart.begin());
      CHECK_GT(nhopsStart.size(), 1);
      nhopsTo.push_back(nhopsStart);
    }
    return nhopsTo;
  }
  void updateRoutes(const std::shared_ptr<SwitchState>& newStateIn) {
    // Stamp NextHop IDs on the made-up routes so getNextHops() can resolve them
    // via resolvedNextHopSetID once FLAGS_resolve_nexthops_from_id is on.
    auto newState = newStateIn->clone();
    assignNextHopIdsToAllRoutes(nextHopIDManager_.get(), newState);
    newState->publish();
    auto updater = sw_->getRouteUpdater();
    StateDelta delta(sw_->getState(), newState);
    processFibsDeltaInHwSwitchOrder(
        delta,
        [&updater, &newState](
            const RouterID& rid,
            const auto& /*oldRoute*/,
            const auto& newRoute) {
          const auto& fwd = newRoute->getForwardInfo();
          std::optional<RouteNextHopSet> resolvedNhops;
          if (FLAGS_resolve_nexthops_from_id) {
            resolvedNhops = getNextHops(newState, fwd);
          }
          updater.addRoute(
              rid,
              newRoute->prefix().network(),
              newRoute->prefix().mask(),
              kClientID,
              fwd,
              resolvedNhops);
        },
        [&updater, &newState](const RouterID& rid, const auto& newRoute) {
          const auto& fwd = newRoute->getForwardInfo();
          std::optional<RouteNextHopSet> resolvedNhops;
          if (FLAGS_resolve_nexthops_from_id) {
            resolvedNhops = getNextHops(newState, fwd);
          }
          updater.addRoute(
              rid,
              newRoute->prefix().network(),
              newRoute->prefix().mask(),
              kClientID,
              fwd,
              resolvedNhops);
        },
        [&updater](RouterID rid, const auto& oldRoute) {
          IpPrefix pfx;
          pfx.ip() = network::toBinaryAddress(oldRoute->prefix().network());
          pfx.prefixLength() = oldRoute->prefix().mask();
          updater.delRoute(rid, pfx, kClientID);
        });

    updater.program();
    assertRibFibEquivalence();
  }
  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_;
  std::unique_ptr<NextHopIDManager> nextHopIDManager_;
};

TEST_F(EcmpResourceManagerRibFibTest, init) {
  assertRibFibEquivalence();
}

TEST_F(EcmpResourceManagerRibFibTest, addRoutesAboveEcmpLimit) {
  // Add new routes pointing to new nhops. ECMP limit is breached.
  auto nhopSets = nextNhopSets();
  auto newState = sw_->getState()->clone();
  auto fib6 = fib(newState);
  auto routesBefore = fib6->size();
  std::set<RouteNextHopSet> nhops;
  std::set<RouteV6::Prefix> overflowPrefixes;
  for (auto i = 0; i < numStartRoutes(); ++i) {
    auto route = makeRoute(makePrefix(routesBefore + i), nhopSets[i]);
    overflowPrefixes.insert(route->prefix());
    nhops.insert(nhopSets[i]);
    fib6->addNode(route);
  }
  updateRoutes(newState);
}

// Warm boot into a binary that reads two persisted NextHops as one, e.g. a
// rollback to a binary that does not know a NextHopThrift field the previous
// binary set. ERM must still reconstruct from the warm boot switch state.
TEST_F(EcmpResourceManagerRibFibTest, ermReconstructsAfterNextHopCollapse) {
  // Warm boot state as persisted on graceful exit.
  auto wb = sw_->gracefulExitState();
  auto& fibInfos = *wb.swSwitchState()->fibsInfoMap();
  const auto& anyFibInfo = fibInfos.begin()->second;
  const auto persistedSets = *anyFibInfo.idToNextHopIdSet();

  // A collapse needs one NextHop under two ids, each used by a different
  // route, so the RIB must renumber one route's set when it merges them.
  // `sharedNhId` is a NextHopID used by both route 0 (nh1..nh20) and route 1
  // (nh2..nh20); `setToMove` is route 1's resolved set, which is moved onto a
  // duplicate of `sharedNhId` below while route 0 keeps the original.
  const auto& fib6 = *anyFibInfo.fibsMap()->at(0).fibV6();
  auto setToMove = *fib6.at(makePrefix(1).str()).fwd()->resolvedNextHopSetID();
  auto sharedNhId = *persistedSets.at(setToMove).begin();

  // Mimic the collapse: persist a second NextHopID with the same
  // NextHopThrift and move only route 1's set onto it, e.g.
  //   idToNextHop:      2 -> nh2, 1020 -> nh2
  //   idToNextHopIdSet: route 0 {1, 2, ...}, route 1 {1020, 3, ...}
  auto duplicateNhId = anyFibInfo.idToNextHop()->rbegin()->first + 1000;
  for (auto& [_, fibInfo] : fibInfos) {
    (*fibInfo.idToNextHop())[duplicateNhId] =
        fibInfo.idToNextHop()->at(sharedNhId);
    auto& members = fibInfo.idToNextHopIdSet()->at(setToMove);
    members.erase(sharedNhId);
    members.insert(duplicateNhId);
  }

  // Rebuild as warm boot does; the RIB keeps one of the two NextHopIDs and
  // gives an affected route a new NextHopSetID.
  auto [state, rib] = reconstructStateAndRib(wb, true /*hasL3*/);

  // Precondition: the RIB minted a NextHopSetID that was not persisted.
  auto ribMintedSetId = false;
  for (const auto& [_, route] : std::as_const(*cfib(state))) {
    auto ribRoute = rib->longestMatch(route->prefix().network(), RouterID(0));
    auto setId = ribRoute->getForwardInfo().getResolvedNextHopSetID();
    ribMintedSetId |= setId && !persistedSets.contains(*setId);
  }
  ASSERT_TRUE(ribMintedSetId);

  // ERM resolves routes by NextHopSetID; it throws if the switch state names
  // a set id its own maps do not have.
  auto erm = makeEcmpResourceManager(
      state, sw_->getHwAsicTable()->getL3Asics().front());
  EXPECT_NO_THROW(erm->reconstructFromSwitchState(state));
}

TEST_F(EcmpResourceManagerRibFibTest, ribMirrorsOverrideNextHopSetId) {
  const NextHopSetID setId(nextHopIdTypeStart(NextHopIdType::OVERRIDE_SET));
  auto withOverrides = [](const auto& state,
                          const std::optional<NextHopSetID>& id,
                          std::optional<cfg::SwitchingMode> mode) {
    auto newState = state->clone();
    auto fib6 = fib(newState);
    auto route = fib6->cbegin()->second->clone();
    RouteNextHopEntry fwd(route->getForwardInfo().toThrift());
    fwd.setOverrideNextHopSetID(id);
    fwd.setOverrideEcmpSwitchingMode(mode);
    route->setResolved(fwd);
    fib6->updateNode(route);
    newState->publish();
    return std::make_pair(newState, route->prefix().network());
  };
  auto ribSetId = [this](const folly::IPAddressV6& ip) {
    waitForStateUpdates(sw_);
    return sw_->getRib()
        ->longestMatch(ip, RouterID(0))
        ->getForwardInfo()
        .getOverrideNextHopSetID();
  };
  // An empty RIB update whose fib delta carries the overrides.
  auto syncOverrides = [this](const auto& oldState, const auto& newState) {
    sw_->getRib()->update(
        sw_->getScopeResolver(),
        RouterID(0),
        ClientID::BGPD,
        AdminDistance::EBGP,
        std::vector<UnicastRoute>{},
        std::vector<IpPrefix>{},
        false,
        "sync overrides",
        [&](const SwitchIdScopeResolver*,
            RouterID,
            const IPv4NetworkToRouteMap&,
            const IPv6NetworkToRouteMap&,
            const LabelToRouteMap&,
            const NextHopIDManager*,
            const MySidTable&,
            void*,
            bool) { return StateDelta(oldState, newState); },
        nullptr);
  };
  auto state0 = sw_->getState();
  auto [state1, ip] = withOverrides(state0, setId, std::nullopt);
  syncOverrides(state0, state1);
  EXPECT_EQ(ribSetId(ip), setId);
  // A mode-only change keeps the id.
  auto [state2, _] =
      withOverrides(state1, setId, cfg::SwitchingMode::PER_PACKET_RANDOM);
  syncOverrides(state1, state2);
  EXPECT_EQ(ribSetId(ip), setId);
  auto [state3, __] = withOverrides(state2, std::nullopt, std::nullopt);
  syncOverrides(state2, state3);
  EXPECT_EQ(ribSetId(ip), std::nullopt);
  // A newly resolved route with only the id still mirrors it.
  auto unresolvedState = state3->clone();
  auto unresolvedFib = fib(unresolvedState);
  unresolvedFib->updateNode(
      unresolvedFib->cbegin()->second->cloneForReresolve());
  unresolvedState->publish();
  auto [state4, ___] = withOverrides(unresolvedState, setId, std::nullopt);
  syncOverrides(unresolvedState, state4);
  EXPECT_EQ(ribSetId(ip), setId);
}

} // namespace facebook::fboss
