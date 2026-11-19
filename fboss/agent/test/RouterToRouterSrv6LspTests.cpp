// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include <array>
#include <string>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
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

    const std::array<int, 4> nextHopCounts{2, 2, 1, 1};
    int nextHopIndex = 0;
    auto updater = sw_->getRouteUpdater();
    for (size_t i = 0; i < routerAddresses_.size(); ++i) {
      RouteNextHopSet openrNextHops;
      for (int j = 0; j < nextHopCounts[i]; ++j) {
        openrNextHops.emplace(getLinkLocalNhop(nextHopIndex++));
      }
      updater.addRoute(
          RouterID(0),
          routerAddresses_[i],
          128,
          ClientID::OPENR,
          RouteNextHopEntry(openrNextHops, AdminDistance::OPENR));
    }
    updater.program();
  }

  ResolvedNextHop getLinkLocalNhop(int index) const {
    return ResolvedNextHop(
        folly::IPAddress("fe80::" + std::to_string(index + 1)),
        InterfaceID(1),
        ECMP_WEIGHT);
  }

  static constexpr int kNumLinkLocalNextHops = 6;
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
  RouteNextHopSet bgpNextHops;
  for (const auto& address : routerAddresses_) {
    bgpNextHops.emplace(UnresolvedNextHop(address, ECMP_WEIGHT));
  }
  auto updater = sw_->getRouteUpdater();
  updater.addRoute(
      RouterID(0),
      bgpPrefix_.first.asV6(),
      bgpPrefix_.second,
      ClientID::BGPD,
      RouteNextHopEntry(bgpNextHops, AdminDistance::EBGP));
  updater.program();

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

} // namespace facebook::fboss
