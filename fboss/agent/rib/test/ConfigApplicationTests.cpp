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
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"

#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <memory>
#include <utility>

using namespace facebook::fboss;

namespace {
cfg::SwitchConfig interfaceRoutesConfig() {
  cfg::SwitchConfig config;
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1;

  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  *config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  config.interfaces()[0].ipAddresses()->resize(2);
  config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[0].ipAddresses()[1] = "1::1/48";

  return config;
}

cfg::SwitchConfig interfaceAndStaticRoutesWithNextHopsConfig() {
  cfg::SwitchConfig config;
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 1;
  *config.vlans()[1].id() = 2;

  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  *config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  config.interfaces()[0].ipAddresses()->resize(2);
  config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[0].ipAddresses()[1] = "1::1/48";
  *config.interfaces()[1].intfID() = 2;
  *config.interfaces()[1].vlanID() = 2;
  *config.interfaces()[1].routerID() = 0;
  config.interfaces()[1].mac() = "00:00:00:00:00:22";
  config.interfaces()[1].ipAddresses()->resize(2);
  config.interfaces()[1].ipAddresses()[0] = "2.2.2.2/24";
  config.interfaces()[1].ipAddresses()[1] = "2::1/48";

  config.staticRoutesWithNhops()->resize(2);
  config.staticRoutesWithNhops()[0].nexthops()->resize(1);
  *config.staticRoutesWithNhops()[0].prefix() = "2001::/64";
  config.staticRoutesWithNhops()[0].nexthops()[0] = "2::2";
  config.staticRoutesWithNhops()[1].nexthops()->resize(1);
  *config.staticRoutesWithNhops()[1].prefix() = "20.20.20.0/24";
  config.staticRoutesWithNhops()[1].nexthops()[0] = "2.2.2.3";

  return config;
}

cfg::SwitchConfig interfaceAndStaticRoutesWithoutNextHopsConfig() {
  cfg::SwitchConfig config;
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1;

  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  *config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  config.interfaces()[0].ipAddresses()->resize(2);
  config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[0].ipAddresses()[1] = "1::1/48";

  config.staticRoutesToCPU()->resize(2);
  *config.staticRoutesToCPU()[0].routerID() = 0;
  *config.staticRoutesToCPU()[0].prefix() = "2001::/64";
  *config.staticRoutesToCPU()[1].routerID() = 0;
  *config.staticRoutesToCPU()[1].prefix() = "2.1.2.2/16";

  config.staticRoutesToNull()->resize(2);
  *config.staticRoutesToNull()[0].routerID() = 0;
  *config.staticRoutesToNull()[0].prefix() = "2002::/64";
  *config.staticRoutesToNull()[1].routerID() = 0;
  *config.staticRoutesToNull()[1].prefix() = "2.2.2.2/16";

  return config;
}

cfg::SwitchConfig dualVrfConfig() {
  cfg::SwitchConfig config;
  config.vlans()->resize(2);
  *config.vlans()[0].id() = 1;
  *config.vlans()[1].id() = 2;

  config.interfaces()->resize(2);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  *config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  config.interfaces()[0].ipAddresses()->resize(2);
  config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[0].ipAddresses()[1] = "1::1/48";
  *config.interfaces()[1].intfID() = 2;
  *config.interfaces()[1].vlanID() = 2;
  *config.interfaces()[1].routerID() = 1;
  config.vrfs().ensure().emplace_back().vrfId() = 1;
  config.interfaces()[1].mac() = "00:00:00:00:00:22";
  config.interfaces()[1].ipAddresses()->resize(2);
  config.interfaces()[1].ipAddresses()[0] = "2.2.2.2/24";
  config.interfaces()[1].ipAddresses()[1] = "2::1/48";

  return config;
}

template <typename AddressT>
void checkFibRoute(
    const std::shared_ptr<SwitchState>& state,
    const std::shared_ptr<facebook::fboss::Route<AddressT>>& route,
    AddressT maskedNetworkAddress,
    uint8_t networkMaskLength,
    AddressT nextHopAddress,
    InterfaceID nextHopInterfaceID) {
  EXPECT_EQ(route->prefix().network(), maskedNetworkAddress);
  EXPECT_EQ(route->prefix().mask(), networkMaskLength);
  EXPECT_TRUE(route->isResolved());

  const RouteNextHopEntry& forwardInfo = route->getForwardInfo();
  EXPECT_EQ(
      forwardInfo.getAction(),
      facebook::fboss::RouteNextHopEntry::Action::NEXTHOPS);
  auto nhops = getNextHops(state, forwardInfo);
  EXPECT_EQ(nhops.size(), 1);
  EXPECT_EQ(nhops.begin()->addr(), nextHopAddress);
  EXPECT_EQ(nhops.begin()->intfID(), nextHopInterfaceID);
}

template <typename AddressT>
void checkFibRoute(
    const std::shared_ptr<facebook::fboss::Route<AddressT>>& route,
    AddressT maskedNetworkAddress,
    uint8_t networkMaskLength,
    facebook::fboss::RouteNextHopEntry::Action action) {
  ASSERT_NE(action, facebook::fboss::RouteNextHopEntry::Action::NEXTHOPS);

  EXPECT_EQ(route->prefix().network(), maskedNetworkAddress);
  EXPECT_EQ(route->prefix().mask(), networkMaskLength);
  EXPECT_TRUE(route->isResolved());

  const RouteNextHopEntry& forwardInfo = route->getForwardInfo();
  EXPECT_EQ(forwardInfo.getAction(), action);
}

} // namespace

TEST(ConfigApplication, InterfaceRoutes) {
  RoutingInformationBase rib;

  auto emptyState = std::make_shared<SwitchState>();
  auto platform = createMockPlatform();
  auto config = interfaceRoutesConfig();

  auto state = publishAndApplyConfig(emptyState, &config, platform.get(), &rib);
  ASSERT_NE(nullptr, state);

  auto fibsInfoMap = state->getFibsInfoMap();
  EXPECT_EQ(fibsInfoMap->getVrfCount(), 1);

  auto fibContainer = fibsInfoMap->getFibContainer(RouterID(0));
  EXPECT_NE(nullptr, fibContainer);

  auto v4Fib = fibContainer->getFibV4();
  EXPECT_EQ(v4Fib->size(), 1);
  RoutePrefixV4 v4DirectlyConnectedPrefix{folly::IPAddressV4("1.1.1.0"), 24};
  auto v4DirectlyConnectedRoute = v4Fib->exactMatch(v4DirectlyConnectedPrefix);
  ASSERT_NE(nullptr, v4DirectlyConnectedRoute);
  checkFibRoute(
      state,
      v4DirectlyConnectedRoute,
      folly::IPAddressV4("1.1.1.0"),
      24,
      folly::IPAddressV4("1.1.1.1"),
      InterfaceID(1));

  auto v6Fib = fibContainer->getFibV6();
  EXPECT_EQ(v6Fib->size(), 2);
  RoutePrefixV6 v6DirectlyConnectedPrefix{folly::IPAddressV6("1::"), 48};
  auto v6DirectlyConnectedRoute = v6Fib->exactMatch(v6DirectlyConnectedPrefix);
  ASSERT_NE(nullptr, v6DirectlyConnectedRoute);
  checkFibRoute(
      state,
      v6DirectlyConnectedRoute,
      folly::IPAddressV6("1::"),
      48,
      folly::IPAddressV6("1::1"),
      InterfaceID(1));

  uint64_t v4RouteCount = 0;
  uint64_t v6RouteCount = 0;
  std::tie(v4RouteCount, v6RouteCount) = fibsInfoMap->getRouteCount();
  EXPECT_EQ(v4RouteCount, 1);
  EXPECT_EQ(v6RouteCount, 2);
}

TEST(ConfigApplication, StaticRoutesWithNextHops) {
  RoutingInformationBase rib;

  auto emptyState = std::make_shared<SwitchState>();
  auto platform = createMockPlatform();
  auto config = interfaceAndStaticRoutesWithNextHopsConfig();

  auto state = publishAndApplyConfig(emptyState, &config, platform.get(), &rib);
  ASSERT_NE(nullptr, state);

  auto fibsInfoMap = state->getFibsInfoMap();
  EXPECT_EQ(fibsInfoMap->getVrfCount(), 1);

  auto fibContainer = fibsInfoMap->getFibContainer(RouterID(0));
  EXPECT_NE(nullptr, fibContainer);

  auto v4Fib = fibContainer->getFibV4();
  EXPECT_EQ(v4Fib->size(), 3);
  RoutePrefixV4 v4Prefix{folly::IPAddressV4("20.20.20.0"), 24};
  auto v4StaticRoute = v4Fib->exactMatch(v4Prefix);
  ASSERT_NE(nullptr, v4StaticRoute);
  checkFibRoute(
      state,
      v4StaticRoute,
      folly::IPAddressV4("20.20.20.0"),
      24,
      folly::IPAddressV4("2.2.2.3"),
      InterfaceID(2));

  auto v6Fib = fibContainer->getFibV6();
  EXPECT_EQ(v6Fib->size(), 4);
  RoutePrefixV6 v6Prefix{folly::IPAddressV6("2001::"), 64};
  auto v6StaticRoute = v6Fib->exactMatch(v6Prefix);
  ASSERT_NE(nullptr, v6StaticRoute);
  checkFibRoute(
      state,
      v6StaticRoute,
      folly::IPAddressV6("2001::"),
      64,
      folly::IPAddressV6("2::2"),
      InterfaceID(2));

  uint64_t v4RouteCount = 0;
  uint64_t v6RouteCount = 0;
  std::tie(v4RouteCount, v6RouteCount) = fibsInfoMap->getRouteCount();
  EXPECT_EQ(v4RouteCount, 3);
  EXPECT_EQ(v6RouteCount, 4);
}

TEST(ConfigApplication, StaticRoutesWithoutNextHops) {
  RoutingInformationBase rib;

  auto emptyState = std::make_shared<SwitchState>();
  auto platform = createMockPlatform();
  auto config = interfaceAndStaticRoutesWithoutNextHopsConfig();

  auto state = publishAndApplyConfig(emptyState, &config, platform.get(), &rib);
  ASSERT_NE(nullptr, state);

  auto fibsInfoMap = state->getFibsInfoMap();
  EXPECT_EQ(fibsInfoMap->getVrfCount(), 1);

  auto fibContainer = fibsInfoMap->getFibContainer(RouterID(0));
  EXPECT_NE(nullptr, fibContainer);

  auto v4Fib = fibContainer->getFibV4();
  EXPECT_EQ(v4Fib->size(), 3);

  RoutePrefixV4 v4PrefixToCpu{folly::IPAddressV4("2.1.0.0"), 16};
  auto v4RouteToCpu = v4Fib->exactMatch(v4PrefixToCpu);
  ASSERT_NE(nullptr, v4RouteToCpu);
  checkFibRoute(
      v4RouteToCpu,
      folly::IPAddressV4("2.1.0.0"),
      16,
      facebook::fboss::RouteForwardAction::TO_CPU);

  RoutePrefixV4 v4PrefixToDrop{folly::IPAddressV4("2.2.0.0"), 16};
  auto v4RouteToDrop = v4Fib->exactMatch(v4PrefixToDrop);
  ASSERT_NE(nullptr, v4RouteToDrop);
  checkFibRoute(
      v4RouteToDrop,
      folly::IPAddressV4("2.2.0.0"),
      16,
      facebook::fboss::RouteForwardAction::DROP);

  auto v6Fib = fibContainer->getFibV6();
  EXPECT_EQ(v6Fib->size(), 4);

  RoutePrefixV6 v6PrefixToCpu{folly::IPAddressV6("2001::"), 64};
  auto v6RouteToCpu = v6Fib->exactMatch(v6PrefixToCpu);
  ASSERT_NE(nullptr, v6RouteToCpu);
  checkFibRoute(
      v6RouteToCpu,
      folly::IPAddressV6("2001::"),
      64,
      facebook::fboss::RouteForwardAction::TO_CPU);

  RoutePrefixV6 v6PrefixToDrop{folly::IPAddressV6("2002::"), 64};
  auto v6RouteToDrop = v6Fib->exactMatch(v6PrefixToDrop);
  ASSERT_NE(nullptr, v6RouteToDrop);
  checkFibRoute(
      v6RouteToDrop,
      folly::IPAddressV6("2002::"),
      64,
      facebook::fboss::RouteForwardAction::DROP);
}

TEST(ConfigApplication, MultiVrfColdBoot) {
  RoutingInformationBase rib;

  auto emptyState = std::make_shared<SwitchState>();
  auto platform = createMockPlatform();
  auto config = dualVrfConfig();

  auto state = publishAndApplyConfig(emptyState, &config, platform.get(), &rib);
  ASSERT_NE(nullptr, state);

  auto fibsInfoMap = state->getFibsInfoMap();
  EXPECT_EQ(fibsInfoMap->getVrfCount(), 2);

  std::shared_ptr<ForwardingInformationBaseContainer> fibContainer{nullptr};

  fibContainer = fibsInfoMap->getFibContainer(RouterID(0));
  EXPECT_NE(nullptr, fibContainer);

  fibContainer = fibsInfoMap->getFibContainer(RouterID(1));
  EXPECT_NE(nullptr, fibContainer);
}
