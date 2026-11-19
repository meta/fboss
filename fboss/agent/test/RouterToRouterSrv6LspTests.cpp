// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <map>
#include <string>
#include <vector>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {

class RouterToRouterSrv6LspTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = true;
    auto config = testConfigA();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();

    int nextHopIndex = 0;
    std::vector<std::pair<std::string, RouteNextHopSet>> namedGroups;
    auto updater = sw_->getRouteUpdater();
    for (size_t i = 0; i < routerAddresses_.size(); ++i) {
      RouteNextHopSet openrNextHops;
      RouteNextHopSet srv6NextHops;
      for (int j = 0; j < kNextHopCounts[i]; ++j) {
        openrNextHops.emplace(getLinkLocalNhop(nextHopIndex));
        srv6NextHops.emplace(getSrv6Nhop(nextHopIndex));
        ++nextHopIndex;
      }
      namedGroups.emplace_back(getNhgName(i), std::move(srv6NextHops));
      updater.addRoute(
          RouterID(0),
          routerAddresses_[i],
          128,
          ClientID::OPENR,
          RouteNextHopEntry(openrNextHops, AdminDistance::OPENR));
    }
    updater.program();
    sw_->getRib()->addOrUpdateNamedNextHopGroups(
        sw_->getScopeResolver(),
        namedGroups,
        createRibToSwitchStateFunction(),
        sw_);
  }

  ResolvedNextHop getLinkLocalNhop(int index) const {
    return ResolvedNextHop(
        folly::IPAddress("fe80::" + std::to_string(index + 1)),
        InterfaceID(1),
        ECMP_WEIGHT);
  }

  std::vector<folly::IPAddressV6> getSidList(int index) const {
    return {folly::IPAddressV6(
        "fdad:ffff:" + std::to_string(index + 1) + ":7fff::")};
  }

  ResolvedNextHop getSrv6Nhop(int index) const {
    const auto nextHop = getLinkLocalNhop(index);
    return ResolvedNextHop(
        nextHop.addr(),
        nextHop.intfID(),
        nextHop.weight(),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        getSidList(index),
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0"));
  }

  std::string getNhgName(int index) const {
    return "siteX-gold-lsp-rtr" + std::to_string(index + 1);
  }

  void addBgpRoute(
      const std::vector<int>& routerAddressIndexes = {0, 1, 2, 3}) {
    RouteNextHopSet bgpNextHops;
    for (const auto index : routerAddressIndexes) {
      bgpNextHops.emplace(
          UnresolvedNextHop(routerAddresses_.at(index), ECMP_WEIGHT));
    }
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        bgpPrefix_.first.asV6(),
        bgpPrefix_.second,
        ClientID::BGPD,
        RouteNextHopEntry(bgpNextHops, AdminDistance::EBGP));
    updater.program();
  }

  static constexpr int kNumLinkLocalNextHops = 6;
  static constexpr std::array<int, 4> kNextHopCounts{2, 2, 1, 1};
  gflags::FlagSaver flagSaver_;
  std::unique_ptr<HwTestHandle> handle_;
  SwSwitch* sw_{nullptr};
  const std::array<folly::IPAddressV6, 4> routerAddresses_{
      folly::IPAddressV6("fdad:feff:8201::d:0"),
      folly::IPAddressV6("fdad:feff:8202::d:0"),
      folly::IPAddressV6("fdad:feff:8203::d:0"),
      folly::IPAddressV6("fdad:feff:8204::d:0")};
  const folly::CIDRNetwork bgpPrefix_{folly::IPAddress("2001::"), 64};
};

TEST_F(RouterToRouterSrv6LspTest, bgpResolvesToOpenrNextHops) {
  addBgpRoute();

  auto state = sw_->getState();
  auto route = findRoute<folly::IPAddressV6>(RouterID(0), bgpPrefix_, state);
  ASSERT_NE(route, nullptr);
  ASSERT_TRUE(route->isResolved());
  RouteNextHopSet expectedBgpNextHops;
  for (int i = 0; i < kNumLinkLocalNextHops; ++i) {
    expectedBgpNextHops.emplace(getLinkLocalNhop(i));
  }
  EXPECT_EQ(expectedBgpNextHops, getNextHops(state, route->getForwardInfo()));
}

TEST_F(
    RouterToRouterSrv6LspTest,
    teAgentRoutesResolveToSrv6NamedNextHopGroups) {
  auto updater = sw_->getRouteUpdater();
  for (size_t i = 0; i < routerAddresses_.size(); ++i) {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(routerAddresses_[i]);
    route.dest()->prefixLength() = 128;
    NamedRouteDestination namedDestination;
    namedDestination.nextHopGroup_ref() = getNhgName(i);
    route.namedRouteDestination() = namedDestination;
    route.counterID() = getNhgName(i);
    updater.addRoute(RouterID(0), ClientID::TE_AGENT, route);
  }
  updater.program();
  addBgpRoute();

  auto state = sw_->getState();
  int nextHopIndex = 0;
  for (size_t i = 0; i < routerAddresses_.size(); ++i) {
    const auto name = getNhgName(i);
    RouteNextHopSet nextHops;
    for (int j = 0; j < kNextHopCounts[i]; ++j) {
      nextHops.emplace(getSrv6Nhop(nextHopIndex++));
    }
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), {routerAddresses_[i], 128}, state);
    ASSERT_NE(route, nullptr) << name;
    ASSERT_TRUE(route->isResolved()) << name;
    EXPECT_EQ(route->getBestEntry().first, ClientID::TE_AGENT) << name;
    auto teEntry = route->getEntryForClient(ClientID::TE_AGENT);
    ASSERT_NE(teEntry, nullptr) << name;
    EXPECT_EQ(teEntry->getNamedNextHopGroup(), name);
    EXPECT_EQ(route->getForwardInfo().getCounterID(), name);
    EXPECT_EQ(nextHops, getNextHops(state, route->getForwardInfo())) << name;
  }

  auto bgpRoute = findRoute<folly::IPAddressV6>(RouterID(0), bgpPrefix_, state);
  ASSERT_NE(bgpRoute, nullptr);
  ASSERT_TRUE(bgpRoute->isResolved());
  RouteNextHopSet expectedBgpNextHops;
  std::map<folly::IPAddress, std::vector<folly::IPAddressV6>> expectedSidLists;
  for (int i = 0; i < kNumLinkLocalNextHops; ++i) {
    expectedBgpNextHops.emplace(getSrv6Nhop(i));
    expectedSidLists.emplace(getLinkLocalNhop(i).addr(), getSidList(i));
  }
  const auto bgpNextHops = getNextHops(state, bgpRoute->getForwardInfo());
  EXPECT_EQ(expectedBgpNextHops, bgpNextHops);
  std::map<folly::IPAddress, std::vector<folly::IPAddressV6>> actualSidLists;
  for (const auto& nextHop : bgpNextHops) {
    actualSidLists.emplace(nextHop.addr(), nextHop.srv6SegmentList());
  }
  EXPECT_EQ(expectedSidLists, actualSidLists);

  const auto counterId = bgpRoute->getForwardInfo().getCounterID();
  ASSERT_TRUE(counterId.has_value());
  EXPECT_THAT(
      *counterId,
      ::testing::AnyOf(
          getNhgName(0), getNhgName(1), getNhgName(2), getNhgName(3)));
  auto bgpEntry = bgpRoute->getEntryForClient(ClientID::BGPD);
  ASSERT_NE(bgpEntry, nullptr);
  EXPECT_FALSE(bgpEntry->getCounterID().has_value());
}

} // namespace facebook::fboss
