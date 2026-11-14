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
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/if/gen-cpp2/mpls_types.h"
#include "fboss/agent/rib/ConfigApplier.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include "fboss/agent/AddressUtil.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>

#include <gtest/gtest.h>
#include <memory>

using namespace facebook::fboss;
using facebook::network::toBinaryAddress;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using std::make_shared;

auto kStaticClient = ClientID::STATIC_ROUTE;

class StaticRouteTest : public ::testing::TestWithParam<bool> {
  cfg::SwitchConfig initialConfig() const {
    cfg::SwitchConfig config;
    config.switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU)}};
    config.vlans()->resize(1);
    config.vlans()[0].id() = 1;
    config.vlans()[0].name() = "Vlan1";
    config.vlans()[0].intfID() = 1;
    config.interfaces()->resize(1);
    config.interfaces()[0].ipAddresses()->resize(3);
    config.interfaces()[0].ipAddresses()[0] = "10.0.0.1/24";
    config.interfaces()[0].ipAddresses()[1] = "192.168.0.1/24";
    config.interfaces()[0].ipAddresses()[2] = "2401:db00:2110:3001::0001/64";
    config.interfaces()[0].intfID() = 1;
    config.interfaces()[0].routerID() = 0;
    config.interfaces()[0].vlanID() = 1;
    config.interfaces()[0].name() = "fboss1";
    config.interfaces()[0].mac() = "00:02:00:00:00:01";
    config.interfaces()[0].mtu() = 9000;
    config.staticRoutesToNull()->resize(2);
    config.staticRoutesToNull()[0].prefix() = "1.1.1.1/32";
    config.staticRoutesToNull()[1].prefix() = "2001::1/128";
    config.staticRoutesToCPU()->resize(2);
    config.staticRoutesToCPU()[0].prefix() = "2.2.2.2/32";
    config.staticRoutesToCPU()[1].prefix() = "2001::2/128";
    config.staticRoutesWithNhops()->resize(4);
    config.staticRoutesWithNhops()[0].prefix() = "3.3.3.3/32";
    config.staticRoutesWithNhops()[0].nexthops()->resize(1);
    config.staticRoutesWithNhops()[0].nexthops()[0] = "1.1.1.1";
    config.staticRoutesWithNhops()[1].prefix() = "4.4.4.4/32";
    config.staticRoutesWithNhops()[1].nexthops()->resize(1);
    config.staticRoutesWithNhops()[1].nexthops()[0] = "2.2.2.2";
    // Now add v6 recursive routes
    config.staticRoutesWithNhops()[2].prefix() = "2001::3/128";
    config.staticRoutesWithNhops()[2].nexthops()->resize(1);
    config.staticRoutesWithNhops()[2].nexthops()[0] = "2001::1";
    config.staticRoutesWithNhops()[3].prefix() = "2001::4/128";
    config.staticRoutesWithNhops()[3].nexthops()->resize(1);
    config.staticRoutesWithNhops()[3].nexthops()[0] = "2001::2";

    // Now add v6 route with stack
    config.staticIp2MplsRoutes()->resize(1);
    config.staticIp2MplsRoutes()[0].prefix() = "2001::5/128";
    config.staticIp2MplsRoutes()[0].nexthops()->resize(1);
    config.staticIp2MplsRoutes()[0].nexthops()[0].address() =
        toBinaryAddress(folly::IPAddress("2001::1"));
    MplsAction action;
    action.action() = MplsActionCode::PUSH;
    action.pushLabels() = {101, 102};
    config.staticIp2MplsRoutes()[0].nexthops()[0].mplsAction() = action;
    return config;
  }

 public:
  void SetUp() override {
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }
  template <typename AddrT>
  std::shared_ptr<Route<AddrT>> findRoute(const RoutePrefix<AddrT>& nw) {
    return ::findRoute<AddrT>(
        RouterID(0), nw.toCidrNetwork(), this->sw_->getState());
  }

  ConfigApplier getConfigApplier(
      cfg::SwitchConfig& config,
      IPv4NetworkToRouteMap* v4Table,
      IPv6NetworkToRouteMap* v6Table,
      LabelToRouteMap* labelTable) {
    return ConfigApplier(
        RouterID(0),
        v4Table,
        v6Table,
        labelTable,
        {},
        folly::range(
            config.staticRoutesToCPU()->begin(),
            config.staticRoutesToCPU()->end()),
        folly::range(
            config.staticRoutesToNull()->begin(),
            config.staticRoutesToNull()->end()),
        folly::range(
            config.staticRoutesWithNhops()->begin(),
            config.staticRoutesWithNhops()->end()),
        folly::range(
            config.staticIp2MplsRoutes()->begin(),
            config.staticIp2MplsRoutes()->end()),
        folly::range(
            config.staticMplsRoutesWithNhops()->begin(),
            config.staticMplsRoutesWithNhops()->end()),
        folly::range(
            config.staticMplsRoutesToNull()->begin(),
            config.staticMplsRoutesToNull()->end()),
        folly::range(
            config.staticMplsRoutesToCPU()->begin(),
            config.staticMplsRoutesToCPU()->end()),
        folly::Range<ConfigApplier::StaticMySidIterator>{},
        &nextHopIDManager_,
        nullptr,
        getEcmpWidth(sw_->getState()));
  }

 protected:
  SwSwitch* sw_;
  std::unique_ptr<HwTestHandle> handle_;
  NextHopIDManager nextHopIDManager_;
};

TEST_F(StaticRouteTest, configureUnconfigure) {
  auto stateV1 = this->sw_->getState();
  ASSERT_NE(nullptr, stateV1);
  RouterID rid0(0);
  RouteV4::Prefix prefix1v4{IPAddressV4("1.1.1.1"), 32};
  RouteV4::Prefix prefix2v4{IPAddressV4("2.2.2.2"), 32};
  RouteV4::Prefix prefix3v4{IPAddressV4("3.3.3.3"), 32};
  RouteV4::Prefix prefix4v4{IPAddressV4("4.4.4.4"), 32};

  RouteV6::Prefix prefix1v6{IPAddressV6("2001::1"), 128};
  RouteV6::Prefix prefix2v6{IPAddressV6("2001::2"), 128};
  RouteV6::Prefix prefix3v6{IPAddressV6("2001::3"), 128};
  RouteV6::Prefix prefix4v6{IPAddressV6("2001::4"), 128};

  RouteV6::Prefix prefix1v6ToMpls{IPAddressV6("2001::5"), 128};

  auto r1v4 = this->findRoute(prefix1v4);
  ASSERT_NE(nullptr, r1v4);
  EXPECT_TRUE(r1v4->isResolved());
  EXPECT_FALSE(r1v4->isUnresolvable());
  EXPECT_FALSE(r1v4->isConnected());
  EXPECT_FALSE(r1v4->needResolve());
  EXPECT_EQ(
      r1v4->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));
  auto r2v4 = this->findRoute(prefix2v4);
  ASSERT_NE(nullptr, r2v4);
  EXPECT_TRUE(r2v4->isResolved());
  EXPECT_FALSE(r2v4->isUnresolvable());
  EXPECT_FALSE(r2v4->isConnected());
  EXPECT_FALSE(r2v4->needResolve());
  EXPECT_EQ(
      r2v4->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE));
  // Recursive resolution to RouteForwardAction::DROP
  auto r3v4 = this->findRoute(prefix3v4);
  ASSERT_NE(nullptr, r3v4);
  EXPECT_TRUE(r3v4->isResolved());
  EXPECT_FALSE(r3v4->isUnresolvable());
  EXPECT_FALSE(r3v4->isConnected());
  EXPECT_FALSE(r3v4->needResolve());
  EXPECT_EQ(
      r3v4->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));

  // Recursive resolution to CPU
  auto r4v4 = this->findRoute(prefix4v4);
  ASSERT_NE(nullptr, r4v4);
  EXPECT_TRUE(r4v4->isResolved());
  EXPECT_FALSE(r4v4->isUnresolvable());
  EXPECT_FALSE(r4v4->isConnected());
  EXPECT_FALSE(r4v4->needResolve());
  EXPECT_EQ(
      r4v4->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE));

  auto r1v6 = this->findRoute(prefix1v6);
  ASSERT_NE(nullptr, r1v6);
  EXPECT_TRUE(r1v6->isResolved());
  EXPECT_FALSE(r1v6->isUnresolvable());
  EXPECT_FALSE(r1v6->isConnected());
  EXPECT_FALSE(r1v6->needResolve());
  EXPECT_EQ(
      r1v6->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));

  auto r2v6 = this->findRoute(prefix2v6);
  ASSERT_NE(nullptr, r2v6);
  EXPECT_TRUE(r2v6->isResolved());
  EXPECT_FALSE(r2v6->isUnresolvable());
  EXPECT_FALSE(r2v6->isConnected());
  EXPECT_FALSE(r2v6->needResolve());
  EXPECT_EQ(
      r2v6->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE));

  // Recursive resolution to RouteForwardAction::DROP
  auto r3v6 = this->findRoute(prefix3v6);
  ASSERT_NE(nullptr, r3v6);
  EXPECT_TRUE(r3v6->isResolved());
  EXPECT_FALSE(r3v6->isUnresolvable());
  EXPECT_FALSE(r3v6->isConnected());
  EXPECT_FALSE(r3v6->needResolve());
  EXPECT_EQ(
      r3v6->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));

  // Recursive resolution to CPU
  auto r4v6 = this->findRoute(prefix4v6);
  ASSERT_NE(nullptr, r4v6);
  EXPECT_TRUE(r4v6->isResolved());
  EXPECT_FALSE(r4v6->isUnresolvable());
  EXPECT_FALSE(r4v6->isConnected());
  EXPECT_FALSE(r4v6->needResolve());
  EXPECT_EQ(
      r4v6->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE));
  // Recursive resolution to CPU

  auto r5v6ToMpls1 = this->findRoute(prefix1v6ToMpls);
  ASSERT_NE(nullptr, r5v6ToMpls1);
  EXPECT_TRUE(r5v6ToMpls1->isResolved());
  EXPECT_FALSE(r5v6ToMpls1->isUnresolvable());
  EXPECT_FALSE(r5v6ToMpls1->isConnected());
  EXPECT_FALSE(r5v6ToMpls1->needResolve());
  EXPECT_EQ(
      r4v6->getForwardInfo(),
      RouteNextHopEntry(
          RouteForwardAction::TO_CPU, AdminDistance::MAX_ADMIN_DISTANCE));

  // Now blow away the static routes from config.
  cfg::SwitchConfig emptyConfig;
  emptyConfig.switchSettings()->switchIdToSwitchInfo() = {
      {0, createSwitchInfo(cfg::SwitchType::NPU)}};
  this->sw_->applyConfig("Empty config", emptyConfig);
  auto [v4Routes, v6Routes] = getRouteCount(this->sw_->getState());
  // Only null routes remain
  EXPECT_EQ(1, v4Routes);
  EXPECT_EQ(1, v6Routes);
}

TEST_P(StaticRouteTest, NextHopResolvedInNextLevelRejected) {
  FLAGS_mpls_rib = GetParam();
  auto config = this->sw_->getConfig();
  config.staticIp2MplsRoutes()[0].nexthops()[0].resolvedInNextLevel() = true;
  EXPECT_THROW(this->sw_->applyConfig("ip2mpls", config), FbossError);

  config = this->sw_->getConfig();
  NextHopThrift nhop;
  nhop.address() = toBinaryAddress(folly::IPAddress("2001::1"));
  MplsAction swap;
  swap.action() = MplsActionCode::SWAP;
  swap.swapLabel() = 101;
  nhop.mplsAction() = swap;
  nhop.resolvedInNextLevel() = true;
  config.staticMplsRoutesWithNhops()->resize(1);
  config.staticMplsRoutesWithNhops()[0].ingressLabel() = 100;
  config.staticMplsRoutesWithNhops()[0].nexthops() = {nhop};
  EXPECT_THROW(this->sw_->applyConfig("mpls", config), FbossError);
}

TEST_P(StaticRouteTest, MplsStaticRoutes) {
  FLAGS_mpls_rib = GetParam();
  auto platform = createMockPlatform();
  auto stateV0 = make_shared<SwitchState>();

  cfg::SwitchConfig config0;
  config0.vlans()->resize(1);
  config0.vlans()[0].id() = 1;
  config0.interfaces()->resize(1);
  auto* intfConfig = &config0.interfaces()[0];
  intfConfig->name() = "fboss1";
  intfConfig->intfID() = 1;
  intfConfig->vlanID() = 1;
  intfConfig->mac() = "00:02:00:11:22:33";
  intfConfig->ipAddresses()->resize(3);
  intfConfig->ipAddresses()[0] = "10.0.0.0/24";
  intfConfig->ipAddresses()[1] = "1::/64";
  intfConfig->ipAddresses()[2] = "2::/64";

  // try to set link local nhop without interface
  config0.staticMplsRoutesWithNhops()->resize(1);
  config0.staticMplsRoutesWithNhops()[0].ingressLabel() = 100;
  std::vector<NextHopThrift> nexthops;
  nexthops.resize(1);
  MplsAction swap;
  swap.action() = MplsActionCode::SWAP;
  swap.swapLabel() = 101;
  nexthops[0].mplsAction() = swap;
  nexthops[0].address() =
      toBinaryAddress(folly::IPAddress("fe80:abcd:1234:dcab::1"));
  config0.staticMplsRoutesWithNhops()[0].nexthops() = nexthops;
  RoutingInformationBase rib;
  if (FLAGS_mpls_rib) {
    auto v4Table = IPv4NetworkToRouteMap();
    auto v6Table = IPv6NetworkToRouteMap();
    auto labelTable = LabelToRouteMap();
    auto configApplier = StaticRouteTest::getConfigApplier(
        config0, &v4Table, &v6Table, &labelTable);
    EXPECT_THROW(configApplier.apply(), FbossError);
  } else {
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config0, platform.get(), &rib),
        FbossError);
  }

  // try to set non-link local without interface and unreachable via interface
  nexthops[0].address() = toBinaryAddress(folly::IPAddress("3::1"));
  config0.staticMplsRoutesWithNhops()[0].nexthops() = nexthops;
  if (!FLAGS_mpls_rib) {
    EXPECT_THROW(
        publishAndApplyConfig(stateV0, &config0, platform.get(), &rib),
        FbossError);
  } else {
    auto stateV1 =
        publishAndApplyConfig(stateV0, &config0, platform.get(), &rib);
    auto entry = stateV1->getLabelForwardingInformationBase()->getNodeIf(100);
    EXPECT_EQ(entry, nullptr);
  }

  // setup link local with interface and non-link local without interface
  // reachable via interface
  nexthops.resize(2);
  nexthops[0].address() =
      toBinaryAddress(folly::IPAddress("fe80:abcd:1234:dcab::1"));
  nexthops[0].address()->ifName() = *intfConfig->name();

  swap.swapLabel() = 102;
  nexthops[1].mplsAction() = swap;
  nexthops[1].address() = toBinaryAddress(folly::IPAddress("1::10"));
  config0.staticMplsRoutesWithNhops()[0].nexthops() = nexthops;
  auto stateV1 = publishAndApplyConfig(stateV0, &config0, platform.get(), &rib);
  auto entry = stateV1->getLabelForwardingInformationBase()->getNodeIf(100);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(getMplsNextHops(stateV1, entry->getForwardInfo()).size(), 2);

  // setup non-link local with interface, still valid
  nexthops.resize(3);
  swap.swapLabel() = 103;
  nexthops[2].mplsAction() = swap;
  nexthops[2].address() = toBinaryAddress(folly::IPAddress("2::10"));
  nexthops[2].address()->ifName() = *intfConfig->name();
  config0.staticMplsRoutesWithNhops()[0].nexthops() = nexthops;
  auto stateV2 = publishAndApplyConfig(stateV1, &config0, platform.get(), &rib);
  entry = stateV2->getLabelForwardingInformationBase()->getNodeIf(100);
  EXPECT_NE(entry, nullptr);
  EXPECT_EQ(getMplsNextHops(stateV2, entry->getForwardInfo()).size(), 3);
}

INSTANTIATE_TEST_CASE_P(StaticRouteTest, StaticRouteTest, ::testing::Bool());
