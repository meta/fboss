// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/rib/RoutingInformationBase.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>

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

UnicastRoute makeRoute(const std::string& prefix, const NextHopThrift& nhop) {
  auto route = makeUnicastRoute(folly::IPAddress::createNetwork(prefix), {});
  route.nextHops() = {nhop};
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
