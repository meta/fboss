/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "common/network/if/gen-cpp2/Address_types.h"
#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"

#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <optional>

using namespace facebook::fboss;

namespace {

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(10)}};
}

} // namespace

TEST(RibToSwitchStateUpdater, ModifyUnpublishedSwitchState) {
  auto vrfOne = RouterID(1);

  // First, we put together a Forwarding Information Base tree containing
  // no routes and hang it off an unpublished SwitchState.
  auto v4Fib = std::make_shared<facebook::fboss::ForwardingInformationBaseV4>();
  auto v6Fib = std::make_shared<facebook::fboss::ForwardingInformationBaseV6>();

  auto fibContainer =
      std::make_shared<facebook::fboss::ForwardingInformationBaseContainer>(
          vrfOne);
  fibContainer->ref<switch_state_tags::fibV4>() = v4Fib;
  fibContainer->ref<switch_state_tags::fibV6>() = v6Fib;

  // Create ForwardingInformationBaseMap and add the container
  auto fibsMap =
      std::make_shared<facebook::fboss::ForwardingInformationBaseMap>();
  fibsMap->updateForwardingInformationBaseContainer(fibContainer);

  // Create FibInfo and set the fibsMap
  auto fibInfo = std::make_shared<facebook::fboss::FibInfo>();
  fibInfo->ref<switch_state_tags::fibsMap>() = fibsMap;

  // Create MultiSwitchFibInfoMap and add the FibInfo
  auto fibInfoMap = std::make_shared<facebook::fboss::MultiSwitchFibInfoMap>();
  fibInfoMap->updateFibInfo(fibInfo, scope());

  auto initialState = std::make_shared<facebook::fboss::SwitchState>();
  initialState->resetFibsInfoMap(fibInfoMap);

  // Second, we pass the unpublished SwitchState through an update, which
  // transitively invokes  modify() on the nodes in the Forwarding Information
  // Base subtree
  facebook::fboss::IPv4NetworkToRouteMap v4NetworkToRouteMap;
  facebook::fboss::IPv6NetworkToRouteMap v6NetworkToRouteMap;
  facebook::fboss::LabelToRouteMap labelToRouteMap;
  facebook::fboss::NextHopIDManager nextHopIDManager;
  facebook::fboss::MySidTable mySidTable;
  facebook::fboss::RibToSwitchStateUpdater updater(
      nullptr,
      vrfOne,
      v4NetworkToRouteMap,
      v6NetworkToRouteMap,
      labelToRouteMap,
      &nextHopIDManager,
      mySidTable);
  auto updatedState = updater(initialState);

  // Lastly, we check that the invocations of modify() operated on the
  // unpublished SwitchState in-place
  ASSERT_EQ(updatedState, initialState);
  ASSERT_FALSE(updatedState->isPublished());

  ASSERT_EQ(updatedState->getFibsInfoMap(), initialState->getFibsInfoMap());
  ASSERT_FALSE(updatedState->getFibsInfoMap()->isPublished());

  ASSERT_EQ(
      updatedState->getFibsInfoMap()->getFibContainerIf(vrfOne),
      initialState->getFibsInfoMap()->getFibContainerIf(vrfOne));
  ASSERT_FALSE(
      updatedState->getFibsInfoMap()->getFibContainerIf(vrfOne)->isPublished());

  ASSERT_EQ(
      updatedState->getFibsInfoMap()->getFibContainerIf(vrfOne)->getFibV4(),
      initialState->getFibsInfoMap()->getFibContainerIf(vrfOne)->getFibV4());
  ASSERT_FALSE(updatedState->getFibsInfoMap()
                   ->getFibContainerIf(vrfOne)
                   ->getFibV4()
                   ->isPublished());

  ASSERT_EQ(
      updatedState->getFibsInfoMap()->getFibContainerIf(vrfOne)->getFibV6(),
      initialState->getFibsInfoMap()->getFibContainerIf(vrfOne)->getFibV6());
  ASSERT_FALSE(updatedState->getFibsInfoMap()
                   ->getFibContainerIf(vrfOne)
                   ->getFibV6()
                   ->isPublished());
}

namespace {
template <typename AddressT>
std::shared_ptr<facebook::fboss::Route<AddressT>> getRoute(
    const std::shared_ptr<facebook::fboss::SwitchState>& state,
    facebook::fboss::RouterID vrf,
    AddressT address,
    uint8_t mask) {
  const auto& fibsInfoMap = state->getFibsInfoMap();
  const auto& fibContainer = fibsInfoMap->getFibContainer(vrf);

  const std::shared_ptr<facebook::fboss::ForwardingInformationBase<AddressT>>&
      fib = fibContainer->template getFib<AddressT>();

  facebook::fboss::RoutePrefix<AddressT> prefix{address, mask};
  return fib->exactMatch(prefix);
}

template <typename AddressT>
void EXPECT_ROUTE(
    const std::shared_ptr<facebook::fboss::SwitchState>& state,
    facebook::fboss::RouterID vrf,
    AddressT address,
    uint8_t mask) {
  auto route = getRoute(state, vrf, address, mask);
  EXPECT_NE(nullptr, route);
}

template <typename AddressT>
void EXPECT_NO_ROUTE(
    const std::shared_ptr<facebook::fboss::SwitchState>& state,
    facebook::fboss::RouterID vrf,
    AddressT address,
    uint8_t mask) {
  auto route = getRoute(state, vrf, address, mask);
  EXPECT_EQ(nullptr, route);
}

facebook::fboss::UnicastRoute createUnicastRoute(
    folly::IPAddress address,
    uint8_t mask,
    folly::IPAddress nexthop) {
  facebook::fboss::UnicastRoute unicastRoute;

  facebook::fboss::IpPrefix network;
  network.ip() = facebook::network::toBinaryAddress(address);
  network.prefixLength() = mask;
  unicastRoute.dest() = network;

  std::vector<facebook::fboss::NextHopThrift> nexthops(1);
  nexthops.back().address() = facebook::network::toBinaryAddress(nexthop);
  nexthops.back().weight() = facebook::fboss::ECMP_WEIGHT;
  unicastRoute.nextHops() = std::move(nexthops);

  return unicastRoute;
}

void EXPECT_FIB_SIZE(
    const std::shared_ptr<facebook::fboss::SwitchState>& state,
    facebook::fboss::RouterID vrf,
    std::size_t v4FibSize,
    std::size_t v6FibSize) {
  const auto& fibsInfoMap = state->getFibsInfoMap();
  const auto& fibContainer = fibsInfoMap->getFibContainer(vrf);

  EXPECT_EQ(fibContainer->getFibV4()->size(), v4FibSize);
  EXPECT_EQ(fibContainer->getFibV6()->size(), v6FibSize);
}

const RouterID vrfZero{0};
} // namespace

void programRoutes(
    SwSwitch* sw,
    ClientID client,
    const std::vector<UnicastRoute>& routes,
    const std::vector<IpPrefix>& toDel,
    bool syncFib = false) {
  auto routeUpdater = sw->getRouteUpdater();
  std::for_each(
      routes.begin(), routes.end(), [&routeUpdater, client](const auto& route) {
        routeUpdater.addRoute(vrfZero, client, route);
      });
  std::for_each(
      toDel.begin(), toDel.end(), [&routeUpdater, client](const auto& pfx) {
        routeUpdater.delRoute(vrfZero, pfx, client);
      });
  SwSwitchRouteUpdateWrapper::SyncFibFor syncFibFor;
  if (syncFib) {
    syncFibFor.insert({vrfZero, client});
  }
  routeUpdater.program(
      {syncFibFor, RouteUpdateWrapper::SyncFibInfo::SyncFibType::IP_ONLY});
}
void programRoutes(
    SwSwitch* sw,
    ClientID client,
    const std::vector<UnicastRoute>& routes,
    bool syncFib = false) {
  programRoutes(sw, client, routes, {}, syncFib);
}

TEST(Rib, Update) {
  using namespace facebook::fboss;

  auto v4Default = folly::CIDRNetworkV4(folly::IPAddressV4("0.0.0.0"), 0);
  auto v6Default = folly::CIDRNetworkV6(folly::IPAddressV6("::"), 0);

  cfg::SwitchConfig config;
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1;
  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  config.interfaces()[0].routerID() = 0;
  config.interfaces()[0].mac() = "00:02:00:00:00:01";
  config.interfaces()[0].ipAddresses()->resize(3);
  config.interfaces()[0].ipAddresses()[0] = "0.0.0.0/0";
  config.interfaces()[0].ipAddresses()[1] = "192.168.0.19/24";
  config.interfaces()[0].ipAddresses()[2] = "::/0";

  auto testHandle = createTestHandle(&config);
  auto sw = testHandle->getSw();

  EXPECT_FIB_SIZE(sw->getState(), vrfZero, 2, 2);

  auto client10Nexthop4 = folly::IPAddressV4("11.11.11.11");
  auto client10Nexthop6 = folly::IPAddressV6("11:11::0");
  auto client20Nexthop4 = folly::IPAddressV4("22.22.22.22");
  auto client20Nexthop6 = folly::IPAddressV6("22:22::0");
  auto client30Nexthop6 = folly::IPAddressV6("33:33::0");
  auto client10Nexthop6b = folly::IPAddressV6("44:44::0");

  // Prefix A will include next-hops from client 10 only
  auto prefixA4 = folly::CIDRNetworkV4(folly::IPAddressV4("7.1.0.0"), 16);
  auto prefixA6 = folly::CIDRNetworkV6(folly::IPAddressV6("aaaa:1::0"), 64);

  std::vector<UnicastRoute> routesToPrefixA;
  routesToPrefixA.push_back(
      createUnicastRoute(prefixA4.first, prefixA4.second, client10Nexthop4));
  routesToPrefixA.push_back(
      createUnicastRoute(prefixA6.first, prefixA6.second, client10Nexthop6));

  // Get number of routes in rib
  const size_t numStaticRoutes =
      sw->getRib()->getRouteTableDetails(vrfZero).size();

  programRoutes(sw, ClientID(10), routesToPrefixA);

  EXPECT_FIB_SIZE(sw->getState(), vrfZero, 3, 3);

  // Verify that new routes are reflected in route table details
  EXPECT_EQ(
      numStaticRoutes + 2, sw->getRib()->getRouteTableDetails(vrfZero).size());

  // Prefix B will include next-hops from clients 10 and 20
  auto prefixB4 = folly::CIDRNetworkV4(folly::IPAddressV4("7.2.0.0"), 16);

  std::vector<UnicastRoute> routeToPrefixBFromClient10;
  routeToPrefixBFromClient10.push_back(
      createUnicastRoute(prefixB4.first, prefixB4.second, client10Nexthop4));

  programRoutes(sw, ClientID(10), routeToPrefixBFromClient10);

  std::vector<UnicastRoute> routeToPrefixBFromClient20;
  routeToPrefixBFromClient20.push_back(
      createUnicastRoute(prefixB4.first, prefixB4.second, client20Nexthop4));
  programRoutes(sw, ClientID(20), routeToPrefixBFromClient20);
  EXPECT_FIB_SIZE(sw->getState(), vrfZero, 4, 3);

  // Prefix C will include nexthops from clients 10, 20, and 30
  auto prefixC6 = folly::CIDRNetworkV6(folly::IPAddressV6("aaaa:3::0"), 64);

  std::vector<UnicastRoute> routeToPrefixCFromClient10;
  routeToPrefixCFromClient10.push_back(
      createUnicastRoute(prefixC6.first, prefixC6.second, client10Nexthop6));
  programRoutes(sw, ClientID(10), routeToPrefixBFromClient10);

  std::vector<UnicastRoute> routeToPrefixCFromClient20;
  routeToPrefixCFromClient20.push_back(
      createUnicastRoute(prefixC6.first, prefixC6.second, client20Nexthop6));
  programRoutes(sw, ClientID(20), routeToPrefixBFromClient20);

  std::vector<UnicastRoute> routeToPrefixCFromClient30;
  routeToPrefixCFromClient30.push_back(
      createUnicastRoute(prefixC6.first, prefixC6.second, client30Nexthop6));
  programRoutes(sw, ClientID(30), routeToPrefixCFromClient30);

  // Make sure all the static and link-local routes are there
  auto state = sw->getState();
  EXPECT_ROUTE(state, vrfZero, v4Default.first, v4Default.second);
  EXPECT_ROUTE(state, vrfZero, folly::IPAddressV4("192.168.0.0"), 24);
  EXPECT_ROUTE(state, vrfZero, v6Default.first, v6Default.second);
  EXPECT_ROUTE(state, vrfZero, folly::IPAddressV6("fe80::"), 64);
  // Make sure the client 10&20&30 routes are there.
  EXPECT_ROUTE(state, vrfZero, prefixA4.first, prefixA4.second);
  EXPECT_ROUTE(state, vrfZero, prefixA6.first, prefixA6.second);
  EXPECT_ROUTE(state, vrfZero, prefixB4.first, prefixB4.second);
  EXPECT_ROUTE(state, vrfZero, prefixC6.first, prefixC6.second);

  EXPECT_FIB_SIZE(sw->getState(), vrfZero, 4, 4);

  //
  // Now use syncFib to remove all the routes for client 10 and add
  // some new ones
  // Statics, link-locals, and clients 20 and 30 should remain unchanged.
  //
  auto prefixD4 = folly::CIDRNetworkV4(folly::IPAddressV4("7.4.0.0"), 16);
  auto prefixD6 = folly::CIDRNetworkV6(folly::IPAddressV6("aaaa:4::0"), 64);

  std::vector<UnicastRoute> syncFibRoutes;
  syncFibRoutes.push_back(
      createUnicastRoute(prefixC6.first, prefixC6.second, client10Nexthop6b));
  syncFibRoutes.push_back(
      createUnicastRoute(prefixD6.first, prefixD6.second, client10Nexthop6b));
  syncFibRoutes.push_back(
      createUnicastRoute(prefixD4.first, prefixD4.second, client10Nexthop4));
  programRoutes(sw, ClientID(10), syncFibRoutes, true);

  // Make sure all the static and link-local routes are still there
  state = sw->getState();
  EXPECT_ROUTE(state, vrfZero, v4Default.first, v4Default.second);
  EXPECT_ROUTE(state, vrfZero, folly::IPAddressV4("192.168.0.0"), 24);
  EXPECT_ROUTE(state, vrfZero, v6Default.first, v6Default.second);
  EXPECT_ROUTE(state, vrfZero, folly::IPAddressV6("fe80::"), 64);

  // The prefixA* routes should have disappeared
  EXPECT_NO_ROUTE(state, vrfZero, prefixA4.first, prefixA4.second);
  EXPECT_NO_ROUTE(state, vrfZero, prefixA6.first, prefixA6.second);

  EXPECT_ROUTE(state, vrfZero, prefixB4.first, prefixB4.second);

  // The prefixD4 and prefixD6 routes should have been created
  EXPECT_ROUTE(state, vrfZero, prefixD4.first, prefixD4.second);
  EXPECT_ROUTE(state, vrfZero, prefixD6.first, prefixD6.second);

  // Make sure there are no more routes (ie. the old ones were deleted) + the
  // one that gets added by default
  EXPECT_FIB_SIZE(state, vrfZero, 4, 4);
}

// There are 3 cases that should be exercised:
// 1) a route has been added whose prefix _doesn't_ exist in the RIB
// 2) a route has been added whose prefix exists in the RIB BUT whose
//    forwarding information differs from that in the FIB
// 3) a route has been added whose prefix exists in the RIB AND whose
//    forwarding information matches that in the FIB
TEST(RibToSwitchStateUpdater, Deduplication) {
  using namespace facebook::fboss;

  RoutePrefixV6 prefix{folly::IPAddressV6("2a03:2880:ff:1e::"), 64};

  cfg::SwitchConfig config;
  config.vlans()->resize(1);
  *config.vlans()[0].id() = 1;
  config.interfaces()->resize(1);
  *config.interfaces()[0].intfID() = 1;
  *config.interfaces()[0].vlanID() = 1;
  config.interfaces()[0].routerID() = vrfZero;
  config.interfaces()[0].mac() = "00:00:00:00:00:11";
  config.interfaces()[0].ipAddresses()->resize(2);
  config.interfaces()[0].ipAddresses()[0] = "10.120.70.44/31";
  config.interfaces()[0].ipAddresses()[1] = "2401:db00:e003:9100:1006::2c/127";

  auto testHandle = createTestHandle(&config);
  auto sw = testHandle->getSw();

  std::vector<UnicastRoute> routesToAdd;
  routesToAdd.push_back(createUnicastRoute(
      prefix.network(),
      prefix.mask(),
      folly::IPAddress("2401:db00:e003:9100:1006::2c")));
  std::vector<IpPrefix> routesToDelete;

  // 1)
  programRoutes(sw, ClientID(0), routesToAdd, routesToDelete);

  auto route = sw->getState()
                   ->getFibsInfoMap()
                   ->getFibContainer(vrfZero)
                   ->getFibV6()
                   ->exactMatch(prefix);
  ASSERT_TRUE(route);

  // 3)
  programRoutes(sw, ClientID(0), routesToAdd, routesToDelete);

  auto route2 = sw->getState()
                    ->getFibsInfoMap()
                    ->getFibContainer(vrfZero)
                    ->getFibV6()
                    ->exactMatch(prefix);
  ASSERT_TRUE(route2);
  EXPECT_EQ(route, route2);

  routesToAdd.push_back(createUnicastRoute(
      prefix.network(),
      prefix.mask(),
      folly::IPAddress("2401:db00:e003:9100:1006::2b")));

  // 2)
  programRoutes(sw, ClientID(0), routesToAdd, routesToDelete);

  auto route3 = sw->getState()
                    ->getFibsInfoMap()
                    ->getFibContainer(vrfZero)
                    ->getFibV6()
                    ->exactMatch(prefix);
  ASSERT_TRUE(route3);
  EXPECT_NE(route, route3);
}
