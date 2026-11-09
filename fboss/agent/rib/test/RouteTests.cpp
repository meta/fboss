/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FbossError.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/RouteNextHop.h"

#include "fboss/agent/rib/RouteUpdater.h"

#include <folly/IPAddress.h>
#include <folly/json/dynamic.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace facebook::fboss;
const ClientID kClientA = ClientID(1001);
const ClientID kClientB = ClientID(1002);
constexpr uint32_t kEcmpWidth = 64;
const std::string kSrv6Tunnel0{"srv6Tunnel0"};
} // namespace

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;

constexpr AdminDistance kDistance = AdminDistance::MAX_ADMIN_DISTANCE;

namespace {

RouteNextHopSet makeNextHops(
    std::vector<std::string> ipsAsStrings,
    std::optional<LabelForwardingAction> mplsAction = std::nullopt) {
  RouteNextHopSet nhops;
  for (const std::string& ipAsString : ipsAsStrings) {
    nhops.emplace(
        UnresolvedNextHop(IPAddress(ipAsString), ECMP_WEIGHT, mplsAction));
  }
  return nhops;
}

template <typename AddrT>
void EXPECT_ROUTES_MATCH(
    const NetworkToRouteMap<AddrT>* routesA,
    const NetworkToRouteMap<AddrT>* routesB) {
  EXPECT_EQ(routesA->size(), routesB->size());
  for (const auto& entryA : *routesA) {
    auto routeA = entryA.value();
    auto prefixA = routeA->prefix();

    auto iterB = routesB->exactMatch(prefixA.network(), prefixA.mask());
    ASSERT_NE(routesB->end(), iterB);
    auto routeB = iterB->value();

    EXPECT_TRUE(routeB->isSame(routeA.get()));
  }
}

} // namespace

namespace facebook::fboss {

TEST(Route, removeRoutesForClient) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  // 2 different nexthops
  RouteNextHopSet nhop1 = makeNextHops({"1.1.1.10"}); // resolved by intf 1
  RouteNextHopSet nhop2 = makeNextHops({"2.2.2.10"}); // resolved by intf 2
  // 4 prefixes
  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV4::Prefix r2{IPAddressV4("20.1.1.0"), 24};
  RouteV6::Prefix r3{IPAddressV6("1001::0"), 48};
  RouteV6::Prefix r4{IPAddressV6("2001::0"), 48};

  NextHopIDManager nhopIds;
  RibRouteUpdater u2(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u2.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhop1, kDistance)},
          {{r3.network(), r3.mask()}, RouteNextHopEntry(nhop1, kDistance)},
      },
      {},
      false);
  u2.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientB,
      {
          {{r2.network(), r2.mask()}, RouteNextHopEntry(nhop2, kDistance)},
          {{r4.network(), r4.mask()}, RouteNextHopEntry(nhop2, kDistance)},
      },
      {},
      false);
}

TEST(Route, serializeRouteTable) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  LabelToRouteMap mplsRoutes;

  // 2 different nexthops
  RouteNextHopSet nhop1 = makeNextHops({"1.1.1.10"}); // resolved by intf 1
  RouteNextHopSet nhop2 = makeNextHops({"2.2.2.10"}); // resolved by intf 2
  // 4 prefixes
  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV4::Prefix r2{IPAddressV4("20.1.1.0"), 24};
  RouteV6::Prefix r3{IPAddressV6("1001::0"), 48};
  RouteV6::Prefix r4{IPAddressV6("2001::0"), 48};

  std::optional<RouteCounterID> counterID1("route.counter.0");
  std::optional<RouteCounterID> counterID2("route.counter.1");
  std::optional<cfg::AclLookupClass> classID(
      cfg::AclLookupClass::DST_CLASS_L3_DPR);

  NextHopIDManager nhopIds;
  RibRouteUpdater u2(
      &v4Routes, &v6Routes, &mplsRoutes, &nhopIds, nullptr, kEcmpWidth);
  u2.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhop1, kDistance)},
          {{r2.network(), r2.mask()},
           RouteNextHopEntry(nhop2, kDistance, counterID1, classID)},
          {{r3.network(), r3.mask()}, RouteNextHopEntry(nhop1, kDistance)},
          {{r4.network(), r4.mask()},
           RouteNextHopEntry(nhop2, kDistance, counterID2)},
      },
      {},
      false);

  RouteNextHopSet mplsNhop1 = makeNextHops(
      {"1.1.1.10"},
      LabelForwardingAction(LabelForwardingAction::LabelForwardingType::PHP));
  RouteNextHopSet mplsNhop2 = makeNextHops(
      {"2.2.2.10"},
      LabelForwardingAction(LabelForwardingAction::LabelForwardingType::PHP));
  u2.update<RibRouteUpdater::MplsRouteEntry, LabelID>(
      kClientA,
      {
          {LabelID(100), RouteNextHopEntry(mplsNhop1, kDistance)},
          {LabelID(101), RouteNextHopEntry(mplsNhop2, kDistance)},
      },
      {},
      false);
}

TEST(Route, addRouteWithSrv6NextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      ECMP_WEIGHT,
      std::nullopt, // action
      std::nullopt, // disableTTLDecrement
      std::nullopt, // topologyInfo
      std::nullopt, // adjustedWeight
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("2.2.2.10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV6::Prefix r2{IPAddressV6("3001::0"), 48};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
          {{r2.network(), r2.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
      },
      {},
      false);

  // Verify routes were added
  EXPECT_EQ(v4Routes.size(), 1);
  EXPECT_EQ(v6Routes.size(), 1);

  // Verify SRv6 fields are preserved in the stored routes
  auto v4It = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), v4It);
  auto v4Route = v4It->value();
  auto v4Nhops = v4Route->getEntryForClient(kClientA);
  ASSERT_TRUE(v4Nhops);
  for (const auto& nh : getClientNextHopsFromRib(&nhopIds, *v4Nhops)) {
    EXPECT_EQ(nh.srv6SegmentList(), segList);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
  }
}

TEST(Route, serializeRouteTableWithSrv6) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  LabelToRouteMap mplsRoutes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  // Also add a regular (non-SRv6) nexthop
  RouteNextHopSet regularNhops = makeNextHops({"2.2.2.10"});

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV4::Prefix r2{IPAddressV4("20.1.1.0"), 24};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(
      &v4Routes, &v6Routes, &mplsRoutes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
          {{r2.network(), r2.mask()},
           RouteNextHopEntry(regularNhops, kDistance)},
      },
      {},
      false);

  // Verify both routes exist
  EXPECT_EQ(v4Routes.size(), 2);

  // Verify SRv6 route preserved its fields
  auto it = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), it);
  auto route = it->value();
  auto entry = route->getEntryForClient(kClientA);
  ASSERT_TRUE(entry);
  auto nhops = getClientNextHopsFromRib(&nhopIds, *entry);
  ASSERT_EQ(nhops.size(), 1);
  const auto& nh = *nhops.begin();
  EXPECT_EQ(nh.srv6SegmentList(), segList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);

  // Verify regular route has empty SRv6 fields
  auto it2 = v4Routes.exactMatch(r2.network(), r2.mask());
  ASSERT_NE(v4Routes.end(), it2);
  auto route2 = it2->value();
  auto entry2 = route2->getEntryForClient(kClientA);
  ASSERT_TRUE(entry2);
  auto nhops2 = getClientNextHopsFromRib(&nhopIds, *entry2);
  ASSERT_EQ(nhops2.size(), 1);
  const auto& nh2 = *nhops2.begin();
  EXPECT_TRUE(nh2.srv6SegmentList().empty());
  EXPECT_FALSE(nh2.tunnelType().has_value());
  EXPECT_FALSE(nh2.tunnelId().has_value());
}

// Test that SRv6 fields survive through ECMP route resolution
// (mergeForwardInfosEcmp path - weight=0)
TEST(Route, resolveEcmpRouteWithSrv6NextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  // Step 1: Add interface routes so nexthops can be resolved
  // Interface route for 1.1.1.0/24 via interface 1
  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("1.1.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  // Interface route for 2.2.2.0/24 via interface 2
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("2.2.2.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("1.1.1.0"), 24},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2.2.2.0"), 24},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Step 2: Add a route with SRv6 ECMP nexthops (weight=0 triggers ECMP merge)
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      ECMP_WEIGHT,
      std::nullopt, // action
      std::nullopt, // disableTTLDecrement
      std::nullopt, // topologyInfo
      std::nullopt, // adjustedWeight
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("2.2.2.10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
      },
      {},
      false);

  // Step 3: Verify the route is resolved and SRv6 fields are preserved
  auto it = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, fwdInfo);
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_EQ(nh.srv6SegmentList(), segList);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    // Verify resolved to correct interfaces
    EXPECT_TRUE(nh.isResolved());
    EXPECT_TRUE(nh.intf() == InterfaceID(1) || nh.intf() == InterfaceID(2));
    // ECMP path sets weight to 0
    EXPECT_EQ(nh.weight(), ECMP_WEIGHT);
  }
}

// Test that SRv6 fields survive through UCMP route resolution
// (combineWeights + optimizeWeights path - non-zero weights)
TEST(Route, resolveUcmpRouteWithSrv6NextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("3001:db8:1::"),
      folly::IPAddressV6("3001:db8:2::"),
      folly::IPAddressV6("3001:db8:3::")};

  // Step 1: Add interface routes
  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("1.1.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("2.2.2.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("1.1.1.0"), 24},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2.2.2.0"), 24},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Step 2: Add a route with SRv6 UCMP nexthops (non-zero weights)
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      3, // weight
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("2.2.2.10"),
      2, // weight
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
      },
      {},
      false);

  // Step 3: Verify the route is resolved and SRv6 fields are preserved
  auto it = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, fwdInfo);
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_EQ(nh.srv6SegmentList(), segList);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    EXPECT_TRUE(nh.isResolved());
    EXPECT_TRUE(nh.intf() == InterfaceID(1) || nh.intf() == InterfaceID(2));
    // UCMP path preserves non-zero weights (optimized)
    EXPECT_GT(nh.weight(), 0);
  }

  // Verify weight ratio is preserved (3:2)
  std::map<InterfaceID, NextHopWeight> weightByIntf;
  for (const auto& nh : resolvedNhops) {
    weightByIntf[nh.intf()] = nh.weight();
  }
  // Weight ratio should be 3:2 (or equivalent after optimization)
  EXPECT_EQ(weightByIntf[InterfaceID(1)] * 2, weightByIntf[InterfaceID(2)] * 3);
}

// Test that SRv6 fields survive through IPv6 route resolution
TEST(Route, resolveV6RouteWithSrv6NextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  // Step 1: Add IPv6 interface route
  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Step 2: Add a v6 route with SRv6 nexthop
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
      },
      {},
      false);

  // Step 3: Verify
  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, fwdInfo);
  ASSERT_EQ(resolvedNhops.size(), 1);

  const auto& nh = *resolvedNhops.begin();
  EXPECT_EQ(nh.srv6SegmentList(), segList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
  EXPECT_TRUE(nh.isResolved());
  EXPECT_EQ(nh.intf(), InterfaceID(1));
}

// Test mixed SRv6 and plain nexthops through ECMP merge resolution
TEST(Route, resolveEcmpMixedSrv6AndPlainNextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  // Step 1: Add interface routes
  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("1.1.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("2.2.2.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("1.1.1.0"), 24},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2.2.2.0"), 24},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Step 2: Add a route with a mix of SRv6 and plain nexthops
  RouteNextHopSet mixedNhops;
  // SRv6 nexthop
  mixedNhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  // Plain nexthop (no SRv6)
  mixedNhops.emplace(UnresolvedNextHop(IPAddress("2.2.2.10"), ECMP_WEIGHT));

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(mixedNhops, kDistance)},
      },
      {},
      false);

  // Step 3: Verify both nexthops are resolved with correct SRv6 fields
  auto it = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, fwdInfo);
  ASSERT_EQ(resolvedNhops.size(), 2);

  bool foundSrv6Nhop = false;
  bool foundPlainNhop = false;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    if (nh.intf() == InterfaceID(1)) {
      // SRv6 nexthop
      EXPECT_EQ(nh.srv6SegmentList(), segList);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      foundSrv6Nhop = true;
    } else if (nh.intf() == InterfaceID(2)) {
      // Plain nexthop - should have empty SRv6 fields
      EXPECT_TRUE(nh.srv6SegmentList().empty());
      EXPECT_FALSE(nh.tunnelType().has_value());
      EXPECT_FALSE(nh.tunnelId().has_value());
      foundPlainNhop = true;
    }
  }
  EXPECT_TRUE(foundSrv6Nhop);
  EXPECT_TRUE(foundPlainNhop);
}

// Test that different SRv6 SID lists on same interface are kept distinct
// through UCMP resolution (NextHopCombinedWeightsKey differentiation)
TEST(Route, resolveUcmpDistinctSrv6SegmentLists) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segListA{
      folly::IPAddressV6("2001:db8::1")};
  const std::vector<folly::IPAddressV6> segListB{
      folly::IPAddressV6("2001:db8::2")};

  // Step 1: Add interface route
  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("1.1.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("1.1.1.0"), 24},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Step 2: Add a route with two SRv6 nexthops to same IP and same tunnel
  // but different segment lists
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      5, // weight
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  srv6Nhops.emplace(UnresolvedNextHop(
      IPAddress("1.1.1.10"),
      3, // weight
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(srv6Nhops, kDistance)},
      },
      {},
      false);

  // Step 3: Verify both nexthops are distinct in resolved set
  // (NextHopCombinedWeightsKey must differentiate by SRv6 fields)
  auto it = v4Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v4Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, fwdInfo);
  // Both should be present as distinct nexthops
  ASSERT_EQ(resolvedNhops.size(), 2);

  bool foundSegListA = false;
  bool foundSegListB = false;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.intf(), InterfaceID(1));
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    if (nh.srv6SegmentList() == segListA) {
      foundSegListA = true;
    } else if (nh.srv6SegmentList() == segListB) {
      foundSegListB = true;
    }
  }
  EXPECT_TRUE(foundSegListA);
  EXPECT_TRUE(foundSegListB);
}

TEST(Route, resolveEcmpRouteWithCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100)));
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200)));

  RouteV6::Prefix r1{IPAddressV6("2800:1::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_TRUE(nh.cost().has_value());
    if (nh.intf() == InterfaceID(1)) {
      EXPECT_EQ(nh.cost(), int64_t(100));
    } else {
      EXPECT_EQ(nh.cost(), int64_t(200));
    }
  }
}

TEST(Route, resolveUcmpRouteWithCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      3,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(500)));
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:2::10"),
      2,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(600)));

  RouteV6::Prefix r1{IPAddressV6("2800:1::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_TRUE(nh.cost().has_value());
    if (nh.intf() == InterfaceID(1)) {
      EXPECT_EQ(nh.cost(), int64_t(500));
    } else {
      EXPECT_EQ(nh.cost(), int64_t(600));
    }
  }
}

TEST(Route, resolveUcmpDistinctCosts) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      5,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100)));
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      3,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200)));

  RouteV6::Prefix r1{IPAddressV6("2800:1::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  bool foundCost100 = false;
  bool foundCost200 = false;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.intf(), InterfaceID(1));
    if (nh.cost() == int64_t(100)) {
      foundCost100 = true;
    } else if (nh.cost() == int64_t(200)) {
      foundCost200 = true;
    }
  }
  EXPECT_TRUE(foundCost100);
  EXPECT_TRUE(foundCost200);
}

TEST(Route, resolveMixedCostAndNoCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(42)));
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:2::10"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("2800:1::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    if (nh.intf() == InterfaceID(1)) {
      EXPECT_EQ(nh.cost(), int64_t(42));
    } else {
      EXPECT_FALSE(nh.cost().has_value());
    }
  }
}

TEST(Route, resolveRecursiveRouteWithCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route: fc00:10::/32 via fc00:1::10 (resolves via interface)
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(
      UnresolvedNextHop(IPAddress("fc00:1::10"), ECMP_WEIGHT));

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{IPAddress("fc00:10::"), 32},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  // Final route: 2800:2::/64 via fc00:10::10 with cost
  // Resolves recursively through the intermediate route
  RouteNextHopSet finalNhops;
  finalNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:10::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(999)));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(finalNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 1);

  const auto& nh = *resolvedNhops.begin();
  EXPECT_TRUE(nh.isResolved());
  EXPECT_EQ(nh.intf(), InterfaceID(1));
  EXPECT_EQ(nh.cost(), int64_t(999));
}

TEST(Route, resolveRecursiveUcmpRouteWithCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route: fc00:10::/32 via fc00:1::10 and fc00:2::10
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(
      UnresolvedNextHop(IPAddress("fc00:1::10"), ECMP_WEIGHT));
  intermediateNhops.emplace(
      UnresolvedNextHop(IPAddress("fc00:2::10"), ECMP_WEIGHT));

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{IPAddress("fc00:10::"), 32},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  // Final route: 2800:2::/64 via fc00:10::10 with cost, UCMP weight
  // Resolves recursively — the non-connected branch copies the intermediate
  // route's resolved next hops but applies cost from the original next hop
  RouteNextHopSet finalNhops;
  finalNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:10::10"),
      3,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(777)));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(finalNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.cost(), int64_t(777));
  }
}

// A route that states a weighting is resolved by combineWeights, where its
// ECMP_WEIGHT next hop counts as the single share it means rather than as no
// share at all. Counting it as none would zero that next hop's contribution,
// and the gcd of that zero and the weight beside it would then divide the
// weighting away entirely.
TEST(Route, resolveEcmpRouteAppliesParentWeightsOverUnweightedChildren) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Both next hops resolve straight through connected routes, so neither
  // child states a weight of its own.
  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:1::10"), 3));
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:2::10"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  std::map<folly::IPAddress, NextHopWeight> weights;
  for (const auto& nh :
       getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo())) {
    weights[nh.addr()] = nh.weight();
  }

  // Three shares against the one the ECMP_WEIGHT next hop is read as taking.
  const std::map<folly::IPAddress, NextHopWeight> expectedWeights{
      {IPAddress("fc00:1::10"), 3},
      {IPAddress("fc00:2::10"), 1},
  };
  EXPECT_EQ(weights, expectedWeights);
}

// When the next hop being resolved and the next hops it resolves onto both
// state a weight, its share is divided among them in their own ratio rather
// than handed to each of them in full. Both weightings survive: the route's
// split across its own next hops, and the intermediate route's across its.
TEST(Route, resolveEcmpRouteDividesParentWeightAmongWeightedChildren) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route splits 3:1, so the children below do state weights.
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(UnresolvedNextHop(IPAddress("fc00:1::10"), 3));
  intermediateNhops.emplace(UnresolvedNextHop(IPAddress("fc00:2::10"), 1));

  RouteV6::Prefix intermediate{IPAddressV6("fc00:10::"), 32};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{intermediate.network(), intermediate.mask()},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  auto weightsOf = [&nhopIds, &v6Routes](const RouteV6::Prefix& prefix) {
    auto it = v6Routes.exactMatch(prefix.network(), prefix.mask());
    EXPECT_NE(v6Routes.end(), it);
    auto route = it->value();
    EXPECT_TRUE(route->isResolved());
    std::map<folly::IPAddress, NextHopWeight> weights;
    for (const auto& nh :
         getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo())) {
      weights[nh.addr()] = nh.weight();
    }
    return weights;
  };

  const std::map<folly::IPAddress, NextHopWeight> expectedIntermediate{
      {IPAddress("fc00:1::10"), 3},
      {IPAddress("fc00:2::10"), 1},
  };
  EXPECT_EQ(weightsOf(intermediate), expectedIntermediate);

  // The top level route reaches that weighted set through a next hop of its
  // own stating weight 5, alongside one at ECMP_WEIGHT resolving elsewhere so
  // that the resolution lands in this merge.
  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:10::10"), 5));
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:2::20"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  // Scaled to the LCM of the two resolved sets' totals, 4: the weight 5 next
  // hop takes 5 * 4 = 20 shares, split 3:1 by the intermediate route into 15
  // and 5, and the ECMP_WEIGHT one takes 1 * 4 = 4. So the route's own 5:1
  // holds as 20:4, and the intermediate's 3:1 holds as 15:5.
  const std::map<folly::IPAddress, NextHopWeight> expectedWeights{
      {IPAddress("fc00:1::10"), 15},
      {IPAddress("fc00:2::10"), 5},
      {IPAddress("fc00:2::20"), 4},
  };
  EXPECT_EQ(weightsOf(r1), expectedWeights);
}
// A next hop at ECMP_WEIGHT states no weighting of its own, so it should not
// flatten a weighting the route it resolves through was programmed with.
// Without inheritance the recursive route comes out plain ECMP and the split
// the intermediate route expressed is silently lost.
TEST(Route, resolveEcmpRouteInheritsWeightsOfRecursiveNextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route splits 3:1 across its two next hops. Neither is at
  // ECMP_WEIGHT, so it resolves down the weighted path.
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(UnresolvedNextHop(IPAddress("fc00:1::10"), 3));
  intermediateNhops.emplace(UnresolvedNextHop(IPAddress("fc00:2::10"), 1));

  RouteV6::Prefix intermediate{IPAddressV6("fc00:10::"), 32};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{intermediate.network(), intermediate.mask()},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  const std::map<folly::IPAddress, NextHopWeight> expectedWeights{
      {IPAddress("fc00:1::10"), 3},
      {IPAddress("fc00:2::10"), 1},
  };

  auto weightsOf = [&nhopIds, &v6Routes](const RouteV6::Prefix& prefix) {
    auto it = v6Routes.exactMatch(prefix.network(), prefix.mask());
    EXPECT_NE(v6Routes.end(), it);
    auto route = it->value();
    EXPECT_TRUE(route->isResolved());
    std::map<folly::IPAddress, NextHopWeight> weights;
    for (const auto& nh :
         getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo())) {
      weights[nh.addr()] = nh.weight();
    }
    return weights;
  };

  EXPECT_EQ(weightsOf(intermediate), expectedWeights);

  // The recursive route's own next hop is at ECMP_WEIGHT, so the split above
  // is carried through rather than collapsed.
  RouteNextHopSet finalNhops;
  finalNhops.emplace(UnresolvedNextHop(IPAddress("fc00:10::10"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(finalNhops, kDistance)},
      },
      {},
      false);

  EXPECT_EQ(weightsOf(r1), expectedWeights);
}

// With neither the next hop being resolved nor the one it resolves onto
// stating a weight, the result stays unweighted. Resolution floors
// ECMP_WEIGHT at one share, so a plain ECMP child arrives here weighted 1;
// counting that as a weighting would rewrite the next hops of every
// recursively resolved ECMP route from ECMP_WEIGHT to 1. Hardware programs
// the two the same, since normalizeNextHops applies that floor again above
// the SAI and BCM layers, but NextHop::operator< orders on weight, so the
// rewrite would change next hop group identity for routes that carry no
// weighting at all.
TEST(Route, resolveEcmpRouteRemainsUnweightedIfParentAndChildUnweighted) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Resolving fc00:1::10 through the connected route above mints a resolved
  // next hop at UCMP_DEFAULT_WEIGHT, which is the value that must not read as
  // a weighting. One level is enough to show it: an un-thresholded inheritance
  // turns this route's next hop into a one-share UCMP one, and from there
  // every route recursing onto it inherits the same.
  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(IPAddress("fc00:1::10"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("fc00:10::"), 32};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(nhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 1);
  EXPECT_EQ(resolvedNhops.begin()->weight(), ECMP_WEIGHT);
}

// Intermediate route's next hops carry cost, but the final (immediate) route's
// next hop has no cost. The final resolved next hops should have no cost
// because getFwdInfoFromNhop uses the caller's cost, not the intermediate's.
TEST(Route, resolveRecursiveEcmpIntermediateCostDropped) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route with cost on its next hops
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(333)));
  intermediateNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(444)));

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{IPAddress("fc00:10::"), 32},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  // Verify intermediate route resolved with cost preserved
  auto intIt = v6Routes.exactMatch(IPAddressV6("fc00:10::"), 32);
  ASSERT_NE(v6Routes.end(), intIt);
  auto intRoute = intIt->value();
  EXPECT_TRUE(intRoute->isResolved());
  for (const auto& nh :
       getResolvedNextHopsFromRib(&nhopIds, intRoute->getForwardInfo())) {
    EXPECT_TRUE(nh.cost().has_value());
  }

  // Final (immediate) route with NO cost, resolves recursively
  RouteNextHopSet finalNhops;
  finalNhops.emplace(UnresolvedNextHop(IPAddress("fc00:10::10"), ECMP_WEIGHT));

  RouteV6::Prefix r1{IPAddressV6("2800:3::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(finalNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  // Intermediate costs (333, 444) are not inherited — the immediate next
  // hop's cost (nullopt) is what gets applied during recursive resolution
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_FALSE(nh.cost().has_value());
  }
}

TEST(Route, resolveRecursiveUcmpIntermediateCostDropped) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  RouteNextHopSet intfNhop1;
  intfNhop1.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));
  RouteNextHopSet intfNhop2;
  intfNhop2.emplace(ResolvedNextHop(
      IPAddress("fc00:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("fc00:2::"), 64},
           RouteNextHopEntry(intfNhop2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate route with cost on its next hops
  RouteNextHopSet intermediateNhops;
  intermediateNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(111)));
  intermediateNhops.emplace(UnresolvedNextHop(
      IPAddress("fc00:2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(222)));

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{IPAddress("fc00:10::"), 32},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  // Verify intermediate route resolved with cost preserved
  auto intIt = v6Routes.exactMatch(IPAddressV6("fc00:10::"), 32);
  ASSERT_NE(v6Routes.end(), intIt);
  auto intRoute = intIt->value();
  EXPECT_TRUE(intRoute->isResolved());
  for (const auto& nh :
       getResolvedNextHopsFromRib(&nhopIds, intRoute->getForwardInfo())) {
    EXPECT_TRUE(nh.cost().has_value());
  }

  // Final (immediate) route with NO cost, resolves recursively
  RouteNextHopSet finalNhops;
  finalNhops.emplace(UnresolvedNextHop(IPAddress("fc00:10::10"), 5));

  RouteV6::Prefix r1{IPAddressV6("2800:3::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()}, RouteNextHopEntry(finalNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  // Intermediate costs (111, 222) are not inherited — the immediate next
  // hop's cost (nullopt) is what gets applied during recursive resolution
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_FALSE(nh.cost().has_value());
  }
}

TEST(Route, resolveRecursiveSrv6WithIntermediateLinkLocalCost) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segListA{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};
  const std::vector<folly::IPAddressV6> segListB{
      folly::IPAddressV6("2001:db8::3"), folly::IPAddressV6("2001:db8::4")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // Covering route for fdad:ff02:10b::d:0 with 2 link-local next hops
  // that carry cost
  RouteNextHopSet coverNhopsD;
  coverNhopsD.emplace(ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100)));
  coverNhopsD.emplace(ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(200)));

  // Covering route for fdad:ff02:10b::c:0 with 2 link-local next hops
  // that carry cost
  RouteNextHopSet coverNhopsC;
  coverNhopsC.emplace(ResolvedNextHop(
      IPAddress("fe80::3"),
      InterfaceID(3),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(300)));
  coverNhopsC.emplace(ResolvedNextHop(
      IPAddress("fe80::4"),
      InterfaceID(4),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(400)));

  // OpenR routes (covering routes) with link-local nexthops carrying cost
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:ff02:10b::d:0"), 112},
           RouteNextHopEntry(coverNhopsD, AdminDistance::OPENR)},
          {{IPAddress("fdad:ff02:10b::c:0"), 112},
           RouteNextHopEntry(coverNhopsC, AdminDistance::OPENR)},
      },
      {},
      false);

  // Verify covering routes resolved with cost on their link-local next hops
  auto covDIt = v6Routes.exactMatch(IPAddressV6("fdad:ff02:10b::d:0"), 112);
  ASSERT_NE(v6Routes.end(), covDIt);
  EXPECT_TRUE(covDIt->value()->isResolved());
  for (const auto& nh : getResolvedNextHopsFromRib(
           &nhopIds, covDIt->value()->getForwardInfo())) {
    EXPECT_TRUE(nh.cost().has_value());
  }

  auto covCIt = v6Routes.exactMatch(IPAddressV6("fdad:ff02:10b::c:0"), 112);
  ASSERT_NE(v6Routes.end(), covCIt);
  EXPECT_TRUE(covCIt->value()->isResolved());
  for (const auto& nh : getResolvedNextHopsFromRib(
           &nhopIds, covCIt->value()->getForwardInfo())) {
    EXPECT_TRUE(nh.cost().has_value());
  }

  // BGP route 2001::/64 with 2 next hops, each with distinct SID lists
  // but no cost — resolves recursively over the OpenR covering routes
  RouteNextHopSet bgpNhops;
  bgpNhops.emplace(UnresolvedNextHop(
      IPAddress("fdad:ff02:10b::d:0"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  bgpNhops.emplace(UnresolvedNextHop(
      IPAddress("fdad:ff02:10b::c:0"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteV6::Prefix bgpPrefix{IPAddressV6("2001::"), 64};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {
          {{bgpPrefix.network(), bgpPrefix.mask()},
           RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());

  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 4);

  int segListACount = 0;
  int segListBCount = 0;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_FALSE(nh.cost().has_value());

    if (nh.srv6SegmentList() == segListA) {
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      EXPECT_TRUE(nh.intf() == InterfaceID(1) || nh.intf() == InterfaceID(2));
      segListACount++;
    } else if (nh.srv6SegmentList() == segListB) {
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      EXPECT_TRUE(nh.intf() == InterfaceID(3) || nh.intf() == InterfaceID(4));
      segListBCount++;
    } else {
      FAIL() << "Unexpected SID list on resolved next hop";
    }
  }
  EXPECT_EQ(segListACount, 2);
  EXPECT_EQ(segListBCount, 2);
}

TEST(Route, resolveRecursiveSrv6OpenrRouteChange) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> segListA{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // OpenR covering route with link-local nexthops on interfaces 1 and 2.
  RouteNextHopSet openrNhops{
      ResolvedNextHop(IPAddress("fe80::1"), InterfaceID(1), ECMP_WEIGHT),
      ResolvedNextHop(IPAddress("fe80::2"), InterfaceID(2), ECMP_WEIGHT)};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:ff02:10b::d:0"), 112},
           RouteNextHopEntry(openrNhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // BGP route resolving over the OpenR route with SRV6 fields.
  RouteNextHopSet bgpNhops{UnresolvedNextHop(
      IPAddress("fdad:ff02:10b::d:0"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};

  RouteV6::Prefix bgpPrefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {
          {{bgpPrefix.network(), bgpPrefix.mask()},
           RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)},
      },
      {},
      false);

  // Verify initial resolution: 2 nexthops on interfaces 1 and 2.
  {
    auto it = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
    ASSERT_NE(v6Routes.end(), it);
    EXPECT_TRUE(it->value()->isResolved());
    auto resolved =
        getResolvedNextHopsFromRib(&nhopIds, it->value()->getForwardInfo());
    ASSERT_EQ(resolved.size(), 2);
    std::set<InterfaceID> intfs;
    for (const auto& nh : resolved) {
      EXPECT_EQ(nh.srv6SegmentList(), segListA);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      intfs.insert(nh.intf());
    }
    EXPECT_TRUE(intfs.count(InterfaceID(1)));
    EXPECT_TRUE(intfs.count(InterfaceID(2)));
  }

  // Update the OpenR route to use different link-local nexthops on
  // interfaces 3 and 4.
  RouteNextHopSet updatedOpenrNhops{
      ResolvedNextHop(IPAddress("fe80::3"), InterfaceID(3), ECMP_WEIGHT),
      ResolvedNextHop(IPAddress("fe80::4"), InterfaceID(4), ECMP_WEIGHT)};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:ff02:10b::d:0"), 112},
           RouteNextHopEntry(updatedOpenrNhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // Verify re-resolution: BGP route now resolves to fe80::3 and fe80::4
  // on interfaces 3 and 4, while retaining SRV6 fields from the BGP nhop.
  auto it = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, it->value()->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 2);

  std::set<InterfaceID> intfs;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.srv6SegmentList(), segListA);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    intfs.insert(nh.intf());
  }
  EXPECT_TRUE(intfs.count(InterfaceID(3)));
  EXPECT_TRUE(intfs.count(InterfaceID(4)));
}

std::optional<RouteCounterID> resolveRecursiveCounterID(
    std::optional<RouteCounterID> childCounterID,
    std::optional<RouteCounterID> parentCounterID) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  const IPAddressV6 childPrefix{"fdad:feff:202::d:0"};
  const RouteNextHopSet childNhops{
      ResolvedNextHop(IPAddress("fe80::1"), InterfaceID(1), ECMP_WEIGHT)};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {{{childPrefix, 128},
        RouteNextHopEntry(
            childNhops, AdminDistance::OPENR, std::move(childCounterID))}},
      {},
      false);

  const RouteNextHopSet parentNhops{
      UnresolvedNextHop(childPrefix, ECMP_WEIGHT)};
  const RouteV6::Prefix parentPrefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {{{parentPrefix.network(), parentPrefix.mask()},
        RouteNextHopEntry(
            parentNhops, AdminDistance::EBGP, std::move(parentCounterID))}},
      {},
      false);

  const auto parent =
      v6Routes.exactMatch(parentPrefix.network(), parentPrefix.mask());
  if (parent == v6Routes.end()) {
    return std::nullopt;
  }
  return parent->value()->getForwardInfo().getCounterID();
}

TEST(Route, resolveRecursiveUsesParentCounterID) {
  const RouteCounterID parentCounter{"parentCounter"};

  EXPECT_EQ(
      resolveRecursiveCounterID(std::nullopt, parentCounter), parentCounter);
}

TEST(Route, resolveRecursiveInheritsChildCounterID) {
  const RouteCounterID childCounter{"childCounter"};

  EXPECT_EQ(
      resolveRecursiveCounterID(childCounter, std::nullopt), childCounter);
}

TEST(Route, resolveRecursivePrefersParentCounterID) {
  const RouteCounterID childCounter{"childCounter"};
  const RouteCounterID parentCounter{"parentCounter"};

  EXPECT_EQ(
      resolveRecursiveCounterID(childCounter, parentCounter), parentCounter);
}

// SRv6-over-SRv6 recursion: outer route R1 has no SID list and resolves over a
// single OpenR route R2 whose link-local next hops themselves carry SID lists.
// The outer next hop carries no SID list, so each resolved leaf inherits the
// inner (child) SID list from the OpenR route it resolved through.
TEST(Route, resolveRecursiveSrv6InnerSidListThroughSingleOpenrRoute) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("fdad:ffff:0001:0002::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("fdad:ffff:0003:0004::")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // 1. OpenR route R2 with link-local next hops that carry their own SID lists.
  RouteNextHopSet openrNhops;
  openrNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  openrNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:feff:0202::0:d:0"), 112},
           RouteNextHopEntry(openrNhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // 2. BGP route R1 with a single plain next hop (no SID list) resolving
  //    recursively over R2.
  RouteNextHopSet bgpNhops{
      UnresolvedNextHop(IPAddress("fdad:feff:0202::0:d:0"), ECMP_WEIGHT)};

  RouteV6::Prefix bgpPrefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {
          {{bgpPrefix.network(), bgpPrefix.mask()},
           RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)},
      },
      {},
      false);

  // 3. R1 resolves to fe80::1/fe80::2 and each leaf inherits the inner SID list
  //    from the OpenR route it resolved through, since the outer carried none.
  auto it = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  const auto& resolvedNhops = it->value()->getForwardInfo().getNextHopSet();
  ASSERT_EQ(resolvedNhops.size(), 2);

  std::map<InterfaceID, std::vector<folly::IPAddressV6>> segListByIntf;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    segListByIntf[nh.intf()] = nh.srv6SegmentList();
  }
  EXPECT_EQ(segListByIntf[InterfaceID(1)], sidList1);
  EXPECT_EQ(segListByIntf[InterfaceID(2)], sidList2);
}

// SRv6-over-SRv6 recursion: outer route R1 carries sidList1 and resolves over a
// single OpenR route R2 whose link-local next hops carry their own SID lists.
// The outer SID list overrides the inner ones on every resolved leaf.
TEST(Route, resolveRecursiveSrv6OuterSidListThroughSingleOpenrRoute) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("fdad:ffff:0001:0002::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("fdad:ffff:0003:0004::")};
  const std::vector<folly::IPAddressV6> sidList3{
      folly::IPAddressV6("fdad:ffff:0005:0006::")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // 1. OpenR route R2 with link-local next hops carrying their own SID lists.
  RouteNextHopSet openrNhops;
  openrNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  openrNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList3,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  // 2. OpenR route R1 carrying sidList1, resolving recursively over R2.
  RouteNextHopSet r1Nhops{UnresolvedNextHop(
      IPAddress("fdad:feff:0202::0:d:0"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};

  RouteV6::Prefix r1Prefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:feff:0202::0:d:0"), 112},
           RouteNextHopEntry(openrNhops, AdminDistance::OPENR)},
          {{r1Prefix.network(), r1Prefix.mask()},
           RouteNextHopEntry(r1Nhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // 3. Both resolved leaves carry the outer sidList1, not the inner SID lists.
  auto it = v6Routes.exactMatch(r1Prefix.network(), r1Prefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  const auto& resolvedNhops = it->value()->getForwardInfo().getNextHopSet();
  ASSERT_EQ(resolvedNhops.size(), 2);

  std::set<InterfaceID> intfs;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.srv6SegmentList(), sidList1);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    intfs.insert(nh.intf());
  }
  EXPECT_TRUE(intfs.count(InterfaceID(1)));
  EXPECT_TRUE(intfs.count(InterfaceID(2)));
}

// SRv6-over-SRv6 recursion: outer route R1 has no SID list and its two plain
// next hops resolve over two distinct OpenR routes R2/R3, each with a
// link-local next hop carrying its own SID list. Each resolved leaf inherits
// the inner (child) SID list, since the outer next hops carried none.
TEST(Route, resolveRecursiveSrv6InnerSidListThroughTwoOpenrRoutes) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("fdad:ffff:0001:0002::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("fdad:ffff:0003:0004::")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // 1. OpenR routes R2 (d:0) and R3 (c:0), each with one link-local next hop
  //    carrying its own SID list.
  RouteNextHopSet openrNhopsD{ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};
  RouteNextHopSet openrNhopsC{ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:feff:0202::0:d:0"), 112},
           RouteNextHopEntry(openrNhopsD, AdminDistance::OPENR)},
          {{IPAddress("fdad:feff:0202::0:c:0"), 112},
           RouteNextHopEntry(openrNhopsC, AdminDistance::OPENR)},
      },
      {},
      false);

  // 2. BGP route R1 with two plain next hops (no SID list), one resolving over
  //    R2 and the other over R3.
  RouteNextHopSet bgpNhops{
      UnresolvedNextHop(IPAddress("fdad:feff:0202::0:d:0"), ECMP_WEIGHT),
      UnresolvedNextHop(IPAddress("fdad:feff:0202::0:c:0"), ECMP_WEIGHT)};

  RouteV6::Prefix bgpPrefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {
          {{bgpPrefix.network(), bgpPrefix.mask()},
           RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)},
      },
      {},
      false);

  // 3. R1 resolves to fe80::1/fe80::2, each leaf inheriting the inner SID list
  //    from the OpenR route it resolved through.
  auto it = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  const auto& resolvedNhops = it->value()->getForwardInfo().getNextHopSet();
  ASSERT_EQ(resolvedNhops.size(), 2);

  std::map<InterfaceID, std::vector<folly::IPAddressV6>> segListByIntf;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    segListByIntf[nh.intf()] = nh.srv6SegmentList();
  }
  EXPECT_EQ(segListByIntf[InterfaceID(1)], sidList1);
  EXPECT_EQ(segListByIntf[InterfaceID(2)], sidList2);
}

// SRv6-over-SRv6 recursion: outer route R1 carries sidList1 on both next hops,
// which resolve over two distinct OpenR routes R2/R3 whose link-local next hops
// carry their own SID lists. The outer sidList1 overrides the inner ones.
TEST(Route, resolveRecursiveSrv6OuterSidListThroughTwoOpenrRoutes) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("fdad:ffff:0001:0002::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("fdad:ffff:0003:0004::")};
  const std::vector<folly::IPAddressV6> sidList3{
      folly::IPAddressV6("fdad:ffff:0005:0006::")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  // 1. OpenR routes R2 (d:0) and R3 (c:0), each with one link-local next hop
  //    carrying its own SID list.
  RouteNextHopSet openrNhopsD{ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};
  RouteNextHopSet openrNhopsC{ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList3,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};

  // 2. OpenR route R1 carrying sidList1 on both next hops, resolving over
  // R2/R3.
  RouteNextHopSet r1Nhops{
      UnresolvedNextHop(
          IPAddress("fdad:feff:0202::0:d:0"),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidList1,
          TunnelType::SRV6_ENCAP,
          kSrv6Tunnel0),
      UnresolvedNextHop(
          IPAddress("fdad:feff:0202::0:c:0"),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidList1,
          TunnelType::SRV6_ENCAP,
          kSrv6Tunnel0)};

  RouteV6::Prefix r1Prefix{IPAddressV6("2001::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("fdad:feff:0202::0:d:0"), 112},
           RouteNextHopEntry(openrNhopsD, AdminDistance::OPENR)},
          {{IPAddress("fdad:feff:0202::0:c:0"), 112},
           RouteNextHopEntry(openrNhopsC, AdminDistance::OPENR)},
          {{r1Prefix.network(), r1Prefix.mask()},
           RouteNextHopEntry(r1Nhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // 3. Both resolved leaves carry the outer sidList1, not the inner SID lists.
  auto it = v6Routes.exactMatch(r1Prefix.network(), r1Prefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  const auto& resolvedNhops = it->value()->getForwardInfo().getNextHopSet();
  ASSERT_EQ(resolvedNhops.size(), 2);

  std::set<InterfaceID> intfs;
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.srv6SegmentList(), sidList1);
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    intfs.insert(nh.intf());
  }
  EXPECT_TRUE(intfs.count(InterfaceID(1)));
  EXPECT_TRUE(intfs.count(InterfaceID(2)));
}

// SRv6 recursive-resolution encap-merge matrix. `resolveSrv6Encap` prefers the
// parent next hop's SID list and inherits the child's only when the parent has
// none. It runs at both `getFwdInfoFromNhop` call sites: the connected branch
// (next hop resolves directly over an interface route) and the recursive branch
// (next hop resolves over a non-connected route). Full 3x2 coverage:
//   parent-only : connected -> resolveEcmpRouteWithSrv6NextHops (et al.)
//                 recursive -> resolveRecursiveSrv6ParentOnlyKeepsParent
//   child-only  : connected -> resolveConnectedSrv6ParentNoneInheritsChild
//                 recursive -> resolveRecursiveSrv6InnerSidList* (above)
//   both        : connected -> resolveConnectedSrv6BothPrefersParent
//                 recursive -> resolveRecursiveSrv6OuterSidList* (above)

// Connected branch, child-only: parent next hop has no SID list and resolves
// directly over an interface route whose next hop carries one -> inherit child.
TEST(Route, resolveConnectedSrv6ParentNoneInheritsChild) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> childSidList{
      folly::IPAddressV6("2001:db8:c::1")};

  // Interface (connected) route whose next hop carries a SID list.
  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      childSidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Parent route with a plain next hop resolving directly over the connected
  // route.
  RouteNextHopSet parentNhops{
      UnresolvedNextHop(IPAddress("fc00:1::10"), ECMP_WEIGHT)};
  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()},
           RouteNextHopEntry(parentNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, it->value()->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 1);

  const auto& nh = *resolvedNhops.begin();
  EXPECT_EQ(nh.intf(), InterfaceID(1));
  EXPECT_EQ(nh.srv6SegmentList(), childSidList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
}

// Connected branch, both: parent next hop and the connected interface route's
// next hop both carry SID lists -> parent's is preferred.
TEST(Route, resolveConnectedSrv6BothPrefersParent) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> parentSidList{
      folly::IPAddressV6("2001:db8:a::1")};
  const std::vector<folly::IPAddressV6> childSidList{
      folly::IPAddressV6("2001:db8:c::1")};

  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      childSidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet parentNhops{UnresolvedNextHop(
      IPAddress("fc00:1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      parentSidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};
  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()},
           RouteNextHopEntry(parentNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, it->value()->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 1);

  const auto& nh = *resolvedNhops.begin();
  EXPECT_EQ(nh.intf(), InterfaceID(1));
  EXPECT_EQ(nh.srv6SegmentList(), parentSidList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
}

// Recursive branch, parent-only: parent next hop carries a SID list and
// resolves over a non-connected route whose next hop has none -> keep parent's.
TEST(Route, resolveRecursiveSrv6ParentOnlyKeepsParent) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> parentSidList{
      folly::IPAddressV6("2001:db8:a::1")};

  // Connected interface route (no SID list).
  RouteNextHopSet intfNhop;
  intfNhop.emplace(ResolvedNextHop(
      IPAddress("fc00:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT));

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("fc00:1::"), 64},
           RouteNextHopEntry(intfNhop, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  // Intermediate (non-connected) route with a plain next hop (no SID list).
  RouteNextHopSet intermediateNhops{
      UnresolvedNextHop(IPAddress("fc00:1::10"), ECMP_WEIGHT)};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{IPAddress("fc00:10::"), 32},
           RouteNextHopEntry(intermediateNhops, kDistance)},
      },
      {},
      false);

  // Parent route whose next hop carries a SID list, resolving recursively over
  // the intermediate route.
  RouteNextHopSet parentNhops{UnresolvedNextHop(
      IPAddress("fc00:10::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      parentSidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};
  RouteV6::Prefix r1{IPAddressV6("2800:2::"), 64};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA,
      {
          {{r1.network(), r1.mask()},
           RouteNextHopEntry(parentNhops, kDistance)},
      },
      {},
      false);

  auto it = v6Routes.exactMatch(r1.network(), r1.mask());
  ASSERT_NE(v6Routes.end(), it);
  EXPECT_TRUE(it->value()->isResolved());
  auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, it->value()->getForwardInfo());
  ASSERT_EQ(resolvedNhops.size(), 1);

  const auto& nh = *resolvedNhops.begin();
  EXPECT_EQ(nh.intf(), InterfaceID(1));
  EXPECT_EQ(nh.srv6SegmentList(), parentSidList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
}

TEST(Route, RecursiveResolutionInheritsRoleFromTopLevelNextHop) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;
  RibRouteUpdater updater(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  RouteNextHopSet interfaceNhops1{ResolvedNextHop(
      IPAddress("2001:db8:1::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT)};
  RouteNextHopSet interfaceNhops2{ResolvedNextHop(
      IPAddress("2001:db8:2::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT)};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("2001:db8:1::"), 64},
           RouteNextHopEntry(
               interfaceNhops1, AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2001:db8:2::"), 64},
           RouteNextHopEntry(
               interfaceNhops2, AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet openrNhops1{
      UnresolvedNextHop(IPAddress("2001:db8:1::10"), ECMP_WEIGHT)};
  RouteNextHopSet openrNhops2{
      UnresolvedNextHop(IPAddress("2001:db8:2::10"), ECMP_WEIGHT)};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("2001:db8:3::"), 64},
           RouteNextHopEntry(openrNhops1, AdminDistance::OPENR)},
          {{IPAddress("2001:db8:4::"), 64},
           RouteNextHopEntry(openrNhops2, AdminDistance::OPENR)},
      },
      {},
      false);

  NextHop primaryBgpNextHop =
      UnresolvedNextHop(IPAddress("2001:db8:3::10"), ECMP_WEIGHT);
  NextHop backupBgpNextHop =
      UnresolvedNextHop(IPAddress("2001:db8:4::10"), ECMP_WEIGHT);
  auto backupBgpNextHopThrift = backupBgpNextHop.toThrift();
  *backupBgpNextHopThrift.role() = NextHopRole::BACKUP;
  RouteNextHopSet bgpNhops{
      primaryBgpNextHop, util::fromThrift(backupBgpNextHopThrift)};
  RouteV6::Prefix bgpPrefix{IPAddressV6("2001:db8:5::"), 64};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {{{bgpPrefix.network(), bgpPrefix.mask()},
        RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)}},
      {},
      false);

  auto route = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(route, v6Routes.end());
  const auto& forwardInfo = route->value()->getForwardInfo();
  const auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, forwardInfo);
  const std::map<InterfaceID, NextHopRole> expectedRoles{
      {InterfaceID(1), NextHopRole::PRIMARY},
      {InterfaceID(2), NextHopRole::BACKUP},
  };
  auto rolesByInterface = [](const RouteNextHopSet& nhops) {
    std::map<InterfaceID, NextHopRole> roles;
    for (const auto& nhop : nhops) {
      roles.emplace(nhop.intf(), nhop.role());
    }
    return roles;
  };
  EXPECT_EQ(rolesByInterface(resolvedNhops), expectedRoles);

  const auto normalizedID = forwardInfo.getNormalizedResolvedNextHopSetID();
  ASSERT_TRUE(normalizedID.has_value());
  const auto normalizedNhops = nhopIds.getNextHops(*normalizedID);
  EXPECT_EQ(rolesByInterface(normalizedNhops), expectedRoles);
}

TEST(Route, RecursiveResolutionAppliesBackupRoleToAllResolvedNextHops) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;
  RibRouteUpdater updater(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("2001:db8:11::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001:db8:11::1"),
                   InterfaceID(1),
                   UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2001:db8:12::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001:db8:12::1"),
                   InterfaceID(2),
                   UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2001:db8:13::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001:db8:13::1"),
                   InterfaceID(3),
                   UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet primaryOpenrNhops{
      UnresolvedNextHop(IPAddress("2001:db8:11::10"), ECMP_WEIGHT)};
  RouteNextHopSet backupOpenrNhops{
      UnresolvedNextHop(IPAddress("2001:db8:12::10"), ECMP_WEIGHT),
      UnresolvedNextHop(IPAddress("2001:db8:13::10"), ECMP_WEIGHT)};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("2001:db8:21::"), 64},
           RouteNextHopEntry(primaryOpenrNhops, AdminDistance::OPENR)},
          {{IPAddress("2001:db8:22::"), 64},
           RouteNextHopEntry(backupOpenrNhops, AdminDistance::OPENR)},
      },
      {},
      false);

  NextHop primaryBgpNextHop =
      UnresolvedNextHop(IPAddress("2001:db8:21::10"), ECMP_WEIGHT);
  NextHop backupBgpNextHop =
      UnresolvedNextHop(IPAddress("2001:db8:22::10"), ECMP_WEIGHT);
  auto backupBgpNextHopThrift = backupBgpNextHop.toThrift();
  *backupBgpNextHopThrift.role() = NextHopRole::BACKUP;
  RouteNextHopSet bgpNhops{
      primaryBgpNextHop, util::fromThrift(backupBgpNextHopThrift)};
  RouteV6::Prefix bgpPrefix{IPAddressV6("2001:db8:31::"), 64};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {{{bgpPrefix.network(), bgpPrefix.mask()},
        RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)}},
      {},
      false);

  auto route = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(route, v6Routes.end());
  const auto& forwardInfo = route->value()->getForwardInfo();
  const std::map<InterfaceID, NextHopRole> expectedRoles{
      {InterfaceID(1), NextHopRole::PRIMARY},
      {InterfaceID(2), NextHopRole::BACKUP},
      {InterfaceID(3), NextHopRole::BACKUP},
  };
  auto rolesByInterface = [](const RouteNextHopSet& nhops) {
    std::map<InterfaceID, NextHopRole> roles;
    for (const auto& nhop : nhops) {
      roles.emplace(nhop.intf(), nhop.role());
    }
    return roles;
  };

  const auto resolvedNhops = getResolvedNextHopsFromRib(&nhopIds, forwardInfo);
  EXPECT_EQ(rolesByInterface(resolvedNhops), expectedRoles);

  const auto normalizedID = forwardInfo.getNormalizedResolvedNextHopSetID();
  ASSERT_TRUE(normalizedID.has_value());
  EXPECT_EQ(
      rolesByInterface(nhopIds.getNextHops(*normalizedID)), expectedRoles);

  auto swappedBackupThrift = primaryBgpNextHop.toThrift();
  *swappedBackupThrift.role() = NextHopRole::BACKUP;
  RouteNextHopSet swappedBgpNhops{
      util::fromThrift(swappedBackupThrift), backupBgpNextHop};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {{{bgpPrefix.network(), bgpPrefix.mask()},
        RouteNextHopEntry(swappedBgpNhops, AdminDistance::EBGP)}},
      {},
      false);

  route = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(route, v6Routes.end());
  const auto& swappedForwardInfo = route->value()->getForwardInfo();
  const std::map<InterfaceID, NextHopRole> swappedExpectedRoles{
      {InterfaceID(1), NextHopRole::BACKUP},
      {InterfaceID(2), NextHopRole::PRIMARY},
      {InterfaceID(3), NextHopRole::PRIMARY},
  };
  EXPECT_EQ(
      rolesByInterface(
          getResolvedNextHopsFromRib(&nhopIds, swappedForwardInfo)),
      swappedExpectedRoles);

  const auto swappedNormalizedID =
      swappedForwardInfo.getNormalizedResolvedNextHopSetID();
  ASSERT_TRUE(swappedNormalizedID.has_value());
  EXPECT_EQ(
      rolesByInterface(nhopIds.getNextHops(*swappedNormalizedID)),
      swappedExpectedRoles);
}

// This recursive topology mimics the production use case in an NSF deployment.
TEST(Route, RecursiveBgpRoutePreservesTopLevelNextHopRoles) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;
  RibRouteUpdater updater(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("2001::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001::1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("3001::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("3001::1"), InterfaceID(2), UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("4001::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("4001::1"), InterfaceID(3), UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  RouteNextHopSet intermediateNhops{
      UnresolvedNextHop(IPAddress("3001::1"), ECMP_WEIGHT),
      UnresolvedNextHop(IPAddress("4001::1"), ECMP_WEIGHT)};
  NextHop backupNextHop =
      UnresolvedNextHop(IPAddress("1001:1:2:3::1"), ECMP_WEIGHT);
  auto backupNextHopThrift = backupNextHop.toThrift();
  *backupNextHopThrift.role() = NextHopRole::BACKUP;
  RouteNextHopSet topLevelNhops{
      UnresolvedNextHop(IPAddress("2001::1"), ECMP_WEIGHT),
      util::fromThrift(backupNextHopThrift)};
  RouteV6::Prefix intermediatePrefix{IPAddressV6("1001:1:2:3::"), 64};
  RouteV6::Prefix topLevelPrefix{IPAddressV6("5001::"), 56};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {
          {{intermediatePrefix.network(), intermediatePrefix.mask()},
           RouteNextHopEntry(intermediateNhops, AdminDistance::EBGP)},
          {{topLevelPrefix.network(), topLevelPrefix.mask()},
           RouteNextHopEntry(topLevelNhops, AdminDistance::EBGP)},
      },
      {},
      false);

  auto route =
      v6Routes.exactMatch(topLevelPrefix.network(), topLevelPrefix.mask());
  ASSERT_NE(route, v6Routes.end());
  const auto resolvedNhops =
      getResolvedNextHopsFromRib(&nhopIds, route->value()->getForwardInfo());
  std::map<IPAddress, NextHopRole> rolesByAddress;
  for (const auto& nextHop : resolvedNhops) {
    rolesByAddress.emplace(nextHop.addr(), nextHop.role());
  }
  const std::map<IPAddress, NextHopRole> expectedRoles{
      {IPAddress("2001::1"), NextHopRole::PRIMARY},
      {IPAddress("3001::1"), NextHopRole::BACKUP},
      {IPAddress("4001::1"), NextHopRole::BACKUP},
  };
  EXPECT_EQ(rolesByAddress, expectedRoles);
}

namespace {

using AddressAndRole = std::vector<std::pair<IPAddress, NextHopRole>>;

NextHop makeBackupNextHop(const char* address) {
  const NextHop nextHop = UnresolvedNextHop(IPAddress(address), ECMP_WEIGHT);
  auto nextHopThrift = nextHop.toThrift();
  *nextHopThrift.role() = NextHopRole::BACKUP;
  return util::fromThrift(nextHopThrift);
}

/*
 * Sorted (address, role) pairs. Unlike a map keyed by address this preserves
 * duplicates, so a backup that should have been pruned is still visible
 * alongside the primary it duplicates.
 */
AddressAndRole addressRoles(const RouteNextHopSet& nextHops) {
  AddressAndRole roles;
  for (const auto& nextHop : nextHops) {
    roles.emplace_back(nextHop.addr(), nextHop.role());
  }
  std::sort(roles.begin(), roles.end());
  return roles;
}

RouteNextHopSet merged(
    const RouteNextHopSet& primaryNextHops,
    const RouteNextHopSet& backupNextHops) {
  RouteNextHopSet allNextHops = primaryNextHops;
  allNextHops.insert(backupNextHops.begin(), backupNextHops.end());
  return allNextHops;
}

} // namespace

/*
 * 10::/64, 20::/64, 30::/64 and 40::/64 are interface routes, and 100::/64
 * recursively resolves to all four of them. A BGP route using 100::1 as a
 * backup next hop therefore picks up 10::1, 20::2, 30::3 and 40::4 as backups,
 * any of which may duplicate a primary next hop on the same route.
 */
class PrimaryBackupDedupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto interfaceRoute = [](const char* address, InterfaceID interface) {
      return RouteNextHopEntry(
          RouteNextHopSet{ResolvedNextHop(
              IPAddress(address), interface, UCMP_DEFAULT_WEIGHT)},
          AdminDistance::DIRECTLY_CONNECTED);
    };
    updater_.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
        ClientID::INTERFACE_ROUTE,
        {
            {{IPAddress("10::"), 64}, interfaceRoute("10::1", InterfaceID(1))},
            {{IPAddress("20::"), 64}, interfaceRoute("20::2", InterfaceID(2))},
            {{IPAddress("30::"), 64}, interfaceRoute("30::3", InterfaceID(3))},
            {{IPAddress("40::"), 64}, interfaceRoute("40::4", InterfaceID(4))},
        },
        {},
        false);

    updater_.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
        ClientID::OPENR,
        {{{IPAddress("100::"), 64},
          RouteNextHopEntry(
              RouteNextHopSet{
                  UnresolvedNextHop(IPAddress("10::1"), ECMP_WEIGHT),
                  UnresolvedNextHop(IPAddress("20::2"), ECMP_WEIGHT),
                  UnresolvedNextHop(IPAddress("30::3"), ECMP_WEIGHT),
                  UnresolvedNextHop(IPAddress("40::4"), ECMP_WEIGHT)},
              AdminDistance::OPENR)}},
        {},
        false);
  }

  /*
   * Programs bgpNextHops over prefix, replacing whatever was programmed there
   * before, and returns the resolved (address, role) pairs. The normalized
   * next hop set is expected to agree with the resolved one.
   */
  AddressAndRole programBgpRoute(
      const RouteV6::Prefix& prefix,
      const RouteNextHopSet& bgpNextHops) {
    updater_.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
        ClientID::BGPD,
        {{{prefix.network(), prefix.mask()},
          RouteNextHopEntry(bgpNextHops, AdminDistance::EBGP)}},
        {},
        false);

    const auto route = v6Routes_.exactMatch(prefix.network(), prefix.mask());
    EXPECT_NE(route, v6Routes_.end());
    if (route == v6Routes_.end()) {
      return {};
    }
    const auto& forwardInfo = route->value()->getForwardInfo();
    const auto resolved =
        addressRoles(getResolvedNextHopsFromRib(&nhopIds_, forwardInfo));

    const auto normalizedID = forwardInfo.getNormalizedResolvedNextHopSetID();
    EXPECT_TRUE(normalizedID.has_value());
    if (normalizedID.has_value()) {
      EXPECT_EQ(addressRoles(nhopIds_.getNextHops(*normalizedID)), resolved);
    }
    return resolved;
  }

  // Programs prefix as a DROP route, i.e. reachable but with no next hops.
  void programDropRoute(const RouteV6::Prefix& prefix) {
    updater_.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
        ClientID::BGPD,
        {{{prefix.network(), prefix.mask()},
          RouteNextHopEntry(RouteForwardAction::DROP, AdminDistance::EBGP)}},
        {},
        false);

    const auto route = v6Routes_.exactMatch(prefix.network(), prefix.mask());
    EXPECT_NE(route, v6Routes_.end());
    if (route != v6Routes_.end()) {
      EXPECT_EQ(
          route->value()->getForwardInfo().getAction(),
          RouteForwardAction::DROP);
    }
  }

  /*
   * Dedup must depend only on the final contents of a route, never on the
   * order its next hops arrived in. Drives the same primary/backup pair to its
   * final state three ways, each on its own prefix, and requires all three to
   * converge on expectedRoles. When startFromDrop is set every prefix is first
   * programmed as a DROP route, so each ordering also covers the transition
   * out of DROP.
   */
  void verifyArrivalOrderIndependence(
      const RouteNextHopSet& primaryNextHops,
      const RouteNextHopSet& backupNextHops,
      const AddressAndRole& expectedRoles,
      bool startFromDrop = false) {
    const auto allNextHops = merged(primaryNextHops, backupNextHops);
    auto seedRoute = [&](const RouteV6::Prefix& prefix) {
      if (startFromDrop) {
        programDropRoute(prefix);
      }
    };
    // Asserted on the intermediate states so that a first update which
    // silently did nothing cannot make an ordering degenerate into the
    // both-together case and still pass.
    auto haveOnlyRole = [](const AddressAndRole& addressAndRoles,
                           NextHopRole role) {
      return !addressAndRoles.empty() &&
          std::all_of(
              addressAndRoles.begin(),
              addressAndRoles.end(),
              [role](const auto& addressAndRole) {
                return addressAndRole.second == role;
              });
    };

    // Primaries arrive first, then the backup next hop is added.
    const RouteV6::Prefix primariesFirst{IPAddressV6("200::"), 64};
    seedRoute(primariesFirst);
    EXPECT_TRUE(haveOnlyRole(
        programBgpRoute(primariesFirst, primaryNextHops),
        NextHopRole::PRIMARY));
    EXPECT_EQ(programBgpRoute(primariesFirst, allNextHops), expectedRoles)
        << "primaries before backups";

    // The backup next hop arrives first, then the primaries are added.
    const RouteV6::Prefix backupsFirst{IPAddressV6("201::"), 64};
    seedRoute(backupsFirst);
    EXPECT_TRUE(haveOnlyRole(
        programBgpRoute(backupsFirst, backupNextHops), NextHopRole::BACKUP));
    EXPECT_EQ(programBgpRoute(backupsFirst, allNextHops), expectedRoles)
        << "backups before primaries";

    // Primaries and the backup next hop arrive together.
    const RouteV6::Prefix together{IPAddressV6("202::"), 64};
    seedRoute(together);
    EXPECT_EQ(programBgpRoute(together, allNextHops), expectedRoles)
        << "primaries and backups together";
  }

  IPv4NetworkToRouteMap v4Routes_;
  IPv6NetworkToRouteMap v6Routes_;
  NextHopIDManager nhopIds_;
  RibRouteUpdater updater_{
      &v4Routes_,
      &v6Routes_,
      &nhopIds_,
      nullptr,
      kEcmpWidth};
};

TEST_F(PrimaryBackupDedupTest, BackupNextHopsWithoutAnyPrimaryAreNotPruned) {
  const RouteV6::Prefix prefix{IPAddressV6("200::"), 64};
  EXPECT_EQ(
      programBgpRoute(prefix, RouteNextHopSet{makeBackupNextHop("100::1")}),
      AddressAndRole({
          {IPAddress("10::1"), NextHopRole::BACKUP},
          {IPAddress("20::2"), NextHopRole::BACKUP},
          {IPAddress("30::3"), NextHopRole::BACKUP},
          {IPAddress("40::4"), NextHopRole::BACKUP},
      }));
}

TEST_F(PrimaryBackupDedupTest, PrimaryNextHopsWithoutAnyBackupAreNotPruned) {
  const RouteV6::Prefix prefix{IPAddressV6("200::"), 64};
  EXPECT_EQ(
      programBgpRoute(
          prefix,
          RouteNextHopSet{
              UnresolvedNextHop(IPAddress("10::1"), ECMP_WEIGHT),
              UnresolvedNextHop(IPAddress("20::2"), ECMP_WEIGHT)}),
      AddressAndRole({
          {IPAddress("10::1"), NextHopRole::PRIMARY},
          {IPAddress("20::2"), NextHopRole::PRIMARY},
      }));
}

TEST_F(PrimaryBackupDedupTest, BackupPrunedForMatchingPrimaryInAnyOrder) {
  verifyArrivalOrderIndependence(
      RouteNextHopSet{UnresolvedNextHop(IPAddress("10::1"), ECMP_WEIGHT)},
      RouteNextHopSet{makeBackupNextHop("100::1")},
      {
          {IPAddress("10::1"), NextHopRole::PRIMARY},
          {IPAddress("20::2"), NextHopRole::BACKUP},
          {IPAddress("30::3"), NextHopRole::BACKUP},
          {IPAddress("40::4"), NextHopRole::BACKUP},
      });
}

TEST_F(PrimaryBackupDedupTest, BackupPrunedForLaterRecursiveNextHopInAnyOrder) {
  verifyArrivalOrderIndependence(
      RouteNextHopSet{UnresolvedNextHop(IPAddress("20::2"), ECMP_WEIGHT)},
      RouteNextHopSet{makeBackupNextHop("100::1")},
      {
          {IPAddress("10::1"), NextHopRole::BACKUP},
          {IPAddress("20::2"), NextHopRole::PRIMARY},
          {IPAddress("30::3"), NextHopRole::BACKUP},
          {IPAddress("40::4"), NextHopRole::BACKUP},
      });
}

TEST_F(PrimaryBackupDedupTest, EveryBackupMatchingAPrimaryPrunedInAnyOrder) {
  verifyArrivalOrderIndependence(
      RouteNextHopSet{
          UnresolvedNextHop(IPAddress("20::2"), ECMP_WEIGHT),
          UnresolvedNextHop(IPAddress("30::3"), ECMP_WEIGHT)},
      RouteNextHopSet{makeBackupNextHop("100::1")},
      {
          {IPAddress("10::1"), NextHopRole::BACKUP},
          {IPAddress("20::2"), NextHopRole::PRIMARY},
          {IPAddress("30::3"), NextHopRole::PRIMARY},
          {IPAddress("40::4"), NextHopRole::BACKUP},
      });
}

TEST_F(PrimaryBackupDedupTest, DropTransitionsToPrimaryAndBackupInAnyOrder) {
  verifyArrivalOrderIndependence(
      RouteNextHopSet{UnresolvedNextHop(IPAddress("10::1"), ECMP_WEIGHT)},
      RouteNextHopSet{makeBackupNextHop("100::1")},
      {
          {IPAddress("10::1"), NextHopRole::PRIMARY},
          {IPAddress("20::2"), NextHopRole::BACKUP},
          {IPAddress("30::3"), NextHopRole::BACKUP},
          {IPAddress("40::4"), NextHopRole::BACKUP},
      },
      /*startFromDrop=*/true);
}

void verifyBgpNextHopRolesOverrideOpenrNextHopRoles(
    NextHopRole secondBgpNextHopRole,
    const std::map<InterfaceID, NextHopRole>& expectedRoles) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;
  RibRouteUpdater updater(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::INTERFACE_ROUTE,
      {
          {{IPAddress("2001:db8:41::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001:db8:41::1"),
                   InterfaceID(1),
                   UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
          {{IPAddress("2001:db8:42::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{ResolvedNextHop(
                   IPAddress("2001:db8:42::1"),
                   InterfaceID(2),
                   UCMP_DEFAULT_WEIGHT)},
               AdminDistance::DIRECTLY_CONNECTED)},
      },
      {},
      false);

  NextHop openrNextHop1 =
      UnresolvedNextHop(IPAddress("2001:db8:41::10"), ECMP_WEIGHT);
  auto openrNextHop1Thrift = openrNextHop1.toThrift();
  *openrNextHop1Thrift.role() = NextHopRole::BACKUP;
  NextHop openrNextHop2 =
      UnresolvedNextHop(IPAddress("2001:db8:42::10"), ECMP_WEIGHT);
  auto openrNextHop2Thrift = openrNextHop2.toThrift();
  *openrNextHop2Thrift.role() = NextHopRole::BACKUP;
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{IPAddress("2001:db8:43::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{util::fromThrift(openrNextHop1Thrift)},
               AdminDistance::OPENR)},
          {{IPAddress("2001:db8:44::"), 64},
           RouteNextHopEntry(
               RouteNextHopSet{util::fromThrift(openrNextHop2Thrift)},
               AdminDistance::OPENR)},
      },
      {},
      false);

  NextHop bgpNextHop1 =
      UnresolvedNextHop(IPAddress("2001:db8:43::10"), ECMP_WEIGHT);
  NextHop bgpNextHop2 =
      UnresolvedNextHop(IPAddress("2001:db8:44::10"), ECMP_WEIGHT);
  auto bgpNextHop2Thrift = bgpNextHop2.toThrift();
  *bgpNextHop2Thrift.role() = secondBgpNextHopRole;
  RouteNextHopSet bgpNhops{bgpNextHop1, util::fromThrift(bgpNextHop2Thrift)};
  RouteV6::Prefix bgpPrefix{IPAddressV6("2001:db8:45::"), 64};
  updater.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::BGPD,
      {{{bgpPrefix.network(), bgpPrefix.mask()},
        RouteNextHopEntry(bgpNhops, AdminDistance::EBGP)}},
      {},
      false);

  auto rolesByInterface = [](const RouteNextHopSet& nhops) {
    std::map<InterfaceID, NextHopRole> roles;
    for (const auto& nhop : nhops) {
      roles.emplace(nhop.intf(), nhop.role());
    }
    return roles;
  };
  auto route = v6Routes.exactMatch(bgpPrefix.network(), bgpPrefix.mask());
  ASSERT_NE(route, v6Routes.end());
  const auto& forwardInfo = route->value()->getForwardInfo();
  EXPECT_EQ(
      rolesByInterface(getResolvedNextHopsFromRib(&nhopIds, forwardInfo)),
      expectedRoles);

  const auto normalizedID = forwardInfo.getNormalizedResolvedNextHopSetID();
  ASSERT_TRUE(normalizedID.has_value());
  EXPECT_EQ(
      rolesByInterface(nhopIds.getNextHops(*normalizedID)), expectedRoles);
}

TEST(Route, BGPNextHopRoleOverridesOpenrNextHopRoles) {
  {
    const std::map<InterfaceID, NextHopRole> expectedRoles{
        {InterfaceID(1), NextHopRole::PRIMARY},
        {InterfaceID(2), NextHopRole::BACKUP},
    };
    verifyBgpNextHopRolesOverrideOpenrNextHopRoles(
        NextHopRole::BACKUP, expectedRoles);
  }

  {
    const std::map<InterfaceID, NextHopRole> expectedRoles{
        {InterfaceID(1), NextHopRole::PRIMARY},
        {InterfaceID(2), NextHopRole::PRIMARY},
    };
    verifyBgpNextHopRolesOverrideOpenrNextHopRoles(
        NextHopRole::PRIMARY, expectedRoles);
  }
}

// Same-prefix client preference: an OpenR route (no SID lists) and a TE_Agent
// route (with SID lists) share a prefix. The TE_Agent route has the lower admin
// distance, so it wins best-entry selection and its SID lists are programmed.
TEST(Route, srv6TeAgentRoutePreferredOverOpenrByAdminDistance) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;

  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("fdad:ffff:0001:0002::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("fdad:ffff:0003:0004::")};

  NextHopIDManager nhopIds;
  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);

  RouteV6::Prefix prefix{IPAddressV6("2001::"), 64};

  // 1. OpenR route for the prefix with two link-local next hops, no SID lists.
  RouteNextHopSet openrNhops{
      ResolvedNextHop(IPAddress("fe80::1"), InterfaceID(1), ECMP_WEIGHT),
      ResolvedNextHop(IPAddress("fe80::2"), InterfaceID(2), ECMP_WEIGHT)};
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::OPENR,
      {
          {{prefix.network(), prefix.mask()},
           RouteNextHopEntry(openrNhops, AdminDistance::OPENR)},
      },
      {},
      false);

  // 2. TE_Agent route for the SAME prefix with the same next hops, but each
  //    carrying its own SID list. Lower admin distance than OpenR.
  RouteNextHopSet teNhops;
  teNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::1"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  teNhops.emplace(ResolvedNextHop(
      IPAddress("fe80::2"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      ClientID::TE_AGENT,
      {
          {{prefix.network(), prefix.mask()},
           RouteNextHopEntry(teNhops, AdminDistance::TE_AGENT)},
      },
      {},
      false);

  // 3. TE_Agent entry wins (admin distance 2 < OpenR's 10), so the resolved
  //    next hops carry the TE_Agent SID lists.
  auto it = v6Routes.exactMatch(prefix.network(), prefix.mask());
  ASSERT_NE(v6Routes.end(), it);
  auto route = it->value();
  EXPECT_TRUE(route->isResolved());
  EXPECT_EQ(route->getBestEntry().first, ClientID::TE_AGENT);
  EXPECT_EQ(
      route->getForwardInfo().getAdminDistance(), AdminDistance::TE_AGENT);

  const auto& resolvedNhops = route->getForwardInfo().getNextHopSet();
  ASSERT_EQ(resolvedNhops.size(), 2);
  for (const auto& nh : resolvedNhops) {
    EXPECT_TRUE(nh.isResolved());
    EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
    if (nh.intf() == InterfaceID(1)) {
      EXPECT_EQ(nh.srv6SegmentList(), sidList1);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else if (nh.intf() == InterfaceID(2)) {
      EXPECT_EQ(nh.srv6SegmentList(), sidList2);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      FAIL() << "Unexpected interface on resolved next hop";
    }
  }
}

TEST(RibRouteTables, getVrfList) {
  RoutingInformationBase rib;

  // Initially no VRFs
  EXPECT_TRUE(rib.getVrfList().empty());

  // Add a single VRF
  rib.ensureVrf(RouterID(0));
  auto vrfList = rib.getVrfList();
  EXPECT_EQ(vrfList.size(), 1);
  EXPECT_EQ(vrfList[0], RouterID(0));

  // Add more VRFs
  rib.ensureVrf(RouterID(1));
  rib.ensureVrf(RouterID(2));
  vrfList = rib.getVrfList();
  EXPECT_EQ(vrfList.size(), 3);

  // Verify all VRFs are present
  std::set<RouterID> vrfSet(vrfList.begin(), vrfList.end());
  EXPECT_EQ(vrfSet.count(RouterID(0)), 1);
  EXPECT_EQ(vrfSet.count(RouterID(1)), 1);
  EXPECT_EQ(vrfSet.count(RouterID(2)), 1);

  // Ensure duplicate VRF does not add a new entry
  rib.ensureVrf(RouterID(1));
  vrfList = rib.getVrfList();
  EXPECT_EQ(vrfList.size(), 3);
}

TEST(Route, cycleDetectionPopulatesUpdateStatistics) {
  RoutingInformationBase rib;
  rib.ensureVrf(RouterID(0));

  // One v4 cycle: 10.0.0.0/24 -> 20.0.0.1, 20.0.0.0/24 -> 10.0.0.1
  // One v6 cycle: 2001::/64  -> 3001::1,  3001::/64  -> 2001::1
  auto stats = rib.update(
      nullptr,
      RouterID(0),
      ClientID::BGPD,
      AdminDistance::EBGP,
      {
          makeUnicastRoute(
              {IPAddress("10.0.0.0"), 24}, {IPAddress("20.0.0.1")}),
          makeUnicastRoute(
              {IPAddress("20.0.0.0"), 24}, {IPAddress("10.0.0.1")}),
          makeUnicastRoute({IPAddress("2001::"), 64}, {IPAddress("3001::1")}),
          makeUnicastRoute({IPAddress("3001::"), 64}, {IPAddress("2001::1")}),
      },
      {},
      false,
      "cycle detection test",
      noopFibUpdate,
      nullptr);

  EXPECT_EQ(stats.resolutionCyclesDetected, 2u);
}

TEST(Route, noCycleDetectedForNonCyclicRoutes) {
  RoutingInformationBase rib;
  rib.ensureVrf(RouterID(0));

  // Non-cyclic routes that still exercise the recursive resolution path:
  //   v4: 10.0.0.0/24 -> 1.1.1.5  (resolves via 1.1.1.0/24)
  //       1.1.1.0/24  -> 2.2.2.1  (leaf with no covering route)
  //   v6: 2001::/64   -> 3001::5  (resolves via 3001::/64)
  //       3001::/64   -> 4001::1  (leaf with no covering route)
  // No nexthop chain loops back, so the cycle counter must stay 0.
  auto stats = rib.update(
      nullptr,
      RouterID(0),
      ClientID::BGPD,
      AdminDistance::EBGP,
      {
          makeUnicastRoute({IPAddress("10.0.0.0"), 24}, {IPAddress("1.1.1.5")}),
          makeUnicastRoute({IPAddress("1.1.1.0"), 24}, {IPAddress("2.2.2.1")}),
          makeUnicastRoute({IPAddress("2001::"), 64}, {IPAddress("3001::5")}),
          makeUnicastRoute({IPAddress("3001::"), 64}, {IPAddress("4001::1")}),
      },
      {},
      false,
      "no cycle test",
      noopFibUpdate,
      nullptr);

  EXPECT_EQ(stats.resolutionCyclesDetected, 0u);
}

// Verify that the RouteUpdater release calls drop the manager refcount
// when a route carrying a clientNextHopSetID is deleted.
TEST(Route, clientIdReleasedOnDelete) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;

  RouteNextHopSet nhop = makeNextHops({"1.1.1.10"});
  RouteNextHopEntry entry(nhop, kDistance);
  RouteV4::Prefix prefix{IPAddressV4("10.1.1.0"), 24};

  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA, {{{prefix.network(), prefix.mask()}, entry}}, {}, false);

  auto routeIt = v4Routes.exactMatch(prefix.network(), prefix.mask());
  ASSERT_NE(routeIt, v4Routes.end());
  auto storedEntry = routeIt->value()->getEntryForClient(kClientA);
  ASSERT_NE(storedEntry, nullptr);
  auto setId = storedEntry->getClientNextHopSetID();
  ASSERT_TRUE(setId.has_value());
  EXPECT_TRUE(nhopIds.getNextHopsIf(*setId).has_value());

  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA, {}, {{prefix.network(), prefix.mask()}}, false);
  EXPECT_FALSE(nhopIds.getNextHopsIf(*setId).has_value());
}

TEST(Route, clientIdReleasedOnResetAllRoutes) {
  IPv4NetworkToRouteMap v4Routes;
  IPv6NetworkToRouteMap v6Routes;
  NextHopIDManager nhopIds;

  RouteNextHopSet nhop = makeNextHops({"2.2.2.20"});
  RouteNextHopEntry entry(nhop, kDistance);
  RouteV4::Prefix prefix{IPAddressV4("30.1.1.0"), 24};

  RibRouteUpdater u(&v4Routes, &v6Routes, &nhopIds, nullptr, kEcmpWidth);
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA, {{{prefix.network(), prefix.mask()}, entry}}, {}, false);

  auto routeIt = v4Routes.exactMatch(prefix.network(), prefix.mask());
  ASSERT_NE(routeIt, v4Routes.end());
  auto storedEntry = routeIt->value()->getEntryForClient(kClientA);
  ASSERT_NE(storedEntry, nullptr);
  auto setId = storedEntry->getClientNextHopSetID();
  ASSERT_TRUE(setId.has_value());
  EXPECT_TRUE(nhopIds.getNextHopsIf(*setId).has_value());

  // resetClientsRoutes=true triggers removeAllUnclaimedRoutesFromClientImpl.
  u.update<RibRouteUpdater::RouteEntry, folly::CIDRNetwork>(
      kClientA, {}, {}, true);
  EXPECT_FALSE(nhopIds.getNextHopsIf(*setId).has_value());
}

} // namespace facebook::fboss
