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
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

using namespace facebook::fboss;

using facebook::fboss::AdminDistance;
using facebook::fboss::ECMP_WEIGHT;
using facebook::fboss::InterfaceID;
using facebook::fboss::IpPrefix;
using facebook::fboss::MplsAction;
using facebook::fboss::NextHopThrift;
using facebook::fboss::UnicastRoute;

DECLARE_bool(wide_ecmp);

namespace {

const AdminDistance kDefaultAdminDistance = AdminDistance::EBGP;

const std::string kSrv6Tunnel0{"srv6Tunnel0"};

const IpPrefix kDestPrefix = IpPrefix(
    apache::thrift::FRAGILE,
    facebook::network::toBinaryAddress(folly::IPAddress("fc00::")),
    7);

const folly::IPAddress nextHopAddr1 = folly::IPAddress("fe80::1");
const folly::IPAddress nextHopAddr2 =
    folly::IPAddress("2401:db00:e112:9103:1028::1b");
const folly::IPAddress nextHopAddr3 =
    folly::IPAddress("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff");
const folly::IPAddress nextHopAddr4 =
    folly::IPAddress("2401:db00:e113:9103:1028::1b");
const folly::IPAddress nextHopAddr5 =
    folly::IPAddress("2401:db00:e114:9103:1028::1b");
const folly::IPAddress nextHopAddr6 =
    folly::IPAddress("2401:db00:e115:9103:1028::1b");
const folly::IPAddress nextHopAddr7 =
    folly::IPAddress("2401:db00:e116:9103:1028::1b");
const folly::IPAddress nextHopAddr8 =
    folly::IPAddress("2401:db00:e117:9103:1028::1b");

UnresolvedNextHop makeSrv6NextHop(const folly::IPAddress& addr) {
  return UnresolvedNextHop(
      addr,
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {folly::IPAddressV6("2001:db8::1")},
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);
}

NetworkTopologyInformation getTopologyInfo() {
  NetworkTopologyInformation topologyInfo;
  topologyInfo.rack_id() = 2;
  topologyInfo.plane_id() = 1;
  topologyInfo.remote_rack_capacity() = 3;
  topologyInfo.spine_capacity() = 4;
  topologyInfo.local_rack_capacity() = 5;
  return topologyInfo;
}

// These next-hops are in our internal representation.
// Unlike other variables, it's not const so that it can be sorted
// in unit tests.
std::vector<NextHop> nextHops = {
    ResolvedNextHop(
        nextHopAddr1,
        InterfaceID(1),
        ECMP_WEIGHT,
        std::nullopt /*label*/,
        false, /*disableTTLdecrement*/
        getTopologyInfo(),
        0 /*adjustedWeight*/),
    UnresolvedNextHop(
        nextHopAddr2,
        ECMP_WEIGHT,
        std::nullopt, /*label*/
        false, /*disableTTLdecrement*/
        getTopologyInfo(),
        0 /*adjustedWeight*/),
    UnresolvedNextHop(
        nextHopAddr3,
        ECMP_WEIGHT,
        std::nullopt, /*label*/
        false, /*disableTTLdecrement*/
        getTopologyInfo(),
        0 /*adjustedWeight*/)};

std::vector<NextHop> nextHopsFromBinary = {
    ResolvedNextHop(nextHopAddr1, InterfaceID(1), ECMP_WEIGHT),
    UnresolvedNextHop(nextHopAddr2, ECMP_WEIGHT),
    UnresolvedNextHop(nextHopAddr3, ECMP_WEIGHT)};

// Any NextHopThrift received via Thrift is considered unresolved, with the
// sole exception of IPv6 link-local next-hops. For an IPv6 link-local
// next-hop to be considered resolved _prior_ to route resolution, it must
// have the field "3: optional string ifName" filled out in the BinaryAddress.
facebook::network::thrift::BinaryAddress createV6LinkLocalNextHop(
    const folly::IPAddress& linkLocalAddr) {
  CHECK(linkLocalAddr.isV6());

  auto linkLocalAddrAsBinaryAddress =
      facebook::network::toBinaryAddress(linkLocalAddr);

  linkLocalAddrAsBinaryAddress.ifName() = "fboss1";

  return linkLocalAddrAsBinaryAddress;
}

// These are the Thrift representations of nextHopAddr{1,2,3}.
std::vector<NextHopThrift> nextHopsThrift() {
  std::vector<NextHopThrift> nexthops;
  std::vector<folly::IPAddress> addrs{nextHopAddr1, nextHopAddr2, nextHopAddr3};
  for (const auto& addr : addrs) {
    NextHopThrift nexthop;
    *nexthop.address() = createV6LinkLocalNextHop(addr);
    *nexthop.weight() = static_cast<int32_t>(ECMP_WEIGHT);
    nexthop.disableTTLDecrement() = false;
    nexthop.adjustedWeight() = 0;
    NetworkTopologyInformation toplogyInfo;
    toplogyInfo.rack_id() = 2;
    toplogyInfo.plane_id() = 1;
    toplogyInfo.remote_rack_capacity() = 3;
    toplogyInfo.spine_capacity() = 4;
    toplogyInfo.local_rack_capacity() = 5;
    nexthop.topologyInfo() = toplogyInfo;
    nexthops.emplace_back(std::move(nexthop));
  }
  return nexthops;
}

// These are the _deprecated_ Thrift representations of nextHopAddr{1,2,3}.
const std::vector<facebook::network::thrift::BinaryAddress>
    nextHopsBinaryAddress = {
        createV6LinkLocalNextHop(nextHopAddr1),
        facebook::network::toBinaryAddress(nextHopAddr2),
        facebook::network::toBinaryAddress(nextHopAddr3),
};

} // namespace

TEST(RouteNextHop, SameInterfaceAndAddressRemainContiguous) {
  const folly::IPAddress lowerAddr("2001:db8::1");
  const folly::IPAddress targetAddr("2001:db8::2");
  const folly::IPAddress higherAddr("2001:db8::3");
  const InterfaceID targetIntf(2);

  const NextHop defaultTarget =
      ResolvedNextHop(targetAddr, targetIntf, ECMP_WEIGHT);
  const NextHop targetWithAllOtherAttributes = ResolvedNextHop(
      targetAddr,
      targetIntf,
      99,
      LabelForwardingAction(LabelForwardingAction::LabelForwardingType::PHP),
      true,
      getTopologyInfo(),
      7,
      {folly::IPAddressV6("3001:db8::1")},
      TunnelType::SRV6_ENCAP,
      std::string("ordering-test-tunnel"),
      int64_t(42),
      NextHopRole::BACKUP);

  const std::vector<NextHop> unorderedNextHops{
      ResolvedNextHop(targetAddr, InterfaceID(1), ECMP_WEIGHT),
      ResolvedNextHop(lowerAddr, targetIntf, ECMP_WEIGHT),
      defaultTarget,
      targetWithAllOtherAttributes,
      ResolvedNextHop(higherAddr, targetIntf, ECMP_WEIGHT),
      ResolvedNextHop(targetAddr, InterfaceID(3), ECMP_WEIGHT),
  };
  const RouteNextHopSet orderedNextHops(
      unorderedNextHops.begin(), unorderedNextHops.end());
  ASSERT_EQ(orderedNextHops.size(), unorderedNextHops.size());

  bool insideTargetGroup{false};
  bool targetGroupEnded{false};
  size_t targetGroupSize{0};
  for (const auto& nextHop : orderedNextHops) {
    const bool isTarget =
        nextHop.intfID() == targetIntf && nextHop.addr() == targetAddr;
    if (isTarget) {
      EXPECT_FALSE(targetGroupEnded);
      insideTargetGroup = true;
      ++targetGroupSize;
    } else if (insideTargetGroup) {
      targetGroupEnded = true;
    }
  }
  EXPECT_EQ(targetGroupSize, 2);
}

TEST(RouteNextHopEntry, FromNextHopsThrift) {
  // Note that we can't use UnicastRoute's constructor because it expects to be
  // passed both nextHopAddrs and nextHops
  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHops() = nextHopsThrift();
  std::optional<RouteCounterID> counterID("route.counter.0");
  std::optional<cfg::AclLookupClass> classID(
      cfg::AclLookupClass::DST_CLASS_L3_DPR);

  auto nextHopEntry =
      RouteNextHopEntry::from(route, kDefaultAdminDistance, counterID, classID);

  ASSERT_EQ(nextHopEntry.getAction(), RouteForwardAction::NEXTHOPS);
  ASSERT_EQ(nextHopEntry.getAdminDistance(), kDefaultAdminDistance);
  ASSERT_EQ(nextHopEntry.getCounterID(), counterID);
  ASSERT_EQ(nextHopEntry.getClassID(), classID);

  std::sort(nextHops.begin(), nextHops.end());
  auto nextHopEntryNextHops = nextHopEntry.getNextHopSet();
  ASSERT_TRUE(
      std::equal(
          nextHopEntryNextHops.begin(),
          nextHopEntryNextHops.end(),
          nextHops.begin()));
}

// The UnicastRoute.nextHopAddrs field has been deprecated in favor of
// UnicastRoutes.nextHops, which equips each next-hop with a weight
// in support of UCMP.
TEST(RouteNextHopEntry, FromBinaryAddresses) {
  // Note that we can't use UnicastRoute's constructor because it expects to be
  // passed both nextHopAddrs and nextHops
  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHopAddrs() = nextHopsBinaryAddress;
  std::optional<RouteCounterID> counterID("route.counter.0");
  std::optional<cfg::AclLookupClass> classID(
      cfg::AclLookupClass::DST_CLASS_L3_DPR);

  auto nextHopEntry =
      RouteNextHopEntry::from(route, kDefaultAdminDistance, counterID, classID);

  ASSERT_EQ(nextHopEntry.getAction(), RouteForwardAction::NEXTHOPS);
  ASSERT_EQ(nextHopEntry.getAdminDistance(), kDefaultAdminDistance);
  ASSERT_EQ(nextHopEntry.getCounterID(), counterID);
  ASSERT_EQ(nextHopEntry.getClassID(), classID);

  std::sort(nextHopsFromBinary.begin(), nextHopsFromBinary.end());
  auto nextHopEntryNextHops = nextHopEntry.getNextHopSet();
  ASSERT_TRUE(
      std::equal(
          nextHopEntryNextHops.begin(),
          nextHopEntryNextHops.end(),
          nextHopsFromBinary.begin()));
}

TEST(RouteNextHopEntry, OverrideDefaultAdminDistance) {
  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHops() = nextHopsThrift();
  route.adminDistance() = AdminDistance::IBGP;

  auto nextHopEntry = RouteNextHopEntry::from(
      route, kDefaultAdminDistance, std::nullopt, std::nullopt);

  ASSERT_EQ(nextHopEntry.getAdminDistance(), AdminDistance::IBGP);
}

TEST(RouteNextHopEntry, EmptyListIsDrop) {
  std::vector<NextHopThrift> noNextHops = {};

  // Note that we can't use UnicastRoute's constructor because it expects to be
  // passed both nextHopAddrs and nextHops
  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHops() = noNextHops;

  auto nextHopEntry = RouteNextHopEntry::from(
      route, kDefaultAdminDistance, std::nullopt, std::nullopt);

  ASSERT_EQ(nextHopEntry.getAction(), RouteForwardAction::DROP);
  ASSERT_EQ(nextHopEntry.getAdminDistance(), kDefaultAdminDistance);
  ASSERT_EQ(nextHopEntry.getNextHopSet().size(), 0);
}

// Total weight greater than 128. Should get normalized to 512
TEST(RouteNextHopEntry, NormalizedFixedSizeWideNextHop) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 55));
    nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 56));
    nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 57));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    NextHopWeight totalWeight = std::accumulate(
        normalizedNextHops.begin(),
        normalizedNextHops.end(),
        0,
        [](NextHopWeight w, const NextHop& nh) { return w + nh.weight(); });
    EXPECT_EQ(totalWeight, FLAGS_ecmp_width);
  }
}

// Total weight greater than ecmp_width but can be reduced
// by proportionally reducing weights first and then fitting to 512
TEST(RouteNextHopEntry, ScaledDownNormalizedFixedSizeWideNextHop) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 550));
    nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 560));
    nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 570));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    NextHopWeight totalWeight = std::accumulate(
        normalizedNextHops.begin(),
        normalizedNextHops.end(),
        0,
        [](NextHopWeight w, const NextHop& nh) { return w + nh.weight(); });
    EXPECT_EQ(totalWeight, FLAGS_ecmp_width);
  }
}

// Total width exceeds 512 after proportionaly reducing weights.
// should get normalized by reducing the high weight members
TEST(RouteNextHopEntry, ScaledDownFixedSizeWideNextHop) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
    nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));
    nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 25235));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    NextHopWeight totalWeight = std::accumulate(
        normalizedNextHops.begin(),
        normalizedNextHops.end(),
        0,
        [](NextHopWeight w, const NextHop& nh) { return w + nh.weight(); });
    // With optimized ucmp, the lowe weight paths should get removed
    EXPECT_EQ(totalWeight, ucmpOptimized ? 1 : FLAGS_ecmp_width);
  }
}

TEST(RouteNextHopEntry, NotNormalizedWideECmpEnabled) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 32));
    nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 33));
    nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 34));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    NextHopWeight totalWeight = std::accumulate(
        normalizedNextHops.begin(),
        normalizedNextHops.end(),
        0,
        [](NextHopWeight w, const NextHop& nh) { return w + nh.weight(); });

    auto originalTotalWeight = std::accumulate(
        nhops.begin(), nhops.end(), 0, [](NextHopWeight w, const NextHop& nh) {
          return w + nh.weight();
        });

    EXPECT_EQ(totalWeight, originalTotalWeight);
  }
}

// optimized algorithm should remove the low weight and normalize remaining
TEST(RouteNextHopEntry, OptimizedUcmpDiscardLowWeights) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 64;
  FLAGS_optimized_ucmp = true;

  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr4, InterfaceID(4), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr5, InterfaceID(5), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr6, InterfaceID(6), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr7, InterfaceID(7), 36));
  nhops.emplace(ResolvedNextHop(nextHopAddr8, InterfaceID(8), 36));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  std::map<folly::IPAddress, NextHopWeight> expectedWeights = {
      {nextHopAddr1, 0},
      {nextHopAddr2, 1},
      {nextHopAddr3, 1},
      {nextHopAddr4, 1},
      {nextHopAddr5, 1},
      {nextHopAddr6, 1},
      {nextHopAddr7, 1},
      {nextHopAddr8, 1}};
  for (const auto& nhop : normalizedNextHops) {
    EXPECT_EQ(nhop.weight(), expectedWeights[nhop.addr()]);
  }
}

// optimized algorithm should remove the multiple weights
// even if removing one increases max error deviation in
// intermediate step
TEST(RouteNextHopEntry, OptimizedUcmpDiscardMultipleLowWeights) {
  RouteNextHopSet nhops;

  FLAGS_ecmp_width = 64;
  FLAGS_optimized_ucmp = true;

  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 50));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 50));
  nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr4, InterfaceID(4), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr5, InterfaceID(5), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr6, InterfaceID(6), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr7, InterfaceID(7), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr8, InterfaceID(8), 1));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  // optimized algorithm should remove the low weight and normalize remaining
  std::map<folly::IPAddress, NextHopWeight> expectedWeights = {
      {nextHopAddr1, 1},
      {nextHopAddr2, 1},
      {nextHopAddr3, 0},
      {nextHopAddr4, 0},
      {nextHopAddr5, 0},
      {nextHopAddr6, 0},
      {nextHopAddr7, 0},
      {nextHopAddr8, 0}};
  for (const auto& nhop : normalizedNextHops) {
    EXPECT_EQ(nhop.weight(), expectedWeights[nhop.addr()]);
  }
}

TEST(RouteNextHopEntry, Thrift) {
  RouteNextHopEntry drop0(
      RouteNextHopEntry::Action::DROP,
      facebook::fboss::AdminDistance::STATIC_ROUTE,
      std::optional<RouteCounterID>("counter_drop"),
      std::optional<cfg::AclLookupClass>(cfg::AclLookupClass::CLASS_DROP));
  RouteNextHopEntry drop1(
      RouteNextHopEntry::Action::DROP,
      facebook::fboss::AdminDistance::STATIC_ROUTE,
      std::optional<RouteCounterID>("counter_drop"));
  RouteNextHopEntry drop2(
      RouteNextHopEntry::Action::DROP,
      facebook::fboss::AdminDistance::STATIC_ROUTE,
      std::optional<RouteCounterID>(std::nullopt),
      std::optional<cfg::AclLookupClass>(cfg::AclLookupClass::CLASS_DROP));

  RouteNextHopEntry cpu0(
      RouteNextHopEntry::Action::TO_CPU,
      facebook::fboss::AdminDistance::STATIC_ROUTE,
      std::optional<RouteCounterID>("counter_cpu"),
      std::optional<cfg::AclLookupClass>(
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0));

  RouteNextHopSet nhops;
  std::vector<ResolvedNextHop> nexthopsVector{};
  nexthopsVector.emplace_back(nextHopAddr1, InterfaceID(1), 50);
  nexthopsVector.emplace_back(nextHopAddr2, InterfaceID(2), 50);
  nexthopsVector.emplace_back(nextHopAddr3, InterfaceID(3), 1);
  nexthopsVector.emplace_back(nextHopAddr4, InterfaceID(4), 1);
  nexthopsVector.emplace_back(nextHopAddr5, InterfaceID(5), 1);
  nexthopsVector.emplace_back(nextHopAddr6, InterfaceID(6), 1);
  nexthopsVector.emplace_back(nextHopAddr7, InterfaceID(7), 1);
  nexthopsVector.emplace_back(nextHopAddr8, InterfaceID(8), 1);

  nhops.insert(std::begin(nexthopsVector), std::end(nexthopsVector));
  RouteNextHopEntry nhops0(nhops, kDefaultAdminDistance);
  RouteNextHopEntry nhops1(
      static_cast<NextHop>(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 50)),
      kDefaultAdminDistance,
      std::optional<RouteCounterID>("counter0"),
      std::optional<cfg::AclLookupClass>(
          cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_0));

  validateThriftStructNodeSerialization<RouteNextHopEntry>(drop0);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(drop1);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(drop2);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(cpu0);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(nhops0);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(nhops1);
}

TEST(RouteNextHopEntry, skipPrunedNextHops) {
  RouteNextHopSet nhops;

  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));
  auto nh = ResolvedNextHop(nextHopAddr3, InterfaceID(3), 1);
  nh.setAdjustedWeight(0);
  nhops.emplace(std::move(nh));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  // path 3 should be skipped
  EXPECT_EQ(normalizedNextHops.size(), nhops.size() - 1);
}

TEST(RouteNextHopEntry, setGetOverrideNextHops) {
  RouteNextHopSet nhops;
  std::optional<RouteNextHopSet> overrideNhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  overrideNhops = nhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));

  RouteNextHopEntry nhopEntry(nhops, kDefaultAdminDistance);
  EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
  EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
  nhopEntry.setOverrideNextHops(overrideNhops);
  ASSERT_TRUE(nhopEntry.getOverrideNextHops().has_value());
  EXPECT_EQ(*nhopEntry.getOverrideNextHops(), overrideNhops);
  EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
  EXPECT_EQ(nhopEntry.normalizedNextHops(), overrideNhops);
  // Reset override nhops
  nhopEntry.setOverrideNextHops(std::nullopt);
  EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
  EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
  EXPECT_EQ(nhopEntry.normalizedNextHops(), nhops);
}

TEST(RouteNextHopEntry, setGetOverrideSwitchingMode) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));

  RouteNextHopEntry nhopEntry(nhops, kDefaultAdminDistance);
  EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
  EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
  nhopEntry.setOverrideEcmpSwitchingMode(cfg::SwitchingMode::PER_PACKET_RANDOM);
  ASSERT_TRUE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
  EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
  EXPECT_EQ(
      *nhopEntry.getOverrideEcmpSwitchingMode(),
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  // Reset override SwitchingMode
  nhopEntry.setOverrideEcmpSwitchingMode(std::nullopt);
  EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
  EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
}

TEST(RouteNextHopEntry, createWithOverrideNhops) {
  RouteNextHopSet nhops;
  std::optional<RouteNextHopSet> overrideNhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  overrideNhops = nhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));
  std::optional<RouteCounterID> nullCounterId;
  std::optional<cfg::AclLookupClass> nullLookupClass;
  std::optional<cfg::SwitchingMode> nullSwitchingMode;
  std::optional<RouteNextHopEntry::NextHopSet> nullNhopSet;
  {
    RouteNextHopEntry nhopEntry(
        nhops,
        kDefaultAdminDistance,
        nullCounterId,
        nullLookupClass,
        nullSwitchingMode,
        nullNhopSet);
    EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(nhopEntry.normalizedNextHops(), nhops);
    // Set override nhops
    nhopEntry.setOverrideNextHops(overrideNhops);
    EXPECT_TRUE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_EQ(nhopEntry.getOverrideNextHops(), overrideNhops);
    EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(nhopEntry.normalizedNextHops(), overrideNhops);
    // reset override nhops
    nhopEntry.setOverrideNextHops(std::nullopt);
    EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(nhopEntry.normalizedNextHops(), nhops);
  }
  {
    RouteNextHopEntry nhopEntry(
        nhops,
        kDefaultAdminDistance,
        nullCounterId,
        nullLookupClass,
        nullSwitchingMode,
        overrideNhops);
    EXPECT_TRUE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_EQ(nhopEntry.getOverrideNextHops(), overrideNhops);
    EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(nhopEntry.normalizedNextHops(), overrideNhops);
    // reset override nhops
    nhopEntry.setOverrideNextHops(std::nullopt);
    EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(nhopEntry.normalizedNextHops(), nhops);
  }
}

TEST(RouteNextHopEntry, createWithOverrideSwitchingMode) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(nextHopAddr1, InterfaceID(1), 1));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 1));
  std::optional<RouteCounterID> nullCounterId;
  std::optional<cfg::AclLookupClass> nullLookupClass;
  std::optional<cfg::SwitchingMode> nullSwitchingMode;
  {
    RouteNextHopEntry nhopEntry(
        nhops,
        kDefaultAdminDistance,
        nullCounterId,
        nullLookupClass,
        nullSwitchingMode);
    EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    EXPECT_FALSE(nhopEntry.getOverrideNextHops().has_value());
    EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    nhopEntry.setOverrideEcmpSwitchingMode(
        cfg::SwitchingMode::PER_PACKET_RANDOM);
    ASSERT_TRUE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(
        *nhopEntry.getOverrideEcmpSwitchingMode(),
        cfg::SwitchingMode::PER_PACKET_RANDOM);
    // Reset override SwitchingMode
    nhopEntry.setOverrideEcmpSwitchingMode(std::nullopt);
    EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
  }
  {
    std::optional<cfg::SwitchingMode> switchingMode{
        cfg::SwitchingMode::PER_PACKET_RANDOM};
    RouteNextHopEntry nhopEntry(
        nhops,
        kDefaultAdminDistance,
        nullCounterId,
        nullLookupClass,
        switchingMode);
    ASSERT_TRUE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    EXPECT_TRUE(nhopEntry.hasOverrideSwitchingModeOrNhops());
    EXPECT_EQ(
        *nhopEntry.getOverrideEcmpSwitchingMode(),
        cfg::SwitchingMode::PER_PACKET_RANDOM);
    // Reset override SwitchingMode
    nhopEntry.setOverrideEcmpSwitchingMode(std::nullopt);
    EXPECT_FALSE(nhopEntry.getOverrideEcmpSwitchingMode().has_value());
    EXPECT_FALSE(nhopEntry.hasOverrideSwitchingModeOrNhops());
  }
}

TEST(RouteNextHopEntry, Srv6NextHopsThriftRoundTrip) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 20));

  auto thriftNhops = util::fromRouteNextHopSet(nhops);
  auto roundTripped = util::toRouteNextHopSet(thriftNhops, true);

  EXPECT_EQ(nhops.size(), roundTripped.size());
  for (const auto& nh : roundTripped) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.srv6SegmentList(), segList);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      EXPECT_TRUE(nh.srv6SegmentList().empty());
      EXPECT_FALSE(nh.tunnelType().has_value());
      EXPECT_FALSE(nh.tunnelId().has_value());
    }
  }
}

TEST(RouteNextHopEntry, FromRouteNextHopSetWithSrv6Fields) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("3001:db8:1::"),
      folly::IPAddressV6("3001:db8:2::"),
      folly::IPAddressV6("3001:db8:3::")};

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  auto thriftNhops = util::fromRouteNextHopSet(nhops);
  ASSERT_EQ(thriftNhops.size(), 1);

  const auto& thriftNh = thriftNhops.at(0);

  // Verify SRv6 segment list
  ASSERT_EQ(thriftNh.srv6SegmentList()->size(), segList.size());
  for (size_t i = 0; i < segList.size(); ++i) {
    EXPECT_EQ(
        facebook::network::toIPAddress(thriftNh.srv6SegmentList()->at(i)),
        segList.at(i));
  }

  // Verify tunnel type and tunnel id
  EXPECT_EQ(thriftNh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(thriftNh.tunnelId(), kSrv6Tunnel0);

  // Verify standard fields are preserved
  EXPECT_EQ(facebook::network::toIPAddress(*thriftNh.address()), nextHopAddr1);
  EXPECT_EQ(*thriftNh.weight(), 10);
}

TEST(RouteNextHopEntry, Srv6NextHopEntryThriftSerialization) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteNextHopEntry entry(nhops, kDefaultAdminDistance);
  validateThriftStructNodeSerialization<RouteNextHopEntry>(entry);
}

TEST(RouteNextHopEntry, Srv6NextHopEntryEquality) {
  const std::vector<folly::IPAddressV6> segList1{
      folly::IPAddressV6("2001:db8::1")};
  const std::vector<folly::IPAddressV6> segList2{
      folly::IPAddressV6("2001:db8::2")};

  RouteNextHopSet nhops1;
  nhops1.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteNextHopSet nhops2;
  nhops2.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteNextHopSet nhopsDiffSegList;
  nhopsDiffSegList.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteNextHopEntry entry1(nhops1, kDefaultAdminDistance);
  RouteNextHopEntry entry2(nhops2, kDefaultAdminDistance);
  RouteNextHopEntry entryDiff(nhopsDiffSegList, kDefaultAdminDistance);

  EXPECT_EQ(entry1, entry2);
  EXPECT_FALSE(entry1 == entryDiff);
}

TEST(RouteNextHopEntry, Srv6ToUnicastRoutePreservesSrv6Fields) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  RouteNextHopEntry entry(nhops, kDefaultAdminDistance);
  const folly::CIDRNetwork network{folly::IPAddress("fc00::"), 7};
  auto unicastRoute = util::toUnicastRoute(network, entry);

  ASSERT_EQ(unicastRoute.nextHops()->size(), 1);
  const auto& thriftNh = unicastRoute.nextHops()->at(0);
  EXPECT_EQ(thriftNh.srv6SegmentList()->size(), 2);
  EXPECT_EQ(thriftNh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(thriftNh.tunnelId(), kSrv6Tunnel0);
}

TEST(RouteNextHopEntry, Srv6FromNextHopsThrift) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  std::vector<NextHopThrift> thriftNhops;
  NextHopThrift nh;
  *nh.address() = createV6LinkLocalNextHop(nextHopAddr1);
  *nh.weight() = 10;
  for (const auto& seg : segList) {
    nh.srv6SegmentList()->push_back(facebook::network::toBinaryAddress(seg));
  }
  nh.tunnelType() = TunnelType::SRV6_ENCAP;
  nh.tunnelId() = kSrv6Tunnel0;
  thriftNhops.push_back(std::move(nh));

  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHops() = std::move(thriftNhops);

  auto nhopEntry = RouteNextHopEntry::from(
      route, kDefaultAdminDistance, std::nullopt, std::nullopt);

  ASSERT_EQ(nhopEntry.getAction(), RouteForwardAction::NEXTHOPS);
  auto nhopSet = nhopEntry.getNextHopSet();
  ASSERT_EQ(nhopSet.size(), 1);
  const auto& resultNh = *nhopSet.begin();
  EXPECT_EQ(resultNh.srv6SegmentList(), segList);
  EXPECT_EQ(resultNh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(resultNh.tunnelId(), kSrv6Tunnel0);
}

// Verify normalizedNextHops() preserves SRv6 fields when no weight scaling
// is needed (totalWeight <= ecmp_width)
TEST(RouteNextHopEntry, NormalizedNextHopsPreservesSrv6Fields) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("3001:db8:1::"),
      folly::IPAddressV6("3001:db8:2::"),
      folly::IPAddressV6("3001:db8:3::")};

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 10));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  ASSERT_EQ(normalizedNextHops.size(), 2);
  for (const auto& nh : normalizedNextHops) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.srv6SegmentList(), segList);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      EXPECT_TRUE(nh.srv6SegmentList().empty());
      EXPECT_FALSE(nh.tunnelType().has_value());
      EXPECT_FALSE(nh.tunnelId().has_value());
    }
  }
}

// Verify normalizedNextHops() preserves SRv6 fields when weight scaling
// is triggered (totalWeight > ecmp_width)
TEST(RouteNextHopEntry, NormalizedNextHopsPreservesSrv6FieldsWithScaling) {
  const std::vector<folly::IPAddressV6> segList1{
      folly::IPAddressV6("3001:db8:1::")};
  const std::vector<folly::IPAddressV6> segList2{
      folly::IPAddressV6("3001:db8:2::")};

  FLAGS_ecmp_width = 64;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    RouteNextHopSet nhops;
    nhops.emplace(ResolvedNextHop(
        nextHopAddr1,
        InterfaceID(1),
        40,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        segList1,
        TunnelType::SRV6_ENCAP,
        kSrv6Tunnel0));
    nhops.emplace(ResolvedNextHop(
        nextHopAddr2,
        InterfaceID(2),
        40,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        segList2,
        TunnelType::SRV6_ENCAP,
        kSrv6Tunnel0));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    for (const auto& nh : normalizedNextHops) {
      if (nh.addr() == nextHopAddr1) {
        EXPECT_EQ(nh.srv6SegmentList(), segList1);
        EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
        EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      } else {
        EXPECT_EQ(nh.srv6SegmentList(), segList2);
        EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
        EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
      }
    }
  }
}

// Verify normalizedNextHops() preserves SRv6 fields through the wide ECMP
// normalization path
TEST(RouteNextHopEntry, NormalizedNextHopsPreservesSrv6FieldsWideEcmp) {
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("3001:db8:1::"), folly::IPAddressV6("3001:db8:2::")};

  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      55,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(ResolvedNextHop(
      nextHopAddr2,
      InterfaceID(2),
      56,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(ResolvedNextHop(nextHopAddr3, InterfaceID(3), 57));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  ASSERT_EQ(normalizedNextHops.size(), 3);
  for (const auto& nh : normalizedNextHops) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.srv6SegmentList(), segList);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else if (nh.addr() == nextHopAddr2) {
      EXPECT_EQ(nh.srv6SegmentList(), segList);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      EXPECT_TRUE(nh.srv6SegmentList().empty());
      EXPECT_FALSE(nh.tunnelType().has_value());
      EXPECT_FALSE(nh.tunnelId().has_value());
    }
  }
}

TEST(RouteNextHopEntry, CostThriftRoundTrip) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(42)));
  nhops.emplace(ResolvedNextHop(nextHopAddr2, InterfaceID(2), 20));

  auto thriftNhops = util::fromRouteNextHopSet(nhops);
  auto roundTripped = util::toRouteNextHopSet(thriftNhops, true);

  EXPECT_EQ(nhops.size(), roundTripped.size());
  for (const auto& nh : roundTripped) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.cost(), int64_t(42));
    } else {
      EXPECT_FALSE(nh.cost().has_value());
    }
  }
}

TEST(RouteNextHopEntry, CostFromNextHopsThrift) {
  std::vector<NextHopThrift> thriftNhops;
  NextHopThrift nh;
  *nh.address() = createV6LinkLocalNextHop(nextHopAddr1);
  *nh.weight() = 10;
  nh.cost() = 77;
  thriftNhops.push_back(std::move(nh));

  UnicastRoute route;
  route.dest() = kDestPrefix;
  route.nextHops() = std::move(thriftNhops);

  auto nhopEntry = RouteNextHopEntry::from(
      route, kDefaultAdminDistance, std::nullopt, std::nullopt);

  ASSERT_EQ(nhopEntry.getAction(), RouteForwardAction::NEXTHOPS);
  auto nhopSet = nhopEntry.getNextHopSet();
  ASSERT_EQ(nhopSet.size(), 1);
  EXPECT_EQ(nhopSet.begin()->cost(), int64_t(77));
}

TEST(RouteNextHopEntry, CostPreservedThroughNormalization) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(100)));
  nhops.emplace(ResolvedNextHop(
      nextHopAddr2,
      InterfaceID(2),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      std::nullopt));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  ASSERT_EQ(normalizedNextHops.size(), 2);
  for (const auto& nh : normalizedNextHops) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.cost(), int64_t(100));
    } else {
      EXPECT_FALSE(nh.cost().has_value());
    }
  }
}

TEST(RouteNextHopEntry, CostPreservedThroughNormalizationWithScaling) {
  FLAGS_ecmp_width = 64;

  for (const auto ucmpOptimized : {false, true}) {
    FLAGS_optimized_ucmp = ucmpOptimized;
    RouteNextHopSet nhops;
    nhops.emplace(ResolvedNextHop(
        nextHopAddr1,
        InterfaceID(1),
        40,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        int64_t(500)));
    nhops.emplace(ResolvedNextHop(
        nextHopAddr2,
        InterfaceID(2),
        40,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        int64_t(600)));

    auto normalizedNextHops =
        RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

    for (const auto& nh : normalizedNextHops) {
      if (nh.addr() == nextHopAddr1) {
        EXPECT_EQ(nh.cost(), int64_t(500));
      } else {
        EXPECT_EQ(nh.cost(), int64_t(600));
      }
    }
  }
}

TEST(RouteNextHopEntry, CostPreservedThroughWideEcmpNormalization) {
  FLAGS_ecmp_width = 512;
  FLAGS_wide_ecmp = true;

  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      55,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(1000)));
  nhops.emplace(ResolvedNextHop(
      nextHopAddr2,
      InterfaceID(2),
      56,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      std::nullopt));

  auto normalizedNextHops =
      RouteNextHopEntry(nhops, kDefaultAdminDistance).normalizedNextHops();

  ASSERT_EQ(normalizedNextHops.size(), 2);
  for (const auto& nh : normalizedNextHops) {
    if (nh.addr() == nextHopAddr1) {
      EXPECT_EQ(nh.cost(), int64_t(1000));
    } else {
      EXPECT_FALSE(nh.cost().has_value());
    }
  }
}

TEST(RouteNextHopEntry, CostToUnicastRoutePreservesCost) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr1,
      InterfaceID(1),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      {},
      std::nullopt,
      std::nullopt,
      int64_t(42)));

  RouteNextHopEntry entry(nhops, kDefaultAdminDistance);
  const folly::CIDRNetwork network{folly::IPAddress("fc00::"), 7};
  auto unicastRoute = util::toUnicastRoute(network, entry);

  ASSERT_EQ(unicastRoute.nextHops()->size(), 1);
  EXPECT_EQ(unicastRoute.nextHops()->at(0).cost(), 42);
}

TEST(RouteNextHopEntry, ClientNextHopSetIDAccessors) {
  RouteNextHopEntry entry(
      RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);

  EXPECT_FALSE(entry.getClientNextHopSetID().has_value());

  std::optional<NextHopSetID> id{NextHopSetID(42)};
  entry.setClientNextHopSetID(id);
  EXPECT_EQ(entry.getClientNextHopSetID(), NextHopSetID(42));

  EXPECT_FALSE(entry.getResolvedNextHopSetID().has_value());
  EXPECT_FALSE(entry.getNormalizedResolvedNextHopSetID().has_value());

  RouteNextHopEntry roundTripped(
      RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  roundTripped.fromThrift(entry.toThrift());
  EXPECT_EQ(roundTripped.getClientNextHopSetID(), NextHopSetID(42));

  std::optional<NextHopSetID> empty;
  entry.setClientNextHopSetID(empty);
  EXPECT_FALSE(entry.getClientNextHopSetID().has_value());

  validateThriftStructNodeSerialization<RouteNextHopEntry>(roundTripped);
}

TEST(RouteNextHopEntry, ClientNextHopSetIDInEquality) {
  RouteNextHopEntry a(RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  RouteNextHopEntry b(RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  EXPECT_EQ(a, b);

  std::optional<NextHopSetID> id7{NextHopSetID(7)};
  a.setClientNextHopSetID(id7);
  EXPECT_NE(a, b);

  b.setClientNextHopSetID(id7);
  EXPECT_EQ(a, b);

  std::optional<NextHopSetID> id8{NextHopSetID(8)};
  b.setClientNextHopSetID(id8);
  EXPECT_NE(a, b);
}

TEST(RouteNextHopEntry, OverrideNextHopSetID) {
  RouteNextHopEntry a(RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  RouteNextHopEntry b(RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  EXPECT_FALSE(a.getOverrideNextHopSetID().has_value());

  a.setOverrideNextHopSetID(NextHopSetID(7));
  EXPECT_EQ(a.getOverrideNextHopSetID(), NextHopSetID(7));
  EXPECT_FALSE(a.getClientNextHopSetID().has_value());
  EXPECT_NE(a, b);

  RouteNextHopEntry roundTripped(
      RouteNextHopEntry::Action::DROP, kDefaultAdminDistance);
  roundTripped.fromThrift(a.toThrift());
  EXPECT_EQ(roundTripped.getOverrideNextHopSetID(), NextHopSetID(7));
  validateThriftStructNodeSerialization<RouteNextHopEntry>(roundTripped);

  RouteNextHopEntry fromCtor(
      RouteNextHopEntry::Action::DROP,
      kDefaultAdminDistance,
      std::optional<RouteCounterID>(),
      std::optional<RouteNextHopEntry::AclLookupClass>(),
      std::optional<cfg::SwitchingMode>(),
      std::optional<RouteNextHopEntry::NextHopSet>(),
      std::optional<NextHopSetID>(),
      std::optional<NextHopSetID>(),
      std::optional<NextHopSetID>(),
      std::optional<NextHopSetID>(7));
  EXPECT_EQ(fromCtor, a);

  b.setOverrideNextHopSetID(NextHopSetID(7));
  EXPECT_EQ(a, b);
  b.setOverrideNextHopSetID(NextHopSetID(8));
  EXPECT_NE(a, b);

  a.setOverrideNextHopSetID(std::nullopt);
  EXPECT_FALSE(a.getOverrideNextHopSetID().has_value());
}

namespace {

NextHopThrift makeNextHopThrift(const folly::IPAddress& addr, int32_t weight) {
  NextHopThrift nh;
  nh.address() = facebook::network::toBinaryAddress(addr);
  nh.weight() = weight;
  return nh;
}

RouteNextHopSet combineDuplicates(const std::vector<NextHopThrift>& nhts) {
  return util::toRouteNextHopSet(
      nhts, true /* allowV6NonLinkLocal */, true /* combineDuplicateWeights */);
}

} // namespace

TEST(RouteNextHopEntry, CombineDuplicateWeightsSumsEcmpDuplicates) {
  const RouteNextHopSet expected{UnresolvedNextHop(nextHopAddr2, 2)};

  EXPECT_EQ(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
           makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT)}),
      expected);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsSumsDifferingWeights) {
  const RouteNextHopSet expected{UnresolvedNextHop(nextHopAddr2, 8)};

  // Without combining these are two distinct set members, since weight
  // participates in NextHop's ordering.
  EXPECT_EQ(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, 5),
           makeNextHopThrift(nextHopAddr2, 3)}),
      expected);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsLeavesDistinctEcmpGroupAlone) {
  const RouteNextHopSet expected{
      UnresolvedNextHop(nextHopAddr2, ECMP_WEIGHT),
      UnresolvedNextHop(nextHopAddr3, ECMP_WEIGHT)};

  EXPECT_EQ(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
           makeNextHopThrift(nextHopAddr3, ECMP_WEIGHT)}),
      expected);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsGivesSinglesAnExplicitShare) {
  // Once anything is combined the next hop listed once is given an explicit
  // share rather than being left at ECMP_WEIGHT, otherwise route resolution
  // downgrades the whole set back to plain ECMP.
  const RouteNextHopSet expected{
      UnresolvedNextHop(nextHopAddr2, 2),
      UnresolvedNextHop(nextHopAddr3, UCMP_DEFAULT_WEIGHT)};

  EXPECT_EQ(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
           makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
           makeNextHopThrift(nextHopAddr3, ECMP_WEIGHT)}),
      expected);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsDistinguishesTunnelId) {
  auto withTunnel = makeNextHopThrift(nextHopAddr2, 5);
  withTunnel.tunnelId() = kSrv6Tunnel0;

  const RouteNextHopSet expected{
      UnresolvedNextHop(
          nextHopAddr2,
          5,
          std::nullopt /*label*/,
          std::nullopt /*disableTTLDecrement*/,
          std::nullopt /*topologyInfo*/,
          std::nullopt /*adjustedWeight*/,
          {} /*srv6SegmentList*/,
          std::nullopt /*tunnelType*/,
          kSrv6Tunnel0),
      UnresolvedNextHop(nextHopAddr2, 5)};

  EXPECT_EQ(
      combineDuplicates({makeNextHopThrift(nextHopAddr2, 5), withTunnel}),
      expected);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsThrowsOnOverflow) {
  constexpr int32_t kNearMax = std::numeric_limits<int32_t>::max() - 1;

  EXPECT_THROW(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, kNearMax),
           makeNextHopThrift(nextHopAddr2, kNearMax)}),
      FbossError);
}

TEST(RouteNextHopEntry, CombineDuplicateWeightsAllowsMaxWeight) {
  constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
  const RouteNextHopSet expected{UnresolvedNextHop(nextHopAddr2, kMax)};

  EXPECT_EQ(
      combineDuplicates(
          {makeNextHopThrift(nextHopAddr2, kMax - 1),
           makeNextHopThrift(nextHopAddr2, 1)}),
      expected);
}

TEST(RouteNextHopEntry, DuplicateWeightsNotCombinedByDefault) {
  const std::vector<NextHopThrift> nhts{
      makeNextHopThrift(nextHopAddr2, 5), makeNextHopThrift(nextHopAddr2, 3)};

  // Default keeps both, since the weights make them distinct.
  const RouteNextHopSet distinctWeights{
      UnresolvedNextHop(nextHopAddr2, 5), UnresolvedNextHop(nextHopAddr2, 3)};
  EXPECT_EQ(util::toRouteNextHopSet(nhts, true), distinctWeights);

  // Identical entries still collapse to one, dropping the extra weight.
  const RouteNextHopSet identical{UnresolvedNextHop(nextHopAddr2, 5)};
  EXPECT_EQ(
      util::toRouteNextHopSet(
          {makeNextHopThrift(nextHopAddr2, 5),
           makeNextHopThrift(nextHopAddr2, 5)},
          true),
      identical);
}

namespace {

using AddrAndWeight = std::pair<std::string, int32_t>;

// Address and weight of each thrift next hop, sorted so the comparison does
// not depend on the next hop set's ordering.
std::vector<AddrAndWeight> toAddrAndWeights(
    const std::vector<NextHopThrift>& nhts) {
  std::vector<AddrAndWeight> out;
  out.reserve(nhts.size());
  for (const auto& nht : nhts) {
    out.emplace_back(
        facebook::network::toIPAddress(*nht.address()).str(), *nht.weight());
  }
  std::sort(out.begin(), out.end());
  return out;
}

std::vector<AddrAndWeight> replicateWeighted(const RouteNextHopSet& nhs) {
  return toAddrAndWeights(
      util::fromRouteNextHopSet(nhs, true /* replicateWeightedNexthops */));
}

} // namespace

TEST(RouteNextHopEntry, DetectMixedSrv6NextHops) {
  EXPECT_FALSE(hasSrv6AndNonSrv6NextHops({}));
  EXPECT_FALSE(hasSrv6AndNonSrv6NextHops(
      {UnresolvedNextHop(nextHopAddr2, ECMP_WEIGHT)}));
  EXPECT_FALSE(hasSrv6AndNonSrv6NextHops(
      {UnresolvedNextHop(nextHopAddr2, ECMP_WEIGHT),
       UnresolvedNextHop(nextHopAddr3, ECMP_WEIGHT)}));
  EXPECT_FALSE(hasSrv6AndNonSrv6NextHops({makeSrv6NextHop(nextHopAddr2)}));
  EXPECT_FALSE(hasSrv6AndNonSrv6NextHops(
      {makeSrv6NextHop(nextHopAddr2), makeSrv6NextHop(nextHopAddr3)}));
  EXPECT_TRUE(hasSrv6AndNonSrv6NextHops(
      {makeSrv6NextHop(nextHopAddr2),
       UnresolvedNextHop(nextHopAddr3, ECMP_WEIGHT)}));
}

TEST(RouteNextHopEntry, ReplicateWeightedNexthopsExpandsWeight) {
  const std::vector<AddrAndWeight> expected{
      {nextHopAddr2.str(), ECMP_WEIGHT},
      {nextHopAddr2.str(), ECMP_WEIGHT},
      {nextHopAddr2.str(), ECMP_WEIGHT}};

  const RouteNextHopSet weighted{UnresolvedNextHop(nextHopAddr2, 3)};
  EXPECT_EQ(replicateWeighted(weighted), expected);
}

TEST(RouteNextHopEntry, ReplicateWeightedNexthopsLeavesUnweightedAlone) {
  const std::vector<AddrAndWeight> expected{
      {nextHopAddr3.str(), ECMP_WEIGHT},
      {nextHopAddr2.str(), UCMP_DEFAULT_WEIGHT}};

  const RouteNextHopSet unweighted{
      UnresolvedNextHop(nextHopAddr2, UCMP_DEFAULT_WEIGHT),
      UnresolvedNextHop(nextHopAddr3, ECMP_WEIGHT)};
  EXPECT_EQ(replicateWeighted(unweighted), expected);
}

TEST(RouteNextHopEntry, ReplicateWeightedNexthopsUndoesCombine) {
  const std::vector<NextHopThrift> nhts{
      makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
      makeNextHopThrift(nextHopAddr2, ECMP_WEIGHT),
      makeNextHopThrift(nextHopAddr3, ECMP_WEIGHT)};

  // The duplicated next hop expands back into its two copies. The one listed
  // once comes back at UCMP_DEFAULT_WEIGHT rather than ECMP_WEIGHT, since
  // combining now gives singles an explicit share, so the round trip is no
  // longer weight-for-weight identical to the input.
  const std::vector<AddrAndWeight> expected{
      {nextHopAddr3.str(), UCMP_DEFAULT_WEIGHT},
      {nextHopAddr2.str(), ECMP_WEIGHT},
      {nextHopAddr2.str(), ECMP_WEIGHT}};

  EXPECT_EQ(replicateWeighted(combineDuplicates(nhts)), expected);
}

TEST(RouteNextHopEntry, WeightedNexthopsNotReplicatedByDefault) {
  const std::vector<AddrAndWeight> expected{{nextHopAddr2.str(), 3}};

  const RouteNextHopSet weighted{UnresolvedNextHop(nextHopAddr2, 3)};
  EXPECT_EQ(toAddrAndWeights(util::fromRouteNextHopSet(weighted)), expected);
}

TEST(RouteNextHopEntry, VxlanNextHopsThriftRoundTrip) {
  RouteNextHopSet nhops;
  nhops.emplace(ResolvedNextHop(
      nextHopAddr2,
      InterfaceID(2),
      10,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::vector<folly::IPAddressV6>{},
      TunnelType::VXLAN,
      "vxlan_tunnel_1",
      std::nullopt,
      NextHopRole::PRIMARY,
      folly::MacAddress("02:00:00:00:00:01")));

  auto thriftNhops = util::fromRouteNextHopSet(nhops);
  ASSERT_EQ(thriftNhops.size(), 1);
  EXPECT_EQ(thriftNhops.at(0).overlayDstMac(), "02:00:00:00:00:01");
  auto roundTripped = util::toRouteNextHopSet(thriftNhops, true);
  ASSERT_EQ(roundTripped.size(), 1);
  EXPECT_EQ(
      roundTripped.begin()->overlayDstMac(),
      folly::MacAddress("02:00:00:00:00:01"));
  EXPECT_EQ(roundTripped.begin()->tunnelType(), TunnelType::VXLAN);

  // Normalization (the RouteNextHopEntry weight-scaling path) must keep
  // the MAC: dropping it would merge tunnel NHs for different remotes.
  auto normalized = RouteNextHopEntry::normalizeNextHops(nhops, 1);
  ASSERT_EQ(normalized.size(), 1);
  EXPECT_EQ(
      normalized.begin()->overlayDstMac(),
      folly::MacAddress("02:00:00:00:00:01"));
}
