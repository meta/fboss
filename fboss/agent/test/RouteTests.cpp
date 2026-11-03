/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/mpls_types.h"
#include "fboss/agent/state/SwitchState-defs.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"

#include <fmt/format.h>
#include <folly/portability/GFlags.h>
#include <gtest/gtest.h>
#include <optional>
#include <set>

using namespace facebook::fboss;
using facebook::network::toBinaryAddress;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using std::make_shared;
using std::shared_ptr;
using ::testing::Return;

namespace {
const auto kDestPrefix =
    RouteV6::Prefix{folly::IPAddressV6("2401:bad:cad:dad::"), 64};
const auto kDestAddress = folly::IPAddressV6("2401:bad:cad:dad::beef");

// bgp next hops
const std::array<folly::IPAddressV6, 4> kBgpNextHopAddrs{
    folly::IPAddressV6("2801::1"),
    folly::IPAddressV6("2802::1"),
    folly::IPAddressV6("2803::1"),
    folly::IPAddressV6("2804::1"),
};

// igp next hops
const std::array<folly::IPAddressV6, 4> kIgpAddrs{
    folly::IPAddressV6("fe80::101"),
    folly::IPAddressV6("fe80::102"),
    folly::IPAddressV6("fe80::103"),
    folly::IPAddressV6("fe80::104"),
};

// label stacks
const std::array<LabelForwardingAction::LabelStack, 4> kLabelStacks{
    LabelForwardingAction::LabelStack({101, 201, 301}),
    LabelForwardingAction::LabelStack({102, 202, 302}),
    LabelForwardingAction::LabelStack({103, 203, 303}),
    LabelForwardingAction::LabelStack({104, 204, 304}),
};
const std::array<InterfaceID, 4> kInterfaces{
    InterfaceID(1),
    InterfaceID(2),
    InterfaceID(3),
    InterfaceID(4),
};
const ClientID kClientA = ClientID(1001);
const ClientID kClientB = ClientID(2001);

const std::optional<RouteCounterID> kCounterID1("route.counter.0");
const std::optional<RouteCounterID> kCounterID2("route.counter.1");

const std::optional<cfg::AclLookupClass> kClassID1(
    cfg::AclLookupClass::DST_CLASS_L3_DPR);
const std::optional<cfg::AclLookupClass> kClassID2(
    cfg::AclLookupClass::DST_CLASS_L3_LOCAL_2);

constexpr AdminDistance DISTANCE = AdminDistance::MAX_ADMIN_DISTANCE;
constexpr AdminDistance EBGP_DISTANCE = AdminDistance::EBGP;
const RouterID kRid0 = RouterID(0);
const std::string kSrv6Tunnel0{"srv6Tunnel0"};

//
// Helper functions
//
template <typename AddrT>
void EXPECT_FWD_INFO(
    const std::shared_ptr<SwitchState>& state,
    std::shared_ptr<Route<AddrT>> rt,
    InterfaceID intf,
    std::string ipStr) {
  const auto& fwds = getNextHops(state, rt->getForwardInfo());
  EXPECT_EQ(1, fwds.size());
  const auto& fwd = *fwds.begin();
  EXPECT_EQ(intf, fwd.intf());
  EXPECT_EQ(IPAddress(ipStr), fwd.addr());
}

template <typename AddrT>
void EXPECT_RESOLVED(std::shared_ptr<Route<AddrT>> rt) {
  ASSERT_NE(nullptr, rt);
  EXPECT_TRUE(rt->isResolved());
  EXPECT_FALSE(rt->isUnresolvable());
  EXPECT_FALSE(rt->needResolve());
}

RouteNextHopSet newNextHops(int n, std::string prefix) {
  RouteNextHopSet h;
  for (int i = 0; i < n; i++) {
    auto ipStr = prefix + std::to_string(i + 10);
    h.emplace(UnresolvedNextHop(IPAddress(ipStr), UCMP_DEFAULT_WEIGHT));
  }
  return h;
}

std::set<std::string> routePrefixesFromState(
    const std::shared_ptr<SwitchState>& state,
    RouterID rid) {
  std::set<std::string> routes;
  forAllRoutes(state, [&routes, rid](const RouterID& routeRid, auto& route) {
    if (routeRid != rid) {
      return;
    }
    routes.insert(route->prefix().str());
  });
  return routes;
}

std::set<std::string> routePrefixesFromRouteDetails(
    const std::vector<RouteDetails>& routeDetails) {
  std::set<std::string> routes;
  for (const auto& route : routeDetails) {
    auto ip = facebook::network::toIPAddress(*route.dest()->ip());
    routes.insert(
        folly::IPAddress::networkToString(
            {ip, static_cast<uint8_t>(*route.dest()->prefixLength())}));
  }
  return routes;
}

} // namespace

class RouteTest : public ::testing::Test {
 public:
  void SetUp() override {
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }
  virtual cfg::SwitchConfig initialConfig() const {
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

  std::shared_ptr<Route<IPAddressV4>> findRoute4(
      const std::shared_ptr<SwitchState>& state,
      RouterID rid,
      const RoutePrefixV4& prefix) {
    return findRouteImpl<IPAddressV4>(
        rid, {prefix.network(), prefix.mask()}, state);
  }
  std::shared_ptr<Route<IPAddressV6>> findRoute6(
      const std::shared_ptr<SwitchState>& state,
      RouterID rid,
      const RoutePrefixV6& prefix) {
    return findRouteImpl<IPAddressV6>(
        rid, {prefix.network(), prefix.mask()}, state);
  }
  std::shared_ptr<Route<IPAddressV4>> findRoute4(
      const std::shared_ptr<SwitchState>& state,
      RouterID rid,
      const std::string& prefixStr) {
    return findRoute4(state, rid, makePrefixV4(prefixStr));
  }
  std::shared_ptr<Route<IPAddressV6>> findRoute6(
      const std::shared_ptr<SwitchState>& state,
      RouterID rid,
      const std::string& prefixStr) {
    return findRoute6(state, rid, makePrefixV6(prefixStr));
  }

 private:
  template <typename AddrT>
  std::shared_ptr<Route<AddrT>> findRouteImpl(
      RouterID rid,
      const folly::CIDRNetwork& prefix,
      const std::shared_ptr<SwitchState>& state) {
    return findRoute<AddrT>(rid, prefix, state);
  }

 protected:
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(RouteTest, routeApi) {
  RoutePrefixV6 pfx6{folly::IPAddressV6("2001::"), 64};
  RouteNextHopSet nhops = makeNextHops({"2::10"});
  RouteNextHopEntry nhopEntry(nhops, DISTANCE);
  auto testRouteApi = [&](auto route) {
    EXPECT_TRUE(std::make_shared<RouteV6>(route.toThrift())->isSame(&route));
    EXPECT_EQ(pfx6, route.prefix());
    // Locally built route with no IDs allocated, so read inline explicitly.
    ClientNextHopsResolver readInline = [](const RouteNextHopEntry& entry) {
      return entry.getNextHopSet();
    };
    EXPECT_EQ(
        route.toRouteDetails(
            route.getForwardInfo().getNextHopSet(), std::nullopt, readInline),
        route.toRouteDetails(
            route.getForwardInfo().getNextHopSet(), std::nullopt, readInline));
    EXPECT_EQ(route.str(), route.str());
    EXPECT_EQ(route.flags(), 0);
    EXPECT_FALSE(route.isResolved());
    EXPECT_FALSE(route.isUnresolvable());
    EXPECT_FALSE(route.isConnected());
    EXPECT_FALSE(route.isProcessing());
    EXPECT_FALSE(route.isDrop());
    EXPECT_FALSE(route.isToCPU());
    EXPECT_TRUE(route.needResolve());
    EXPECT_EQ(
        route.getForwardInfo(),
        RouteNextHopEntry(
            RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));

    EXPECT_EQ(nhopEntry, *route.getEntryForClient(kClientA));
    auto [bestClient, bestNhopEntry] = route.getBestEntry();
    EXPECT_EQ(bestClient, kClientA);
    EXPECT_EQ(*bestNhopEntry, nhopEntry);
    EXPECT_FALSE(route.hasNoEntry());
    EXPECT_TRUE(route.has(kClientA, nhopEntry));
    EXPECT_FALSE(route.has(kClientB, nhopEntry));
    RouteNextHopEntry nhopEntry2(makeNextHops({"1.1.1.1"}), EBGP_DISTANCE);
    route.update(kClientB, nhopEntry2);
    EXPECT_TRUE(route.has(kClientB, nhopEntry2));
    EXPECT_FALSE(route.has(kClientA, nhopEntry2));
    std::tie(bestClient, bestNhopEntry) = route.getBestEntry();
    EXPECT_EQ(bestClient, kClientB);
    EXPECT_EQ(*bestNhopEntry, nhopEntry2);
    // del entry for client, should not be found after
    route.delEntryForClient(kClientA);
    EXPECT_FALSE(route.has(kClientA, nhopEntry));
    // get, set classID
    EXPECT_EQ(std::nullopt, route.getClassID());
    route.updateClassID(cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1);
    EXPECT_EQ(
        cfg::AclLookupClass::CLASS_QUEUE_PER_HOST_QUEUE_1, route.getClassID());
    // check counterID
    route.update(kClientA, nhopEntry);
    route.update(kClientB, nhopEntry2);
    EXPECT_EQ(std::nullopt, route.getForwardInfo().getCounterID());
    EXPECT_TRUE(!route.getEntryForClient(kClientA)->getCounterID());
    EXPECT_TRUE(!route.getEntryForClient(kClientB)->getCounterID());
    RouteNextHopEntry nhopEntry3(
        makeNextHops({"1.1.1.1"}), EBGP_DISTANCE, kCounterID1, kClassID1);
    RouteNextHopEntry nhopEntry4(
        makeNextHops({"2.2.2.2"}), EBGP_DISTANCE, kCounterID2, kClassID2);
    route.update(kClientA, nhopEntry3);
    route.update(kClientB, nhopEntry4);
    route.setResolved(nhopEntry3);
    EXPECT_EQ(kCounterID1, route.getForwardInfo().getCounterID());
    if (auto counter = route.getEntryForClient(kClientA)->getCounterID()) {
      EXPECT_EQ(kCounterID1, *counter);
    }
    if (auto counter = route.getEntryForClient(kClientB)->getCounterID()) {
      EXPECT_EQ(kCounterID2, *counter);
    }
    EXPECT_EQ(kClassID1, route.getForwardInfo().getClassID());
    if (auto classID = route.getEntryForClient(kClientA)->getClassID()) {
      EXPECT_EQ(kClassID1, *classID);
    }
    if (auto classID = route.getEntryForClient(kClientB)->getClassID()) {
      EXPECT_EQ(kClassID2, *classID);
    }
  };
  testRouteApi(RouteV6(RouteV6::makeThrift(pfx6, kClientA, nhopEntry)));
}

TEST_F(RouteTest, dedup) {
  auto stateV1 = this->sw_->getState();
  auto rid = RouterID(0);
  // 2 different nexthops
  RouteNextHopSet nhop1 = makeNextHops({"1.1.1.10"}); // resolved by intf 1
  RouteNextHopSet nhop2 = makeNextHops({"2.2.2.10"}); // resolved by intf 2
  // 4 prefixes
  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV4::Prefix r2{IPAddressV4("20.1.1.0"), 24};
  RouteV6::Prefix r3{IPAddressV6("1001::0"), 48};
  RouteV6::Prefix r4{IPAddressV6("2001::0"), 48};

  auto u2 = this->sw_->getRouteUpdater();
  u2.addRoute(
      rid,
      r1.network(),
      r1.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u2.addRoute(
      rid,
      r2.network(),
      r2.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u2.addRoute(
      rid,
      r3.network(),
      r3.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u2.addRoute(
      rid,
      r4.network(),
      r4.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u2.program();
  auto stateV2 = this->sw_->getState();
  EXPECT_NE(stateV1, stateV2);
  // Re-add the same routes; expect no change
  auto u3 = this->sw_->getRouteUpdater();
  u3.addRoute(
      rid,
      r1.network(),
      r1.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u3.addRoute(
      rid,
      r2.network(),
      r2.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u3.addRoute(
      rid,
      r3.network(),
      r3.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u3.addRoute(
      rid,
      r4.network(),
      r4.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u3.program();

  auto stateV3 = this->sw_->getState();
  EXPECT_EQ(stateV2, stateV3);
  // Re-add the same routes, except for one difference.  Expect an update.
  auto u4 = this->sw_->getRouteUpdater();
  u4.addRoute(
      rid,
      r1.network(),
      r1.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u4.addRoute(
      rid,
      r2.network(),
      r2.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u4.addRoute(
      rid,
      r3.network(),
      r3.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u4.addRoute(
      rid,
      r4.network(),
      r4.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u4.program();
  auto stateV4 = this->sw_->getState();
  EXPECT_NE(stateV4, stateV3);

  // get all 4 routes from stateV2
  auto stateV2r1 = this->findRoute4(stateV2, rid, r1);
  auto stateV2r2 = this->findRoute4(stateV2, rid, r2);
  auto stateV2r3 = this->findRoute6(stateV2, rid, r3);
  auto stateV2r4 = this->findRoute6(stateV2, rid, r4);

  // get all 4 routes from stateV4
  auto stateV4r1 = this->findRoute4(stateV4, rid, r1);
  auto stateV4r2 = this->findRoute4(stateV4, rid, r2);
  auto stateV4r3 = this->findRoute6(stateV4, rid, r3);
  auto stateV4r4 = this->findRoute6(stateV4, rid, r4);

  EXPECT_EQ(stateV2r1, stateV4r1);
  EXPECT_NE(stateV2r2, stateV4r2); // different routes
  EXPECT_EQ(stateV2r2->getGeneration() + 1, stateV4r2->getGeneration());
  EXPECT_EQ(stateV2r3, stateV4r3);
  EXPECT_EQ(stateV2r4, stateV4r4);
}

TEST_F(RouteTest, resolve) {
  auto rid = RouterID(0);
  auto stateV1 = this->sw_->getState();
  // recursive lookup
  {
    auto u1 = this->sw_->getRouteUpdater();
    RouteNextHopSet nexthops1 =
        makeNextHops({"1.1.1.10"}); // resolved by intf 1
    u1.addRoute(
        rid,
        IPAddress("1.1.3.0"),
        24,
        kClientA,
        RouteNextHopEntry(nexthops1, DISTANCE));
    RouteNextHopSet nexthops2 = makeNextHops({"1.1.3.10"}); // rslvd. by
                                                            // '1.1.3/24'
    u1.addRoute(
        rid,
        IPAddress("8.8.8.0"),
        24,
        kClientA,
        RouteNextHopEntry(nexthops2, DISTANCE));
    u1.program();
    auto stateV2 = this->sw_->getState();
    EXPECT_NE(stateV1, stateV2);

    auto r21 = this->findRoute4(stateV2, rid, "1.1.3.0/24");
    EXPECT_RESOLVED(r21);
    EXPECT_FALSE(r21->isConnected());

    auto r22 = this->findRoute4(stateV2, rid, "8.8.8.0/24");
    EXPECT_RESOLVED(r22);
    EXPECT_FALSE(r22->isConnected());
    // r21 and r22 are different routes
    EXPECT_NE(r21, r22);
    EXPECT_NE(r21->prefix(), r22->prefix());
    // check the forwarding info
    RouteNextHopSet expFwd2;
    expFwd2.emplace(
        ResolvedNextHop(IPAddress("1.1.1.10"), InterfaceID(1), ECMP_WEIGHT));
    EXPECT_EQ(expFwd2, getNextHops(stateV2, r21->getForwardInfo()));
    EXPECT_EQ(expFwd2, getNextHops(stateV2, r22->getForwardInfo()));
  }
  // recursive lookup loop
  {
    // create a route table w/ the following 3 routes
    // 1. 30/8 -> 20.1.1.1
    // 2. 20/8 -> 10.1.1.1
    // 3. 10/8 -> 30.1.1.1
    // The above 3 routes causes lookup loop, which should result in
    // all unresolvable.
    auto u1 = this->sw_->getRouteUpdater();
    u1.addRoute(
        rid,
        IPAddress("30.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"20.1.1.1"}), DISTANCE));
    u1.addRoute(
        rid,
        IPAddress("20.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"10.1.1.1"}), DISTANCE));
    u1.addRoute(
        rid,
        IPAddress("10.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"30.1.1.1"}), DISTANCE));
    u1.program();
    auto stateV2 = this->sw_->getState();
    EXPECT_NE(stateV1, stateV2);

    auto verifyPrefix = [&](std::string prefixStr) {
      auto route = this->findRoute4(stateV2, rid, prefixStr);
      // In standalone RIB, unresolved routes never make it to FIB
      EXPECT_EQ(route, nullptr);
    };
    verifyPrefix("10.0.0.0/8");
    verifyPrefix("20.0.0.0/8");
    verifyPrefix("30.0.0.0/8");
  }
  // recursive lookup across 2 updates
  {
    auto u1 = this->sw_->getRouteUpdater();
    RouteNextHopSet nexthops1 = makeNextHops({"50.0.0.1"});
    u1.addRoute(
        rid,
        IPAddress("40.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(nexthops1, DISTANCE));

    u1.program();

    auto stateV2 = this->sw_->getState();
    // 40.0.0.0/8 -> 50.0.0.1 which should be resolved by default
    // NULL route
    auto r21 = this->findRoute4(stateV2, rid, "40.0.0.0/8");
    EXPECT_TRUE(r21->isResolved());
    EXPECT_TRUE(r21->isDrop());
    EXPECT_FALSE(r21->isConnected());
    EXPECT_FALSE(r21->needResolve());

    // Resolve 50.0.0.1 this should also resolve 40.0.0.0/8
    auto u2 = this->sw_->getRouteUpdater();
    u2.addRoute(
        rid,
        IPAddress("50.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"1.1.1.1"}), DISTANCE));
    u2.program();

    // 40.0.0.0/8 should be resolved
    auto stateV3 = this->sw_->getState();
    auto r31 = this->findRoute4(stateV3, rid, "40.0.0.0/8");
    EXPECT_RESOLVED(r31);
    EXPECT_FALSE(r31->isConnected());

    // 50.0.0.1/32 will recurse to 50.0.0.0/8->1.1.1.1 (connected)
    auto r31NextHops = getNextHops(stateV3, r31->getForwardInfo());
    EXPECT_EQ(1, r31NextHops.size());
    auto r32 = findLongestMatchRoute(
        this->sw_->getRib(), rid, r31NextHops.begin()->addr().asV4(), stateV3);
    EXPECT_RESOLVED(r32);
    EXPECT_TRUE(r32->isConnected());

    // 50.0.0.0/8 should be resolved
    auto r33 = this->findRoute4(stateV3, rid, "50.0.0.0/8");
    EXPECT_RESOLVED(r33);
    EXPECT_FALSE(r33->isConnected());
  }
}

TEST_F(RouteTest, resolveV4AndV6BgpOverIpv6LinkLocalNextHops) {
  const auto rid = RouterID(0);
  const auto bgpV6Route = IPAddressV6("2000::1");
  const auto bgpV4Route = IPAddressV4("200.10.0.0");
  const std::array<IPAddressV6, 2> bgpNextHopAddrs{
      IPAddressV6("fdad:db00::1"),
      IPAddressV6("fdad:dbff::1"),
  };
  const auto linkLocalNextHopAddr = IPAddressV6("fe80:face:b00c::1");

  RouteNextHopSet bgpNextHops;
  for (const auto& bgpNextHopAddr : bgpNextHopAddrs) {
    bgpNextHops.emplace(UnresolvedNextHop(bgpNextHopAddr, ECMP_WEIGHT));
  }

  RouteNextHopSet expectedLinkLocalNextHops;
  for (const auto& intf : kInterfaces) {
    expectedLinkLocalNextHops.emplace(
        ResolvedNextHop(linkLocalNextHopAddr, intf, ECMP_WEIGHT));
  }

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      bgpV6Route,
      64,
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));
  updater.addRoute(
      rid,
      bgpV4Route,
      24,
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));
  for (const auto& bgpNextHopAddr : bgpNextHopAddrs) {
    updater.addRoute(
        rid,
        bgpNextHopAddr,
        128,
        ClientID::OPENR,
        RouteNextHopEntry(
            expectedLinkLocalNextHops, AdminDistance::DIRECTLY_CONNECTED));
  }
  updater.program();

  const auto routeState = this->sw_->getState();
  const auto& routeV6 =
      findLongestMatchRoute(this->sw_->getRib(), rid, bgpV6Route, routeState);
  EXPECT_RESOLVED(routeV6);
  EXPECT_EQ(
      expectedLinkLocalNextHops,
      getNextHops(routeState, routeV6->getForwardInfo()));

  const auto& routeV4 =
      findLongestMatchRoute(this->sw_->getRib(), rid, bgpV4Route, routeState);
  EXPECT_RESOLVED(routeV4);
  EXPECT_EQ(
      expectedLinkLocalNextHops,
      getNextHops(routeState, routeV4->getForwardInfo()));
}

TEST_F(RouteTest, resolveDropToCPUMix) {
  auto rid = RouterID(0);

  // add a DROP route and a ToCPU route
  auto u1 = this->sw_->getRouteUpdater();
  u1.addRoute(
      rid,
      IPAddress("11.1.1.0"),
      24,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  u1.addRoute(
      rid,
      IPAddress("22.1.1.0"),
      24,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  // then, add a route with 4 nexthops. One to each interface, one
  // to the DROP and one to the ToCPU
  RouteNextHopSet nhops = makeNextHops(
      {"1.1.1.10", // intf 1
       "2.2.2.10", // intf 2
       "11.1.1.10", // DROP
       "22.1.1.10"}); // ToCPU
  u1.addRoute(
      rid,
      IPAddress("8.8.8.0"),
      24,
      kClientA,
      RouteNextHopEntry(nhops, DISTANCE));
  u1.program();
  auto stateV2 = this->sw_->getState();
  {
    auto r2 = this->findRoute4(stateV2, rid, "8.8.8.0/24");
    EXPECT_RESOLVED(r2);
    EXPECT_FALSE(r2->isDrop());
    EXPECT_FALSE(r2->isToCPU());
    EXPECT_FALSE(r2->isConnected());
    const auto& fwd = r2->getForwardInfo();
    EXPECT_EQ(RouteForwardAction::NEXTHOPS, fwd.getAction());
    EXPECT_EQ(2, getNextHops(stateV2, fwd).size());
  }

  // now update the route with just DROP and ToCPU, expect ToCPU to win
  auto u2 = this->sw_->getRouteUpdater();
  RouteNextHopSet nhops2 = makeNextHops(
      {"11.1.1.10", // DROP
       "22.1.1.10"}); // ToCPU
  u2.addRoute(
      rid,
      IPAddress("8.8.8.0"),
      24,
      kClientA,
      RouteNextHopEntry(nhops2, DISTANCE));
  u2.program();
  auto stateV3 = this->sw_->getState();
  {
    auto r2 = this->findRoute4(stateV3, rid, "8.8.8.0/24");
    EXPECT_RESOLVED(r2);
    EXPECT_FALSE(r2->isDrop());
    EXPECT_TRUE(r2->isToCPU());
    EXPECT_FALSE(r2->isConnected());
    const auto& fwd = r2->getForwardInfo();
    EXPECT_EQ(RouteForwardAction::TO_CPU, fwd.getAction());
    EXPECT_EQ(0, getNextHops(stateV3, fwd).size());
  }
  // now update the route with just DROP
  auto u3 = this->sw_->getRouteUpdater();
  RouteNextHopSet nhops3 = makeNextHops({"11.1.1.10"}); // DROP
  u3.addRoute(
      rid,
      IPAddress("8.8.8.0"),
      24,
      kClientA,
      RouteNextHopEntry(nhops3, DISTANCE));
  u3.program();
  auto stateV4 = this->sw_->getState();
  {
    auto r2 = this->findRoute4(stateV4, rid, "8.8.8.0/24");
    EXPECT_RESOLVED(r2);
    EXPECT_TRUE(r2->isDrop());
    EXPECT_FALSE(r2->isToCPU());
    EXPECT_FALSE(r2->isConnected());
    const auto& fwd = r2->getForwardInfo();
    EXPECT_EQ(RouteForwardAction::DROP, fwd.getAction());
    EXPECT_EQ(0, getNextHops(stateV4, fwd).size());
  }
}

// Testing add and delete ECMP routes
TEST_F(RouteTest, addDel) {
  auto rid = RouterID(0);

  RouteNextHopSet nexthops = makeNextHops(
      {"1.1.1.10", // intf 1
       "2::2", // intf 2
       "1.1.2.10"}); // Drop (via default null route)
  RouteNextHopSet nexthops2 = makeNextHops(
      {"1.1.3.10", // Drop (via default null route)
       "11:11::1"}); // Drop (via default null route)

  auto u1 = this->sw_->getRouteUpdater();
  u1.addRoute(
      rid,
      IPAddress("10.1.1.1"),
      24,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u1.addRoute(
      rid,
      IPAddress("2001::1"),
      48,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u1.program();

  auto stateV2 = this->sw_->getState();
  // v4 route
  auto r2 = this->findRoute4(stateV2, rid, "10.1.1.0/24");
  EXPECT_RESOLVED(r2);
  EXPECT_FALSE(r2->isDrop());
  EXPECT_FALSE(r2->isToCPU());
  EXPECT_FALSE(r2->isConnected());
  // v6 route
  auto r2v6 = this->findRoute6(stateV2, rid, "2001::0/48");
  EXPECT_RESOLVED(r2v6);
  EXPECT_FALSE(r2v6->isDrop());
  EXPECT_FALSE(r2v6->isToCPU());
  EXPECT_FALSE(r2v6->isConnected());
  // forwarding info
  EXPECT_EQ(RouteForwardAction::NEXTHOPS, r2->getForwardInfo().getAction());
  EXPECT_EQ(RouteForwardAction::NEXTHOPS, r2v6->getForwardInfo().getAction());
  const auto& fwd2 = getNextHops(stateV2, r2->getForwardInfo());
  const auto& fwd2v6 = getNextHops(stateV2, r2v6->getForwardInfo());
  EXPECT_EQ(2, fwd2.size());
  EXPECT_EQ(2, fwd2v6.size());
  RouteNextHopSet expFwd2;
  expFwd2.emplace(
      ResolvedNextHop(IPAddress("1.1.1.10"), InterfaceID(1), ECMP_WEIGHT));
  expFwd2.emplace(
      ResolvedNextHop(IPAddress("2::2"), InterfaceID(2), ECMP_WEIGHT));
  EXPECT_EQ(expFwd2, fwd2);
  EXPECT_EQ(expFwd2, fwd2v6);

  // change the nexthops of the V4 route
  auto u2 = this->sw_->getRouteUpdater();
  u2.addRoute(
      rid,
      IPAddress("10.1.1.1"),
      24,
      kClientA,
      RouteNextHopEntry(nexthops2, DISTANCE));
  u2.program();
  auto stateV3 = this->sw_->getState();

  auto r3 = this->findRoute4(stateV3, rid, "10.1.1.0/24");
  ASSERT_NE(nullptr, r3);
  EXPECT_TRUE(r3->isResolved()); // Resolved to default NULL
  EXPECT_TRUE(r3->isDrop());
  EXPECT_FALSE(r3->isConnected());
  EXPECT_FALSE(r3->needResolve());

  // re-add the same route does not cause change
  auto u3 = this->sw_->getRouteUpdater();
  u3.addRoute(
      rid,
      IPAddress("10.1.1.1"),
      24,
      kClientA,
      RouteNextHopEntry(nexthops2, DISTANCE));
  u3.program();
  EXPECT_EQ(stateV3, this->sw_->getState());

  // now delete the V4 route
  auto u4 = this->sw_->getRouteUpdater();
  u4.delRoute(rid, IPAddress("10.1.1.1"), 24, kClientA);
  u4.program();

  auto r5 = this->findRoute4(this->sw_->getState(), rid, "10.1.1.0/24");
  EXPECT_EQ(nullptr, r5);

  // change an old route to punt to CPU, add a new route to DROP
  auto u5 = this->sw_->getRouteUpdater();
  u5.addRoute(
      rid,
      IPAddress("10.1.1.0"),
      24,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  u5.addRoute(
      rid,
      IPAddress("10.1.2.0"),
      24,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  u5.program();
  auto stateV6 = this->sw_->getState();

  auto r6_1 = this->findRoute4(stateV6, rid, "10.1.1.0/24");
  EXPECT_RESOLVED(r6_1);
  EXPECT_FALSE(r6_1->isConnected());
  EXPECT_TRUE(r6_1->isToCPU());
  EXPECT_FALSE(r6_1->isDrop());
  EXPECT_EQ(RouteForwardAction::TO_CPU, r6_1->getForwardInfo().getAction());

  auto r6_2 = this->findRoute4(stateV6, rid, "10.1.2.0/24");
  EXPECT_RESOLVED(r6_2);
  EXPECT_FALSE(r6_2->isConnected());
  EXPECT_FALSE(r6_2->isToCPU());
  EXPECT_TRUE(r6_2->isDrop());
  EXPECT_EQ(RouteForwardAction::DROP, r6_2->getForwardInfo().getAction());
}

TEST_F(RouteTest, InterfaceRoutes) {
  RouterID rid = RouterID(0);
  auto stateV1 = this->sw_->getState();
  // verify the ipv4 interface route
  {
    auto rt = this->findRoute4(stateV1, rid, "1.1.1.0/24");
    EXPECT_EQ(0, rt->getGeneration());
    EXPECT_RESOLVED(rt);
    EXPECT_TRUE(rt->isConnected());
    EXPECT_FALSE(rt->isToCPU());
    EXPECT_FALSE(rt->isDrop());
    EXPECT_EQ(RouteForwardAction::NEXTHOPS, rt->getForwardInfo().getAction());
    EXPECT_FWD_INFO(stateV1, rt, InterfaceID(1), "1.1.1.1");
  }
  // verify the ipv6 interface route
  {
    auto rt = this->findRoute6(stateV1, rid, "2::0/48");
    EXPECT_EQ(0, rt->getGeneration());
    EXPECT_RESOLVED(rt);
    EXPECT_TRUE(rt->isConnected());
    EXPECT_FALSE(rt->isToCPU());
    EXPECT_FALSE(rt->isDrop());
    EXPECT_EQ(RouteForwardAction::NEXTHOPS, rt->getForwardInfo().getAction());
    EXPECT_FWD_INFO(stateV1, rt, InterfaceID(2), "2::1");
  }

  {
    // verify v6 link local route
    auto rt = this->findRoute6(stateV1, rid, "fe80::/64");
    EXPECT_EQ(0, rt->getGeneration());
    EXPECT_RESOLVED(rt);
    EXPECT_FALSE(rt->isConnected());
    EXPECT_TRUE(rt->isToCPU());
    EXPECT_EQ(RouteForwardAction::TO_CPU, rt->getForwardInfo().getAction());
    const auto& fwds = getNextHops(stateV1, rt->getForwardInfo());
    EXPECT_EQ(0, fwds.size());
  }

  auto config = this->initialConfig();
  // swap the interface addresses which causes route change
  config.interfaces()[1].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[1].ipAddresses()[1] = "1::1/48";
  config.interfaces()[0].ipAddresses()[0] = "2.2.2.2/24";
  config.interfaces()[0].ipAddresses()[1] = "2::1/48";
  this->sw_->applyConfig("New config", config);
  auto stateV2 = this->sw_->getState();
  EXPECT_NE(stateV1, stateV2);
  auto expectedGen = 1;
  // verify the ipv4 route
  {
    auto rt = this->findRoute4(stateV2, rid, "1.1.1.0/24");
    EXPECT_EQ(expectedGen, rt->getGeneration());
    EXPECT_FWD_INFO(stateV2, rt, InterfaceID(2), "1.1.1.1");
  }
  // verify the ipv6 route
  {
    auto rt = this->findRoute6(stateV2, rid, "2::0/48");
    EXPECT_EQ(expectedGen, rt->getGeneration());
    EXPECT_FWD_INFO(stateV2, rt, InterfaceID(1), "2::1");
  }
}

TEST_F(RouteTest, dropRoutes) {
  auto rid = RouterID(0);
  auto u1 = this->sw_->getRouteUpdater();
  u1.addRoute(
      rid,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  u1.addRoute(
      rid,
      IPAddress("2001::0"),
      128,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
  // Check recursive resolution for drop routes
  RouteNextHopSet v4nexthops = makeNextHops({"10.10.10.10"});
  u1.addRoute(
      rid,
      IPAddress("20.20.20.0"),
      24,
      kClientA,
      RouteNextHopEntry(v4nexthops, DISTANCE));
  RouteNextHopSet v6nexthops = makeNextHops({"2001::0"});
  u1.addRoute(
      rid,
      IPAddress("2001:1::"),
      64,
      kClientA,
      RouteNextHopEntry(v6nexthops, DISTANCE));

  u1.program();
  auto state2 = this->sw_->getState();

  // Check routes
  auto r1 = this->findRoute4(state2, rid, "10.10.10.10/32");
  EXPECT_RESOLVED(r1);
  EXPECT_FALSE(r1->isConnected());
  EXPECT_TRUE(
      r1->has(kClientA, RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE)));
  EXPECT_EQ(
      r1->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));

  auto r2 = this->findRoute4(state2, rid, "20.20.20.0/24");
  EXPECT_RESOLVED(r2);
  EXPECT_FALSE(r2->isConnected());
  EXPECT_FALSE(
      r2->has(kClientA, RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE)));
  EXPECT_EQ(
      r2->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));

  auto r3 = this->findRoute6(state2, rid, "2001::0/128");
  EXPECT_RESOLVED(r3);
  EXPECT_FALSE(r3->isConnected());
  EXPECT_TRUE(
      r3->has(kClientA, RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE)));
  EXPECT_EQ(
      r3->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));

  auto r4 = this->findRoute6(state2, rid, "2001:1::/64");
  EXPECT_RESOLVED(r4);
  EXPECT_FALSE(r4->isConnected());
  EXPECT_EQ(
      r4->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::DROP, DISTANCE));
}

TEST_F(RouteTest, toCPURoutes) {
  auto rid = RouterID(0);
  auto u1 = this->sw_->getRouteUpdater();
  u1.addRoute(
      rid,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  u1.addRoute(
      rid,
      IPAddress("2001::0"),
      128,
      kClientA,
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
  // Check recursive resolution for to_cpu routes
  RouteNextHopSet v4nexthops = makeNextHops({"10.10.10.10"});
  u1.addRoute(
      rid,
      IPAddress("20.20.20.0"),
      24,
      kClientA,
      RouteNextHopEntry(v4nexthops, DISTANCE));
  RouteNextHopSet v6nexthops = makeNextHops({"2001::0"});
  u1.addRoute(
      rid,
      IPAddress("2001:1::"),
      64,
      kClientA,
      RouteNextHopEntry(v6nexthops, DISTANCE));

  u1.program();
  auto state2 = this->sw_->getState();

  // Check routes
  auto r1 = this->findRoute4(state2, rid, "10.10.10.10/32");
  EXPECT_RESOLVED(r1);
  EXPECT_FALSE(r1->isConnected());
  EXPECT_TRUE(r1->has(
      kClientA, RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE)));
  EXPECT_EQ(
      r1->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));

  auto r2 = this->findRoute4(state2, rid, "20.20.20.0/24");
  EXPECT_RESOLVED(r2);
  EXPECT_FALSE(r2->isConnected());
  EXPECT_EQ(
      r2->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));

  auto r3 = this->findRoute6(state2, rid, "2001::0/128");
  EXPECT_RESOLVED(r3);
  EXPECT_FALSE(r3->isConnected());
  EXPECT_TRUE(r3->has(
      kClientA, RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE)));
  EXPECT_EQ(
      r3->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));

  auto r5 = this->findRoute6(state2, rid, "2001:1::/64");
  EXPECT_RESOLVED(r5);
  EXPECT_FALSE(r5->isConnected());
  EXPECT_EQ(
      r5->getForwardInfo(),
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));
}

namespace TEMP {
struct Route {
  uint32_t vrf;
  IPAddress prefix;
  uint8_t len;
  Route(uint32_t vrf, IPAddress prefix, uint8_t len)
      : vrf(vrf), prefix(prefix), len(len) {}
  bool operator<(const Route& rt) const {
    return std::tie(vrf, len, prefix) < std::tie(rt.vrf, rt.len, rt.prefix);
  }
  bool operator==(const Route& rt) const {
    return vrf == rt.vrf && len == rt.len && prefix == rt.prefix;
  }
};
} // namespace TEMP

void checkChangedRoute(
    const shared_ptr<SwitchState>& oldState,
    const shared_ptr<SwitchState>& newState,
    const std::set<TEMP::Route> changedIDs,
    const std::set<TEMP::Route> addedIDs,
    const std::set<TEMP::Route> removedIDs) {
  std::set<TEMP::Route> foundChanged;
  std::set<TEMP::Route> foundAdded;
  std::set<TEMP::Route> foundRemoved;
  StateDelta delta(oldState, newState);

  forEachChangedRoute(
      delta,
      [&](RouterID id, const auto& oldRt, const auto& newRt) {
        EXPECT_EQ(oldRt->prefix(), newRt->prefix());
        EXPECT_NE(oldRt, newRt);
        const auto prefix = newRt->prefix();
        auto ret = foundChanged.insert(
            TEMP::Route(id, IPAddress(prefix.network()), prefix.mask()));
        EXPECT_TRUE(ret.second);
      },
      [&](RouterID id, const auto& rt) {
        const auto prefix = rt->prefix();
        auto ret = foundAdded.insert(
            TEMP::Route(id, IPAddress(prefix.network()), prefix.mask()));
        EXPECT_TRUE(ret.second);
      },
      [&](RouterID id, const auto& rt) {
        const auto prefix = rt->prefix();
        auto ret = foundRemoved.insert(
            TEMP::Route(id, IPAddress(prefix.network()), prefix.mask()));
        EXPECT_TRUE(ret.second);
      });

  EXPECT_EQ(changedIDs, foundChanged);
  EXPECT_EQ(addedIDs, foundAdded);
  EXPECT_EQ(removedIDs, foundRemoved);
}

TEST_F(RouteTest, applyNewConfig) {
  auto config = this->initialConfig();
  config.vlans()->resize(6);
  config.vlans()[4].id() = 5;
  config.vlans()[5].id() = 6;
  config.interfaces()->resize(6);
  config.interfaces()[4].intfID() = 5;
  config.interfaces()[4].vlanID() = 5;
  config.interfaces()[4].routerID() = 0;
  config.interfaces()[4].mac() = "00:00:00:00:00:55";
  config.interfaces()[4].ipAddresses()->resize(4);
  config.interfaces()[4].ipAddresses()[0] = "5.1.1.1/24";
  config.interfaces()[4].ipAddresses()[1] = "5.1.1.2/24";
  config.interfaces()[4].ipAddresses()[2] = "5.1.1.10/24";
  config.interfaces()[4].ipAddresses()[3] = "::1/48";
  config.interfaces()[5].intfID() = 6;
  config.interfaces()[5].vlanID() = 6;
  config.interfaces()[5].routerID() = 1;
  config.vrfs().ensure().emplace_back().vrfId() = 1;
  config.interfaces()[5].mac() = "00:00:00:00:00:66";
  config.interfaces()[5].ipAddresses()->resize(2);
  config.interfaces()[5].ipAddresses()[0] = "5.1.1.1/24";
  config.interfaces()[5].ipAddresses()[1] = "::1/48";

  auto stateV0 = this->sw_->getState();
  this->sw_->applyConfig("Apply config1", config);
  auto stateV1 = this->sw_->getState();
  ASSERT_NE(nullptr, stateV1);

  checkChangedRoute(
      stateV0,
      stateV1,
      {},
      {
          TEMP::Route{0, IPAddress("5.1.1.0"), 24},
          TEMP::Route{0, IPAddress("::0"), 48},
          TEMP::Route{1, IPAddress("5.1.1.0"), 24},
          TEMP::Route{1, IPAddress("::0"), 48},
          TEMP::Route{1, IPAddress("fe80::"), 64},
      },
      {});
  // change an interface address
  config.interfaces()[4].ipAddresses()[3] = "11::11/48";

  this->sw_->applyConfig("Apply config2", config);
  auto stateV2 = this->sw_->getState();
  ASSERT_NE(nullptr, stateV2);

  checkChangedRoute(
      stateV1,
      stateV2,
      {},
      {TEMP::Route{0, IPAddress("11::0"), 48}},
      {TEMP::Route{0, IPAddress("::0"), 48}});

  // move one interface to cause same route prefix conflict
  config.interfaces()[5].routerID() = 0;
  EXPECT_THROW(this->sw_->applyConfig("Bogus config", config), FbossError);

  // add a new interface in a new VRF
  config.vlans()->resize(7);
  config.vlans()[6].id() = 7;
  config.interfaces()->resize(7);
  config.interfaces()[6].intfID() = 7;
  config.interfaces()[6].vlanID() = 7;
  config.interfaces()[6].routerID() = 2;
  config.vrfs().ensure().emplace_back().vrfId() = 2;
  config.interfaces()[6].mac() = "00:00:00:00:00:77";
  config.interfaces()[6].ipAddresses()->resize(2);
  config.interfaces()[6].ipAddresses()[0] = "1.1.1.1/24";
  config.interfaces()[6].ipAddresses()[1] = "::1/48";
  // and move one interface to another vrf and fix the address conflict
  config.interfaces()[5].routerID() = 0;
  config.interfaces()[5].ipAddresses()->resize(2);
  config.interfaces()[5].ipAddresses()[0] = "5.2.2.1/24";
  config.interfaces()[5].ipAddresses()[1] = "5::6/48";

  this->sw_->applyConfig("Apply config", config);
  auto stateV3 = this->sw_->getState();
  ASSERT_NE(nullptr, stateV3);
  checkChangedRoute(
      stateV2,
      stateV3,
      {},
      {
          TEMP::Route{0, IPAddress("5.2.2.0"), 24},
          TEMP::Route{0, IPAddress("5::0"), 48},
          TEMP::Route{2, IPAddress("1.1.1.0"), 24},
          TEMP::Route{2, IPAddress("::0"), 48},
          TEMP::Route{2, IPAddress("fe80::"), 64},
      },
      {
          TEMP::Route{1, IPAddress("5.1.1.0"), 24},
          TEMP::Route{1, IPAddress("::0"), 48},
          TEMP::Route{1, IPAddress("fe80::"), 64},
      });

  // re-apply the same configure generates no change
  this->sw_->applyConfig("Apply config", config);
  auto stateV4 = this->sw_->getState();
  ASSERT_NE(nullptr, stateV4);
  if (stateV4) {
    // FIXME - reapplying the same config on standalone rib should yield null,
    // but it does result in new state. The state delta is empty though, so
    // no change will be programmed down
    checkChangedRoute(stateV3, stateV4, {}, {}, {});
  }
}

TEST_F(RouteTest, changedRoutesPostUpdate) {
  auto rid = RouterID(0);
  RouteNextHopSet nexthops = makeNextHops(
      {"1.1.1.10", // resolved by intf 1
       "2::2"}); // resolved by intf 2

  auto state1 = this->sw_->getState();
  // Add a couple of routes
  auto u1 = this->sw_->getRouteUpdater();
  u1.addRoute(
      rid,
      IPAddress("10.1.1.0"),
      24,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u1.addRoute(
      rid,
      IPAddress("2001::0"),
      48,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u1.program();
  auto state2 = this->sw_->getState();
  // v4 route
  auto rtV4 = this->findRoute4(state2, rid, "10.1.1.0/24");
  EXPECT_TRUE(rtV4->isResolved());
  EXPECT_FALSE(rtV4->isConnected());
  // v6 route
  auto rtV6 = this->findRoute6(state2, rid, "2001::/48");
  EXPECT_TRUE(rtV6->isResolved());
  EXPECT_FALSE(rtV6->isConnected());
  checkChangedRoute(
      state1,
      state2,
      {},
      {
          TEMP::Route{0, IPAddress("10.1.1.0"), 24},
          TEMP::Route{0, IPAddress("2001::0"), 48},
      },
      {});
  // Add 2 more routes
  auto u2 = this->sw_->getRouteUpdater();
  u2.addRoute(
      rid,
      IPAddress("10.10.1.0"),
      24,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u2.addRoute(
      rid,
      IPAddress("2001:10::0"),
      48,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  u2.program();
  auto state3 = this->sw_->getState();
  // v4 route
  rtV4 = this->findRoute4(state3, rid, "10.10.1.0/24");
  EXPECT_TRUE(rtV4->isResolved());
  EXPECT_FALSE(rtV4->isConnected());
  // v6 route
  rtV6 = this->findRoute6(state3, rid, "2001:10::/48");
  EXPECT_TRUE(rtV6->isResolved());
  EXPECT_FALSE(rtV6->isConnected());
  checkChangedRoute(
      state2,
      state3,
      {},
      {
          TEMP::Route{0, IPAddress("10.10.1.0"), 24},
          TEMP::Route{0, IPAddress("2001:10::"), 48},
      },
      {});
}

TEST_F(RouteTest, PruneAddedRoutes) {
  auto rid0 = RouterID(0);
  auto updater = this->sw_->getRouteUpdater();
  auto r1prefix = IPAddressV4("20.0.1.51");
  auto r1prefixLen = 24;
  auto r1nexthops = makeNextHops({"10.0.0.1", "30.0.21.51" /* unresolved */});
  updater.addRoute(
      rid0,
      r1prefix,
      r1prefixLen,
      kClientA,
      RouteNextHopEntry(r1nexthops, DISTANCE));

  updater.program();
  RouteV4::Prefix prefix1{IPAddressV4("20.0.1.51"), 24};

  auto newRouteEntry = findRoute<IPAddressV4>(
      rid0, prefix1.toCidrNetwork(), this->sw_->getState());
  EXPECT_NE(nullptr, newRouteEntry);
  EXPECT_EQ(
      newRouteEntry->prefix(), (RouteV4::Prefix{IPAddressV4("20.0.1.0"), 24}));

  EXPECT_TRUE(newRouteEntry->isPublished());
  auto revertState = this->sw_->getState();
  SwitchState::revertNewRouteEntry<IPAddressV4>(
      rid0, newRouteEntry, nullptr, &revertState);
  // Make sure that state3 changes as a result of pruning
  auto remainingRouteEntry =
      findRoute<IPAddressV4>(rid0, prefix1.toCidrNetwork(), revertState);
  // Route removed after delete
  EXPECT_EQ(nullptr, remainingRouteEntry);
}

// Test that pruning of changed routes happens correctly.
TEST_F(RouteTest, PruneChangedRoutes) {
  // Add routes
  // Change one of them
  // Prune the changed one
  // Check that the pruning happened correctly
  // state1
  //  ... Add route for prefix41
  //  ... Add route for prefix42 (RouteForwardAction::TO_CPU)
  // state2
  auto rid0 = RouterID(0);
  auto updater = this->sw_->getRouteUpdater();

  RouteV4::Prefix prefix41{IPAddressV4("20.0.21.41"), 32};
  auto nexthops41 = makeNextHops({"10.0.0.1", "face:b00c:0:21::41"});
  updater.addRoute(
      rid0,
      prefix41.network(),
      prefix41.mask(),
      kClientA,
      RouteNextHopEntry(nexthops41, DISTANCE));

  RouteV6::Prefix prefix42{IPAddressV6("facf:b00c:0:21::42"), 96};
  updater.addRoute(
      rid0,
      prefix42.network(),
      prefix42.mask(),
      kClientA,
      RouteNextHopEntry(RouteForwardAction::TO_CPU, DISTANCE));

  updater.program();

  auto oldEntry = findRoute<IPAddressV6>(
      rid0, prefix42.toCidrNetwork(), this->sw_->getState());
  EXPECT_NE(nullptr, oldEntry);
  EXPECT_TRUE(oldEntry->isToCPU());

  // state2
  //  ... Make route for prefix42 resolve to actual nexthops
  // state3

  auto nexthops42 = makeNextHops({"10.0.0.1", "face:b00c:0:21::42"});
  updater.addRoute(
      rid0,
      prefix42.network(),
      prefix42.mask(),
      kClientA,
      RouteNextHopEntry(nexthops42, DISTANCE));
  updater.program();

  auto newEntry = findRoute<IPAddressV6>(
      rid0, prefix42.toCidrNetwork(), this->sw_->getState());

  EXPECT_FALSE(newEntry->isToCPU());
  // state3
  //  ... revert route for prefix42
  // state4
  auto revertState = this->sw_->getState();
  SwitchState::revertNewRouteEntry(rid0, newEntry, oldEntry, &revertState);

  auto revertedEntry =
      findRoute<IPAddressV6>(rid0, prefix42.toCidrNetwork(), revertState);
  EXPECT_TRUE(revertedEntry->isToCPU());
}

// Test adding and deleting per-client nexthops lists
TEST_F(RouteTest, modRoutes) {
  auto u1 = this->sw_->getRouteUpdater();

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.10"), 32};
  RouteV4::Prefix prefix99{IPAddressV4("99.99.99.99"), 32};

  RouteNextHopSet nexthops1 =
      makeResolvedNextHops({{InterfaceID(1), "1.1.1.1"}});
  RouteNextHopSet nexthops2 =
      makeResolvedNextHops({{InterfaceID(2), "2.2.2.2"}});
  RouteNextHopSet nexthops3 =
      makeResolvedNextHops({{InterfaceID(3), "3.3.3.3"}});

  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientB,
      RouteNextHopEntry(nexthops2, DISTANCE));
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(nexthops1, DISTANCE));
  u1.addRoute(
      kRid0,
      IPAddress("99.99.99.99"),
      32,
      kClientA,
      RouteNextHopEntry(nexthops3, DISTANCE));
  u1.program();

  auto state1 = this->sw_->getState();
  auto rt10 = this->findRoute4(state1, kRid0, prefix10);
  auto rt99 = this->findRoute4(state1, kRid0, prefix99);
  EXPECT_EQ(getNextHops(state1, rt10->getForwardInfo()), nexthops1);
  EXPECT_EQ(getNextHops(state1, rt99->getForwardInfo()), nexthops3);

  auto u2 = this->sw_->getRouteUpdater();
  u2.delRoute(kRid0, IPAddress("10.10.10.10"), 32, kClientA);
  u2.program();
  // 10.10.10.10 should now get clientBs nhops
  auto state2 = this->sw_->getState();
  rt10 = this->findRoute4(state2, kRid0, prefix10);
  rt99 = this->findRoute4(state2, kRid0, prefix99);
  EXPECT_EQ(getNextHops(state2, rt10->getForwardInfo()), nexthops2);
  EXPECT_EQ(getNextHops(state2, rt99->getForwardInfo()), nexthops3);
  // Delete the second client/nexthop pair
  // The route & prefix should disappear altogether.

  auto u3 = this->sw_->getRouteUpdater();
  u3.delRoute(kRid0, IPAddress("10.10.10.10"), 32, kClientB);
  u3.program();
  auto state3 = this->sw_->getState();
  rt10 = this->findRoute4(state3, kRid0, prefix10);
  rt99 = this->findRoute4(state3, kRid0, prefix99);
  EXPECT_EQ(rt10, nullptr);
  EXPECT_EQ(getNextHops(state3, rt99->getForwardInfo()), nexthops3);
}

// Test adding empty nextHops lists
TEST_F(RouteTest, disallowEmptyNexthops) {
  auto u1 = this->sw_->getRouteUpdater();

  // It's illegal to add an empty nextHops list to a route

  // Test the case where the empty list is the first to be added to the Route
  EXPECT_THROW(
      u1.addRoute(
          kRid0,
          IPAddress("5.5.5.5"),
          32,
          kClientA,
          RouteNextHopEntry(newNextHops(0, "20.20.20."), DISTANCE)),
      FbossError);

  // Test the case where the empty list is the second to be added to the Route
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(newNextHops(3, "10.10.10."), DISTANCE));
  EXPECT_THROW(
      u1.addRoute(
          kRid0,
          IPAddress("10.10.10.10"),
          32,
          kClientB,
          RouteNextHopEntry(newNextHops(0, "20.20.20."), DISTANCE)),
      FbossError);
}

bool stringStartsWith(std::string s1, std::string prefix) {
  return s1.compare(0, prefix.size(), prefix) == 0;
}

void expectFwdInfo(
    const SwSwitch* sw,
    RouteV4::Prefix prefix,
    std::string ipPrefix) {
  auto state = sw->getState();
  const auto& fwd = findRoute<IPAddressV4>(kRid0, prefix.toCidrNetwork(), state)
                        ->getForwardInfo();
  const auto& nhops = getNextHops(state, fwd);
  // Expect the fwd'ing info to be 3 IPs all starting with 'ipPrefix'
  EXPECT_EQ(3, nhops.size());
  for (auto const& it : nhops) {
    EXPECT_TRUE(stringStartsWith(it.addr().str(), ipPrefix));
  }
}

void addNextHopsForClient(
    SwSwitch* sw,
    RouteV4::Prefix prefix,
    ClientID clientId,
    std::string ipPrefix,
    AdminDistance adminDistance = AdminDistance::MAX_ADMIN_DISTANCE) {
  auto u = sw->getRouteUpdater();
  u.addRoute(
      kRid0,
      prefix.network(),
      prefix.mask(),
      clientId,
      RouteNextHopEntry(newNextHops(3, ipPrefix), adminDistance));
  u.program();
}

void deleteNextHopsForClient(
    SwSwitch* sw,
    RouteV4::Prefix prefix,
    ClientID clientId) {
  auto u = sw->getRouteUpdater();
  u.delRoute(kRid0, prefix.network(), prefix.mask(), clientId);
  u.program();
}
// Add and remove per-client NextHop lists to the same route, and make sure
// the lowest admin distance is the one that determines the forwarding info
TEST_F(RouteTest, fwdInfoRanking) {
  // We'll be adding and removing a bunch of nexthops for this Network & Mask
  auto network = IPAddressV4("22.22.22.22");
  uint8_t mask = 32;
  RouteV4::Prefix prefix{network, mask};

  // Add client 30, plus an interface for resolution.
  auto u1 = this->sw_->getRouteUpdater();
  // This is the route all the others will resolve to.
  u1.addRoute(
      kRid0,
      IPAddress("10.10.0.0"),
      16,
      ClientID::INTERFACE_ROUTE,
      RouteNextHopEntry(
          static_cast<NextHop>(ResolvedNextHop(
              IPAddress("1.1.1.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT)),
          AdminDistance::DIRECTLY_CONNECTED));
  u1.addRoute(
      kRid0,
      network,
      mask,
      ClientID(30),
      RouteNextHopEntry(newNextHops(3, "10.10.30."), DISTANCE));
  u1.program();

  // Expect fwdInfo based on client 30
  expectFwdInfo(this->sw_, prefix, "10.10.30.");
  // Add client BGP
  addNextHopsForClient(
      this->sw_, prefix, ClientID::BGPD, "10.10.20.", AdminDistance::EBGP);

  // Expect fwdInfo based on client BGP
  expectFwdInfo(this->sw_, prefix, "10.10.20.");
  // Add client 40
  addNextHopsForClient(this->sw_, prefix, ClientID(40), "10.10.40.");

  // Expect fwdInfo still based on client BGP
  expectFwdInfo(this->sw_, prefix, "10.10.20.");
  // Add client STATIC_ROUTE
  addNextHopsForClient(
      this->sw_,
      prefix,
      ClientID::STATIC_ROUTE,
      "10.10.10.",
      AdminDistance::STATIC_ROUTE);

  // Expect fwdInfo based on client STATIC_ROUTE
  expectFwdInfo(this->sw_, prefix, "10.10.10.");
  // Remove client BGP
  deleteNextHopsForClient(this->sw_, prefix, ClientID::BGPD);

  // Winner should still be STATIC_ROUTE
  expectFwdInfo(this->sw_, prefix, "10.10.10.");

  // Remove client STATIC_ROUTE
  deleteNextHopsForClient(this->sw_, prefix, ClientID::STATIC_ROUTE);

  // Winner should now be 30
  expectFwdInfo(this->sw_, prefix, "10.10.30.");
  // Remove client 30
  deleteNextHopsForClient(this->sw_, prefix, ClientID(30));

  // Winner should now be 40
  expectFwdInfo(this->sw_, prefix, "10.10.40.");
}

TEST_F(RouteTest, StaticIp2MplsRoutes) {
  auto config = this->initialConfig();

  config.staticIp2MplsRoutes()->resize(2);
  config.staticIp2MplsRoutes()[0].prefix() = "10.0.0.0/24";
  config.staticIp2MplsRoutes()[0].nexthops()->resize(1);
  NextHopThrift v4Nexthop;
  v4Nexthop.address() = toBinaryAddress(folly::IPAddress("1.1.1.10"));
  MplsAction action;
  action.action() = MplsActionCode::PUSH;
  action.pushLabels() = {101, 102};
  v4Nexthop.mplsAction() = action;
  config.staticIp2MplsRoutes()[0].nexthops()[0] = v4Nexthop;

  config.staticIp2MplsRoutes()[1].prefix() = "1:1::/64";
  config.staticIp2MplsRoutes()[1].nexthops()->resize(1);
  NextHopThrift v6Nexthop;
  v6Nexthop.address() = toBinaryAddress(folly::IPAddress("1::10"));
  v6Nexthop.mplsAction() = action;
  config.staticIp2MplsRoutes()[1].nexthops()[0] = v6Nexthop;

  this->sw_->applyConfig("New config", config);

  auto state = this->sw_->getState();
  auto v4Route = this->findRoute4(state, RouterID(0), "10.0.0.0/24");
  EXPECT_RESOLVED(v4Route);
  EXPECT_FALSE(v4Route->isDrop());
  EXPECT_FALSE(v4Route->isToCPU());
  EXPECT_FALSE(v4Route->isConnected());

  const auto& v4Fwd = v4Route->getForwardInfo();
  EXPECT_EQ(RouteForwardAction::NEXTHOPS, v4Fwd.getAction());
  EXPECT_EQ(1, getNextHops(state, v4Fwd).size());
  for (auto& nexthop : getNextHops(state, v4Fwd)) {
    auto labelAction = nexthop.labelForwardingAction();
    EXPECT_TRUE(labelAction.has_value());
    EXPECT_EQ(labelAction->type(), MplsActionCode::PUSH);
    EXPECT_EQ(labelAction->pushStack(), MplsLabelStack({101, 102}));
  }

  // v6 route
  auto v6Route = this->findRoute6(state, RouterID(0), "1:1::/64");
  EXPECT_RESOLVED(v6Route);
  EXPECT_FALSE(v6Route->isDrop());
  EXPECT_FALSE(v6Route->isToCPU());
  EXPECT_FALSE(v6Route->isConnected());

  const auto& v6Fwd = v4Route->getForwardInfo();
  EXPECT_EQ(RouteForwardAction::NEXTHOPS, v6Fwd.getAction());
  EXPECT_EQ(1, getNextHops(state, v6Fwd).size());

  for (auto& nexthop : getNextHops(state, v6Fwd)) {
    auto labelAction = nexthop.labelForwardingAction();
    EXPECT_TRUE(labelAction.has_value());
    EXPECT_EQ(labelAction->type(), MplsActionCode::PUSH);
    EXPECT_EQ(labelAction->pushStack(), MplsLabelStack({101, 102}));
  }
}

// Test interface routes when we have more than one address per
// address family in an interface
class MultipleAddressInterfaceTest : public RouteTest {
 public:
  cfg::SwitchConfig initialConfig() const override {
    cfg::SwitchConfig config;
    config.vlans()->resize(1);
    config.vlans()[0].id() = 1;

    config.interfaces()->resize(1);
    config.interfaces()[0].intfID() = 1;
    config.interfaces()[0].vlanID() = 1;
    config.interfaces()[0].routerID() = 0;
    config.interfaces()[0].mac() = "00:00:00:00:00:11";
    config.interfaces()[0].ipAddresses()->resize(4);
    config.interfaces()[0].ipAddresses()[0] = "1.1.1.1/24";
    config.interfaces()[0].ipAddresses()[1] = "1.1.1.2/24";
    config.interfaces()[0].ipAddresses()[2] = "1::1/48";
    config.interfaces()[0].ipAddresses()[3] = "1::2/48";
    return config;
  }
};

TEST_F(MultipleAddressInterfaceTest, twoAddrsForInterface) {
  auto rid = RouterID(0);
  auto [v4Routes, v6Routes] = getRouteCount(this->sw_->getState());

  EXPECT_EQ(2, v4Routes); // ALPM default + intf routes
  EXPECT_EQ(3, v6Routes); // ALPM default + intf + link local routes
  {
    auto state = this->sw_->getState();
    auto rt = this->findRoute4(state, rid, "1.1.1.0/24");
    EXPECT_EQ(0, rt->getGeneration());
    EXPECT_TRUE(rt->isResolved());
    EXPECT_TRUE(rt->isConnected());
    EXPECT_FALSE(rt->isToCPU());
    EXPECT_FALSE(rt->isDrop());
    EXPECT_FWD_INFO(state, rt, InterfaceID(1), "1.1.1.2");
  }
  {
    auto state = this->sw_->getState();
    auto rt = this->findRoute6(state, rid, "1::0/48");
    EXPECT_EQ(0, rt->getGeneration());
    EXPECT_TRUE(rt->isResolved());
    EXPECT_TRUE(rt->isConnected());
    EXPECT_FALSE(rt->isToCPU());
    EXPECT_FALSE(rt->isDrop());
    EXPECT_FWD_INFO(state, rt, InterfaceID(1), "1::2");
  }
}

TEST_F(RouteTest, withLabelForwardingAction) {
  auto rid = RouterID(0);

  std::array<folly::IPAddressV4, 4> nextHopAddrs{
      folly::IPAddressV4("1.1.1.0"),
      folly::IPAddressV4("1.1.2.0"),
      folly::IPAddressV4("1.1.3.0"),
      folly::IPAddressV4("1.1.4.0"),
  };

  std::array<LabelForwardingAction::LabelStack, 4> nextHopStacks{
      LabelForwardingAction::LabelStack({101, 201, 301}),
      LabelForwardingAction::LabelStack({102, 202, 302}),
      LabelForwardingAction::LabelStack({103, 203, 303}),
      LabelForwardingAction::LabelStack({104, 204, 304}),
  };

  std::map<folly::IPAddressV4, LabelForwardingAction::LabelStack>
      labeledNextHops;
  RouteNextHopSet nexthops;

  for (auto i = 0; i < 4; i++) {
    labeledNextHops.emplace(nextHopAddrs[i], nextHopStacks[i]);
    nexthops.emplace(UnresolvedNextHop(
        nextHopAddrs[i],
        1,
        std::make_optional<LabelForwardingAction>(
            LabelForwardingAction::LabelForwardingType::PUSH,
            nextHopStacks[i])));
  }

  auto state = this->sw_->getState();

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      folly::IPAddressV4("1.1.0.0"),
      16,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));

  updater.program();
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(),
      rid,
      folly::IPAddressV4("1.1.2.2"),
      this->sw_->getState());

  // Add clientNextHopSetID to expectedEntry.
  RouteNextHopEntry expectedEntry(nexthops, DISTANCE);
  auto storedEntry = route->getEntryForClient(kClientA);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(kClientA, expectedEntry), true);
  auto entry = route->getBestEntry();
  for (const auto& nh :
       getClientNextHops(this->sw_->getState(), *entry.second)) {
    EXPECT_EQ(nh.labelForwardingAction().has_value(), true);
    EXPECT_EQ(
        nh.labelForwardingAction()->type(),
        LabelForwardingAction::LabelForwardingType::PUSH);
    EXPECT_EQ(nh.labelForwardingAction()->pushStack().has_value(), true);
    EXPECT_EQ(nh.labelForwardingAction()->pushStack()->size(), 3);
    EXPECT_EQ(
        labeledNextHops[nh.addr().asV4()],
        nh.labelForwardingAction()->pushStack().value());
  }
}

TEST_F(RouteTest, unresolvedWithRouteLabels) {
  auto rid = RouterID(0);
  RouteNextHopSet bgpNextHops;
  RouteNextHopSet bgpResolvedNextHops;

  for (auto i = 0; i < 4; i++) {
    // bgp next hops with some labels, for some prefix
    bgpNextHops.emplace(UnresolvedNextHop(
        kBgpNextHopAddrs[i],
        1,
        std::make_optional<LabelForwardingAction>(
            LabelForwardingAction::LabelForwardingType::PUSH,
            LabelForwardingAction::LabelStack{
                kLabelStacks[i].begin(), kLabelStacks[i].end()})));
  }
  auto updater = this->sw_->getRouteUpdater();
  // routes to remote prefix to bgp next hops
  updater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));
  updater.program();

  // route to remote destination under kDestPrefix advertised by bgp
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(), rid, kDestAddress, this->sw_->getState());

  // Add clientNextHopSetID to expectedEntry.
  RouteNextHopEntry expectedEntry(bgpNextHops, AdminDistance::EBGP);
  auto storedEntry = route->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(ClientID::BGPD, expectedEntry), true);

  // Will resolve to DROP null routes
  EXPECT_TRUE(route->isDrop());
}

TEST_F(RouteTest, withTunnelAndRouteLabels) {
  auto rid = RouterID(0);
  RouteNextHopSet bgpNextHops;

  for (auto i = 0; i < 4; i++) {
    // bgp next hops with some labels, for some prefix
    bgpNextHops.emplace(UnresolvedNextHop(
        kBgpNextHopAddrs[i],
        1,
        std::make_optional<LabelForwardingAction>(
            LabelForwardingAction::LabelForwardingType::PUSH,
            LabelForwardingAction::LabelStack{
                kLabelStacks[i].begin(), kLabelStacks[i].begin() + 2})));
  }

  auto updater = this->sw_->getRouteUpdater();
  // routes to remote prefix to bgp next hops
  updater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));

  std::vector<ResolvedNextHop> igpNextHops;
  for (auto i = 0; i < 4; i++) {
    igpNextHops.emplace_back(
        kIgpAddrs[i],
        kInterfaces[i],
        ECMP_WEIGHT,
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::PUSH,
            LabelForwardingAction::LabelStack{
                kLabelStacks[i].begin() + 2, kLabelStacks[i].begin() + 3}));
  }

  // igp routes to bgp nexthops,
  for (auto i = 0; i < 4; i++) {
    updater.addRoute(
        rid,
        kBgpNextHopAddrs[i],
        64,
        ClientID::OPENR,
        RouteNextHopEntry(
            static_cast<NextHop>(igpNextHops[i]),
            AdminDistance::DIRECTLY_CONNECTED));
  }

  updater.program();

  // route to remote destination under kDestPrefix advertised by bgp

  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(), rid, kDestAddress, this->sw_->getState());

  RouteNextHopEntry expectedEntry(bgpNextHops, AdminDistance::EBGP);
  auto storedEntry = route->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(ClientID::BGPD, expectedEntry), true);

  EXPECT_TRUE(route->isResolved());

  auto routeState = this->sw_->getState();
  for (const auto& nhop : getNextHops(routeState, route->getForwardInfo())) {
    EXPECT_TRUE(nhop.isResolved());
    EXPECT_TRUE(nhop.labelForwardingAction().has_value());
    EXPECT_EQ(
        nhop.labelForwardingAction()->type(),
        LabelForwardingAction::LabelForwardingType::PUSH);

    ASSERT_TRUE(
        nhop.intf() == kInterfaces[0] || nhop.intf() == kInterfaces[1] ||
        nhop.intf() == kInterfaces[2] || nhop.intf() == kInterfaces[3]);

    if (nhop.intf() == kInterfaces[0]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[0]);
    } else if (nhop.intf() == kInterfaces[1]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[1]);
    } else if (nhop.intf() == kInterfaces[2]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[2]);
    } else if (nhop.intf() == kInterfaces[3]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[3]);
    }
  }
}

TEST_F(RouteTest, withOnlyTunnelLabels) {
  auto rid = RouterID(0);
  RouteNextHopSet bgpNextHops;

  for (auto i = 0; i < 4; i++) {
    // bgp next hops with some labels, for some prefix
    bgpNextHops.emplace(UnresolvedNextHop(kBgpNextHopAddrs[i], 1));
  }

  auto updater = this->sw_->getRouteUpdater();
  // routes to remote prefix to bgp next hops
  updater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));

  std::vector<ResolvedNextHop> igpNextHops;
  for (auto i = 0; i < 4; i++) {
    igpNextHops.emplace_back(
        kIgpAddrs[i],
        kInterfaces[i],
        ECMP_WEIGHT,
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::PUSH,
            LabelForwardingAction::LabelStack{
                kLabelStacks[i].begin(), kLabelStacks[i].end()}));
  }

  // igp routes to bgp nexthops,
  for (auto i = 0; i < 4; i++) {
    updater.addRoute(
        rid,
        kBgpNextHopAddrs[i],
        64,
        ClientID::OPENR,
        RouteNextHopEntry(
            static_cast<NextHop>(igpNextHops[i]),
            AdminDistance::DIRECTLY_CONNECTED));
  }

  updater.program();

  // route to remote destination under kDestPrefix advertised by bgp
  auto routeState2 = this->sw_->getState();
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(), rid, kDestAddress, routeState2);

  RouteNextHopEntry expectedEntry(bgpNextHops, AdminDistance::EBGP);
  auto storedEntry = route->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(ClientID::BGPD, expectedEntry), true);

  EXPECT_TRUE(route->isResolved());

  for (const auto& nhop : getNextHops(routeState2, route->getForwardInfo())) {
    EXPECT_TRUE(nhop.isResolved());
    EXPECT_TRUE(nhop.labelForwardingAction().has_value());
    EXPECT_EQ(
        nhop.labelForwardingAction()->type(),
        LabelForwardingAction::LabelForwardingType::PUSH);

    ASSERT_TRUE(
        nhop.intf() == kInterfaces[0] || nhop.intf() == kInterfaces[1] ||
        nhop.intf() == kInterfaces[2] || nhop.intf() == kInterfaces[3]);

    if (nhop.intf() == kInterfaces[0]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[0]);
    } else if (nhop.intf() == kInterfaces[1]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[1]);
    } else if (nhop.intf() == kInterfaces[2]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[2]);
    } else if (nhop.intf() == kInterfaces[3]) {
      EXPECT_EQ(
          nhop.labelForwardingAction()->pushStack().value(), kLabelStacks[3]);
    }
  }
}

TEST_F(RouteTest, invalidLinkLocalNextHopIntfDoesNotUpdateState) {
  auto rid = RouterID(0);
  auto stateBeforeUpdate = this->sw_->getState();
  const auto routePrefixesBeforeUpdate =
      routePrefixesFromState(stateBeforeUpdate, rid);
  EXPECT_EQ(
      routePrefixesBeforeUpdate,
      routePrefixesFromRouteDetails(
          this->sw_->getRib()->getRouteTableDetails(rid)));

  RouteNextHopSet linkLocalNextHops;
  for (auto i = 0; i < 4; i++) {
    linkLocalNextHops.emplace(ResolvedNextHop(
        kIgpAddrs[i], i == 2 ? InterfaceID(999) : kInterfaces[i], ECMP_WEIGHT));
  }

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      IPAddress("10.10.10.0"),
      24,
      kClientA,
      RouteNextHopEntry(makeNextHops({"1.1.1.10"}), DISTANCE));
  updater.addRoute(
      rid,
      IPAddress("2001::0"),
      48,
      kClientA,
      RouteNextHopEntry(makeNextHops({"1::10"}), DISTANCE));
  updater.addRoute(
      rid,
      IPAddress("3001::0"),
      48,
      kClientA,
      RouteNextHopEntry(linkLocalNextHops, DISTANCE));

  EXPECT_THROW(updater.program(), FbossError);
  EXPECT_EQ(stateBeforeUpdate, this->sw_->getState());
  EXPECT_EQ(
      routePrefixesBeforeUpdate,
      routePrefixesFromRouteDetails(
          this->sw_->getRib()->getRouteTableDetails(rid)));
}

TEST_F(RouteTest, updateTunnelLabels) {
  auto rid = RouterID(0);
  RouteNextHopSet bgpNextHops;
  bgpNextHops.emplace(UnresolvedNextHop(
      kBgpNextHopAddrs[0],
      1,
      std::make_optional<LabelForwardingAction>(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[0].begin(), kLabelStacks[0].begin() + 2})));

  auto updater = this->sw_->getRouteUpdater();
  // routes to remote prefix to bgp next hops
  updater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));

  ResolvedNextHop igpNextHop{
      kIgpAddrs[0],
      kInterfaces[0],
      ECMP_WEIGHT,
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[0].begin() + 2, kLabelStacks[0].begin() + 3})};
  // igp routes to bgp nexthops,
  updater.addRoute(
      rid,
      kBgpNextHopAddrs[0],
      64,
      ClientID::OPENR,
      RouteNextHopEntry(
          static_cast<NextHop>(igpNextHop), AdminDistance::DIRECTLY_CONNECTED));

  updater.program();

  // igp next hop is updated
  ResolvedNextHop updatedIgpNextHop{
      kIgpAddrs[0],
      kInterfaces[0],
      ECMP_WEIGHT,
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[1].begin() + 2, kLabelStacks[1].begin() + 3})};

  auto anotherUpdater = this->sw_->getRouteUpdater();
  anotherUpdater.delRoute(rid, kBgpNextHopAddrs[0], 64, ClientID::OPENR);
  anotherUpdater.addRoute(
      rid,
      kBgpNextHopAddrs[0],
      64,
      ClientID::OPENR,
      RouteNextHopEntry(
          static_cast<NextHop>(updatedIgpNextHop),
          AdminDistance::DIRECTLY_CONNECTED));

  anotherUpdater.program();

  LabelForwardingAction::LabelStack updatedStack;
  updatedStack.push_back(*kLabelStacks[0].begin());
  updatedStack.push_back(*(kLabelStacks[0].begin() + 1));
  updatedStack.push_back(*(kLabelStacks[1].begin() + 2));

  // route to remote destination under kDestPrefix advertised by bgp
  auto routeState3 = this->sw_->getState();
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(), rid, kDestAddress, routeState3);

  RouteNextHopEntry expectedEntry(bgpNextHops, AdminDistance::EBGP);
  auto storedEntry = route->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(ClientID::BGPD, expectedEntry), true);

  EXPECT_TRUE(route->isResolved());

  for (const auto& nhop : getNextHops(routeState3, route->getForwardInfo())) {
    EXPECT_TRUE(nhop.isResolved());
    EXPECT_TRUE(nhop.labelForwardingAction().has_value());
    EXPECT_EQ(
        nhop.labelForwardingAction()->type(),
        LabelForwardingAction::LabelForwardingType::PUSH);

    ASSERT_TRUE(nhop.intf() == kInterfaces[0]);

    EXPECT_EQ(nhop.labelForwardingAction()->pushStack().value(), updatedStack);
  }
}

TEST_F(RouteTest, updateRouteLabels) {
  auto rid = RouterID(0);
  RouteNextHopSet bgpNextHops;
  bgpNextHops.emplace(UnresolvedNextHop(
      kBgpNextHopAddrs[0],
      1,
      std::make_optional<LabelForwardingAction>(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[0].begin(), kLabelStacks[0].begin() + 2})));

  auto updater = this->sw_->getRouteUpdater();
  // routes to remote prefix to bgp next hops
  updater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, DISTANCE));

  ResolvedNextHop igpNextHop{
      kIgpAddrs[0],
      kInterfaces[0],
      ECMP_WEIGHT,
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[0].begin() + 2, kLabelStacks[0].begin() + 3})};
  // igp routes to bgp nexthops,
  updater.addRoute(
      rid,
      kBgpNextHopAddrs[0],
      64,
      ClientID::OPENR,
      RouteNextHopEntry(
          static_cast<NextHop>(igpNextHop), AdminDistance::DIRECTLY_CONNECTED));

  updater.program();

  // igp next hop is updated
  UnresolvedNextHop updatedBgpNextHop{
      kBgpNextHopAddrs[0],
      1,
      std::make_optional<LabelForwardingAction>(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack{
              kLabelStacks[1].begin(), kLabelStacks[1].begin() + 2})};

  auto anotherUpdater = this->sw_->getRouteUpdater();
  anotherUpdater.delRoute(
      rid, kDestPrefix.network(), kDestPrefix.mask(), ClientID::BGPD);
  anotherUpdater.program();

  anotherUpdater.addRoute(
      rid,
      kDestPrefix.network(),
      kDestPrefix.mask(),
      ClientID::BGPD,
      RouteNextHopEntry(static_cast<NextHop>(updatedBgpNextHop), DISTANCE));

  anotherUpdater.program();

  LabelForwardingAction::LabelStack updatedStack;
  updatedStack.push_back(*kLabelStacks[1].begin());
  updatedStack.push_back(*(kLabelStacks[1].begin() + 1));
  updatedStack.push_back(*(kLabelStacks[0].begin() + 2));

  // route to remote destination under kDestPrefix advertised by bgp
  auto routeState4 = this->sw_->getState();
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(), rid, kDestAddress, routeState4);

  RouteNextHopEntry expectedEntry(
      static_cast<NextHop>(updatedBgpNextHop), EBGP_DISTANCE);
  auto storedEntry = route->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  expectedEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(ClientID::BGPD, expectedEntry), true);

  EXPECT_TRUE(route->isResolved());

  for (const auto& nhop : getNextHops(routeState4, route->getForwardInfo())) {
    EXPECT_TRUE(nhop.isResolved());
    EXPECT_TRUE(nhop.labelForwardingAction().has_value());
    EXPECT_EQ(
        nhop.labelForwardingAction()->type(),
        LabelForwardingAction::LabelForwardingType::PUSH);

    ASSERT_TRUE(nhop.intf() == kInterfaces[0]);

    EXPECT_EQ(nhop.labelForwardingAction()->pushStack().value(), updatedStack);
  }
}

TEST_F(RouteTest, withNoLabelForwardingAction) {
  auto rid = RouterID(0);

  auto routeNextHopEntry = RouteNextHopEntry(
      makeNextHops({"1.1.1.1", "1.1.2.1", "1.1.3.1", "1.1.4.1"}), DISTANCE);
  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid, folly::IPAddressV4("1.1.0.0"), 16, kClientA, routeNextHopEntry);

  updater.program();
  const auto& route = findLongestMatchRoute(
      this->sw_->getRib(),
      rid,
      folly::IPAddressV4("1.1.2.2"),
      this->sw_->getState());

  // Add clientNextHopSetID to routeNextHopEntry.
  auto storedEntry = route->getEntryForClient(kClientA);
  ASSERT_NE(storedEntry, nullptr);
  auto storedClientNextHopSetID = storedEntry->getClientNextHopSetID();
  routeNextHopEntry.setClientNextHopSetID(storedClientNextHopSetID);
  EXPECT_EQ(route->has(kClientA, routeNextHopEntry), true);
  auto entry = route->getBestEntry();
  for (const auto& nh :
       getClientNextHops(this->sw_->getState(), *entry.second)) {
    EXPECT_EQ(nh.labelForwardingAction().has_value(), false);
  }
  EXPECT_EQ(*entry.second, routeNextHopEntry);
}

TEST_F(RouteTest, withInvalidLabelForwardingAction) {
  auto rid = RouterID(0);

  std::array<folly::IPAddressV4, 5> nextHopAddrs{
      folly::IPAddressV4("1.1.1.0"),
      folly::IPAddressV4("1.1.2.0"),
      folly::IPAddressV4("1.1.3.0"),
      folly::IPAddressV4("1.1.4.0"),
      folly::IPAddressV4("1.1.5.0"),
  };

  std::array<LabelForwardingAction, 5> nextHopLabelActions{
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::PUSH,
          LabelForwardingAction::LabelStack({101, 201, 301})),
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::SWAP,
          LabelForwardingAction::Label{202}),
      LabelForwardingAction(LabelForwardingAction::LabelForwardingType::NOOP),
      LabelForwardingAction(
          LabelForwardingAction::LabelForwardingType::POP_AND_LOOKUP),
      LabelForwardingAction(LabelForwardingAction::LabelForwardingType::PHP),
  };

  std::map<folly::IPAddressV4, LabelForwardingAction::LabelStack>
      labeledNextHops;
  RouteNextHopSet nexthops;

  for (auto i = 0; i < 5; i++) {
    nexthops.emplace(
        UnresolvedNextHop(nextHopAddrs[i], 1, nextHopLabelActions[i]));
  }

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      folly::IPAddressV4("1.1.0.0"),
      16,
      kClientA,
      RouteNextHopEntry(nexthops, DISTANCE));
  EXPECT_THROW(updater.program(), FbossError);
}

TEST_F(RouteTest, serializeRouteTable) {
  auto rid = RouterID(0);
  // 2 different nexthops
  RouteNextHopSet nhop1 = makeNextHops({"1.1.1.10"}); // resolved by intf 1
  RouteNextHopSet nhop2 = makeNextHops({"2.2.2.10"}); // resolved by intf 2
  // 4 prefixes
  RouteV4::Prefix r1{IPAddressV4("10.1.1.0"), 24};
  RouteV4::Prefix r2{IPAddressV4("20.1.1.0"), 24};
  RouteV6::Prefix r3{IPAddressV6("1001::0"), 48};
  RouteV6::Prefix r4{IPAddressV6("2001::0"), 48};

  auto u2 = this->sw_->getRouteUpdater();
  u2.addRoute(
      rid,
      r1.network(),
      r1.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u2.addRoute(
      rid,
      r2.network(),
      r2.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u2.addRoute(
      rid,
      r3.network(),
      r3.mask(),
      kClientA,
      RouteNextHopEntry(nhop1, DISTANCE));
  u2.addRoute(
      rid,
      r4.network(),
      r4.mask(),
      kClientA,
      RouteNextHopEntry(nhop2, DISTANCE));
  u2.program();

  // to thrift
  auto obj = this->sw_->getState()->toThrift();
  // back to Route object
  auto deserState = SwitchState::fromThrift(obj);
  // In new rib  only FIB is part of the switch state
  auto dyn0 = this->sw_->getState()->getFibsInfoMap()->toThrift();
  auto dyn1 = deserState->getFibsInfoMap()->toThrift();
  EXPECT_EQ(dyn0, dyn1);
}

class StaticRoutesTest : public RouteTest {
 public:
  cfg::SwitchConfig initialConfig() const override {
    auto config = RouteTest::initialConfig();
    // Add v4/v6 static routes with nhops
    config.staticRoutesWithNhops()->resize(2);
    config.staticRoutesWithNhops()[0].nexthops()->resize(1);
    config.staticRoutesWithNhops()[0].prefix() = "2001::/64";
    config.staticRoutesWithNhops()[0].nexthops()[0] = "2::2";
    config.staticRoutesWithNhops()[1].nexthops()->resize(1);
    config.staticRoutesWithNhops()[1].prefix() = "20.20.20.0/24";
    config.staticRoutesWithNhops()[1].nexthops()[0] = "2.2.2.3";

    auto insertStaticNoNhopRoutes = [=](auto& staticRouteNoNhops,
                                        int prefixStartIdx) {
      staticRouteNoNhops.resize(2);
      staticRouteNoNhops[0].prefix() =
          fmt::format("240{}::/64", prefixStartIdx);
      staticRouteNoNhops[1].prefix() =
          fmt::format("30.30.{}.0/24", prefixStartIdx);
    };
    // Add v4/v6 static routes to CPU/NULL
    insertStaticNoNhopRoutes(*config.staticRoutesToCPU(), 1);
    insertStaticNoNhopRoutes(*config.staticRoutesToNull(), 2);

    return config;
  }
};

TEST_F(StaticRoutesTest, staticRoutesGetApplied) {
  auto [numV4Routes, numV6Routes] = getRouteCount(this->sw_->getState());
  EXPECT_EQ(8, numV4Routes);
  EXPECT_EQ(9, numV6Routes);
}

/*
 * Class that makes it easy to run tests with the following
 * configurable entities:
 * Four interfaces: I1, I2, I3, I4
 * Three routes which require resolution: R1, R2, R3
 */
class UcmpTest : public RouteTest {
 public:
  void resolveRoutes(
      std::vector<std::pair<folly::IPAddress, RouteNextHopSet>>
          networkAndNextHops) {
    auto ru = this->sw_->getRouteUpdater();
    for (const auto& nnhs : networkAndNextHops) {
      folly::IPAddress net = nnhs.first;
      RouteNextHopSet nhs = nnhs.second;
      ru.addRoute(rid_, net, mask, kClientA, RouteNextHopEntry(nhs, DISTANCE));
    }
    ru.program();
    auto state = this->sw_->getState();
    ASSERT_NE(nullptr, state);
    for (const auto& nnhs : networkAndNextHops) {
      folly::IPAddress net = nnhs.first;
      auto pfx = fmt::format("{}/{}", net.str(), mask);
      auto r = this->findRoute4(state, rid_, pfx);
      EXPECT_RESOLVED(r);
      EXPECT_FALSE(r->isConnected());
      resolvedRoutes.push_back(r);
    }
  }

  void runRecursiveTest(
      const std::vector<RouteNextHopSet>& routeUnresolvedNextHops,
      const std::vector<NextHopWeight>& resolvedWeights) {
    std::vector<std::pair<folly::IPAddress, RouteNextHopSet>>
        networkAndNextHops;
    auto netsIter = nets.begin();
    for (const auto& nhs : routeUnresolvedNextHops) {
      networkAndNextHops.emplace_back(*netsIter, nhs);
      netsIter++;
    }
    resolveRoutes(networkAndNextHops);

    RouteNextHopSet expFwd1;
    uint8_t i = 0;
    for (const auto& w : resolvedWeights) {
      expFwd1.emplace(ResolvedNextHop(intfIps[i], InterfaceID(i + 1), w));
      ++i;
    }
    EXPECT_EQ(
        expFwd1,
        getNextHops(
            this->sw_->getState(), resolvedRoutes[0]->getForwardInfo()));
  }

  void runTwoDeepRecursiveTest(
      const std::vector<std::vector<NextHopWeight>>& unresolvedWeights,
      const std::vector<NextHopWeight>& resolvedWeights) {
    std::vector<RouteNextHopSet> routeUnresolvedNextHops;
    auto rsIter = rnhs.begin();
    for (const auto& ws : unresolvedWeights) {
      auto nhIter = rsIter->begin();
      RouteNextHopSet nexthops;
      for (const auto& w : ws) {
        nexthops.insert(UnresolvedNextHop(*nhIter, w));
        nhIter++;
      }
      routeUnresolvedNextHops.push_back(nexthops);
      rsIter++;
    }
    runRecursiveTest(routeUnresolvedNextHops, resolvedWeights);
  }

  void runVaryFromHundredTest(
      NextHopWeight w,
      const std::vector<NextHopWeight>& resolvedWeights) {
    runRecursiveTest(
        {{UnresolvedNextHop(intfIp1, 100),
          UnresolvedNextHop(intfIp2, 100),
          UnresolvedNextHop(intfIp3, 100),
          UnresolvedNextHop(intfIp4, w)}},
        resolvedWeights);
  }

  std::vector<std::shared_ptr<Route<folly::IPAddressV4>>> resolvedRoutes;
  const folly::IPAddress intfIp1{"1.1.1.10"};
  const folly::IPAddress intfIp2{"2.2.2.20"};
  const folly::IPAddress intfIp3{"3.3.3.30"};
  const folly::IPAddress intfIp4{"4.4.4.40"};
  const std::array<folly::IPAddress, 4> intfIps{
      {intfIp1, intfIp2, intfIp3, intfIp4}};
  const folly::IPAddress r2Nh{"42.42.42.42"};
  const folly::IPAddress r3Nh{"43.43.43.43"};
  std::array<folly::IPAddress, 2> r1Nhs{{r2Nh, r3Nh}};
  std::array<folly::IPAddress, 2> r2Nhs{{intfIp1, intfIp2}};
  std::array<folly::IPAddress, 2> r3Nhs{{intfIp3, intfIp4}};
  const std::array<std::array<folly::IPAddress, 2>, 3> rnhs{
      {r1Nhs, r2Nhs, r3Nhs}};
  const folly::IPAddress r1Net{"41.41.41.0"};
  const folly::IPAddress r2Net{"42.42.42.0"};
  const folly::IPAddress r3Net{"43.43.43.0"};
  const std::array<folly::IPAddress, 3> nets{{r1Net, r2Net, r3Net}};
  const uint8_t mask{24};

 private:
  RouterID rid_{0};
};

/*
 * Four interfaces: I1, I2, I3, I4
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with weights 3 and 2
 * R2 has I1 and I2 as next hops with weights 5 and 4
 * R3 has I3 and I4 as next hops with weights 3 and 2
 * expect R1 to resolve to I1:25, I2:20, I3:18, I4:12
 */
TEST_F(UcmpTest, recursiveUcmp) {
  this->runTwoDeepRecursiveTest({{3, 2}, {5, 4}, {3, 2}}, {25, 20, 18, 12});
}

/*
 * Two interfaces: I1, I2
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with weights 2 and 1
 * R2 has I1 and I2 as next hops with weights 1 and 1
 * R3 has I1 as next hop with weight 1
 * expect R1 to resolve to I1:2, I2:1
 */
TEST_F(UcmpTest, recursiveUcmpDuplicateIntf) {
  this->runRecursiveTest(
      {{UnresolvedNextHop(this->r2Nh, 2), UnresolvedNextHop(this->r3Nh, 1)},
       {UnresolvedNextHop(this->intfIp1, 1),
        UnresolvedNextHop(this->intfIp2, 1)},
       {UnresolvedNextHop(this->intfIp1, 1)}},
      {2, 1});
}

/*
 * Two interfaces: I1, I2
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with ECMP
 * R2 has I1 and I2 as next hops with ECMP
 * R3 has I1 as next hop with weight 1
 * expect R1 to resolve to ECMP
 */
TEST_F(UcmpTest, recursiveEcmpDuplicateIntf) {
  this->runRecursiveTest(
      {{UnresolvedNextHop(this->r2Nh, ECMP_WEIGHT),
        UnresolvedNextHop(this->r3Nh, ECMP_WEIGHT)},
       {UnresolvedNextHop(this->intfIp1, ECMP_WEIGHT),
        UnresolvedNextHop(this->intfIp2, ECMP_WEIGHT)},
       {UnresolvedNextHop(this->intfIp1, 1)}},
      {ECMP_WEIGHT, ECMP_WEIGHT});
}

/*
 * Two interfaces: I1, I2
 * One route which requires resolution: R1
 * R1 has I1 and I2 as next hops with weights 0 (ECMP) and 1
 * expect R1 to resolve to I1:1, I2:1
 *
 * R1 states a weighting, so it is resolved by combineWeights rather than
 * flattened. ECMP_WEIGHT counts as the single share normalizeNextHops would
 * settle it at anyway, so I1 asks for exactly what I2 does and the two come
 * out equal. The traffic split was already 1:1 before this; only the
 * representation changed.
 */
TEST_F(UcmpTest, mixedUcmpAndEcmpKeepTheirWeights) {
  this->runRecursiveTest(
      {{UnresolvedNextHop(this->intfIp1, ECMP_WEIGHT),
        UnresolvedNextHop(this->intfIp2, 1)}},
      {1, 1});
}

/*
 * Four interfaces: I1, I2, I3, I4
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with weights 3 and 2
 * R2 has I1 and I2 as next hops with weights 5 and 4
 * R3 has I3 and I4 as next hops with ECMP
 * expect R1 to resolve to I1:5, I2:4, I3:3, I4:3
 *
 * R3 states no weighting, but that no longer discards R1's. Its two next
 * hops take a share each, so R3's total is 2 rather than 0 and the LCM it
 * feeds is no longer driven to zero. R2 and R3 end up with 9 and 6 shares,
 * holding R1's 3:2, while R2's 5:4 and R3's even split both survive.
 *
 * Deliberately near-identical to recursiveMixedEcmpKeepsParentWeights, which
 * differs only in R3 being {ECMP, 1} rather than {ECMP, ECMP}. Both describe
 * an even split within R3, so both must resolve the same way.
 */
TEST_F(UcmpTest, recursiveEcmpChildKeepsParentWeights) {
  this->runTwoDeepRecursiveTest({{3, 2}, {5, 4}, {0, 0}}, {5, 4, 3, 3});
}

/*
 * Four interfaces: I1, I2, I3, I4
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with weights 3 and 2
 * R2 has I1 and I2 as next hops with weights 5 and 4
 * R3 has I3 and I4 as next hops with weights 0 (ECMP) and 1
 * expect R1 to resolve to I1:5, I2:4, I3:3, I4:3
 *
 * I3 at ECMP_WEIGHT asks for the same single share I4 does, so R3 splits
 * evenly and reaches R1 with a total of 2 rather than 0. R1's 3:2 survives
 * as 9:6, R2's 5:4 survives, and R3's even split survives both when R3 is
 * resolved directly and when it is reached through R1.
 *
 * Deliberately near-identical to recursiveEcmpChildKeepsParentWeights: R3
 * being {ECMP, 1} rather than {ECMP, ECMP} describes the same even split, so
 * the two must agree.
 */
TEST_F(UcmpTest, recursiveMixedEcmpKeepsParentWeights) {
  this->runTwoDeepRecursiveTest({{3, 2}, {5, 4}, {0, 1}}, {5, 4, 3, 3});
}

/*
 * Four interfaces: I1, I2, I3, I4
 * Three routes which require resolution: R1, R2, R3
 * R1 has R2 and R3 as next hops with ECMP
 * R2 has I1 and I2 as next hops with weights 5 and 4
 * R3 has I3 and I4 as next hops with weights 3 and 2
 * expect R1 to resolve to I1:25, I2:20, I3:27, I4:18
 *
 * R1 states no weighting of its own, which is not the same as asking for an
 * even one across everything below it. R2 and R3 are weighted, so their
 * splits are kept and scaled against each other: both end up with 45 shares,
 * giving R2 and R3 the equal share R1 does imply, while 5:4 holds within R2
 * as 25:20 and 3:2 within R3 as 27:18.
 *
 * Flattening these to ECMP would have served all four interfaces equally,
 * which is neither split anyone asked for.
 */
TEST_F(UcmpTest, recursiveEcmpKeepsChildWeights) {
  this->runTwoDeepRecursiveTest({{0, 0}, {5, 4}, {3, 2}}, {25, 20, 27, 18});
}

/*
 * Two interfaces: I1, I2
 * Two routes which require resolution: R1, R2
 * R1 has I1 and I2 as next hops with ECMP
 * R2 has I1 and I2 as next hops with weights 2 and 1
 * expect R1 to resolve to ECMP between I1, I2
 * expect R2 to resolve to I1:2, I2: 1
 */
TEST_F(UcmpTest, separateEcmpUcmp) {
  this->runRecursiveTest(
      {{UnresolvedNextHop(this->intfIp1, ECMP_WEIGHT),
        UnresolvedNextHop(this->intfIp2, ECMP_WEIGHT)},
       {UnresolvedNextHop(this->intfIp1, 2),
        UnresolvedNextHop(this->intfIp2, 1)}},
      {ECMP_WEIGHT, ECMP_WEIGHT});
  RouteNextHopSet route2ExpFwd;
  route2ExpFwd.emplace(
      ResolvedNextHop(IPAddress("1.1.1.10"), InterfaceID(1), 2));
  route2ExpFwd.emplace(
      ResolvedNextHop(IPAddress("2.2.2.20"), InterfaceID(2), 1));
  EXPECT_EQ(
      route2ExpFwd,
      getNextHops(
          this->sw_->getState(), this->resolvedRoutes[1]->getForwardInfo()));
}

// The following set of tests will start with 4 next hops all weight 100
// then vary one next hop by 10 weight increments to 90, 80, ... , 10

TEST_F(UcmpTest, Hundred) {
  this->runVaryFromHundredTest(100, {1, 1, 1, 1});
}

TEST_F(UcmpTest, Ninety) {
  this->runVaryFromHundredTest(90, {10, 10, 10, 9});
}

TEST_F(UcmpTest, Eighty) {
  this->runVaryFromHundredTest(80, {5, 5, 5, 4});
}

TEST_F(UcmpTest, Seventy) {
  this->runVaryFromHundredTest(70, {10, 10, 10, 7});
}

TEST_F(UcmpTest, Sixty) {
  this->runVaryFromHundredTest(60, {5, 5, 5, 3});
}

TEST_F(UcmpTest, Fifty) {
  this->runVaryFromHundredTest(50, {2, 2, 2, 1});
}

TEST_F(UcmpTest, Forty) {
  this->runVaryFromHundredTest(40, {5, 5, 5, 2});
}

TEST_F(UcmpTest, Thirty) {
  this->runVaryFromHundredTest(30, {10, 10, 10, 3});
}

TEST_F(UcmpTest, Twenty) {
  this->runVaryFromHundredTest(20, {5, 5, 5, 1});
}

TEST_F(UcmpTest, Ten) {
  this->runVaryFromHundredTest(10, {10, 10, 10, 1});
}

TEST_F(RouteTest, CounterIDTest) {
  auto u1 = this->sw_->getRouteUpdater();

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.10"), 32};

  RouteNextHopSet nexthops1 =
      makeResolvedNextHops({{InterfaceID(1), "1.1.1.1"}});

  // Add route with counter id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1, DISTANCE, std::optional<RouteCounterID>(kCounterID1)));
  u1.program();

  auto rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getForwardInfo().getCounterID(), kCounterID1);
  // EXPECT_EQ(rt10->getEntryForClient(kClientA)->getCounterID(), kCounterID1);

  // Modify counter id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1, DISTANCE, std::optional<RouteCounterID>(kCounterID2)));
  u1.program();

  rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getForwardInfo().getCounterID(), kCounterID2);
  EXPECT_EQ(*(rt10->getEntryForClient(kClientA)->getCounterID()), kCounterID2);

  // Modify route to remove counter id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1, DISTANCE, std::optional<RouteCounterID>(std::nullopt)));
  u1.program();

  rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getForwardInfo().getCounterID(), std::nullopt);
  EXPECT_TRUE(!rt10->getEntryForClient(kClientA)->getCounterID());
}

TEST_F(RouteTest, DropAndPuntRouteCounterID) {
  auto verifyCounter = [&](const auto& fwdAction) {
    auto u1 = sw_->getRouteUpdater();
    RouteV4::Prefix prefix10{IPAddressV4("10.10.10.10"), 32};
    // Add route with counter id
    u1.addRoute(
        kRid0,
        IPAddress("10.10.10.10"),
        32,
        kClientA,
        RouteNextHopEntry(
            fwdAction, DISTANCE, std::optional<RouteCounterID>(std::nullopt)));
    u1.program();

    auto rt10 = findRoute4(sw_->getState(), kRid0, prefix10);
    EXPECT_EQ(rt10->getForwardInfo().getCounterID(), std::nullopt);
    EXPECT_TRUE(!rt10->getEntryForClient(kClientA)->getCounterID());

    // Add counter id
    u1.addRoute(
        kRid0,
        IPAddress("10.10.10.10"),
        32,
        kClientA,
        RouteNextHopEntry(fwdAction, DISTANCE, kCounterID1));
    u1.program();

    rt10 = findRoute4(sw_->getState(), kRid0, prefix10);
    EXPECT_EQ(rt10->getForwardInfo().getCounterID(), kCounterID1);
    if (auto counter = rt10->getEntryForClient(kClientA)->getCounterID()) {
      EXPECT_EQ(counter, kCounterID1);
    }

    // Modify counter id
    u1.addRoute(
        kRid0,
        IPAddress("10.10.10.10"),
        32,
        kClientA,
        RouteNextHopEntry(fwdAction, DISTANCE, kCounterID2));
    u1.program();

    rt10 = findRoute4(sw_->getState(), kRid0, prefix10);
    EXPECT_EQ(rt10->getForwardInfo().getCounterID(), kCounterID2);
    if (auto counter = rt10->getEntryForClient(kClientA)->getCounterID()) {
      EXPECT_EQ(*counter, kCounterID2);
    }

    // Modify route to remove counter id
    u1.addRoute(
        kRid0,
        IPAddress("10.10.10.10"),
        32,
        kClientA,
        RouteNextHopEntry(
            fwdAction, DISTANCE, std::optional<RouteCounterID>(std::nullopt)));
    u1.program();

    rt10 = findRoute4(sw_->getState(), kRid0, prefix10);
    EXPECT_EQ(rt10->getForwardInfo().getCounterID(), std::nullopt);
    EXPECT_TRUE(!rt10->getEntryForClient(kClientA)->getCounterID());
  };
  verifyCounter(RouteForwardAction::DROP);
  verifyCounter(RouteForwardAction::TO_CPU);
}

TEST_F(RouteTest, ClassIDTest) {
  auto u1 = this->sw_->getRouteUpdater();

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.10"), 32};

  RouteNextHopSet nexthops1 =
      makeResolvedNextHops({{InterfaceID(1), "1.1.1.1"}});

  // Add route with class id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1,
          DISTANCE,
          std::optional<RouteCounterID>(std ::nullopt),
          std::optional<cfg::AclLookupClass>(kClassID1)));
  u1.program();

  auto rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getClassID(), kClassID1);
  EXPECT_EQ(rt10->getForwardInfo().getClassID(), kClassID1);
  if (auto classID = rt10->getEntryForClient(kClientA)->getClassID()) {
    EXPECT_EQ(*classID, kClassID1);
  }

  // Modify class id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1,
          DISTANCE,
          std::optional<RouteCounterID>(std::nullopt),
          std::optional<cfg::AclLookupClass>(kClassID2)));
  u1.program();

  rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getClassID(), kClassID2);
  EXPECT_EQ(rt10->getForwardInfo().getClassID(), kClassID2);
  if (auto classID = rt10->getEntryForClient(kClientA)->getClassID()) {
    EXPECT_EQ(*classID, kClassID2);
  }

  // Modify route to remove class id
  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.10"),
      32,
      kClientA,
      RouteNextHopEntry(
          nexthops1,
          DISTANCE,
          std::optional<RouteCounterID>(std::nullopt),
          std::optional<cfg::AclLookupClass>(std::nullopt)));
  u1.program();

  rt10 = this->findRoute4(this->sw_->getState(), kRid0, prefix10);
  EXPECT_EQ(rt10->getClassID(), std::nullopt);
  EXPECT_EQ(rt10->getForwardInfo().getClassID(), std::nullopt);
  EXPECT_TRUE(!rt10->getEntryForClient(kClientA)->getClassID());
}

TEST_F(RouteTest, routePrune) {
  auto u1 = this->sw_->getRouteUpdater();
  FLAGS_enable_capacity_pruning = true;

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.0"), 24};
  NetworkTopologyInformation topologyInfo;
  topologyInfo.plane_id() = 0;
  topologyInfo.rack_id() = 1;
  topologyInfo.remote_rack_capacity() = 0;
  topologyInfo.spine_capacity() = 0;
  topologyInfo.local_rack_capacity() = 3;

  auto nexthop1 =
      makeResolvedNextHop(InterfaceID(1), "1.1.1.1", 1, topologyInfo);
  auto nexthop2 =
      makeResolvedNextHop(InterfaceID(2), "2.2.2.1", 1, topologyInfo);
  auto nexthop3 =
      makeResolvedNextHop(InterfaceID(3), "3.3.3.1", 1, topologyInfo);
  auto nexthop4 =
      makeResolvedNextHop(InterfaceID(4), "4.4.4.1", 1, topologyInfo);

  RouteNextHopSet nexthops1;
  nexthops1.emplace(nexthop1);
  nexthops1.emplace(nexthop2);
  nexthops1.emplace(nexthop3);
  nexthops1.emplace(nexthop4);

  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.0"),
      24,
      ClientID(0),
      RouteNextHopEntry(nexthops1, DISTANCE));
  u1.program();

  auto pruneState = this->sw_->getState();
  auto rt10 = this->findRoute4(pruneState, kRid0, prefix10);
  int numPrunedPaths = 0;
  for (auto& nhop : getNextHops(pruneState, rt10->getForwardInfo())) {
    if (nhop.adjustedWeight().has_value() && *nhop.adjustedWeight() == 0) {
      numPrunedPaths++;
    }
  }
  EXPECT_EQ(numPrunedPaths, 1);
}

TEST_F(RouteTest, invalidRouteWeights) {
  auto u1 = this->sw_->getRouteUpdater();
  FLAGS_enable_capacity_pruning = true;

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.0"), 24};
  NetworkTopologyInformation topologyInfo;
  topologyInfo.plane_id() = 0;
  topologyInfo.rack_id() = 0;
  topologyInfo.remote_rack_capacity() = 0;
  topologyInfo.spine_capacity() = 0;
  topologyInfo.local_rack_capacity() = 3;

  RouteNextHopSet nexthops;
  nexthops.emplace(
      makeResolvedNextHop(InterfaceID(1), "1.1.1.1", 1, topologyInfo));

  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.0"),
      24,
      ClientID(0),
      RouteNextHopEntry(nexthops, DISTANCE));
  EXPECT_THROW(u1.program(), FbossError);
}

TEST_F(RouteTest, routePruneFpf) {
  // enable_capacity_pruning is the master switch; enable_fpf_capacity_pruning
  // selects the FPF (per-STSW) behavior. The latter is captured when the
  // normalizer is constructed during program(), so set it up front.
  FLAGS_enable_capacity_pruning = true;
  FLAGS_enable_fpf_capacity_pruning = true;
  auto u1 = this->sw_->getRouteUpdater();

  RouteV4::Prefix prefix10{IPAddressV4("10.10.10.0"), 24};
  NetworkTopologyInformation topologyInfo;
  topologyInfo.spine_id() = 0;
  // 4 local nexthops toward the STSW exceed the STSW->remote GTSW capacity
  // (remote_rack_capacity = 3) => 1 path is oversubscribed and should be pruned
  topologyInfo.remote_rack_capacity() = 3;

  RouteNextHopSet nexthops1;
  nexthops1.emplace(
      makeResolvedNextHop(InterfaceID(1), "1.1.1.1", 1, topologyInfo));
  nexthops1.emplace(
      makeResolvedNextHop(InterfaceID(2), "2.2.2.1", 1, topologyInfo));
  nexthops1.emplace(
      makeResolvedNextHop(InterfaceID(3), "3.3.3.1", 1, topologyInfo));
  nexthops1.emplace(
      makeResolvedNextHop(InterfaceID(4), "4.4.4.1", 1, topologyInfo));

  u1.addRoute(
      kRid0,
      IPAddress("10.10.10.0"),
      24,
      ClientID(0),
      RouteNextHopEntry(nexthops1, DISTANCE));
  u1.program();

  auto pruneState = this->sw_->getState();
  auto rt10 = this->findRoute4(pruneState, kRid0, prefix10);
  int numPrunedPaths = 0;
  for (auto& nhop : getNextHops(pruneState, rt10->getForwardInfo())) {
    if (nhop.adjustedWeight().has_value() && *nhop.adjustedWeight() == 0) {
      numPrunedPaths++;
    }
  }
  EXPECT_EQ(numPrunedPaths, 1);
  FLAGS_enable_fpf_capacity_pruning = false;
}

TEST_F(RouteTest, addRouteWithSingleSrv6NextHop) {
  auto rid = RouterID(0);
  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("3001:db8:1::"),
      folly::IPAddressV6("3001:db8:2::"),
      folly::IPAddressV6("3001:db8:3::")};

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      IPAddress("2800:1::"),
      64,
      kClientA,
      RouteNextHopEntry(nhops, EBGP_DISTANCE));
  updater.program();

  auto rt = this->findRoute6(this->sw_->getState(), rid, "2800:1::/64");
  ASSERT_NE(nullptr, rt);

  auto fwdNhops =
      getClientNextHops(this->sw_->getState(), *rt->getBestEntry().second);
  ASSERT_EQ(fwdNhops.size(), 1);
  const auto& nh = *fwdNhops.begin();
  EXPECT_EQ(nh.addr(), folly::IPAddress("1::10"));
  EXPECT_EQ(nh.srv6SegmentList(), sidList);
  EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
  EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
}

TEST_F(RouteTest, addRouteWithMultipleSrv6NextHops) {
  auto rid = RouterID(0);
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("3001:db8:1::"), folly::IPAddressV6("3001:db8:2::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("3001:db8:3::"), folly::IPAddressV6("3001:db8:4::")};

  RouteNextHopSet nhops;
  nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      IPAddress("2800:2::"),
      64,
      kClientA,
      RouteNextHopEntry(nhops, EBGP_DISTANCE));
  updater.program();

  auto rt = this->findRoute6(this->sw_->getState(), rid, "2800:2::/64");
  ASSERT_NE(nullptr, rt);

  auto fwdNhops =
      getClientNextHops(this->sw_->getState(), *rt->getBestEntry().second);
  ASSERT_EQ(fwdNhops.size(), 2);

  for (const auto& nh : fwdNhops) {
    if (nh.addr() == folly::IPAddress("1::10")) {
      EXPECT_EQ(nh.srv6SegmentList(), sidList1);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      EXPECT_EQ(nh.addr(), folly::IPAddress("2::10"));
      EXPECT_EQ(nh.srv6SegmentList(), sidList2);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    }
  }
}

TEST_F(RouteTest, allowMixedSrv6AndPlainNextHopsWhenSrv6IsDisabled) {
  folly::FlagSaver flagSaver;
  FLAGS_srv6 = false;

  auto rid = RouterID(0);
  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("3001:db8:1::")};
  RouteNextHopSet nhops{
      UnresolvedNextHop(
          folly::IPAddress("1::10"),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidList,
          TunnelType::SRV6_ENCAP,
          kSrv6Tunnel0),
      UnresolvedNextHop(folly::IPAddress("2::10"), ECMP_WEIGHT)};

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      IPAddress("2800:3::"),
      64,
      kClientA,
      RouteNextHopEntry(nhops, EBGP_DISTANCE));
  updater.program();

  auto route = this->findRoute6(this->sw_->getState(), rid, "2800:3::/64");
  ASSERT_NE(nullptr, route);
  ASSERT_TRUE(route->isResolved());
  const auto fwdNhops =
      getClientNextHops(this->sw_->getState(), *route->getBestEntry().second);
  EXPECT_EQ(2, fwdNhops.size());
}

TEST_F(RouteTest, rejectRouteWithMixedSrv6AndPlainNextHopsAndRollback) {
  folly::FlagSaver flagSaver;
  FLAGS_srv6 = true;

  auto rid = RouterID(0);
  const std::vector<folly::IPAddressV6> sidList1{
      folly::IPAddressV6("3001:db8:1::"), folly::IPAddressV6("3001:db8:2::")};
  const std::vector<folly::IPAddressV6> sidList2{
      folly::IPAddressV6("3001:db8:3::"), folly::IPAddressV6("3001:db8:4::")};

  RouteNextHopSet nhops;
  // 2 SRv6 next hops
  nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  // 2 plain (non-SRv6) next hops
  nhops.emplace(UnresolvedNextHop(folly::IPAddress("3::10"), ECMP_WEIGHT));
  nhops.emplace(UnresolvedNextHop(folly::IPAddress("4::10"), ECMP_WEIGHT));

  // Verify input next hops before programming
  ASSERT_EQ(nhops.size(), 4);
  for (const auto& nh : nhops) {
    if (nh.addr() == folly::IPAddress("1::10")) {
      EXPECT_EQ(nh.srv6SegmentList(), sidList1);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else if (nh.addr() == folly::IPAddress("2::10")) {
      EXPECT_EQ(nh.srv6SegmentList(), sidList2);
      EXPECT_EQ(nh.tunnelType(), TunnelType::SRV6_ENCAP);
      EXPECT_EQ(nh.tunnelId(), kSrv6Tunnel0);
    } else {
      EXPECT_TRUE(nh.srv6SegmentList().empty());
      EXPECT_EQ(nh.tunnelType(), std::nullopt);
      EXPECT_EQ(nh.tunnelId(), std::nullopt);
    }
  }

  auto stateBeforeUpdate = this->sw_->getState();
  const auto fibPrefixesBeforeUpdate =
      routePrefixesFromState(stateBeforeUpdate, rid);
  const auto ribPrefixesBeforeUpdate = routePrefixesFromRouteDetails(
      this->sw_->getRib()->getRouteTableDetails(rid));

  auto updater = this->sw_->getRouteUpdater();
  updater.addRoute(
      rid,
      IPAddress("2700:3::"),
      64,
      kClientA,
      RouteNextHopEntry(
          RouteNextHopSet{
              UnresolvedNextHop(folly::IPAddress("1::20"), ECMP_WEIGHT)},
          EBGP_DISTANCE));
  updater.addRoute(
      rid,
      IPAddress("2800:3::"),
      64,
      kClientA,
      RouteNextHopEntry(nhops, EBGP_DISTANCE));
  EXPECT_THROW(updater.program(), FbossHwUpdateError);

  EXPECT_EQ(stateBeforeUpdate, this->sw_->getState());
  EXPECT_EQ(
      fibPrefixesBeforeUpdate,
      routePrefixesFromState(this->sw_->getState(), rid));
  EXPECT_EQ(
      ribPrefixesBeforeUpdate,
      routePrefixesFromRouteDetails(
          this->sw_->getRib()->getRouteTableDetails(rid)));
  EXPECT_EQ(
      this->findRoute6(this->sw_->getState(), rid, "2800:3::/64"), nullptr);
  EXPECT_EQ(
      this->findRoute6(this->sw_->getState(), rid, "2700:3::/64"), nullptr);
}

TEST_F(RouteTest, rejectRouteThatBecomesMixedAfterRecursiveResolution) {
  folly::FlagSaver flagSaver;
  FLAGS_srv6 = true;

  auto rid = RouterID(0);
  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("3001:db8:1::")};

  RouteNextHopSet srv6ChildNextHops{UnresolvedNextHop(
      folly::IPAddress("1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0)};
  RouteNextHopSet plainChildNextHops{
      UnresolvedNextHop(folly::IPAddress("2::10"), ECMP_WEIGHT)};

  auto childUpdater = this->sw_->getRouteUpdater();
  childUpdater.addRoute(
      rid,
      IPAddress("100::"),
      64,
      kClientA,
      RouteNextHopEntry(srv6ChildNextHops, EBGP_DISTANCE));
  childUpdater.addRoute(
      rid,
      IPAddress("200::"),
      64,
      kClientA,
      RouteNextHopEntry(plainChildNextHops, EBGP_DISTANCE));
  childUpdater.program();

  RouteNextHopSet parentNextHops{
      UnresolvedNextHop(folly::IPAddress("100::10"), ECMP_WEIGHT),
      UnresolvedNextHop(folly::IPAddress("200::10"), ECMP_WEIGHT)};
  ASSERT_FALSE(hasSrv6AndNonSrv6NextHops(parentNextHops));

  auto stateBeforeUpdate = this->sw_->getState();
  const auto fibPrefixesBeforeUpdate =
      routePrefixesFromState(stateBeforeUpdate, rid);
  const auto ribPrefixesBeforeUpdate = routePrefixesFromRouteDetails(
      this->sw_->getRib()->getRouteTableDetails(rid));

  auto parentUpdater = this->sw_->getRouteUpdater();
  parentUpdater.addRoute(
      rid,
      IPAddress("300::"),
      64,
      kClientA,
      RouteNextHopEntry(parentNextHops, EBGP_DISTANCE));
  EXPECT_THROW(parentUpdater.program(), FbossHwUpdateError);

  EXPECT_EQ(stateBeforeUpdate, this->sw_->getState());
  EXPECT_EQ(
      fibPrefixesBeforeUpdate,
      routePrefixesFromState(this->sw_->getState(), rid));
  EXPECT_EQ(
      ribPrefixesBeforeUpdate,
      routePrefixesFromRouteDetails(
          this->sw_->getRib()->getRouteTableDetails(rid)));
  EXPECT_EQ(this->findRoute6(this->sw_->getState(), rid, "300::/64"), nullptr);
}

TEST_F(RouteTest, resolveRouteWithSrv6NextHopToMultipleResolvedNextHops) {
  auto rid = RouterID(0);

  // SRv6 segment list that should propagate through recursive resolution
  const std::vector<folly::IPAddressV6> sidList{
      folly::IPAddressV6("3001:db8:1::"),
      folly::IPAddressV6("3001:db8:2::"),
      folly::IPAddressV6("3001:db8:3::")};

  // Step 1: Add an IGP route 2801::/48 with 2 directly-resolvable next hops.
  // 1::10 resolves via interface 1 (1::1/48), 2::10 via interface 2 (2::1/48).
  auto updater = this->sw_->getRouteUpdater();
  RouteNextHopSet igpNhops = makeNextHops({"1::10", "2::10"});
  updater.addRoute(
      rid,
      IPAddress("2801::"),
      48,
      kClientA,
      RouteNextHopEntry(igpNhops, DISTANCE));

  // Step 2: Add an SRv6 route 2800:1::/64 with a single unresolved next hop
  // pointing at 2801::1, which should resolve recursively via 2801::/48.
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("2801::1"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  updater.addRoute(
      rid,
      IPAddress("2800:1::"),
      64,
      kClientA,
      RouteNextHopEntry(srv6Nhops, EBGP_DISTANCE));
  updater.program();

  // Step 3: Verify the SRv6 route resolved to 2 next hops, each carrying the
  // original srv6SegmentList, tunnelType, and tunnelId.
  auto rt = this->findRoute6(this->sw_->getState(), rid, "2800:1::/64");
  ASSERT_NE(nullptr, rt);
  EXPECT_TRUE(rt->isResolved());

  auto fwdNhops = getNextHops(this->sw_->getState(), rt->getForwardInfo());
  ASSERT_EQ(2, fwdNhops.size());

  // Build expected resolved next hops with SRv6 fields
  RouteNextHopSet expectedNhops;
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("1::10"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("2::10"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  EXPECT_EQ(expectedNhops, fwdNhops);
}

TEST_F(RouteTest, resolveRouteWithTwoSrv6NextHopsRecursively) {
  auto rid = RouterID(0);

  // Segment lists for the top-level SRv6 route (should be preserved)
  const std::vector<folly::IPAddressV6> sidListA{
      folly::IPAddressV6("3001:db8:a1::"),
      folly::IPAddressV6("3001:db8:a2::"),
      folly::IPAddressV6("3001:db8:a3::")};
  const std::vector<folly::IPAddressV6> sidListB{
      folly::IPAddressV6("3001:db8:b1::"),
      folly::IPAddressV6("3001:db8:b2::"),
      folly::IPAddressV6("3001:db8:b3::")};

  // Segment lists on the IGP routes (should NOT propagate to the top-level
  // route)
  const std::vector<folly::IPAddressV6> igpSidListA{
      folly::IPAddressV6("4001:db8:a1::"), folly::IPAddressV6("4001:db8:a2::")};
  const std::vector<folly::IPAddressV6> igpSidListB{
      folly::IPAddressV6("4001:db8:b1::"), folly::IPAddressV6("4001:db8:b2::")};

  auto updater = this->sw_->getRouteUpdater();

  // Step 1: Add IGP route A (2801::/48) with SRv6 next hops carrying
  // igpSidListA. 1::10 resolves via intf 1, 2::10 via intf 2.
  RouteNextHopSet igpNhopsA;
  igpNhopsA.emplace(UnresolvedNextHop(
      folly::IPAddress("1::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      igpSidListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  igpNhopsA.emplace(UnresolvedNextHop(
      folly::IPAddress("2::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      igpSidListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  updater.addRoute(
      rid,
      IPAddress("2801::"),
      48,
      kClientA,
      RouteNextHopEntry(igpNhopsA, DISTANCE));

  // Step 2: Add IGP route B (2802::/48) with SRv6 next hops carrying
  // igpSidListB. 3::10 resolves via intf 3, 4::10 via intf 4.
  RouteNextHopSet igpNhopsB;
  igpNhopsB.emplace(UnresolvedNextHop(
      folly::IPAddress("3::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      igpSidListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  igpNhopsB.emplace(UnresolvedNextHop(
      folly::IPAddress("4::10"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      igpSidListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  updater.addRoute(
      rid,
      IPAddress("2802::"),
      48,
      kClientA,
      RouteNextHopEntry(igpNhopsB, DISTANCE));

  // Step 3: Add SRv6 route (2800:5::/64) with 2 unresolved next hops pointing
  // at 2801::1 (sidListA) and 2802::1 (sidListB).
  RouteNextHopSet srv6Nhops;
  srv6Nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("2801::1"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  srv6Nhops.emplace(UnresolvedNextHop(
      folly::IPAddress("2802::1"),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  updater.addRoute(
      rid,
      IPAddress("2800:5::"),
      64,
      kClientA,
      RouteNextHopEntry(srv6Nhops, EBGP_DISTANCE));
  updater.program();

  // Step 4: Verify the SRv6 route resolved to 4 next hops, each carrying the
  // original route's segment list (sidListA or sidListB), NOT the IGP route's.
  auto rt = this->findRoute6(this->sw_->getState(), rid, "2800:5::/64");
  ASSERT_NE(nullptr, rt);
  EXPECT_TRUE(rt->isResolved());

  auto fwdNhops = getNextHops(this->sw_->getState(), rt->getForwardInfo());
  ASSERT_EQ(4, fwdNhops.size());

  RouteNextHopSet expectedNhops;
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("1::10"),
      InterfaceID(1),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("2::10"),
      InterfaceID(2),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListA,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("3::10"),
      InterfaceID(3),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));
  expectedNhops.emplace(ResolvedNextHop(
      IPAddress("4::10"),
      InterfaceID(4),
      ECMP_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      sidListB,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0));

  EXPECT_EQ(expectedNhops, fwdNhops);
}

TEST_F(RouteTest, selfReferencingRouteDropsRemovedNexthop) {
  auto rid = RouterID(0);

  // Step 1: Add an aggregate route 1.0.0.0/8 with:
  //   - 1.1.1.10 (connected via intf 1, 1.1.1.0/24)
  //   - 2.2.2.10 (connected via intf 2, 2.2.2.0/24)
  //   - 1.2.3.10 (NOT connected, but IS within 1.0.0.0/8 — self-reference!)
  // 1.2.3.10 has no /24 connected route, so LPM falls back to 1.0.0.0/8 itself.
  {
    auto u = this->sw_->getRouteUpdater();
    u.addRoute(
        rid,
        IPAddress("1.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(
            makeNextHops({"1.1.1.10", "2.2.2.10", "1.2.3.10"}), DISTANCE));
    u.program();

    auto state = this->sw_->getState();
    auto route = this->findRoute4(state, rid, "1.0.0.0/8");
    ASSERT_NE(route, nullptr);
    EXPECT_TRUE(route->isResolved());
    auto fwdNhops = getNextHops(state, route->getForwardInfo());
    EXPECT_EQ(fwdNhops.size(), 2);
    for (const auto& nh : fwdNhops) {
      EXPECT_TRUE(
          nh.addr() == IPAddress("1.1.1.10") ||
          nh.addr() == IPAddress("2.2.2.10"));
      EXPECT_NE(nh.addr(), IPAddress("1.2.3.10"))
          << "Self-referencing nexthop 1.2.3.10 should not be in forwarding info";
    }
  }

  // Step 2: Update the route to remove 1.1.1.10.
  // Keep 2.2.2.10 and 1.2.3.10 (self-referencing).
  {
    auto u = this->sw_->getRouteUpdater();
    u.addRoute(
        rid,
        IPAddress("1.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"2.2.2.10", "1.2.3.10"}), DISTANCE));
    u.program();

    auto state = this->sw_->getState();
    auto route = this->findRoute4(state, rid, "1.0.0.0/8");
    ASSERT_NE(route, nullptr);
    EXPECT_TRUE(route->isResolved());
    auto fwdNhops = getNextHops(state, route->getForwardInfo());
    EXPECT_EQ(fwdNhops.size(), 1);
    EXPECT_EQ(fwdNhops.begin()->addr(), IPAddress("2.2.2.10"));
    EXPECT_EQ(fwdNhops.begin()->intf(), InterfaceID(2));
    for (const auto& nh : fwdNhops) {
      EXPECT_NE(nh.addr(), IPAddress("1.1.1.10"))
          << "Removed nexthop 1.1.1.10 should not be in forwarding info";
      EXPECT_NE(nh.addr(), IPAddress("1.2.3.10"))
          << "Self-referencing nexthop 1.2.3.10 should not be in forwarding info";
    }
  }
}

// Routes that were previously resolved should not retain stale forwarding
// info after being updated to form a circular dependency.
TEST_F(RouteTest, routesWithCircularDependentNexthopsUnresolvable) {
  auto rid = RouterID(0);

  // Step 1: Add routes with connected nexthops — all should resolve
  {
    auto u = this->sw_->getRouteUpdater();
    u.addRoute(
        rid,
        IPAddress("10.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"1.1.1.10"}), DISTANCE));
    u.addRoute(
        rid,
        IPAddress("20.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"2.2.2.10"}), DISTANCE));
    u.addRoute(
        rid,
        IPAddress("30.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"3.3.3.10"}), DISTANCE));
    u.program();

    auto state = this->sw_->getState();
    EXPECT_RESOLVED(this->findRoute4(state, rid, "10.0.0.0/8"));
    EXPECT_RESOLVED(this->findRoute4(state, rid, "20.0.0.0/8"));
    EXPECT_RESOLVED(this->findRoute4(state, rid, "30.0.0.0/8"));
  }

  // Step 2: Update all three to form A → B → C → A cycle.
  // They should become unresolvable and not retain stale forwarding info.
  {
    auto u = this->sw_->getRouteUpdater();
    u.addRoute(
        rid,
        IPAddress("10.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"20.1.1.10"}), DISTANCE));
    u.addRoute(
        rid,
        IPAddress("20.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"30.1.1.10"}), DISTANCE));
    u.addRoute(
        rid,
        IPAddress("30.0.0.0"),
        8,
        kClientA,
        RouteNextHopEntry(makeNextHops({"10.1.1.10"}), DISTANCE));
    u.program();

    auto state = this->sw_->getState();
    EXPECT_EQ(this->findRoute4(state, rid, "10.0.0.0/8"), nullptr);
    EXPECT_EQ(this->findRoute4(state, rid, "20.0.0.0/8"), nullptr);
    EXPECT_EQ(this->findRoute4(state, rid, "30.0.0.0/8"), nullptr);

    auto ribRouteA = findLongestMatchRoute(
        this->sw_->getRib(), rid, IPAddressV4("10.0.0.0"), state);
    auto ribRouteB = findLongestMatchRoute(
        this->sw_->getRib(), rid, IPAddressV4("20.0.0.0"), state);
    auto ribRouteC = findLongestMatchRoute(
        this->sw_->getRib(), rid, IPAddressV4("30.0.0.0"), state);
    ASSERT_NE(ribRouteA, nullptr);
    ASSERT_NE(ribRouteB, nullptr);
    ASSERT_NE(ribRouteC, nullptr);
    EXPECT_EQ(ribRouteA->prefix(), makePrefixV4("10.0.0.0/8"));
    EXPECT_EQ(ribRouteB->prefix(), makePrefixV4("20.0.0.0/8"));
    EXPECT_EQ(ribRouteC->prefix(), makePrefixV4("30.0.0.0/8"));
    EXPECT_TRUE(ribRouteA->isUnresolvable());
    EXPECT_TRUE(ribRouteB->isUnresolvable());
    EXPECT_TRUE(ribRouteC->isUnresolvable());
  }
}

// Three-node cycle where C also has a valid connected nexthop.
// C resolves (via connected), B resolves (via C), A resolves (via B).
// The cyclic nexthop in C is dropped, but the connected one propagates up.
TEST_F(RouteTest, cyclicNexthopsResolveViaConnected) {
  auto rid = RouterID(0);
  auto u = this->sw_->getRouteUpdater();
  // A (10.0.0.0/8) → 20.1.1.10 → B
  u.addRoute(
      rid,
      IPAddress("10.0.0.0"),
      8,
      kClientA,
      RouteNextHopEntry(makeNextHops({"20.1.1.10"}), DISTANCE));
  // B (20.0.0.0/8) → 30.1.1.10 → C
  u.addRoute(
      rid,
      IPAddress("20.0.0.0"),
      8,
      kClientA,
      RouteNextHopEntry(makeNextHops({"30.1.1.10"}), DISTANCE));
  // C (30.0.0.0/8) → 10.1.1.10 (cyclic → A) AND 3.3.3.10 (connected via intf 3)
  u.addRoute(
      rid,
      IPAddress("30.0.0.0"),
      8,
      kClientA,
      RouteNextHopEntry(makeNextHops({"10.1.1.10", "3.3.3.10"}), DISTANCE));
  u.program();

  auto state = this->sw_->getState();
  auto routeA = this->findRoute4(state, rid, "10.0.0.0/8");
  auto routeB = this->findRoute4(state, rid, "20.0.0.0/8");
  auto routeC = this->findRoute4(state, rid, "30.0.0.0/8");
  ASSERT_NE(routeA, nullptr);
  ASSERT_NE(routeB, nullptr);
  ASSERT_NE(routeC, nullptr);

  // All three should resolve — C's connected nexthop propagates up
  EXPECT_TRUE(routeC->isResolved());
  EXPECT_TRUE(routeB->isResolved());
  EXPECT_TRUE(routeA->isResolved());

  for (const auto& route : {routeA, routeB, routeC}) {
    auto fwd = getNextHops(state, route->getForwardInfo());
    EXPECT_EQ(fwd.size(), 1);
    EXPECT_EQ(fwd.begin()->addr(), IPAddress("3.3.3.10"));
    EXPECT_EQ(fwd.begin()->intf(), InterfaceID(3));
  }
}
