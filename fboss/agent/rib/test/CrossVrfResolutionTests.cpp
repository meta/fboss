// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/rib/RoutingInformationBase.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

#include <set>

using namespace facebook::fboss;

namespace {
const RouterID kVrf0(0);
const RouterID kTenantVrf(1001);

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kResolver([] {
    std::map<int64_t, cfg::SwitchInfo> switchInfo;
    cfg::SwitchInfo info;
    info.switchType() = cfg::SwitchType::NPU;
    info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    info.switchIndex() = 0;
    switchInfo.emplace(0, info);
    return switchInfo;
  }());
  return &kResolver;
}

NextHopThrift makeNextHop(const std::string& addr, std::optional<int32_t> vrf) {
  NextHopThrift nhop;
  nhop.address() = facebook::network::toBinaryAddress(folly::IPAddress(addr));
  if (vrf) {
    nhop.vrf() = *vrf;
  }
  return nhop;
}

NextHopThrift makeVxlanNextHop(
    const std::string& vtep,
    const std::string& tunnelId) {
  auto nhop = makeNextHop(vtep, 0);
  nhop.tunnelType() = TunnelType::VXLAN;
  nhop.tunnelId() = tunnelId;
  return nhop;
}

UnicastRoute makeRoute(const std::string& prefix, const NextHopThrift& nhop) {
  auto route = makeUnicastRoute(folly::IPAddress::createNetwork(prefix), {});
  route.nextHops() = {nhop};
  return route;
}

UnicastRoute makeOverlayRoute(
    const std::string& prefix,
    const NextHopThrift& nhop) {
  auto route = makeRoute(prefix, nhop);
  route.level() = 1;
  return route;
}
} // namespace

class CrossVrfResolutionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Interfaces 1 and 2 are in the default VRF, 55 in the tenant VRF
    interfaceRoutes_[kVrf0] = {
        {folly::IPAddress::createNetwork("2001:db8:1::/64"),
         {InterfaceID(1), folly::IPAddress("2001:db8:1::1")}},
        {folly::IPAddress::createNetwork("2001:db8:2::/64"),
         {InterfaceID(2), folly::IPAddress("2001:db8:2::1")}},
    };
    interfaceRoutes_[kTenantVrf] = {
        {folly::IPAddress::createNetwork("2001:db8:55::/64"),
         {InterfaceID(55), folly::IPAddress("2001:db8:55::1")}},
    };
    reconfigure();
  }

  void reconfigure() {
    rib_.reconfigure(
        scopeResolver(),
        interfaceRoutes_,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        fibUpdate(),
        nullptr);
  }

  RibToSwitchStateFunction fibUpdate() {
    return [this](
               const SwitchIdScopeResolver*,
               const std::vector<RibVrfFib>& vrfFibs,
               const NextHopIDManager*,
               const MySidTable&,
               void*) {
      auto& vrfs = fibUpdates_.emplace_back();
      for (const auto& vrfFib : vrfFibs) {
        vrfs.push_back(vrfFib.vrf);
      }
      return StateDelta(nullptr, nullptr);
    };
  }

  void addRoute(RouterID vrf, const UnicastRoute& route) {
    rib_.update(
        scopeResolver(),
        vrf,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {route},
        std::vector<IpPrefix>{},
        false,
        "add",
        fibUpdate(),
        nullptr);
  }

  void delRoute(RouterID vrf, const std::string& prefix) {
    auto network = folly::IPAddress::createNetwork(prefix);
    IpPrefix ipPrefix;
    ipPrefix.ip() = facebook::network::toBinaryAddress(network.first);
    ipPrefix.prefixLength() = network.second;
    rib_.update(
        scopeResolver(),
        vrf,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {},
        {ipPrefix},
        false,
        "delete",
        fibUpdate(),
        nullptr);
  }

  RouteNextHopSet nextHops(RouterID vrf, const std::string& addr) {
    auto routeAndNhops =
        rib_.getRouteAndNextHops(folly::IPAddressV6(addr), vrf, false);
    return routeAndNhops ? routeAndNhops->second : RouteNextHopSet{};
  }

  std::optional<int64_t> resolvedNextHopSetID(
      RouterID vrf,
      const std::string& addr) {
    auto routeAndNhops =
        rib_.getRouteAndNextHops(folly::IPAddressV6(addr), vrf, false);
    CHECK(routeAndNhops);
    return routeAndNhops->first->getForwardInfo().getResolvedNextHopSetID();
  }

  RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes_;
  RoutingInformationBase rib_;
  // VRFs of each FIB update
  std::vector<std::vector<RouterID>> fibUpdates_;
};

TEST_F(CrossVrfResolutionTest, UnknownNextHopVrfUnresolved) {
  addRoute(
      kTenantVrf,
      makeRoute("2001:db8:200::/64", makeNextHop("2001:db8:1::10", 2000)));
  auto routeAndNhops = rib_.getRouteAndNextHops(
      folly::IPAddressV6("2001:db8:200::1"), kTenantVrf, false);
  ASSERT_TRUE(routeAndNhops);
  EXPECT_FALSE(routeAndNhops->first->isResolved());
}

TEST_F(CrossVrfResolutionTest, RemoteInterfaceRouteKeepsCrossVrfNextHop) {
  addRoute(
      kTenantVrf,
      makeRoute("2001:db8:200::/64", makeNextHop("2001:db8:1::10", 0)));
  const RouteNextHopSet expected{ResolvedNextHop(
      folly::IPAddress("2001:db8:1::10"), InterfaceID(1), ECMP_WEIGHT)};
  EXPECT_EQ(nextHops(kTenantVrf, "2001:db8:200::1"), expected);

  // Re-resolves every route in the tenant VRF
  rib_.updateRemoteInterfaceRoutes(
      scopeResolver(),
      {{kTenantVrf,
        {{folly::IPAddress::createNetwork("2001:db8:66::/64"),
          {InterfaceID(66), folly::IPAddress("2001:db8:66::1")}}}}},
      {},
      fibUpdate(),
      nullptr);
  EXPECT_EQ(nextHops(kTenantVrf, "2001:db8:200::1"), expected);
}

TEST_F(CrossVrfResolutionTest, RemoteInterfaceRouteInDefaultVrfReresolves) {
  addRoute(
      kTenantVrf,
      makeRoute("2001:db8:200::/64", makeNextHop("2001:db8:77::10", 0)));
  EXPECT_TRUE(nextHops(kTenantVrf, "2001:db8:200::1").empty());

  rib_.updateRemoteInterfaceRoutes(
      scopeResolver(),
      {{kVrf0,
        {{folly::IPAddress::createNetwork("2001:db8:77::/64"),
          {InterfaceID(77), folly::IPAddress("2001:db8:77::1")}}}}},
      {},
      fibUpdate(),
      nullptr);
  auto nhops = nextHops(kTenantVrf, "2001:db8:200::1");
  ASSERT_EQ(nhops.size(), 1);
  EXPECT_EQ(nhops.begin()->intf(), InterfaceID(77));
}

TEST_F(CrossVrfResolutionTest, DefaultVrfChangeProgramsOtherVrfs) {
  using FibUpdates = std::vector<std::vector<RouterID>>;
  fibUpdates_.clear();
  addRoute(
      kVrf0,
      makeRoute(
          "2001:db8:300::/64", makeNextHop("2001:db8:1::10", std::nullopt)));
  EXPECT_EQ(fibUpdates_, (FibUpdates{{kVrf0, kTenantVrf}}));

  fibUpdates_.clear();
  addRoute(
      kTenantVrf,
      makeRoute("2001:db8:200::/64", makeNextHop("2001:db8:1::10", 0)));
  EXPECT_EQ(fibUpdates_, (FibUpdates{{kTenantVrf}}));
}

TEST_F(CrossVrfResolutionTest, RouteLevelFromClient) {
  auto level = [this]() {
    auto routeAndNhops = rib_.getRouteAndNextHops(
        folly::IPAddressV6("2001:db8:200::1"), kTenantVrf, false);
    CHECK(routeAndNhops);
    return routeAndNhops->first->getBestEntry().second->getLevel();
  };
  auto route = makeRoute(
      "2001:db8:200::/64", makeNextHop("2001:db8:55::10", std::nullopt));
  route.level() = 1;
  addRoute(kTenantVrf, route);
  EXPECT_EQ(level(), 1);

  // A level change alone updates the route
  route.level().reset();
  addRoute(kTenantVrf, route);
  EXPECT_EQ(level(), 0);
}

TEST_F(CrossVrfResolutionTest, VxlanNextHopOverUnderlayEcmp) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64"),
          {folly::IPAddress("2001:db8:1::10"),
           folly::IPAddress("2001:db8:2::10")}));
  auto vxlanNhop = makeVxlanNextHop("2001:db8:500::1", "vx0");
  vxlanNhop.overlayDstMac() = "02:00:00:00:00:01";
  addRoute(kTenantVrf, makeOverlayRoute("2001:db8:200::/64", vxlanNhop));

  auto nhops = nextHops(kTenantVrf, "2001:db8:200::1");
  ASSERT_EQ(nhops.size(), 1);
  const auto& nhop = *nhops.begin();
  EXPECT_EQ(nhop.addr(), folly::IPAddress("2001:db8:500::1"));
  EXPECT_EQ(nhop.intfID(), std::nullopt);
  EXPECT_TRUE(nhop.resolvedInNextLevel());
  EXPECT_EQ(nhop.tunnelType(), TunnelType::VXLAN);
  EXPECT_EQ(nhop.tunnelId(), "vx0");
  EXPECT_EQ(nhop.overlayDstMac(), folly::MacAddress("02:00:00:00:00:01"));
  auto setID = resolvedNextHopSetID(kTenantVrf, "2001:db8:200::1");

  // Losing an underlay path does not change the VXLAN next hop
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  EXPECT_EQ(nextHops(kTenantVrf, "2001:db8:200::1"), nhops);
  EXPECT_EQ(resolvedNextHopSetID(kTenantVrf, "2001:db8:200::1"), setID);
}

TEST_F(CrossVrfResolutionTest, VxlanNextHopUnresolvedViaDropRoute) {
  addRoute(
      kVrf0,
      makeDropUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64")));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeVxlanNextHop("2001:db8:500::1", "vx0")));
  EXPECT_TRUE(nextHops(kTenantVrf, "2001:db8:200::1").empty());
}

TEST_F(CrossVrfResolutionTest, NextHopResolvedInNextLevelOverIpv4) {
  interfaceRoutes_[kVrf0][folly::IPAddress::createNetwork("10.0.1.0/24")] = {
      InterfaceID(1), folly::IPAddress("10.0.1.1")};
  reconfigure();
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("10.50.0.0/16"),
          {folly::IPAddress("10.0.1.10")}));
  addRoute(
      kTenantVrf,
      makeOverlayRoute("10.200.0.0/16", makeNextHop("10.50.0.1", 0)));

  auto routeAndNhops = rib_.getRouteAndNextHops(
      folly::IPAddressV4("10.200.0.1"), kTenantVrf, false);
  ASSERT_TRUE(routeAndNhops);
  const auto& nhops = routeAndNhops->second;
  ASSERT_EQ(nhops.size(), 1);
  const auto& nhop = *nhops.begin();
  EXPECT_EQ(nhop.addr(), folly::IPAddress("10.50.0.1"));
  EXPECT_EQ(nhop.intfID(), std::nullopt);
  EXPECT_TRUE(nhop.resolvedInNextLevel());
  EXPECT_EQ(nhop.vrf(), kVrf0);
}

namespace {
void expectResolvedInNextLevel(
    const RouteNextHopSet& nhops,
    const std::string& addr) {
  ASSERT_EQ(nhops.size(), 1);
  EXPECT_EQ(nhops.begin()->addr(), folly::IPAddress(addr));
  EXPECT_EQ(nhops.begin()->intfID(), std::nullopt);
  EXPECT_TRUE(nhops.begin()->resolvedInNextLevel());
}
} // namespace

TEST_F(CrossVrfResolutionTest, SameLevelResolvesRecursively) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeVxlanNextHop("2001:db8:500::1", "vx0")));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:201::/64", makeNextHop("2001:db8:200::1", std::nullopt)));

  expectResolvedInNextLevel(
      nextHops(kTenantVrf, "2001:db8:201::1"), "2001:db8:500::1");
}

TEST_F(CrossVrfResolutionTest, LevelChangeInSameVrf) {
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeNextHop("2001:db8:55::10", std::nullopt)));

  expectResolvedInNextLevel(
      nextHops(kTenantVrf, "2001:db8:200::1"), "2001:db8:55::10");
}

TEST_F(CrossVrfResolutionTest, LowerLevelRouteOverHigherLevelRoute) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeVxlanNextHop("2001:db8:500::1", "vx0")));
  addRoute(
      kTenantVrf,
      makeRoute(
          "2001:db8:202::/64", makeNextHop("2001:db8:200::1", std::nullopt)));

  expectResolvedInNextLevel(
      nextHops(kTenantVrf, "2001:db8:202::1"), "2001:db8:200::1");
}

TEST_F(CrossVrfResolutionTest, NextLevelToCpuUnresolved) {
  auto toCpu = makeUnicastRoute(
      folly::IPAddress::createNetwork("2001:db8:500::/64"), {});
  toCpu.action() = RouteForwardAction::TO_CPU;
  addRoute(kVrf0, toCpu);
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeVxlanNextHop("2001:db8:500::1", "vx0")));

  EXPECT_TRUE(nextHops(kTenantVrf, "2001:db8:200::1").empty());
}

TEST_F(CrossVrfResolutionTest, NextLevelUnresolvedRouteUnresolved) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:500::/64"),
          {folly::IPAddress("2001:db8:99::10")}));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:200::/64", makeVxlanNextHop("2001:db8:500::1", "vx0")));

  EXPECT_TRUE(nextHops(kTenantVrf, "2001:db8:200::1").empty());
}

TEST_F(CrossVrfResolutionTest, LevelChangeReresolves) {
  auto route = makeRoute(
      "2001:db8:200::/64", makeNextHop("2001:db8:55::10", std::nullopt));
  addRoute(kTenantVrf, route);
  auto nhops = nextHops(kTenantVrf, "2001:db8:200::1");
  ASSERT_EQ(nhops.size(), 1);
  EXPECT_EQ(nhops.begin()->intfID(), InterfaceID(55));

  route.level() = 1;
  addRoute(kTenantVrf, route);
  expectResolvedInNextLevel(
      nextHops(kTenantVrf, "2001:db8:200::1"), "2001:db8:55::10");
}

TEST_F(CrossVrfResolutionTest, OverlayChainOverRecursiveUnderlay) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:30::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:20::/64"),
          {folly::IPAddress("2001:db8:30::1")}));
  addRoute(
      kTenantVrf,
      makeOverlayRoute("2001:db8:101::/64", makeNextHop("2001:db8:20::1", 0)));
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:100::/64", makeNextHop("2001:db8:101::1", std::nullopt)));

  const auto underlay = nextHops(kVrf0, "2001:db8:20::1");
  ASSERT_EQ(underlay.size(), 1);
  EXPECT_EQ(underlay.begin()->addr(), folly::IPAddress("2001:db8:1::10"));
  EXPECT_EQ(underlay.begin()->intfID(), InterfaceID(1));
  for (const auto& addr : {"2001:db8:101::1", "2001:db8:100::1"}) {
    const auto nhops = nextHops(kTenantVrf, addr);
    expectResolvedInNextLevel(nhops, "2001:db8:20::1");
    EXPECT_EQ(nhops.begin()->vrf(), kVrf0);
  }
}

TEST_F(CrossVrfResolutionTest, DefaultVrfOverlayChainOverRecursiveUnderlay) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:30::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:20::/64"),
          {folly::IPAddress("2001:db8:30::1")}));
  addRoute(
      kVrf0,
      makeOverlayRoute(
          "2001:db8:101::/64", makeNextHop("2001:db8:20::1", std::nullopt)));
  addRoute(
      kVrf0,
      makeOverlayRoute(
          "2001:db8:100::/64", makeNextHop("2001:db8:101::1", std::nullopt)));

  const auto underlay = nextHops(kVrf0, "2001:db8:20::1");
  ASSERT_EQ(underlay.size(), 1);
  EXPECT_EQ(underlay.begin()->addr(), folly::IPAddress("2001:db8:1::10"));
  EXPECT_EQ(underlay.begin()->intfID(), InterfaceID(1));
  for (const auto& addr : {"2001:db8:101::1", "2001:db8:100::1"}) {
    const auto nhops = nextHops(kVrf0, addr);
    expectResolvedInNextLevel(nhops, "2001:db8:20::1");
    EXPECT_EQ(nhops.begin()->vrf(), std::nullopt);
  }
}

TEST_F(CrossVrfResolutionTest, SameNextHopsAtDifferentLevels) {
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:20::/64"),
          {folly::IPAddress("2001:db8:1::10")}));
  const auto nhop = makeNextHop("2001:db8:20::1", std::nullopt);
  addRoute(kVrf0, makeRoute("2001:db8:50::/64", nhop));
  addRoute(kVrf0, makeOverlayRoute("2001:db8:101::/64", nhop));

  const auto underlay = nextHops(kVrf0, "2001:db8:50::1");
  ASSERT_EQ(underlay.size(), 1);
  EXPECT_EQ(underlay.begin()->addr(), folly::IPAddress("2001:db8:1::10"));
  EXPECT_EQ(underlay.begin()->intfID(), InterfaceID(1));
  EXPECT_FALSE(underlay.begin()->resolvedInNextLevel());
  expectResolvedInNextLevel(
      nextHops(kVrf0, "2001:db8:101::1"), "2001:db8:20::1");
}

namespace {
void expectVteps(
    const RouteNextHopSet& nhops,
    const std::set<folly::IPAddress>& vteps) {
  std::set<folly::IPAddress> addrs;
  for (const auto& nhop : nhops) {
    addrs.insert(nhop.addr());
    EXPECT_EQ(nhop.intfID(), std::nullopt);
    EXPECT_TRUE(nhop.resolvedInNextLevel());
    EXPECT_EQ(nhop.tunnelType(), TunnelType::VXLAN);
  }
  EXPECT_EQ(addrs, vteps);
}
} // namespace

TEST_F(CrossVrfResolutionTest, RecursiveOverlayOverTwoVteps) {
  interfaceRoutes_[kVrf0][folly::IPAddress::createNetwork("2001:db8:3::/64")] =
      {InterfaceID(3), folly::IPAddress("2001:db8:3::1")};
  interfaceRoutes_[kVrf0][folly::IPAddress::createNetwork("2001:db8:4::/64")] =
      {InterfaceID(4), folly::IPAddress("2001:db8:4::1")};
  reconfigure();
  const auto vtep0Underlay = makeUnicastRoute(
      folly::IPAddress::createNetwork("2001:db8:100::/64"),
      {folly::IPAddress("2001:db8:1::10"), folly::IPAddress("2001:db8:2::10")});
  addRoute(kVrf0, vtep0Underlay);
  addRoute(
      kVrf0,
      makeUnicastRoute(
          folly::IPAddress::createNetwork("2001:db8:101::/64"),
          {folly::IPAddress("2001:db8:3::10"),
           folly::IPAddress("2001:db8:4::10")}));
  auto overlay = makeOverlayRoute(
      "2001:db8:200::/64", makeVxlanNextHop("2001:db8:100::1", "vx0"));
  overlay.nextHops()->push_back(makeVxlanNextHop("2001:db8:101::1", "vx0"));
  addRoute(kTenantVrf, overlay);
  addRoute(
      kTenantVrf,
      makeOverlayRoute(
          "2001:db8:300::/64", makeNextHop("2001:db8:200::1", std::nullopt)));

  const std::set<folly::IPAddress> bothVteps{
      folly::IPAddress("2001:db8:100::1"), folly::IPAddress("2001:db8:101::1")};
  expectVteps(nextHops(kTenantVrf, "2001:db8:200::1"), bothVteps);
  expectVteps(nextHops(kTenantVrf, "2001:db8:300::1"), bothVteps);
  EXPECT_EQ(
      resolvedNextHopSetID(kTenantVrf, "2001:db8:200::1"),
      resolvedNextHopSetID(kTenantVrf, "2001:db8:300::1"));

  // Losing one VTEP re-resolves both overlay routes in the same FIB update
  delRoute(kVrf0, "2001:db8:100::/64");
  const std::set<folly::IPAddress> secondVtep{
      folly::IPAddress("2001:db8:101::1")};
  expectVteps(nextHops(kTenantVrf, "2001:db8:200::1"), secondVtep);
  expectVteps(nextHops(kTenantVrf, "2001:db8:300::1"), secondVtep);
  const std::vector<RouterID> bothVrfs{kVrf0, kTenantVrf};
  EXPECT_EQ(fibUpdates_.back(), bothVrfs);

  addRoute(kVrf0, vtep0Underlay);
  expectVteps(nextHops(kTenantVrf, "2001:db8:200::1"), bothVteps);
  expectVteps(nextHops(kTenantVrf, "2001:db8:300::1"), bothVteps);
}

TEST_F(CrossVrfResolutionTest, ReconfigureProgramsVrfsInOneUpdate) {
  fibUpdates_.clear();
  interfaceRoutes_[kTenantVrf]
                  [folly::IPAddress::createNetwork("2001:db8:56::/64")] = {
                      InterfaceID(56), folly::IPAddress("2001:db8:56::1")};
  reconfigure();

  ASSERT_EQ(fibUpdates_.size(), 1);
  EXPECT_EQ(fibUpdates_[0], (std::vector<RouterID>{kVrf0, kTenantVrf}));
}

TEST_F(CrossVrfResolutionTest, ReconfigureRemovesVrfInSameUpdate) {
  fibUpdates_.clear();
  interfaceRoutes_.erase(kTenantVrf);
  reconfigure();

  // The removed VRF's FIB is removed in the same update
  ASSERT_EQ(fibUpdates_.size(), 1);
  EXPECT_EQ(fibUpdates_[0], (std::vector<RouterID>{kTenantVrf, kVrf0}));
  EXPECT_EQ(rib_.getVrfList(), std::vector<RouterID>{kVrf0});
}
