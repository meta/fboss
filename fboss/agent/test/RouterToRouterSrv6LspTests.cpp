// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <map>
#include <numeric>
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

  RouteNextHopSet getRouterNextHops(int index, bool useSrv6) const {
    const auto startIndex = std::accumulate(
        kNextHopCounts.begin(), kNextHopCounts.begin() + index, 0);
    RouteNextHopSet nextHops;
    for (int i = 0; i < kNextHopCounts.at(index); ++i) {
      nextHops.emplace(
          useSrv6 ? getSrv6Nhop(startIndex + i)
                  : getLinkLocalNhop(startIndex + i));
    }
    return nextHops;
  }

  void addTeAgentRoutes() {
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
  }

  void verifyTeAgentRoutes() const {
    auto state = sw_->getState();
    for (size_t i = 0; i < routerAddresses_.size(); ++i) {
      const auto name = getNhgName(i);
      auto route = findRoute<folly::IPAddressV6>(
          RouterID(0), {routerAddresses_[i], 128}, state);
      ASSERT_NE(route, nullptr) << name;
      ASSERT_TRUE(route->isResolved()) << name;
      EXPECT_EQ(route->getBestEntry().first, ClientID::TE_AGENT) << name;
      auto teEntry = route->getEntryForClient(ClientID::TE_AGENT);
      ASSERT_NE(teEntry, nullptr) << name;
      EXPECT_EQ(teEntry->getNamedNextHopGroup(), name);
      EXPECT_EQ(route->getForwardInfo().getCounterID(), name);
      EXPECT_EQ(
          getRouterNextHops(i, true),
          getNextHops(state, route->getForwardInfo()))
          << name;
    }
  }

  void verifyBgpRoute(
      bool useSrv6,
      const std::vector<int>& routerAddressIndexes = {0, 1, 2, 3}) const {
    auto state = sw_->getState();
    auto route = findRoute<folly::IPAddressV6>(RouterID(0), bgpPrefix_, state);
    ASSERT_NE(route, nullptr);
    ASSERT_TRUE(route->isResolved());
    RouteNextHopSet expectedNextHops;
    std::vector<RouteCounterID> expectedCounterIds;
    for (const auto index : routerAddressIndexes) {
      const auto nextHops = getRouterNextHops(index, useSrv6);
      expectedNextHops.insert(nextHops.begin(), nextHops.end());
      expectedCounterIds.push_back(getNhgName(index));
    }
    const auto nextHops = getNextHops(state, route->getForwardInfo());
    EXPECT_EQ(expectedNextHops, nextHops);

    std::map<folly::IPAddress, std::vector<folly::IPAddressV6>>
        expectedSidLists;
    for (const auto& nextHop : expectedNextHops) {
      expectedSidLists.emplace(nextHop.addr(), nextHop.srv6SegmentList());
    }
    std::map<folly::IPAddress, std::vector<folly::IPAddressV6>> actualSidLists;
    for (const auto& nextHop : nextHops) {
      actualSidLists.emplace(nextHop.addr(), nextHop.srv6SegmentList());
    }
    EXPECT_EQ(expectedSidLists, actualSidLists);

    const auto counterId = route->getForwardInfo().getCounterID();
    if (useSrv6) {
      ASSERT_TRUE(counterId.has_value());
      EXPECT_THAT(expectedCounterIds, ::testing::Contains(*counterId));
    } else {
      EXPECT_FALSE(counterId.has_value());
    }
    auto bgpEntry = route->getEntryForClient(ClientID::BGPD);
    ASSERT_NE(bgpEntry, nullptr);
    EXPECT_FALSE(bgpEntry->getCounterID().has_value());
  }

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
  verifyBgpRoute(false);
}

TEST_F(
    RouterToRouterSrv6LspTest,
    teAgentRoutesResolveToSrv6NamedNextHopGroups) {
  addTeAgentRoutes();
  addBgpRoute();
  verifyTeAgentRoutes();
  verifyBgpRoute(true);
}

TEST_F(RouterToRouterSrv6LspTest, bgpResolvesToSubsetOfSrv6NextHops) {
  const std::vector<int> routerAddressIndexes{1, 2, 3};
  addTeAgentRoutes();
  addBgpRoute(routerAddressIndexes);
  verifyTeAgentRoutes();
  verifyBgpRoute(true, routerAddressIndexes);
}

} // namespace facebook::fboss
