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

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/if/gen-cpp2/mpls_types.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/utils/NextHopIdTestUtils.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <folly/ScopeGuard.h>

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

namespace {

constexpr AdminDistance DISTANCE = AdminDistance::EBGP;
const RouterID kRid0 = RouterID(0);
const ClientID kClientA = ClientID(1001);

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}

// {1.1.1.10, 2.2.2.10} as they land in a FIB route's fwd entry: recursive
// resolution stamps the egress interface onto each nexthop.
RouteNextHopSet resolvedNextHopsVia1And2() {
  return RouteNextHopSet{
      ResolvedNextHop(
          folly::IPAddress("1.1.1.10"), InterfaceID(1), ECMP_WEIGHT),
      ResolvedNextHop(
          folly::IPAddress("2.2.2.10"), InterfaceID(2), ECMP_WEIGHT)};
}

} // namespace

class NextHopMapPopulationTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }

  cfg::SwitchConfig initialConfig() const {
    cfg::SwitchConfig config;
    config.switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU)}};
    config.vlans()->resize(4);
    config.vlans()[0].id() = 1;
    config.vlans()[1].id() = 2;
    config.vlans()[2].id() = 3;
    config.vlans()[3].id() = 4;

    config.interfaces()->resize(4);
    config.interfaces()[0].intfID() = 1;
    config.interfaces()[0].vlanID() = 1;
    config.interfaces()[0].routerID() = 0;
    config.interfaces()[0].mac() = "00:00:00:00:00:11";
    config.interfaces()[0].ipAddresses()->resize(2);
    config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
    config.interfaces()[0].ipAddresses()[1] = "1::1/48";

    config.interfaces()[1].intfID() = 2;
    config.interfaces()[1].vlanID() = 2;
    config.interfaces()[1].routerID() = 0;
    config.interfaces()[1].mac() = "00:00:00:00:00:22";
    config.interfaces()[1].ipAddresses()->resize(2);
    config.interfaces()[1].ipAddresses()[0] = "2.2.2.2/24";
    config.interfaces()[1].ipAddresses()[1] = "2::1/48";

    config.interfaces()[2].intfID() = 3;
    config.interfaces()[2].vlanID() = 3;
    config.interfaces()[2].routerID() = 0;
    config.interfaces()[2].mac() = "00:00:00:00:00:33";
    config.interfaces()[2].ipAddresses()->resize(2);
    config.interfaces()[2].ipAddresses()[0] = "3.3.3.3/24";
    config.interfaces()[2].ipAddresses()[1] = "3::1/48";

    config.interfaces()[3].intfID() = 4;
    config.interfaces()[3].vlanID() = 4;
    config.interfaces()[3].routerID() = 0;
    config.interfaces()[3].mac() = "00:00:00:00:00:44";
    config.interfaces()[3].ipAddresses()->resize(2);
    config.interfaces()[3].ipAddresses()[0] = "4.4.4.4/24";
    config.interfaces()[3].ipAddresses()[1] = "4::1/48";
    return config;
  }

 protected:
  RouteNextHopSet makeNextHops(
      const std::vector<std::string>& nexthopStrs,
      const std::vector<NextHopWeight>& weights = {}) {
    RouteNextHopSet nhops;
    for (size_t i = 0; i < nexthopStrs.size(); ++i) {
      NextHopWeight weight = weights.empty() ? ECMP_WEIGHT : weights[i];
      nhops.emplace(
          UnresolvedNextHop(folly::IPAddress(nexthopStrs[i]), weight));
    }
    return nhops;
  }

  // Add V4 route with nexthops
  void addV4Route(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr,
      const std::vector<std::string>& nexthopStrs,
      const std::vector<NextHopWeight>& weights = {}) {
    auto prefix = makePrefixV4(prefixStr);
    auto nhops = makeNextHops(nexthopStrs, weights);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(nhops, DISTANCE));
  }

  // Add V6 route with nexthops
  void addV6Route(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr,
      const std::vector<std::string>& nexthopStrs,
      const std::vector<NextHopWeight>& weights = {}) {
    auto prefix = makePrefixV6(prefixStr);
    auto nhops = makeNextHops(nexthopStrs, weights);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(nhops, DISTANCE));
  }

  void addV4DropRoute(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV4(prefixStr);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  }

  void addV6DropRoute(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV6(prefixStr);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  }

  void addV4ToCpuRoute(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV4(prefixStr);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  }

  void addV6ToCpuRoute(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV6(prefixStr);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        kClientA,
        RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  }

  // Add MPLS route with POP_AND_LOOKUP action.
  // POP_AND_LOOKUP routes have a null address (::) as nexthop since forwarding
  // is based on inner header lookup.
  void addMplsPopAndLookupRoute(
      SwSwitchRouteUpdateWrapper& updater,
      MplsLabel label) {
    MplsRoute mplsRoute;
    mplsRoute.topLabel() = label;
    NextHopThrift nexthop;
    nexthop.mplsAction() = MplsAction();
    *nexthop.mplsAction()->action() = MplsActionCode::POP_AND_LOOKUP;
    folly::IPAddress nullAddr{"::"};
    nexthop.address()->addr()->append(
        reinterpret_cast<const char*>(nullAddr.bytes()),
        folly::IPAddressV6::byteCount());
    mplsRoute.nextHops()->emplace_back(nexthop);
    updater.addRoute(kClientA, mplsRoute);
  }

  // Add MPLS route with SWAP action (regular MPLS forwarding). Nexthop must
  // be reachable via one of the configured interface subnets so the route
  // resolves and gets a non-empty fwd nexthop set.
  void addMplsSwapRoute(
      SwSwitchRouteUpdateWrapper& updater,
      MplsLabel label,
      MplsLabel swapLabel,
      const folly::IPAddress& nhopAddr) {
    MplsRoute mplsRoute;
    mplsRoute.topLabel() = label;
    NextHopThrift nexthop;
    nexthop.mplsAction() = MplsAction();
    *nexthop.mplsAction()->action() = MplsActionCode::SWAP;
    nexthop.mplsAction()->swapLabel() = swapLabel;
    nexthop.address() = facebook::network::toBinaryAddress(nhopAddr);
    mplsRoute.nextHops()->emplace_back(nexthop);
    updater.addRoute(kClientA, mplsRoute);
  }

  // SWAP MPLS with multiple nexthops (for ECMP scenarios).
  void addMplsSwapRouteMulti(
      SwSwitchRouteUpdateWrapper& updater,
      MplsLabel label,
      const std::vector<std::pair<folly::IPAddress, MplsLabel>>& nhops) {
    MplsRoute mplsRoute;
    mplsRoute.topLabel() = label;
    for (const auto& [nhopAddr, swapLabel] : nhops) {
      NextHopThrift nexthop;
      nexthop.mplsAction() = MplsAction();
      *nexthop.mplsAction()->action() = MplsActionCode::SWAP;
      nexthop.mplsAction()->swapLabel() = swapLabel;
      nexthop.address() = facebook::network::toBinaryAddress(nhopAddr);
      mplsRoute.nextHops()->emplace_back(nexthop);
    }
    updater.addRoute(kClientA, mplsRoute);
  }

  void delMplsRoute(
      SwSwitchRouteUpdateWrapper& updater,
      MplsLabel label,
      ClientID clientId = kClientA) {
    updater.delRoute(MplsLabel(label), clientId);
  }

  std::shared_ptr<MultiLabelForwardingInformationBase> getLabelFib() {
    auto state = sw_->getState();
    return state->getLabelForwardingInformationBase();
  }

  void delV4Route(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV4(prefixStr);
    updater.delRoute(kRid0, prefix.network(), prefix.mask(), kClientA);
  }

  void delV6Route(
      SwSwitchRouteUpdateWrapper& updater,
      const std::string& prefixStr) {
    auto prefix = makePrefixV6(prefixStr);
    updater.delRoute(kRid0, prefix.network(), prefix.mask(), kClientA);
  }

  std::shared_ptr<FibInfo> getFibInfo() {
    auto state = sw_->getState();
    return state->getFibsInfoMap()->getFibInfo(scope());
  }

  std::shared_ptr<IdToNextHopMap> getIdToNextHopMap() {
    auto fibInfo = getFibInfo();
    return fibInfo ? fibInfo->getIdToNextHopMap() : nullptr;
  }

  std::shared_ptr<IdToNextHopIdSetMap> getIdToNextHopIdSetMap() {
    auto fibInfo = getFibInfo();
    return fibInfo ? fibInfo->getIdToNextHopIdSetMap() : nullptr;
  }

  // Verifies that the ID maps in switch state match the ID maps in the
  // NextHopIDManager
  void verifyIdMapsMatchIdManager() {
    auto stateIdToNextHopMap = getIdToNextHopMap();
    auto stateIdToNextHopIdSetMap = getIdToNextHopIdSetMap();
    auto manager = sw_->getRib()->getNextHopIDManagerCopy();
    ASSERT_NE(manager, nullptr);
    const auto& managerIdToNextHop = manager->getIdToNextHop();
    const auto& managerIdToNextHopIdSet = manager->getIdToNextHopIdSet();

    // Verify sizes match
    if (stateIdToNextHopMap) {
      EXPECT_EQ(stateIdToNextHopMap->size(), managerIdToNextHop.size());
    } else {
      EXPECT_EQ(managerIdToNextHop.size(), 0);
    }

    if (stateIdToNextHopIdSetMap) {
      EXPECT_EQ(
          stateIdToNextHopIdSetMap->size(), managerIdToNextHopIdSet.size());
    } else {
      EXPECT_EQ(managerIdToNextHopIdSet.size(), 0);
    }

    // Verify each NextHop in manager exists in state with same value
    for (const auto& [id, nextHopEntry] : managerIdToNextHop) {
      auto stateNextHop = stateIdToNextHopMap->getNextHopIf(id);
      ASSERT_NE(stateNextHop, nullptr)
          << "NextHop id " << id << " not in state";
      auto stateNextHopThrift = stateNextHop->toThrift();
      EXPECT_EQ(
          network::toIPAddress(*stateNextHopThrift.address()),
          nextHopEntry.nextHop.addr())
          << "NextHop address mismatch for id " << id;
    }

    // Verify each NextHopIdSet in manager exists in state with same value
    for (const auto& [setId, nextHopIdSet] : managerIdToNextHopIdSet) {
      auto stateNextHopIdSet =
          stateIdToNextHopIdSetMap->getNextHopIdSetIf(setId);
      ASSERT_NE(stateNextHopIdSet, nullptr)
          << "NextHopIdSet id " << setId << " not in state";

      // Convert manager's NextHopIdSet to a set for comparison
      std::set<NextHopId> managerSet;
      for (const auto& nhId : nextHopIdSet) {
        managerSet.insert(nhId);
      }

      // Use toThrift() to get the set without modifying published node
      auto stateSet = stateNextHopIdSet->toThrift();

      EXPECT_EQ(stateSet, managerSet)
          << "NextHopIdSet mismatch for setId " << setId;
    }
  }

  // Walks every RIB route (resolved and unresolved). For each route checks:
  //   - per-client entries: clientNextHopSetID present iff nexthops non-empty
  //   - forwarding info: resolvedNextHopSetID present iff fwd is resolved
  //   - normalizedResolvedNextHopSetID: resolvability checked when present
  // Defaults to sw_->getRib(); pass a standalone RIB (e.g. one built via
  // RoutingInformationBase::fromThrift) to verify it directly.
  void verifyIDMapsConsistency(const RoutingInformationBase* rib = nullptr) {
    if (!rib) {
      rib = sw_->getRib();
    }
    auto manager = rib->getNextHopIDManagerCopy();
    ASSERT_NE(manager, nullptr);
    auto ribThrift = rib->toThrift();
    auto vrfIt = ribThrift.find(static_cast<int32_t>(kRid0));
    ASSERT_NE(vrfIt, ribThrift.end()) << "VRF " << kRid0 << " not found in RIB";

    auto checkRoute = [&](const auto& routeFields,
                          const std::string& prefixStr) {
      // Per-client check: walk every per-client entry on this route.
      const auto& nhMulti = *routeFields.nexthopsmulti();
      for (const auto& [clientId, perClientThrift] :
           *nhMulti.client2NextHopEntry()) {
        RouteNextHopEntry perClientEntry(perClientThrift);
        const auto& nhopSet = perClientEntry.getNextHopSet();
        auto clientSetId = perClientEntry.getClientNextHopSetID();
        if (nhopSet.empty()) {
          EXPECT_FALSE(clientSetId.has_value())
              << prefixStr << " client=" << static_cast<int>(clientId)
              << ": empty nexthop set must not have clientNextHopSetID";
          continue;
        }
        ASSERT_TRUE(clientSetId.has_value())
            << prefixStr << " client=" << static_cast<int>(clientId)
            << ": missing clientNextHopSetID";
        auto resolved = manager->getNextHopsIf(*clientSetId);
        ASSERT_TRUE(resolved.has_value())
            << prefixStr << " client=" << static_cast<int>(clientId)
            << ": clientNextHopSetID " << *clientSetId
            << " not found in manager";
        EXPECT_EQ(*resolved, nhopSet)
            << "Per-client nexthops mismatch for " << prefixStr
            << " client=" << static_cast<int>(clientId);
      }

      // Forwarding-side check: only meaningful when the route is resolved.
      RouteNextHopEntry fwdEntry(*routeFields.fwd());
      const auto& fwdNhopSet = fwdEntry.getNextHopSet();
      auto resolvedSetId = fwdEntry.getResolvedNextHopSetID();
      auto normalizedSetId = fwdEntry.getNormalizedResolvedNextHopSetID();
      if (fwdNhopSet.empty()) {
        EXPECT_FALSE(resolvedSetId.has_value())
            << prefixStr
            << ": unresolved/DROP route must not have resolvedNextHopSetID";
        EXPECT_FALSE(normalizedSetId.has_value())
            << prefixStr
            << ": unresolved/DROP route must not have normalizedResolvedNextHopSetID";
        return;
      }
      ASSERT_TRUE(resolvedSetId.has_value())
          << prefixStr << ": resolved route missing resolvedNextHopSetID";
      auto resolvedNextHops = manager->getNextHopsIf(*resolvedSetId);
      ASSERT_TRUE(resolvedNextHops.has_value())
          << prefixStr << ": resolvedNextHopSetID " << *resolvedSetId
          << " not found in manager";
      EXPECT_EQ(*resolvedNextHops, fwdNhopSet)
          << "Forwarding nexthops mismatch for " << prefixStr;

      // Normalized ID is only stamped after ECMP normalization; verify
      // resolvability if present, but don't require its presence.
      if (normalizedSetId.has_value()) {
        auto normalizedNextHops = manager->getNextHopsIf(*normalizedSetId);
        EXPECT_TRUE(normalizedNextHops.has_value())
            << prefixStr << ": normalizedResolvedNextHopSetID "
            << *normalizedSetId << " not found in manager";
      }
    };

    for (const auto& [prefixStr, routeFields] :
         *vrfIt->second.v4NetworkToRoute()) {
      checkRoute(routeFields, prefixStr);
    }
    for (const auto& [prefixStr, routeFields] :
         *vrfIt->second.v6NetworkToRoute()) {
      checkRoute(routeFields, prefixStr);
    }
    // MPLS routes share RouteNextHopsMulti/RouteNextHopEntry shape with IP,
    // so the same checks apply.
    for (const auto& [label, routeFields] : *vrfIt->second.labelToRoute()) {
      checkRoute(routeFields, folly::to<std::string>("label=", label));
    }
  }

  // Multi-client variants of addV4Route/delV4Route (the default helpers
  // hardcode kClientA).
  void addV4RouteForClient(
      SwSwitchRouteUpdateWrapper& updater,
      ClientID clientId,
      const std::string& prefixStr,
      const std::vector<std::string>& nexthopStrs) {
    auto prefix = makePrefixV4(prefixStr);
    auto nhops = makeNextHops(nexthopStrs);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        clientId,
        RouteNextHopEntry(nhops, DISTANCE));
  }

  void addV4DropRouteForClient(
      SwSwitchRouteUpdateWrapper& updater,
      ClientID clientId,
      const std::string& prefixStr) {
    auto prefix = makePrefixV4(prefixStr);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        clientId,
        RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  }

  void addV6RouteForClient(
      SwSwitchRouteUpdateWrapper& updater,
      ClientID clientId,
      const std::string& prefixStr,
      const std::vector<std::string>& nexthopStrs) {
    auto prefix = makePrefixV6(prefixStr);
    auto nhops = makeNextHops(nexthopStrs);
    updater.addRoute(
        kRid0,
        prefix.network(),
        prefix.mask(),
        clientId,
        RouteNextHopEntry(nhops, DISTANCE));
  }

  void delV4RouteForClient(
      SwSwitchRouteUpdateWrapper& updater,
      ClientID clientId,
      const std::string& prefixStr) {
    auto prefix = makePrefixV4(prefixStr);
    updater.delRoute(kRid0, prefix.network(), prefix.mask(), clientId);
  }

  // Returns the clientNextHopSetID stored on (prefix, clientId), or nullopt.
  std::optional<NextHopSetID> getStoredClientId(
      const std::string& prefixStr,
      ClientID clientId) {
    auto state = sw_->getState();
    auto fibInfo = getFibInfo();
    if (!fibInfo) {
      return std::nullopt;
    }
    auto fibContainer = fibInfo->getFibContainerIf(kRid0);
    if (!fibContainer) {
      return std::nullopt;
    }
    auto prefix = makePrefixV4(prefixStr);
    auto fibV4 = fibContainer->getFibV4();
    for (const auto& [_, route] : std::as_const(*fibV4)) {
      if (route->prefix().network() == prefix.network() &&
          route->prefix().mask() == prefix.mask()) {
        auto entry = route->getEntryForClient(clientId);
        return entry ? entry->getClientNextHopSetID() : std::nullopt;
      }
    }
    return std::nullopt;
  }

  void addStandardTestRoutes(
      SwSwitchRouteUpdateWrapper& updater,
      bool includeV6 = true) {
    // V4 routes
    // 1. 2 routes with 3 nexthops (same ones) - shared ECMP group
    std::vector<std::string> sharedV4Nexthops = {"1.1.1.10", "2.2.2.10"};
    addV4Route(updater, "10.0.0.0/24", sharedV4Nexthops);
    addV4Route(updater, "10.0.1.0/24", sharedV4Nexthops);

    // 2. 1 route with single nexthop
    addV4Route(updater, "10.1.0.0/24", {"2.2.2.20"});

    // 3. 1 route with 4 nexthops
    addV4Route(
        updater,
        "10.2.0.0/24",
        {"1.1.1.10", "2.2.2.10", "3.3.3.10", "4.4.4.10"});

    // 4. 3 routes with partial overlap
    addV4Route(updater, "10.3.0.0/24", {"1.1.1.10", "2.2.2.10", "3.3.3.10"});
    addV4Route(updater, "10.3.1.0/24", {"2.2.2.10", "3.3.3.10", "4.4.4.10"});
    addV4Route(updater, "10.3.2.0/24", {"3.3.3.10", "4.4.4.10", "1.1.1.10"});

    // 5. 1 route with DROP action
    addV4DropRoute(updater, "10.4.0.0/24");

    // 6. 1 route with TO_CPU action
    addV4ToCpuRoute(updater, "10.5.0.0/24");

    // 7. Weighted routes for normalized ID testing
    addV4Route(updater, "10.6.0.0/24", {"1.1.1.10", "2.2.2.10"}, {1, 1});
    addV4Route(updater, "10.6.1.0/24", {"1.1.1.10", "2.2.2.10"}, {10, 20});
    addV4Route(updater, "10.6.2.0/24", {"1.1.1.10", "2.2.2.10"}, {100, 200});

    if (!includeV6) {
      return;
    }

    // V6 routes (same patterns)
    // 1. 2 routes with shared nexthops
    std::vector<std::string> sharedV6Nexthops = {"1::10", "2::10"};
    addV6Route(updater, "2001:db8:1::/64", sharedV6Nexthops);
    addV6Route(updater, "2001:db8:2::/64", sharedV6Nexthops);

    // 2. 1 route with single nexthop
    addV6Route(updater, "2001:db8:10::/64", {"2::20"});

    // 3. 1 route with 4 nexthops
    addV6Route(
        updater, "2001:db8:20::/64", {"1::10", "2::10", "3::10", "4::10"});

    // 4. 3 routes with partial overlap
    addV6Route(updater, "2001:db8:30::/64", {"1::10", "2::10", "3::10"});
    addV6Route(updater, "2001:db8:31::/64", {"2::10", "3::10", "4::10"});
    addV6Route(updater, "2001:db8:32::/64", {"3::10", "4::10", "1::10"});

    // 5. 1 route with DROP action
    addV6DropRoute(updater, "2001:db8:40::/64");

    // 6. 1 route with TO_CPU action
    addV6ToCpuRoute(updater, "2001:db8:50::/64");
  }

  SwSwitch* sw_ = nullptr;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(NextHopMapPopulationTest, RouteAdditionTestsThroughRIBUpdater) {
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();

  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

TEST_F(NextHopMapPopulationTest, RouteDeletionTestsThroughRIBUpdater) {
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete one route with same nexthop (shared ECMP) and check consistency
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.0.0.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete the other shared route
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.0.1.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.6.2.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete multiple routes in a single update
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.3.0.0/24");
  delV4Route(updater, "10.1.0.0/24");
  delV4Route(updater, "10.2.0.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete DROP and TO_CPU routes
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.4.0.0/24");
  delV4Route(updater, "10.5.0.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete all remaining routes including weighted routes
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.3.1.0/24");
  delV4Route(updater, "10.3.2.0/24");
  delV4Route(updater, "10.6.0.0/24");
  delV4Route(updater, "10.6.1.0/24");
  delV6Route(updater, "2001:db8:1::/64");
  delV6Route(updater, "2001:db8:2::/64");
  delV6Route(updater, "2001:db8:10::/64");
  delV6Route(updater, "2001:db8:20::/64");
  delV6Route(updater, "2001:db8:30::/64");
  delV6Route(updater, "2001:db8:31::/64");
  delV6Route(updater, "2001:db8:32::/64");
  delV6Route(updater, "2001:db8:40::/64");
  delV6Route(updater, "2001:db8:50::/64");
  updater.program();

  verifyIdMapsMatchIdManager();
}

TEST_F(NextHopMapPopulationTest, RouteUpdateTestsThroughRIBUpdater) {
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Update route from having 2 nexthops to 4 (ECMP expand)
  updater = sw_->getRouteUpdater();
  addV4Route(
      updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10", "3.3.3.10", "4.4.4.10"});
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Update route with 4 nexthops (10.2.0.0/24) to have 2 nexthops (ECMP shrink)
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.2.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Update route to a completely new set of nexthops
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"3.3.3.10", "4.4.4.10"});
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Update route from having nexthops to TO_CPU action
  updater = sw_->getRouteUpdater();
  addV4ToCpuRoute(updater, "10.0.0.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Update route from TO_CPU action to having nexthops
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

TEST_F(NextHopMapPopulationTest, SerializationTests) {
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();

  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Get original maps for comparison
  auto originalIdToNextHopMap = getIdToNextHopMap();
  auto originalIdToNextHopIdSetMap = getIdToNextHopIdSetMap();

  // Serialize the FibInfo
  auto fibInfo = getFibInfo();
  ASSERT_NE(fibInfo, nullptr);
  auto serialized = fibInfo->toThrift();

  // Deserialize into a new FibInfo
  auto deserialized = std::make_shared<FibInfo>(serialized);

  // Verify IdToNextHopMap is preserved
  auto deserializedNextHopMap = deserialized->getIdToNextHopMap();
  ASSERT_NE(deserializedNextHopMap, nullptr);
  EXPECT_EQ(deserializedNextHopMap->size(), originalIdToNextHopMap->size())
      << "IdToNextHopMap size should be preserved after deserialization";

  // Verify IdToNextHopIdSetMap is preserved
  auto deserializedNextHopIdSetMap = deserialized->getIdToNextHopIdSetMap();
  ASSERT_NE(deserializedNextHopIdSetMap, nullptr);
  EXPECT_EQ(
      deserializedNextHopIdSetMap->size(), originalIdToNextHopIdSetMap->size())
      << "IdToNextHopIdSetMap size should be preserved after deserialization";
}

// Test general addition of POP_AND_LOOKUP MPLS routes alongside normal
// weighted routes. This verifies that both types of routes are handled
// correctly
TEST_F(NextHopMapPopulationTest, PopAndLookupWithNormalRouteAddition) {
  // First, add standard IP routes
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater, false /* includeV6 */);

  // Now add MPLS POP_AND_LOOKUP routes
  addMplsPopAndLookupRoute(updater, 1001);
  addMplsPopAndLookupRoute(updater, 1002);
  addMplsPopAndLookupRoute(updater, 1003);
  updater.program();

  // Verify MPLS POP_AND_LOOKUP routes have resolvedNextHopSetID but NOT
  // normalizedResolvedNextHopSetID
  auto labelFib = getLabelFib();
  ASSERT_NE(labelFib, nullptr);

  auto labelEntry1 = labelFib->getNodeIf(1001);
  ASSERT_NE(labelEntry1, nullptr);
  const auto& labelFwdInfo1 = labelEntry1->getForwardInfo();
  EXPECT_TRUE(labelFwdInfo1.getResolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route should have resolvedNextHopSetID";
  EXPECT_FALSE(labelFwdInfo1.getNormalizedResolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route should NOT have "
      << "normalizedResolvedNextHopSetID";

  auto labelEntry2 = labelFib->getNodeIf(1002);
  ASSERT_NE(labelEntry2, nullptr);
  const auto& labelFwdInfo2 = labelEntry2->getForwardInfo();
  EXPECT_TRUE(labelFwdInfo2.getResolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route should have resolvedNextHopSetID";
  EXPECT_FALSE(labelFwdInfo2.getNormalizedResolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route should NOT have "
      << "normalizedResolvedNextHopSetID";

  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

// Verifies per-client clientNextHopSetID allocation in
// RibRouteUpdater::addOrReplaceRoute(LabelID, ...). Both SWAP and
// POP_AND_LOOKUP MPLS routes must get clientNextHopSetID stamped on
// their per-client entry, mirroring the v4/v6 allocation path.
TEST_F(NextHopMapPopulationTest, AllocatesClientIdsForMplsRoutes) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // SWAP MPLS route via interface subnet 1::/48.
  addMplsSwapRoute(updater, 100, 101, folly::IPAddress("1::10"));
  // POP_AND_LOOKUP MPLS route.
  addMplsPopAndLookupRoute(updater, 200);
  updater.program();

  auto ribThrift = sw_->getRib()->toThrift();
  ASSERT_FALSE(ribThrift.empty());
  const auto& labelToRoute = *ribThrift.begin()->second.labelToRoute();

  auto verifyClientIdsPresent = [](const auto& routeFields) {
    for (const auto& [_clientId, entry] :
         *routeFields.nexthopsmulti()->client2NextHopEntry()) {
      if (!entry.nexthops()->empty()) {
        EXPECT_TRUE(entry.clientNextHopSetID().has_value())
            << "MPLS per-client entry missing clientNextHopSetID";
      }
    }
  };

  auto swapIt = labelToRoute.find(100);
  ASSERT_NE(swapIt, labelToRoute.end());
  verifyClientIdsPresent(swapIt->second);

  auto popIt = labelToRoute.find(200);
  ASSERT_NE(popIt, labelToRoute.end());
  verifyClientIdsPresent(popIt->second);
}

// --- SwitchStateNextHopIdUpdater incremental diff tests ---
// These verify the updater correctly diffs RIB vs FIB maps and only
// applies the delta (adds missing entries, removes stale entries).

TEST_F(NextHopMapPopulationTest, UpdaterNoChangesReturnsSameState) {
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Run the updater again with no RIB changes — should return same state
  auto stateBefore = sw_->getState();
  auto managerCopy = sw_->getRib()->getNextHopIDManagerCopy();
  SwitchStateNextHopIdUpdater ribUpdater(managerCopy.get());
  auto stateAfter = ribUpdater(stateBefore);
  EXPECT_EQ(stateAfter.get(), stateBefore.get());
}

TEST_F(NextHopMapPopulationTest, UpdaterIncrementalAddViaRouteUpdater) {
  // Start with standard routes (shared ECMP, single nhop, overlapping, etc.)
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Incrementally add routes with both new and existing nexthops
  updater = sw_->getRouteUpdater();
  // These reuse existing nexthops — only new set IDs
  addV4Route(updater, "10.10.0.0/24", {"3.3.3.10"});
  addV4Route(updater, "10.10.1.0/24", {"1.1.1.10", "4.4.4.10"});
  addV6Route(updater, "2001:db8:100::/64", {"3::10", "4::10"});
  // These use brand new nexthops — new nhop IDs + set IDs
  addV4Route(updater, "10.10.4.0/24", {"1.1.1.30", "2.2.2.30"});
  addV6Route(updater, "2001:db8:101::/64", {"1::30"});
  // DROP/TO_CPU don't allocate IDs
  addV4DropRoute(updater, "10.10.2.0/24");
  addV4ToCpuRoute(updater, "10.10.3.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

TEST_F(NextHopMapPopulationTest, UpdaterIncrementalRemoveViaRouteUpdater) {
  // Start with standard routes
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete several routes: one from shared ECMP pair, overlapping groups, etc.
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.0.0.0/24");
  delV4Route(updater, "10.3.0.0/24");
  delV4Route(updater, "10.3.1.0/24");
  delV6Route(updater, "2001:db8:1::/64");
  delV6Route(updater, "2001:db8:30::/64");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Delete remaining routes that used unique nexthops
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.1.0.0/24");
  delV6Route(updater, "2001:db8:10::/64");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

TEST_F(NextHopMapPopulationTest, UpdaterAddAndRemoveInSingleUpdate) {
  // Start with standard routes
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Simultaneously delete some routes and add new ones
  updater = sw_->getRouteUpdater();
  // Delete shared ECMP pair and overlapping group
  delV4Route(updater, "10.0.0.0/24");
  delV4Route(updater, "10.0.1.0/24");
  delV4Route(updater, "10.3.0.0/24");
  delV6Route(updater, "2001:db8:1::/64");
  delV6Route(updater, "2001:db8:2::/64");
  // Add new routes with mix of existing and new nexthops
  addV4Route(updater, "10.20.0.0/24", {"1.1.1.10", "3.3.3.10"});
  addV4Route(updater, "10.20.1.0/24", {"4.4.4.10"});
  addV6Route(updater, "2001:db8:200::/64", {"1::10", "2::10", "3::10"});
  addV4DropRoute(updater, "10.20.2.0/24");
  updater.program();
  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();
}

// ----- Per-client clientNextHopSetID lifecycle tests -----

// Covers: (1) initial alloc stamps an ID, (2) two clients with the same
// nexthop set share the ID via refcount, (3) re-programming the same set
// keeps the ID stable.
TEST_F(NextHopMapPopulationTest, ClientIdAllocationAndSharing) {
  const ClientID kClientB = ClientID(1002);

  // Step 1: kClientA programs a route -> ID gets stamped.
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  auto idA = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(idA.has_value());

  // Step 2: kClientB programs the same nexthops on a different prefix ->
  // refcount-sharing produces the same ID.
  updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientB, "10.1.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  auto idB = getStoredClientId("10.1.0.0/24", kClientB);
  ASSERT_TRUE(idB.has_value());
  EXPECT_EQ(*idA, *idB);

  // Step 3: kClientA re-programs the same nexthops -> same-set short-circuit
  // keeps the original ID stable.
  updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  auto idAReprogram = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(idAReprogram.has_value());
  EXPECT_EQ(*idA, *idAReprogram);

  verifyIDMapsConsistency();
}

// Covers: (1) re-programming with a different set switches to a new ID,
// (2) DROP / TO_CPU entries never get an ID.
TEST_F(NextHopMapPopulationTest, ClientIdSwitchOnDifferentSetAndNoneOnDrop) {
  // Step 1: program a route, then re-program with a different nexthop set.
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();
  auto idBefore = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(idBefore.has_value());

  updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"3.3.3.10", "4.4.4.10"});
  updater.program();
  auto idAfter = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(idAfter.has_value());
  EXPECT_NE(*idBefore, *idAfter);

  // Step 2: a DROP route gets no clientNextHopSetID.
  updater = sw_->getRouteUpdater();
  addV4DropRouteForClient(updater, kClientA, "10.4.0.0/24");
  updater.program();
  EXPECT_FALSE(getStoredClientId("10.4.0.0/24", kClientA).has_value());

  verifyIDMapsConsistency();
}

// Two clients share a nexthop set; deleting one's route drops its refcount
// but the set survives because the other client still references it.
TEST_F(NextHopMapPopulationTest, DeleteOneClientReleasesItsIdOnly) {
  const ClientID kClientB = ClientID(1002);
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4RouteForClient(
      updater, kClientB, "10.1.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();

  auto sharedId = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(sharedId.has_value());
  ASSERT_EQ(*sharedId, *getStoredClientId("10.1.0.0/24", kClientB));

  updater = sw_->getRouteUpdater();
  delV4RouteForClient(updater, kClientA, "10.0.0.0/24");
  updater.program();

  auto idStillThere = getStoredClientId("10.1.0.0/24", kClientB);
  ASSERT_TRUE(idStillThere.has_value());
  EXPECT_EQ(*idStillThere, *sharedId);
  verifyIDMapsConsistency();
}

// Verifies the ID-only primitive getNextHopsFromRib: returns the nexthops
// associated with a known setId, and throws FbossError when the setId is
// not present in the manager.
TEST_F(NextHopMapPopulationTest, getNextHopsFromRibPrimitive) {
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto route =
      fibContainer->getFibV4()->exactMatch(makePrefixV4("10.0.0.0/24"));
  ASSERT_NE(route, nullptr);
  const auto& fwd = route->getForwardInfo();
  auto setId = fwd.getResolvedNextHopSetID();
  ASSERT_TRUE(setId.has_value());

  // Lookup against valid id returns expected set.
  EXPECT_EQ(
      getNextHopsFromRib(manager.get(), NextHopSetID(*setId)),
      fwd.getNextHopSet());

  // Lookup against an unknown id throws FbossError.
  NextHopSetID unknownId(static_cast<int64_t>(*setId) + 9999);
  EXPECT_THROW(getNextHopsFromRib(manager.get(), unknownId), FbossError);
}

// Verifies getNonOverrideNormalizedNextHopsFromRib: returns inline
// non-override normalized nexthops when flag is off, resolves via manager
// (normalizedResolvedNextHopSetID) when flag is on. For NEXTHOPS-action
// entries the two paths produce the same set; DROP/TO_CPU entries return
// empty in both paths.
TEST_F(
    NextHopMapPopulationTest,
    getNonOverrideNormalizedNextHopsFromRibWrapper) {
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4DropRouteForClient(updater, kClientA, "10.4.0.0/24");
  updater.program();

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto fibV4 = fibContainer->getFibV4();

  auto runChecks = [&]() {
    // The RIB width must track the config-sourced width on SwitchState.
    auto ecmpWidth = getEcmpWidth(sw_->getState());
    EXPECT_EQ(ecmpWidth, sw_->getRib()->getEcmpWidth());
    for (const auto& [_, route] : std::as_const(*fibV4)) {
      const auto& fwd = route->getForwardInfo();
      EXPECT_EQ(
          getNonOverrideNormalizedNextHopsFromRib(
              manager.get(), fwd, ecmpWidth),
          fwd.nonOverrideNormalizedNextHops(ecmpWidth));
    }
  };

  // Flag OFF (test fixture default): returns inline normalized.
  FLAGS_resolve_nexthops_from_id = false;
  runChecks();

  // Flag ON: resolves via manager. Must equal inline by invariant.
  FLAGS_resolve_nexthops_from_id = true;
  runChecks();
  FLAGS_resolve_nexthops_from_id = false;
}

// Verifies getNormalizedNextHopsFromRib dispatch: an entry carrying
// overrideNextHops is resolved via normalizedNextHops() (honoring the
// override, independent of flag/manager); an entry without overrides
// delegates to getNonOverrideNormalizedNextHopsFromRib.
TEST_F(NextHopMapPopulationTest, getNormalizedNextHopsFromRibOverrideAware) {
  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);

  // Base set {A}; override adds B so honoring it yields a different set than
  // ignoring it. Nexthops must be resolved -- normalizeNextHops() requires it.
  RouteNextHopSet baseNhops;
  baseNhops.emplace(ResolvedNextHop(
      folly::IPAddress("1.1.1.1"), InterfaceID(1), ECMP_WEIGHT));
  RouteNextHopSet overrideNhops = baseNhops;
  overrideNhops.emplace(ResolvedNextHop(
      folly::IPAddress("2.2.2.2"), InterfaceID(2), ECMP_WEIGHT));

  RouteNextHopEntry overrideEntry(baseNhops, DISTANCE);
  overrideEntry.setOverrideNextHops(overrideNhops);
  ASSERT_TRUE(overrideEntry.getOverrideNextHops().has_value());

  // Override branch short-circuits before the flag/manager logic, so it holds
  // for both flag states.
  // The RIB width must track the config-sourced width on SwitchState.
  auto ecmpWidth = getEcmpWidth(sw_->getState());
  EXPECT_EQ(ecmpWidth, sw_->getRib()->getEcmpWidth());
  for (bool flag : {false, true}) {
    FLAGS_resolve_nexthops_from_id = flag;
    EXPECT_EQ(
        getNormalizedNextHopsFromRib(manager.get(), overrideEntry, ecmpWidth),
        overrideEntry.normalizedNextHops(ecmpWidth));
  }
  FLAGS_resolve_nexthops_from_id = false;
  // The override actually changes the result vs. ignoring it, so honoring it
  // is meaningful.
  EXPECT_NE(
      overrideEntry.normalizedNextHops(ecmpWidth),
      overrideEntry.nonOverrideNormalizedNextHops(ecmpWidth));

  // No overrides: delegates to the non-override helper. Flag off => inline
  // path, so the directly-built entry needs no manager lookup.
  RouteNextHopEntry plainEntry(overrideNhops, DISTANCE);
  ASSERT_FALSE(plainEntry.getOverrideNextHops().has_value());
  EXPECT_EQ(
      getNormalizedNextHopsFromRib(manager.get(), plainEntry, ecmpWidth),
      getNonOverrideNormalizedNextHopsFromRib(
          manager.get(), plainEntry, ecmpWidth));
}

// Verifies getNormalizedNextHopsFromRib sources the ECMP width from the
// RibRouteTables width (config-sourced via cfg.SwitchSettings.ecmpWidth), not
// from FLAGS_ecmp_width.
TEST_F(NextHopMapPopulationTest, getNormalizedNextHopsFromRibUsesRibWidth) {
  gflags::FlagSaver flagSaver;
  FLAGS_resolve_nexthops_from_id = false; // exercise the inline normalize path
  FLAGS_ecmp_width = 64;

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  constexpr uint32_t kNarrowEcmpWidth = 1;
  sw_->getRib()->setEcmpWidth(kNarrowEcmpWidth);

  RouteNextHopSet nhops{
      ResolvedNextHop(folly::IPAddress("1.1.1.1"), InterfaceID(1), ECMP_WEIGHT),
      ResolvedNextHop(
          folly::IPAddress("2.2.2.2"), InterfaceID(2), ECMP_WEIGHT)};
  RouteNextHopEntry entry(nhops, DISTANCE);

  // Must normalize to the RIB width (collapsing to one nexthop), not to
  // FLAGS_ecmp_width (64 would keep both).
  auto result = getNormalizedNextHopsFromRib(
      manager.get(), entry, sw_->getRib()->getEcmpWidth());
  EXPECT_EQ(
      result, RouteNextHopEntry::normalizeNextHops(nhops, kNarrowEcmpWidth));
  EXPECT_NE(result, RouteNextHopEntry::normalizeNextHops(nhops));
}

// Verifies getResolvedNextHopsFromRib: returns inline fwd nexthops when
// flag is off, resolves via manager (resolvedNextHopSetID) when flag is
// on, and returns empty for DROP/TO_CPU entries (no resolvedSetId).
TEST_F(NextHopMapPopulationTest, getResolvedNextHopsFromRibWrapper) {
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4DropRouteForClient(updater, kClientA, "10.4.0.0/24");
  updater.program();

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto fibV4 = fibContainer->getFibV4();

  auto runChecks = [&]() {
    for (const auto& [_, route] : std::as_const(*fibV4)) {
      const auto& fwd = route->getForwardInfo();
      EXPECT_EQ(
          getResolvedNextHopsFromRib(manager.get(), fwd), fwd.getNextHopSet());
    }
  };

  // Flag OFF: returns inline fwd nexthops (DROP returns empty inline too).
  FLAGS_resolve_nexthops_from_id = false;
  runChecks();

  // Flag ON: resolves via manager. Must equal inline by invariant.
  FLAGS_resolve_nexthops_from_id = true;
  runChecks();
  FLAGS_resolve_nexthops_from_id = false;
}

// Verifies getClientNextHopsFromRib: returns inline per-client nexthops
// when flag is off, and resolves via manager (clientNextHopSetID) when flag
// is on. Both paths must return the same set by invariant.
TEST_F(NextHopMapPopulationTest, getClientNextHopsFromRibWrapper) {
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto route =
      fibContainer->getFibV4()->exactMatch(makePrefixV4("10.0.0.0/24"));
  ASSERT_NE(route, nullptr);
  auto entry = route->getEntryForClient(kClientA);
  ASSERT_NE(entry, nullptr);

  // Flag OFF (test fixture default): returns inline.
  FLAGS_resolve_nexthops_from_id = false;
  EXPECT_EQ(
      getClientNextHopsFromRib(manager.get(), *entry), entry->getNextHopSet());

  // Flag ON: resolves via manager. Must equal inline by invariant.
  FLAGS_resolve_nexthops_from_id = true;
  EXPECT_EQ(
      getClientNextHopsFromRib(manager.get(), *entry), entry->getNextHopSet());
  FLAGS_resolve_nexthops_from_id = false;
}

// Covers the resolver being threaded through getRouteTableDetails. Two
// clients, so resolving one through another's entry would be caught.
TEST_F(NextHopMapPopulationTest, getRouteTableDetailsResolvesClientNextHops) {
  // Pinned: preceding tests leave the flag false without restoring it.
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  FLAGS_resolve_nexthops_from_id = true;

  const ClientID kClientB = ClientID(1002);
  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4RouteForClient(updater, kClientB, "10.0.0.0/24", {"3.3.3.10"});
  updater.program();

  for (const auto& rd : sw_->getRib()->getRouteTableDetails(kRid0)) {
    if (*rd.dest()->prefixLength() != 24 ||
        facebook::network::toIPAddress(*rd.dest()->ip()).str() != "10.0.0.0") {
      continue;
    }
    EXPECT_EQ(
        clientNextHops(rd, kClientA),
        (std::set<std::string>{"1.1.1.10", "2.2.2.10"}));
    EXPECT_EQ(
        clientNextHops(rd, kClientB), (std::set<std::string>{"3.3.3.10"}));
    return;
  }
  ADD_FAILURE() << "10.0.0.0/24 missing from route table details";
}

// Label routes carry clientNextHopSetID like v4/v6 and go through the same
// toThriftLegacy path.
TEST_F(NextHopMapPopulationTest, getMplsRouteTableDetailsResolvesClientNhops) {
  // Pinned: preceding tests leave the flag false without restoring it.
  auto savedMplsRib = FLAGS_mpls_rib;
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_mpls_rib = savedMplsRib;
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  FLAGS_mpls_rib = true;
  FLAGS_resolve_nexthops_from_id = true;

  auto updater = sw_->getRouteUpdater();
  addMplsSwapRoute(updater, 100, 101, folly::IPAddress("1::10"));
  updater.program();

  for (const auto& rd : sw_->getRib()->getMplsRouteTableDetails()) {
    if (*rd.topLabel() != 100) {
      continue;
    }
    EXPECT_EQ(clientNextHops(rd, kClientA), (std::set<std::string>{"1::10"}));
    return;
  }
  ADD_FAILURE() << "label 100 missing from MPLS route table details";
}

// Full mix: resolved + DROP + TO_CPU + unresolved + multi-client routes,
// all in a single state. Exercises the unified verifier end-to-end.
TEST_F(NextHopMapPopulationTest, MixedResolvedAndUnresolvedRoutes) {
  const ClientID kClientB = ClientID(1002);
  auto updater = sw_->getRouteUpdater();

  // Resolved + DROP + TO_CPU. Interfaces are on 1.1.1.0/24, 2.2.2.0/24,
  // 3.3.3.0/24, 4.4.4.0/24 (V4) and corresponding V6 subnets.
  addStandardTestRoutes(updater);

  // Unresolved V4 routes: nexthops not on any interface subnet.
  addV4Route(updater, "20.0.0.0/24", {"99.99.99.99"});
  addV4Route(updater, "20.1.0.0/24", {"99.99.99.99", "88.88.88.88"});

  // Unresolved V6 routes.
  addV6Route(updater, "2002:db8::/64", {"99::1"});
  addV6Route(updater, "2002:db8:1::/64", {"99::1", "88::1"});

  // Multi-client routes from kClientB: one resolved (shared nexthop set),
  // one unresolved.
  addV4RouteForClient(
      updater, kClientB, "30.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4RouteForClient(updater, kClientB, "30.1.0.0/24", {"77.77.77.77"});

  updater.program();

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// fromThrift backfill stamps all three IDs on snapshot routes that lack them.
// Mirrors the production flag-OFF -> flag-ON warmboot scenario by programming
// routes with the manager off (so no IDs are stamped at program time), then
// reconstructing with the manager on (backfill should fill in all IDs).
TEST_F(NextHopMapPopulationTest, BackfillsMissingIdsFromWarmBoot) {
  // Tear down the fixture's flag-on handle and rebuild with both flags off so
  // routes are programmed without any IDs. resolve_nexthops_from_id requires
  // enable_nexthop_id_manager, so the flag-OFF world must turn both off.
  // Restore flags on exit so they don't leak to subsequent tests.
  auto savedEnable = FLAGS_enable_nexthop_id_manager;
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedEnable;
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  handle_.reset();
  FLAGS_enable_nexthop_id_manager = false;
  FLAGS_resolve_nexthops_from_id = false;
  auto config = initialConfig();
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();

  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  updater.program();

  auto ribThrift = sw_->getRib()->toThrift();

  // Reconstruct under flag-on -- backfill runs inside fromThrift.
  FLAGS_enable_nexthop_id_manager = true;
  FLAGS_resolve_nexthops_from_id = true;
  auto reconstructedRib = RoutingInformationBase::fromThrift(
      ribThrift,
      nullptr,
      std::make_shared<MultiLabelForwardingInformationBase>(),
      std::make_shared<MultiSwitchMySidMap>(),
      getEcmpWidth(sw_->getState()),
      sw_->getState()->getClassBasedPolicies());

  // Every NEXTHOPS route must have all three IDs populated.
  auto reThrift = reconstructedRib->toThrift();
  size_t verifiedClientEntries = 0;
  size_t verifiedFwd = 0;
  auto verifyRoute = [&](const auto& routeFields) {
    if (!routeFields.fwd()->nexthops()->empty()) {
      EXPECT_TRUE(routeFields.fwd()->resolvedNextHopSetID().has_value());
      EXPECT_TRUE(
          routeFields.fwd()->normalizedResolvedNextHopSetID().has_value());
      ++verifiedFwd;
    }
    for (const auto& [_clientId, entry] :
         *routeFields.nexthopsmulti()->client2NextHopEntry()) {
      if (!entry.nexthops()->empty()) {
        EXPECT_TRUE(entry.clientNextHopSetID().has_value());
        ++verifiedClientEntries;
      }
    }
  };
  for (const auto& [_, routeTable] : reThrift) {
    for (const auto& [__, v4Route] : *routeTable.v4NetworkToRoute()) {
      verifyRoute(v4Route);
    }
    for (const auto& [__, v6Route] : *routeTable.v6NetworkToRoute()) {
      verifyRoute(v6Route);
    }
  }
  EXPECT_GT(verifiedClientEntries, 0u);
  EXPECT_GT(verifiedFwd, 0u);

  auto postManager = reconstructedRib->getNextHopIDManagerCopy();
  ASSERT_NE(postManager, nullptr);
  EXPECT_FALSE(postManager->getIdToNextHop().empty());
  EXPECT_FALSE(postManager->getIdToNextHopIdSet().empty());
}

// MPLS variant of the warmboot backfill test. Verifies:
//   - SWAP MPLS routes get all three IDs stamped on backfill
//     (resolvedNextHopSetID + normalizedResolvedNextHopSetID + per-client).
//   - POP_AND_LOOKUP MPLS routes get resolvedNextHopSetID + per-client only,
//     NOT normalizedResolvedNextHopSetID (POP_AND_LOOKUP forwards via inner-
//     header lookup, so there are no nexthops to normalize). Matches the
//     RouteUpdater allocation-time asymmetry at RouteUpdater.cpp's
//     `if (!labelPopandLookup)` guard.
TEST_F(NextHopMapPopulationTest, BackfillsMissingMplsIdsFromWarmBoot) {
  // Rebuild with both flags off so MPLS routes are programmed without IDs.
  // resolve_nexthops_from_id requires enable_nexthop_id_manager, so the
  // flag-OFF world must turn both off.
  // FLAGS_mpls_rib needed so RIB carries labelToRoute the backfill can walk.
  // Restore flags on exit so they don't leak to subsequent tests.
  auto savedEnable = FLAGS_enable_nexthop_id_manager;
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  auto savedMplsRib = FLAGS_mpls_rib;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = savedEnable;
    FLAGS_resolve_nexthops_from_id = savedResolve;
    FLAGS_mpls_rib = savedMplsRib;
  };
  handle_.reset();
  FLAGS_enable_nexthop_id_manager = false;
  FLAGS_resolve_nexthops_from_id = false;
  FLAGS_mpls_rib = true;
  auto config = initialConfig();
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();

  auto updater = sw_->getRouteUpdater();
  // SWAP MPLS route resolving via interface subnet 1::/48.
  addMplsSwapRoute(updater, 100, 101, folly::IPAddress("1::10"));
  // POP_AND_LOOKUP MPLS route.
  addMplsPopAndLookupRoute(updater, 200);
  updater.program();

  auto ribThrift = sw_->getRib()->toThrift();

  // Reconstruct under flag-on — backfill walks labelToRoute too.
  FLAGS_enable_nexthop_id_manager = true;
  FLAGS_resolve_nexthops_from_id = true;
  auto reconstructedRib = RoutingInformationBase::fromThrift(
      ribThrift,
      nullptr,
      std::make_shared<MultiLabelForwardingInformationBase>(),
      std::make_shared<MultiSwitchMySidMap>(),
      getEcmpWidth(sw_->getState()),
      sw_->getState()->getClassBasedPolicies());
  auto reThrift = reconstructedRib->toThrift();
  ASSERT_FALSE(reThrift.empty());
  const auto& labelToRoute = *reThrift.begin()->second.labelToRoute();

  auto verifyClientIdsPresent = [](const auto& routeFields) {
    for (const auto& [_clientId, entry] :
         *routeFields.nexthopsmulti()->client2NextHopEntry()) {
      if (!entry.nexthops()->empty()) {
        EXPECT_TRUE(entry.clientNextHopSetID().has_value())
            << "MPLS per-client entry missing clientNextHopSetID";
      }
    }
  };

  // SWAP route at label 100: resolved + normalized + per-client all set.
  auto swapIt = labelToRoute.find(100);
  ASSERT_NE(swapIt, labelToRoute.end());
  EXPECT_TRUE(swapIt->second.fwd()->resolvedNextHopSetID().has_value())
      << "SWAP MPLS route missing resolvedNextHopSetID";
  EXPECT_TRUE(
      swapIt->second.fwd()->normalizedResolvedNextHopSetID().has_value())
      << "SWAP MPLS route missing normalizedResolvedNextHopSetID";
  verifyClientIdsPresent(swapIt->second);

  // POP_AND_LOOKUP at label 200: resolved + per-client only, NO normalized.
  auto popIt = labelToRoute.find(200);
  ASSERT_NE(popIt, labelToRoute.end());
  EXPECT_TRUE(popIt->second.fwd()->resolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route missing resolvedNextHopSetID";
  EXPECT_FALSE(
      popIt->second.fwd()->normalizedResolvedNextHopSetID().has_value())
      << "POP_AND_LOOKUP MPLS route should NOT have "
      << "normalizedResolvedNextHopSetID";
  verifyClientIdsPresent(popIt->second);
}

// syncFib's bulk-delete path (removeAllUnclaimedRoutesFromClientImpl) must
// release the resolved + normalized nexthop set IDs when a reclaimed route
// is erased entirely. Covers both v4 and v6 template instantiations in one
// syncFib. Without the release the manager refcounts leak on every BGP
// reconvergence.
TEST_F(NextHopMapPopulationTest, SyncFibReleasesFwdSideIdsForReclaimedRoutes) {
  // Two v4 + two v6 routes with distinct nexthop sets so each owns a unique
  // resolved/normalized setID — no refcount sharing to mask a leak.
  auto updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10"});
  addV4Route(updater, "10.1.0.0/24", {"2.2.2.10"});
  addV6Route(updater, "2001:db8::/64", {"1::10"});
  addV6Route(updater, "2001:db8:1::/64", {"2::10"});
  updater.program();

  // Capture the setIDs of the routes that will be reclaimed
  // (10.0.0.0/24 and 2001:db8::/64).
  NextHopSetID v4ResolvedId, v4NormalizedId, v6ResolvedId, v6NormalizedId;
  {
    auto ribThrift = sw_->getRib()->toThrift();
    const auto& v4Route =
        ribThrift.begin()->second.v4NetworkToRoute()->at("10.0.0.0/24");
    auto v4ResolvedOpt = v4Route.fwd()->resolvedNextHopSetID();
    auto v4NormalizedOpt = v4Route.fwd()->normalizedResolvedNextHopSetID();
    ASSERT_TRUE(v4ResolvedOpt.has_value());
    ASSERT_TRUE(v4NormalizedOpt.has_value());
    v4ResolvedId = NextHopSetID(*v4ResolvedOpt);
    v4NormalizedId = NextHopSetID(*v4NormalizedOpt);

    const auto& v6Route =
        ribThrift.begin()->second.v6NetworkToRoute()->at("2001:db8::/64");
    auto v6ResolvedOpt = v6Route.fwd()->resolvedNextHopSetID();
    auto v6NormalizedOpt = v6Route.fwd()->normalizedResolvedNextHopSetID();
    ASSERT_TRUE(v6ResolvedOpt.has_value());
    ASSERT_TRUE(v6NormalizedOpt.has_value());
    v6ResolvedId = NextHopSetID(*v6ResolvedOpt);
    v6NormalizedId = NextHopSetID(*v6NormalizedOpt);
  }

  auto preManager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(preManager, nullptr);
  ASSERT_TRUE(preManager->getIdToNextHopIdSet().contains(v4ResolvedId));
  ASSERT_TRUE(preManager->getIdToNextHopIdSet().contains(v4NormalizedId));
  ASSERT_TRUE(preManager->getIdToNextHopIdSet().contains(v6ResolvedId));
  ASSERT_TRUE(preManager->getIdToNextHopIdSet().contains(v6NormalizedId));

  // syncFib with only the surviving prefix in each family. The reclaimed
  // routes (10.0.0.0/24, 2001:db8::/64) go through
  // removeAllUnclaimedRoutesFromClientImpl<v4> and <v6> respectively. Each
  // was the only client on its prefix, so the route is erased entirely —
  // its resolved + normalized IDs must be released.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.1.0.0/24", {"2.2.2.10"});
  addV6Route(updater, "2001:db8:1::/64", {"2::10"});
  RouteUpdateWrapper::SyncFibInfo syncInfo;
  syncInfo.ridAndClients.insert({kRid0, kClientA});
  syncInfo.type = RouteUpdateWrapper::SyncFibInfo::SyncFibType::IP_ONLY;
  updater.program(syncInfo);

  auto postManager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(postManager, nullptr);
  EXPECT_FALSE(postManager->getIdToNextHopIdSet().contains(v4ResolvedId))
      << "v4 resolvedNextHopSetID leaked in syncFib reclaim path";
  EXPECT_FALSE(postManager->getIdToNextHopIdSet().contains(v4NormalizedId))
      << "v4 normalizedResolvedNextHopSetID leaked in syncFib reclaim path";
  EXPECT_FALSE(postManager->getIdToNextHopIdSet().contains(v6ResolvedId))
      << "v6 resolvedNextHopSetID leaked in syncFib reclaim path";
  EXPECT_FALSE(postManager->getIdToNextHopIdSet().contains(v6NormalizedId))
      << "v6 normalizedResolvedNextHopSetID leaked in syncFib reclaim path";
}

// Regression: a route going resolved -> truly unresolvable must clear its fwd
// info, not just flip the flag, else the freed resolvedNextHopSetID stays
// stamped and double-frees on the next re-resolve/remove. An A->B->C->A nexthop
// cycle forces genuine unresolvability (no nexthops/drop/to-cpu) despite the
// fixture's default route.
TEST_F(NextHopMapPopulationTest, ResolvedToUnresolvableClearsFwdSideIds) {
  auto fwdResolvedId =
      [&](const std::string& prefixStr) -> std::optional<int64_t> {
    auto ribThrift = sw_->getRib()->toThrift();
    const auto& v4 = *ribThrift.begin()->second.v4NetworkToRoute();
    auto it = v4.find(prefixStr);
    if (it == v4.end()) {
      return std::nullopt;
    }
    return it->second.fwd()->resolvedNextHopSetID().to_optional();
  };
  auto fwdNormalizedId =
      [&](const std::string& prefixStr) -> std::optional<int64_t> {
    auto ribThrift = sw_->getRib()->toThrift();
    const auto& v4 = *ribThrift.begin()->second.v4NetworkToRoute();
    auto it = v4.find(prefixStr);
    if (it == v4.end()) {
      return std::nullopt;
    }
    return it->second.fwd()->normalizedResolvedNextHopSetID().to_optional();
  };

  // Step 1 (unresolved -> resolved): three routes resolve via connected
  // nexthops; each gets a distinct fwd-side resolved + normalized setID the
  // manager holds.
  auto updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/8", {"1.1.1.10"});
  addV4Route(updater, "20.0.0.0/8", {"2.2.2.10"});
  addV4Route(updater, "30.0.0.0/8", {"3.3.3.10"});
  updater.program();
  auto idA = fwdResolvedId("10.0.0.0/8");
  auto idB = fwdResolvedId("20.0.0.0/8");
  auto idC = fwdResolvedId("30.0.0.0/8");
  ASSERT_TRUE(idA.has_value());
  ASSERT_TRUE(idB.has_value());
  ASSERT_TRUE(idC.has_value());
  ASSERT_TRUE(fwdNormalizedId("10.0.0.0/8").has_value());
  ASSERT_TRUE(fwdNormalizedId("20.0.0.0/8").has_value());
  ASSERT_TRUE(fwdNormalizedId("30.0.0.0/8").has_value());
  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // Step 2 (resolved -> truly unresolvable): rewire into an A->B->C->A cycle.
  // No route resolves, so each hits the unresolvable path. The fwd-side IDs
  // must be cleared off the route and freed from the manager. Without the fix
  // the stale freed IDs stay stamped and these EXPECT_FALSEs fail.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/8", {"20.1.1.10"});
  addV4Route(updater, "20.0.0.0/8", {"30.1.1.10"});
  addV4Route(updater, "30.0.0.0/8", {"10.1.1.10"});
  updater.program();
  EXPECT_FALSE(fwdResolvedId("10.0.0.0/8").has_value());
  EXPECT_FALSE(fwdResolvedId("20.0.0.0/8").has_value());
  EXPECT_FALSE(fwdResolvedId("30.0.0.0/8").has_value());
  EXPECT_FALSE(fwdNormalizedId("10.0.0.0/8").has_value());
  EXPECT_FALSE(fwdNormalizedId("20.0.0.0/8").has_value());
  EXPECT_FALSE(fwdNormalizedId("30.0.0.0/8").has_value());
  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // Step 3 (unresolvable -> resolved again): rewire back to connected
  // nexthops. The routes re-resolve with fresh fwd-side IDs; without the fix
  // updating from the dangling old ID double-frees here.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/8", {"1.1.1.10"});
  addV4Route(updater, "20.0.0.0/8", {"2.2.2.10"});
  addV4Route(updater, "30.0.0.0/8", {"3.3.3.10"});
  updater.program();
  EXPECT_TRUE(fwdResolvedId("10.0.0.0/8").has_value());
  EXPECT_TRUE(fwdResolvedId("20.0.0.0/8").has_value());
  EXPECT_TRUE(fwdResolvedId("30.0.0.0/8").has_value());
  EXPECT_TRUE(fwdNormalizedId("10.0.0.0/8").has_value());
  EXPECT_TRUE(fwdNormalizedId("20.0.0.0/8").has_value());
  EXPECT_TRUE(fwdNormalizedId("30.0.0.0/8").has_value());
  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // Step 4 (remove): the routes remove cleanly with no double-free.
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.0.0.0/8");
  delV4Route(updater, "20.0.0.0/8");
  delV4Route(updater, "30.0.0.0/8");
  updater.program();
  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// ===========================================================================
// Comprehensive ID-consistency suite. Every test below includes MPLS routes
// alongside v4/v6 to lock in the symmetric handling across all three families.
// ===========================================================================

// Routes from the same family sharing identical nexthop sets must reuse the
// same setID (getOrAlloc + refcount). Verified across v4, v6, MPLS.
TEST_F(NextHopMapPopulationTest, OverlappingNexthopsShareSetIdsAcrossPrefixes) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // Two v4 routes with identical nexthops.
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4Route(updater, "10.0.1.0/24", {"1.1.1.10", "2.2.2.10"});
  // Two v6 routes with identical nexthops.
  addV6Route(updater, "2001:db8:1::/64", {"1::10", "2::10"});
  addV6Route(updater, "2001:db8:2::/64", {"1::10", "2::10"});
  // Two MPLS SWAP routes with identical nexthops.
  addMplsSwapRouteMulti(
      updater,
      3000,
      {{folly::IPAddress("1::10"), 3001}, {folly::IPAddress("2::10"), 3002}});
  addMplsSwapRouteMulti(
      updater,
      3100,
      {{folly::IPAddress("1::10"), 3001}, {folly::IPAddress("2::10"), 3002}});
  updater.program();

  verifyIdMapsMatchIdManager();
  verifyIDMapsConsistency();

  // Manager must reuse setIDs across the paired prefixes.
  auto ribThrift = sw_->getRib()->toThrift();
  const auto& table = ribThrift.begin()->second;
  auto v4a =
      table.v4NetworkToRoute()->at("10.0.0.0/24").fwd()->resolvedNextHopSetID();
  auto v4b =
      table.v4NetworkToRoute()->at("10.0.1.0/24").fwd()->resolvedNextHopSetID();
  ASSERT_TRUE(v4a.has_value() && v4b.has_value());
  EXPECT_EQ(*v4a, *v4b) << "v4 routes with same nexthops must share setID";

  auto v6a = table.v6NetworkToRoute()
                 ->at("2001:db8:1::/64")
                 .fwd()
                 ->resolvedNextHopSetID();
  auto v6b = table.v6NetworkToRoute()
                 ->at("2001:db8:2::/64")
                 .fwd()
                 ->resolvedNextHopSetID();
  ASSERT_TRUE(v6a.has_value() && v6b.has_value());
  EXPECT_EQ(*v6a, *v6b) << "v6 routes with same nexthops must share setID";

  auto mplsA = table.labelToRoute()->at(3000).fwd()->resolvedNextHopSetID();
  auto mplsB = table.labelToRoute()->at(3100).fwd()->resolvedNextHopSetID();
  ASSERT_TRUE(mplsA.has_value() && mplsB.has_value());
  EXPECT_EQ(*mplsA, *mplsB)
      << "MPLS routes with same nexthops must share setID";
}

// Programs a diverse set, snapshots via toThrift, deserializes via
// fromThrift, and verifies the reconstructed manager state has identical
// nh / set membership for the same IDs.
TEST_F(NextHopMapPopulationTest, WarmbootRoundTripPreservesManagerState) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  addStandardTestRoutes(updater);
  addV4Route(updater, "20.0.0.0/24", {"99.99.99.1"}); // unresolved
  addV6Route(updater, "2099::/64", {"99::1"}); // unresolved
  addMplsSwapRoute(updater, 5000, 5001, folly::IPAddress("1::10"));
  addMplsSwapRoute(
      updater, 5010, 5011, folly::IPAddress("99::99")); // unresolved
  addMplsPopAndLookupRoute(updater, 5100);
  updater.program();

  auto preManager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(preManager, nullptr);
  auto ribThrift = sw_->getRib()->toThrift();

  // Reconstruct via fromThrift (drives reconstructFromSwitchStateMaps +
  // backfill). Pass empty FibInfo so reconstruction relies entirely on the
  // snapshot.
  auto reconstructedRib = RoutingInformationBase::fromThrift(
      ribThrift,
      sw_->getState()->getFibsInfoMap(),
      sw_->getState()->getLabelForwardingInformationBase(),
      std::make_shared<MultiSwitchMySidMap>(),
      getEcmpWidth(sw_->getState()),
      sw_->getState()->getClassBasedPolicies());
  auto postManager = reconstructedRib->getNextHopIDManagerCopy();
  ASSERT_NE(postManager, nullptr);

  // Same nh-IDs and set-IDs must be present in both managers.
  EXPECT_EQ(
      preManager->getIdToNextHop().size(),
      postManager->getIdToNextHop().size());
  EXPECT_EQ(
      preManager->getIdToNextHopIdSet().size(),
      postManager->getIdToNextHopIdSet().size());
  for (const auto& [id, entry] : preManager->getIdToNextHop()) {
    const auto& postMap = postManager->getIdToNextHop();
    auto it = postMap.find(id);
    ASSERT_NE(it, postMap.end()) << "nh-ID " << id << " missing after warmboot";
    EXPECT_EQ(it->second.nextHop, entry.nextHop);
  }
  for (const auto& [setId, members] : preManager->getIdToNextHopIdSet()) {
    const auto& postMap = postManager->getIdToNextHopIdSet();
    auto it = postMap.find(setId);
    ASSERT_NE(it, postMap.end())
        << "set-ID " << setId << " missing after warmboot";
    EXPECT_EQ(it->second, members);
  }

  verifyIDMapsConsistency(reconstructedRib.get());
}

// Same prefix from two clients with disjoint nexthop sets: each client must
// get its own clientNextHopSetID. Underlying NextHopIDs are reused where
// nexthops overlap across families.
TEST_F(NextHopMapPopulationTest, MultiClientPerRoutePerType) {
  FLAGS_mpls_rib = true;
  const ClientID kClientB = ClientID(1002);
  auto updater = sw_->getRouteUpdater();
  // v4 same prefix, two clients, disjoint nexthops.
  addV4RouteForClient(updater, kClientA, "10.0.0.0/24", {"1.1.1.10"});
  addV4RouteForClient(updater, kClientB, "10.0.0.0/24", {"2.2.2.10"});
  // v4 same prefix, two clients, overlapping (but distinct) nexthops.
  addV4RouteForClient(
      updater, kClientA, "10.1.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4RouteForClient(
      updater, kClientB, "10.1.0.0/24", {"1.1.1.10", "3.3.3.10"});
  // v6 same prefix, two clients, overlapping (but distinct) nexthops.
  addV6RouteForClient(updater, kClientA, "2001:db8::/64", {"1::10", "2::10"});
  addV6RouteForClient(updater, kClientB, "2001:db8::/64", {"1::10", "3::10"});
  // MPLS SWAP same label, two clients, disjoint swap labels.
  addMplsSwapRoute(updater, 6000, 6001, folly::IPAddress("1::10"));
  // For client B on same MPLS label, can't use the helper (it hardcodes
  // kClientA). Build the MplsRoute directly.
  {
    MplsRoute mplsRoute;
    mplsRoute.topLabel() = 6000;
    NextHopThrift nexthop;
    nexthop.mplsAction() = MplsAction();
    *nexthop.mplsAction()->action() = MplsActionCode::SWAP;
    nexthop.mplsAction()->swapLabel() = 6002;
    nexthop.address() =
        facebook::network::toBinaryAddress(folly::IPAddress("2::10"));
    mplsRoute.nextHops()->emplace_back(nexthop);
    updater.addRoute(kClientB, mplsRoute);
  }
  updater.program();

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // Each per-client entry sharing a prefix/label must carry its own setID
  // (different nexthop sets => different clientNextHopSetIDs). Verified across
  // all three families (v4, v6, MPLS).
  auto ribThrift = sw_->getRib()->toThrift();
  const auto& vrf = ribThrift.begin()->second;
  auto clientIds = [kClientB](const auto& routeFields) {
    std::optional<int64_t> a, b;
    for (const auto& [clientId, entry] :
         *routeFields.nexthopsmulti()->client2NextHopEntry()) {
      if (clientId == kClientA) {
        a = entry.clientNextHopSetID().to_optional();
      } else if (clientId == kClientB) {
        b = entry.clientNextHopSetID().to_optional();
      }
    }
    return std::make_pair(a, b);
  };

  auto [v4A, v4B] = clientIds(vrf.v4NetworkToRoute()->at("10.1.0.0/24"));
  ASSERT_TRUE(v4A.has_value() && v4B.has_value());
  EXPECT_NE(*v4A, *v4B)
      << "v4: different nexthop sets must produce different clientNextHopSetIDs";

  auto [v6A, v6B] = clientIds(vrf.v6NetworkToRoute()->at("2001:db8::/64"));
  ASSERT_TRUE(v6A.has_value() && v6B.has_value());
  EXPECT_NE(*v6A, *v6B)
      << "v6: different nexthop sets must produce different clientNextHopSetIDs";

  auto [mplsA, mplsB] = clientIds(vrf.labelToRoute()->at(6000));
  ASSERT_TRUE(mplsA.has_value() && mplsB.has_value());
  EXPECT_NE(*mplsA, *mplsB) << "MPLS: different nexthop sets must produce "
                               "different clientNextHopSetIDs";
}

// Update a route's nexthops; old clientNextHopSetID releases (refcount--),
// new one allocates. Resolved/normalized IDs change in lockstep.
TEST_F(
    NextHopMapPopulationTest,
    UpdateRouteNexthopsAllTypesReleasesAndAllocates) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // Seed: unique nexthop sets per route so refcount transitions are
  // observable (not masked by sharing).
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10"});
  addV6Route(updater, "2001:db8::/64", {"1::10"});
  addMplsSwapRoute(updater, 7000, 7001, folly::IPAddress("1::10"));
  updater.program();

  auto v4Before = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(v4Before.has_value());
  auto preManager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(preManager, nullptr);

  // Update each route to a different nexthop set.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"2.2.2.10"});
  addV6Route(updater, "2001:db8::/64", {"2::10"});
  addMplsSwapRoute(updater, 7000, 7001, folly::IPAddress("2::10"));
  updater.program();

  auto v4After = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(v4After.has_value());
  EXPECT_NE(*v4Before, *v4After) << "different nexthops => new setID";

  // The old set (just {1.1.1.10}) must no longer be present in the manager
  // (only the route on it was 10.0.0.0/24, which we updated). Likewise
  // {1::10} and the MPLS SWAP-to-1::10. New sets must be present.
  auto postManager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(postManager, nullptr);
  // Sanity: total set count is unchanged (we replaced 3 unique sets with
  // 3 new unique sets; no overlap with anything else).
  EXPECT_EQ(
      preManager->getIdToNextHopIdSet().size(),
      postManager->getIdToNextHopIdSet().size());

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// Delete a subset of routes. Shared setIDs remain until last referrer goes;
// unique setIDs vanish on first delete.
TEST_F(NextHopMapPopulationTest, DeleteRoutesReleasesIdsRefcountAware) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // Shared nexthop set across two v4 routes.
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV4Route(updater, "10.0.1.0/24", {"1.1.1.10", "2.2.2.10"});
  // Unique v6 route.
  addV6Route(updater, "2001:db8::/64", {"1::10"});
  // Shared MPLS SWAP set across two labels.
  addMplsSwapRouteMulti(updater, 8000, {{folly::IPAddress("1::10"), 8001}});
  addMplsSwapRouteMulti(updater, 8010, {{folly::IPAddress("1::10"), 8001}});
  updater.program();

  auto sharedV4 = getStoredClientId("10.0.0.0/24", kClientA);
  ASSERT_TRUE(sharedV4.has_value());
  auto sharedV4Sibling = getStoredClientId("10.0.1.0/24", kClientA);
  ASSERT_EQ(*sharedV4, *sharedV4Sibling);
  // Capture pre-delete fwd-side setIDs. Snapshot by VALUE (not via the
  // field_ref returned by toThrift's thrift accessors, which would dangle).
  NextHopSetID sharedMpls;
  NextHopSetID v6FwdSetId;
  {
    auto rib = sw_->getRib()->toThrift();
    auto mplsOpt = rib.begin()
                       ->second.labelToRoute()
                       ->at(8000)
                       .fwd()
                       ->resolvedNextHopSetID();
    ASSERT_TRUE(mplsOpt.has_value());
    sharedMpls = NextHopSetID(*mplsOpt);
    auto v6Opt = rib.begin()
                     ->second.v6NetworkToRoute()
                     ->at("2001:db8::/64")
                     .fwd()
                     ->resolvedNextHopSetID();
    ASSERT_TRUE(v6Opt.has_value());
    v6FwdSetId = NextHopSetID(*v6Opt);
  }

  // Helper: does this setID still live in the manager?
  auto setExists = [&](const NextHopSetID& id) {
    auto mgr = sw_->getRib()->getNextHopIDManagerCopy();
    EXPECT_NE(mgr, nullptr);
    if (!mgr) {
      return false;
    }
    return mgr->getIdToNextHopIdSet().contains(id);
  };

  // Phase 1: delete the unique v6 route and one of the shared v4 routes.
  updater = sw_->getRouteUpdater();
  delV6Route(updater, "2001:db8::/64");
  delV4Route(updater, "10.0.0.0/24");
  updater.program();
  // Shared v4 setID still present (10.0.1.0/24 still references it).
  EXPECT_TRUE(setExists(*sharedV4));
  EXPECT_EQ(*getStoredClientId("10.0.1.0/24", kClientA), *sharedV4);
  // Unique v6 set is gone.
  EXPECT_FALSE(setExists(v6FwdSetId));
  // Shared MPLS set unchanged.
  EXPECT_TRUE(setExists(sharedMpls));

  // Phase 2: delete the other v4 referrer and one MPLS label.
  updater = sw_->getRouteUpdater();
  delV4Route(updater, "10.0.1.0/24");
  delMplsRoute(updater, 8000);
  updater.program();
  // Shared v4 set is now gone (no remaining referrer).
  EXPECT_FALSE(setExists(*sharedV4));
  // Shared MPLS set still present (8010 still references it).
  EXPECT_TRUE(setExists(sharedMpls));

  // Phase 3: delete remaining MPLS label: its set is now gone.
  updater = sw_->getRouteUpdater();
  delMplsRoute(updater, 8010);
  updater.program();
  EXPECT_FALSE(setExists(sharedMpls));

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// In a single update, flip one route from unresolved -> resolved AND another
// from resolved -> unresolved. Verifies bidirectional ID transitions hold
// consistency at all once. Covers v4, v6, and MPLS SWAP simultaneously.
TEST_F(NextHopMapPopulationTest, ResolvabilityFlipBothDirections) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // Seed: one resolved + one unresolved per family.
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10"}); // resolved
  addV4Route(updater, "20.0.0.0/24", {"99.99.99.1"}); // unresolved
  addV6Route(updater, "2001:db8::/64", {"1::10"}); // resolved
  addV6Route(updater, "2099::/64", {"99::1"}); // unresolved
  addMplsSwapRoute(
      updater, 12000, 12001, folly::IPAddress("1::10")); // resolved
  addMplsSwapRoute(
      updater, 12100, 12101, folly::IPAddress("99::99")); // unresolved
  updater.program();

  // Snapshot the per-client IDs before the flip so we can verify they
  // change post-update.
  auto idV4ResolvedBefore = getStoredClientId("10.0.0.0/24", kClientA);
  auto idV4UnresolvedBefore = getStoredClientId("20.0.0.0/24", kClientA);
  ASSERT_TRUE(idV4ResolvedBefore.has_value());
  ASSERT_TRUE(idV4UnresolvedBefore.has_value());

  // Confirm fwd-side IDs reflect resolvability:
  {
    auto ribThrift = sw_->getRib()->toThrift();
    const auto& tbl = ribThrift.begin()->second;
    EXPECT_TRUE(tbl.v4NetworkToRoute()
                    ->at("10.0.0.0/24")
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());
    EXPECT_FALSE(tbl.v4NetworkToRoute()
                     ->at("20.0.0.0/24")
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
    EXPECT_TRUE(tbl.v6NetworkToRoute()
                    ->at("2001:db8::/64")
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());
    EXPECT_FALSE(tbl.v6NetworkToRoute()
                     ->at("2099::/64")
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
    EXPECT_TRUE(tbl.labelToRoute()
                    ->at(12000)
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());
    EXPECT_FALSE(tbl.labelToRoute()
                     ->at(12100)
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
  }

  // Single update: flip both directions.
  //   - Previously-unresolved 20.0.0.0/24, 2099::/64, label 12100 become
  //     resolvable by switching nexthop into an interface subnet.
  //   - Previously-resolved 10.0.0.0/24, 2001:db8::/64, label 12000 become
  //     unresolvable by switching nexthop to a non-interface subnet.
  updater = sw_->getRouteUpdater();
  // Resolved -> unresolved
  addV4Route(updater, "10.0.0.0/24", {"99.99.99.1"});
  addV6Route(updater, "2001:db8::/64", {"99::1"});
  addMplsSwapRoute(updater, 12000, 12001, folly::IPAddress("99::99"));
  // Unresolved -> resolved
  addV4Route(updater, "20.0.0.0/24", {"1.1.1.10"});
  addV6Route(updater, "2099::/64", {"1::10"});
  addMplsSwapRoute(updater, 12100, 12101, folly::IPAddress("1::10"));
  updater.program();

  // Per-client IDs must have changed for every flipped route (different
  // underlying nexthop set => different setID).
  auto idV4ResolvedAfter = getStoredClientId("10.0.0.0/24", kClientA);
  auto idV4UnresolvedAfter = getStoredClientId("20.0.0.0/24", kClientA);
  ASSERT_TRUE(idV4ResolvedAfter.has_value());
  ASSERT_TRUE(idV4UnresolvedAfter.has_value());
  EXPECT_NE(*idV4ResolvedBefore, *idV4ResolvedAfter);
  EXPECT_NE(*idV4UnresolvedBefore, *idV4UnresolvedAfter);

  // Fwd-side IDs must reflect the new resolvability:
  //   - 10.0.0.0/24, 2001:db8::/64, label 12000 now have NO resolved/normalized
  //   - 20.0.0.0/24, 2099::/64, label 12100 now have resolved + normalized
  {
    auto ribThrift = sw_->getRib()->toThrift();
    const auto& tbl = ribThrift.begin()->second;
    EXPECT_FALSE(tbl.v4NetworkToRoute()
                     ->at("10.0.0.0/24")
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
    EXPECT_FALSE(tbl.v4NetworkToRoute()
                     ->at("10.0.0.0/24")
                     .fwd()
                     ->normalizedResolvedNextHopSetID()
                     .has_value());
    EXPECT_TRUE(tbl.v4NetworkToRoute()
                    ->at("20.0.0.0/24")
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());
    EXPECT_TRUE(tbl.v4NetworkToRoute()
                    ->at("20.0.0.0/24")
                    .fwd()
                    ->normalizedResolvedNextHopSetID()
                    .has_value());

    EXPECT_FALSE(tbl.v6NetworkToRoute()
                     ->at("2001:db8::/64")
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
    EXPECT_TRUE(tbl.v6NetworkToRoute()
                    ->at("2099::/64")
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());

    EXPECT_FALSE(tbl.labelToRoute()
                     ->at(12000)
                     .fwd()
                     ->resolvedNextHopSetID()
                     .has_value());
    EXPECT_TRUE(tbl.labelToRoute()
                    ->at(12100)
                    .fwd()
                    ->resolvedNextHopSetID()
                    .has_value());
  }

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// Companion of the above. Tests a busy mixed-state RIB where one update
// touches many routes — some flipping resolvability, some unchanged — and
// the manager state remains consistent. Adds extra noise (sharers, multi-
// client) so the test exercises refcount transitions, not just allocation.
TEST_F(NextHopMapPopulationTest, MixedFlipsWithSharedNexthopsRemainConsistent) {
  FLAGS_mpls_rib = true;
  const ClientID kClientB = ClientID(1002);
  auto updater = sw_->getRouteUpdater();
  // Group A: two routes share an unresolvable nexthop set; one is going to
  // flip to resolvable, the other stays unresolvable. So the shared setID
  // should survive the flip (other route still references it).
  addV4Route(updater, "30.0.0.0/24", {"99.99.99.1"});
  addV4Route(updater, "30.1.0.0/24", {"99.99.99.1"});
  // Group B: two MPLS routes share a resolvable nexthop set; one flips to
  // unresolvable.
  addMplsSwapRoute(updater, 13000, 13001, folly::IPAddress("1::10"));
  addMplsSwapRoute(updater, 13100, 13001, folly::IPAddress("1::10"));
  // Group C: multi-client v6 route, one client flips its per-client entry's
  // nexthops from unresolvable to resolvable.
  addV6RouteForClient(updater, kClientA, "2030::/64", {"99::1"});
  addV6RouteForClient(updater, kClientB, "2030::/64", {"1::10"});
  // Idle background routes that don't change.
  addStandardTestRoutes(updater);
  updater.program();
  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // The flip.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "30.0.0.0/24", {"1.1.1.10"}); // unresolved -> resolved
  // 30.1.0.0/24 unchanged: still references the unresolvable nexthop set.
  addMplsSwapRoute(
      updater,
      13100,
      13001,
      folly::IPAddress("99::99")); // resolved -> unresolved
  addV6RouteForClient(
      updater, kClientA, "2030::/64", {"1::10"}); // unresolved -> resolved
  updater.program();

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();

  // Spot-checks:
  //   - 30.0.0.0/24 now has resolved/normalized IDs.
  //   - 30.1.0.0/24 still has no fwd-side IDs (still unresolvable).
  //   - 13100 no longer has resolved/normalized IDs.
  //   - 13000 still has resolved/normalized IDs (its nexthop is still 1::10).
  //   - 2030::/64 has at least one client with resolved-side IDs (kClientA
  //     and kClientB now both contribute the same nexthop {1::10}).
  auto ribThrift = sw_->getRib()->toThrift();
  const auto& tbl = ribThrift.begin()->second;
  EXPECT_TRUE(tbl.v4NetworkToRoute()
                  ->at("30.0.0.0/24")
                  .fwd()
                  ->resolvedNextHopSetID()
                  .has_value());
  EXPECT_FALSE(tbl.v4NetworkToRoute()
                   ->at("30.1.0.0/24")
                   .fwd()
                   ->resolvedNextHopSetID()
                   .has_value());
  EXPECT_TRUE(
      tbl.labelToRoute()->at(13000).fwd()->resolvedNextHopSetID().has_value());
  EXPECT_FALSE(
      tbl.labelToRoute()->at(13100).fwd()->resolvedNextHopSetID().has_value());
}

// Toggle a MPLS route between SWAP and POP_AND_LOOKUP. SWAP form must have
// normalizedResolvedNextHopSetID; POP_AND_LOOKUP form must not.
TEST_F(NextHopMapPopulationTest, SwapPopAndLookupRouteMidUpdate) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  addMplsSwapRoute(updater, 31000, 31001, folly::IPAddress("1::10"));
  updater.program();
  {
    auto rib = sw_->getRib()->toThrift();
    const auto& route = rib.begin()->second.labelToRoute()->at(31000);
    EXPECT_TRUE(route.fwd()->resolvedNextHopSetID().has_value());
    EXPECT_TRUE(route.fwd()->normalizedResolvedNextHopSetID().has_value());
  }

  // Switch to POP_AND_LOOKUP.
  updater = sw_->getRouteUpdater();
  addMplsPopAndLookupRoute(updater, 31000);
  updater.program();
  {
    auto rib = sw_->getRib()->toThrift();
    const auto& route = rib.begin()->second.labelToRoute()->at(31000);
    EXPECT_TRUE(route.fwd()->resolvedNextHopSetID().has_value())
        << "POP_AND_LOOKUP must still have resolvedNextHopSetID";
    EXPECT_FALSE(route.fwd()->normalizedResolvedNextHopSetID().has_value())
        << "POP_AND_LOOKUP must NOT have normalizedResolvedNextHopSetID";
  }

  // Back to SWAP.
  updater = sw_->getRouteUpdater();
  addMplsSwapRoute(updater, 31000, 31001, folly::IPAddress("1::10"));
  updater.program();
  {
    auto rib = sw_->getRib()->toThrift();
    const auto& route = rib.begin()->second.labelToRoute()->at(31000);
    EXPECT_TRUE(route.fwd()->resolvedNextHopSetID().has_value());
    EXPECT_TRUE(route.fwd()->normalizedResolvedNextHopSetID().has_value())
        << "Transition back to SWAP must re-allocate normalized ID";
  }

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// UCMP weights affect normalizedResolvedNextHopSetID even when the
// underlying nexthop set is identical. Verifies the manager treats them
// as distinct sets (different setIDs).
TEST_F(
    NextHopMapPopulationTest,
    WeightedNexthopsProduceDifferentNormalizedIds) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  // Two routes share nexthops {1.1.1.10, 2.2.2.10}. One uses equal weights,
  // the other uses 10:20.
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"}, {1, 1});
  addV4Route(updater, "10.0.1.0/24", {"1.1.1.10", "2.2.2.10"}, {10, 20});
  updater.program();

  auto rib = sw_->getRib()->toThrift();
  const auto& tbl = rib.begin()->second;
  auto eq = tbl.v4NetworkToRoute()
                ->at("10.0.0.0/24")
                .fwd()
                ->normalizedResolvedNextHopSetID();
  auto ucmp = tbl.v4NetworkToRoute()
                  ->at("10.0.1.0/24")
                  .fwd()
                  ->normalizedResolvedNextHopSetID();
  ASSERT_TRUE(eq.has_value());
  ASSERT_TRUE(ucmp.has_value());
  EXPECT_NE(*eq, *ucmp)
      << "Different weight ratios must yield different normalized setIDs";

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// Re-programming a route with an identical entry must not churn IDs.
TEST_F(NextHopMapPopulationTest, IdempotentReprogramSameRoutes) {
  FLAGS_mpls_rib = true;
  auto updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV6Route(updater, "2001:db8::/64", {"1::10"});
  addMplsSwapRoute(updater, 33000, 33001, folly::IPAddress("1::10"));
  addMplsPopAndLookupRoute(updater, 33100);
  updater.program();

  auto before = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(before, nullptr);

  // Re-program identically.
  updater = sw_->getRouteUpdater();
  addV4Route(updater, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  addV6Route(updater, "2001:db8::/64", {"1::10"});
  addMplsSwapRoute(updater, 33000, 33001, folly::IPAddress("1::10"));
  addMplsPopAndLookupRoute(updater, 33100);
  updater.program();

  auto after = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(after, nullptr);

  // Set / nh maps must be identical (no churn).
  EXPECT_EQ(before->getIdToNextHop().size(), after->getIdToNextHop().size());
  EXPECT_EQ(
      before->getIdToNextHopIdSet().size(),
      after->getIdToNextHopIdSet().size());
  for (const auto& [id, _] : before->getIdToNextHop()) {
    EXPECT_TRUE(after->getIdToNextHop().contains(id))
        << "nh ID " << id << " churned";
  }
  for (const auto& [setId, _] : before->getIdToNextHopIdSet()) {
    EXPECT_TRUE(after->getIdToNextHopIdSet().contains(setId))
        << "setID " << setId << " churned";
  }

  verifyIDMapsConsistency();
  verifyIdMapsMatchIdManager();
}

// setEcmpWidth/getEcmpWidth on the RIB round-trip through RibRouteTables.
TEST_F(NextHopMapPopulationTest, SetEcmpWidthRoundTrips) {
  // A value distinct from any FLAGS_ecmp_width default so the round-trip is
  // unambiguous.
  sw_->getRib()->setEcmpWidth(123);
  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 123u);
}

// ApplyThriftConfig sources the RIB ecmpWidth from cfg.SwitchSettings.ecmpWidth
// when set; the config value must win over the gflag. Uses a fresh (cold-boot)
// handle so the width is a first-time add, not a change (which
// StateUpdateValidator would reject).
TEST_F(NextHopMapPopulationTest, EcmpWidthSourcedFromConfigOverridesFlag) {
  auto savedWidth = FLAGS_ecmp_width;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
  };
  handle_.reset();
  FLAGS_ecmp_width = 64;
  auto config = initialConfig();
  config.switchSettings()->ecmpWidth() = 200;
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();

  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 200u);
}

// The direct-RIB ApplyThriftConfig path resolves routes during config apply,
// so it must synchronize the processed state's width before reconfigure.
TEST_F(NextHopMapPopulationTest, EcmpWidthSourcedFromConfigOnDirectRibPath) {
  auto savedWidth = FLAGS_ecmp_width;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
  };
  FLAGS_ecmp_width = 64;
  auto config = initialConfig();
  config.switchSettings()->ecmpWidth() = 200;
  auto platform = createMockPlatform();
  RoutingInformationBase rib;

  auto state = publishAndApplyConfig(
      std::make_shared<SwitchState>(), &config, platform.get(), &rib);

  ASSERT_NE(state, nullptr);
  EXPECT_EQ(getEcmpWidth(state), 200u);
  EXPECT_EQ(rib.getEcmpWidth(), 200u);
}

// A no-op config still synchronizes the independently reconstructed RIB from
// the accepted SwitchState.
TEST_F(NextHopMapPopulationTest, NoOpConfigResynchronizesRibEcmpWidth) {
  auto savedWidth = FLAGS_ecmp_width;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
  };
  handle_.reset();
  FLAGS_ecmp_width = 64;
  auto config = initialConfig();
  config.switchSettings()->ecmpWidth() = 200;
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();
  auto stateBefore = sw_->getState();

  sw_->getRib()->setEcmpWidth(64);
  sw_->applyConfig("reapply identical ecmpWidth config", config);

  EXPECT_EQ(sw_->getState(), stateBefore);
  EXPECT_EQ(getEcmpWidth(sw_->getState()), 200u);
  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 200u);
}

// When cfg.SwitchSettings.ecmpWidth is unset, the RIB falls back to
// FLAGS_ecmp_width during the flag->config migration.
TEST_F(NextHopMapPopulationTest, EcmpWidthFallsBackToFlagWhenConfigUnset) {
  auto savedWidth = FLAGS_ecmp_width;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
  };
  handle_.reset();
  FLAGS_ecmp_width = 99;
  auto config = initialConfig(); // leaves ecmpWidth unset
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();

  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 99u);
}

// The RIB ecmpWidth lives on RibRouteTables, not the NextHopIDManager, so the
// config value must reach the RIB even when enable_nexthop_id_manager is off
// (the manager is null).
TEST_F(
    NextHopMapPopulationTest,
    EcmpWidthSourcedFromConfigWithManagerDisabled) {
  auto savedWidth = FLAGS_ecmp_width;
  auto savedEnable = FLAGS_enable_nexthop_id_manager;
  auto savedResolve = FLAGS_resolve_nexthops_from_id;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
    FLAGS_enable_nexthop_id_manager = savedEnable;
    FLAGS_resolve_nexthops_from_id = savedResolve;
  };
  handle_.reset();
  FLAGS_ecmp_width = 64;
  // resolve_nexthops_from_id requires enable_nexthop_id_manager, so the
  // manager-off world must turn both off.
  FLAGS_enable_nexthop_id_manager = false;
  FLAGS_resolve_nexthops_from_id = false;
  auto config = initialConfig();
  config.switchSettings()->ecmpWidth() = 200;
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();

  // No manager exists, but the RIB still carries the config width.
  ASSERT_EQ(sw_->getRib()->getNextHopIDManagerCopy(), nullptr);
  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 200u);
}

// A width change on a running agent is rejected by ValidateStateUpdate. The RIB
// must NOT be left holding the rejected width -- it is sourced from the
// accepted SwitchState after validation, so a rejected config leaves both the
// RIB and SwitchState on the original width (no desync).
TEST_F(NextHopMapPopulationTest, RejectedEcmpWidthChangeDoesNotDesyncRib) {
  auto savedWidth = FLAGS_ecmp_width;
  SCOPE_EXIT {
    FLAGS_ecmp_width = savedWidth;
  };
  handle_.reset();
  FLAGS_ecmp_width = 64;
  auto config = initialConfig();
  config.switchSettings()->ecmpWidth() = 200;
  handle_ = createTestHandle(&config);
  sw_ = handle_->getSw();
  ASSERT_EQ(sw_->getRib()->getEcmpWidth(), 200u);

  // Attempt to change the width on the running agent; ValidateStateUpdate
  // rejects it (a coldboot is required to change ECMP width).
  auto changed = initialConfig();
  changed.switchSettings()->ecmpWidth() = 400;
  EXPECT_THROW(sw_->applyConfig("change ecmpWidth", changed), FbossError);

  // The RIB must still hold the original width, not the rejected 400.
  EXPECT_EQ(getEcmpWidth(sw_->getState()), 200u);
  EXPECT_EQ(sw_->getRib()->getEcmpWidth(), 200u);
}

// drop_inline_nexthops_with_id strips the inline nexthops off both the
// resolved fwd entry and the per-client entries. The set IDs survive, so
// ID-aware readers still see the full sets.
TEST_F(NextHopMapPopulationTest, DropInlineNextHopsOffFwdAndClientEntries) {
  gflags::FlagSaver flagSaver;
  FLAGS_resolve_nexthops_from_id = true;
  FLAGS_drop_inline_nexthops_with_id = true;

  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();

  auto manager = sw_->getRib()->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto route =
      fibContainer->getFibV4()->exactMatch(makePrefixV4("10.0.0.0/24"));
  ASSERT_NE(route, nullptr);
  ASSERT_TRUE(route->isResolved());

  const auto& fwd = route->getForwardInfo();
  EXPECT_EQ(fwd.getAction(), RouteForwardAction::NEXTHOPS);
  EXPECT_TRUE(fwd.getNextHopSet().empty());
  ASSERT_TRUE(fwd.getResolvedNextHopSetID().has_value());
  EXPECT_EQ(
      getResolvedNextHopsFromRib(manager.get(), fwd),
      resolvedNextHopsVia1And2());

  // The per-client entry is dropped too, and recovered via its own ID.
  auto entry = route->getEntryForClient(kClientA);
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->getNextHopSet().empty());
  ASSERT_TRUE(entry->getClientNextHopSetID().has_value());
  EXPECT_EQ(
      getClientNextHopsFromRib(manager.get(), *entry),
      makeNextHops({"1.1.1.10", "2.2.2.10"}));
}

// With the flag off the fwd entry keeps its inline nexthops, so the drop is
// opt-in rather than implied by the ID manager being on.
TEST_F(NextHopMapPopulationTest, InlineNextHopsRetainedWithoutDropFlag) {
  gflags::FlagSaver flagSaver;
  FLAGS_resolve_nexthops_from_id = true;

  auto updater = sw_->getRouteUpdater();
  addV4RouteForClient(
      updater, kClientA, "10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});
  updater.program();

  auto fibContainer = getFibInfo()->getFibContainerIf(kRid0);
  ASSERT_NE(fibContainer, nullptr);
  auto route =
      fibContainer->getFibV4()->exactMatch(makePrefixV4("10.0.0.0/24"));
  ASSERT_NE(route, nullptr);
  EXPECT_EQ(
      route->getForwardInfo().getNextHopSet(), resolvedNextHopsVia1And2());
}

} // namespace facebook::fboss
