// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <fmt/core.h>
#include <gtest/gtest.h>

#include "fboss/agent/Srv6NextHopResourceAccountant.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

class Srv6NextHopResourceAccountantTest : public ResourceAccountantTestBase {
 public:
  void SetUp() override {
    ResourceAccountantTestBase::SetUp();
    accountant_ =
        std::make_unique<Srv6NextHopResourceAccountant>(asicTable_.get());
  }

  std::unique_ptr<Srv6NextHopResourceAccountant> accountant_;
};

TEST_F(Srv6NextHopResourceAccountantTest, checkAndUpdateSrv6NextHopResource) {
  FLAGS_enable_srv6_nexthop_resource_protection = true;

  auto makeSrv6Nhop = [](int idx) {
    return ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", idx + 1)),
        InterfaceID(1),
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::vector<folly::IPAddressV6>{
            folly::IPAddressV6(fmt::format("3001:db8:{:x}::", idx + 1))},
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0"));
  };

  auto makeRegularNhop = [](int idx) {
    return ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("2.2.2.", idx + 1)),
        InterfaceID(2),
        ECMP_WEIGHT);
  };

  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 0);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 0);

  // Single SRv6 nhop route
  RouteNextHopSet singleSrv6{makeSrv6Nhop(0)};
  RouteNextHopEntry singleEntry(singleSrv6, AdminDistance::EBGP);
  auto singleRoute = makeV6Route(
      {folly::IPAddressV6("2800:1::"), 48}, singleEntry, singleSrv6);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      singleRoute, true, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 0);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 1);

  // ECMP SRv6 route (3 nhops)
  RouteNextHopSet ecmpSrv6{
      makeSrv6Nhop(10), makeSrv6Nhop(11), makeSrv6Nhop(12)};
  RouteNextHopEntry ecmpEntry(ecmpSrv6, AdminDistance::EBGP);
  auto ecmpRoute =
      makeV6Route({folly::IPAddressV6("2800:2::"), 48}, ecmpEntry, ecmpSrv6);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpRoute, true, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 3);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 1);

  // Second route sharing same ECMP nhops — no additional count
  auto ecmpRoute2 =
      makeV6Route({folly::IPAddressV6("2800:3::"), 48}, ecmpEntry, ecmpSrv6);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpRoute2, true, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 3);

  // Remove first ECMP route — still ref'd, no decrement
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpRoute, false, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 3);

  // Remove second ECMP route — ref count reaches 0, decrement
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpRoute2, false, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 0);

  // Regular (non-SRv6) ECMP route — not counted
  RouteNextHopSet regularSet{makeRegularNhop(20), makeRegularNhop(21)};
  RouteNextHopEntry regularEntry(regularSet, AdminDistance::EBGP);
  auto regularRoute = makeV6Route(
      {folly::IPAddressV6("2800:4::"), 48}, regularEntry, regularSet);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      regularRoute, true, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 0);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 1);

  // Same SRv6 nhop used in both single-nhop route and ECMP route.
  // nhop(0) is already in singleSrv6 route above (single pool).
  // Now add it in an ECMP route too — it should count in ECMP pool as well.
  RouteNextHopSet ecmpWithShared{makeSrv6Nhop(0), makeSrv6Nhop(30)};
  RouteNextHopEntry ecmpSharedEntry(ecmpWithShared, AdminDistance::EBGP);
  auto ecmpSharedRoute = makeV6Route(
      {folly::IPAddressV6("2800:5::"), 48}, ecmpSharedEntry, ecmpWithShared);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpSharedRoute, true, state_));
  // nhop(0) counted in both pools: single(1) + ecmp(2)
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 2);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 1);

  // Remove the single-nhop route — single pool decrements
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      singleRoute, false, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 2);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 0);

  // Remove the ECMP route — ecmp pool decrements
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      ecmpSharedRoute, false, state_));
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 0);
  EXPECT_EQ(this->accountant_->srv6SingleNextHopUsage_, 0);
}

TEST_F(Srv6NextHopResourceAccountantTest, srv6NextHopResourceExceeded) {
  FLAGS_enable_srv6_nexthop_resource_protection = true;

  // MockAsic: ECMP=16 (75%=12), Single=8 (75%=6)
  auto makeSrv6Nhop = [](int idx) {
    return ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", idx + 1)),
        InterfaceID(1),
        ECMP_WEIGHT,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::vector<folly::IPAddressV6>{
            folly::IPAddressV6(fmt::format("3001:db8:{:x}::", idx + 1))},
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0"));
  };

  // Add ECMP routes: 4 groups x 3 SRv6 nhops = 12 (exactly 75% of 16)
  for (int i = 0; i < 4; ++i) {
    RouteNextHopSet nhops{
        makeSrv6Nhop(i * 3), makeSrv6Nhop(i * 3 + 1), makeSrv6Nhop(i * 3 + 2)};
    RouteNextHopEntry entry(nhops, AdminDistance::EBGP);
    auto route = makeV6Route(
        {folly::IPAddressV6(fmt::format("2800:{:x}::", i)), 48}, entry, nhops);
    EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
        route, true, state_));
  }
  EXPECT_EQ(this->accountant_->srv6EcmpNextHopUsage_, 12);

  // At 75% — final check passes
  EXPECT_TRUE(this->accountant_->checkSrv6NextHopResource(
      false /* intermediateState */));

  // Add 1 more group (total = 15) — intermediate passes, final fails
  RouteNextHopSet overflowNhops{
      makeSrv6Nhop(100), makeSrv6Nhop(101), makeSrv6Nhop(102)};
  RouteNextHopEntry overflowEntry(overflowNhops, AdminDistance::EBGP);
  auto overflowRoute = makeV6Route(
      {folly::IPAddressV6("2800:ff::"), 48}, overflowEntry, overflowNhops);
  EXPECT_TRUE(this->accountant_->checkAndUpdateSrv6NextHopResource(
      overflowRoute, true, state_));
  EXPECT_FALSE(this->accountant_->checkSrv6NextHopResource(
      false /* intermediateState */));
}

TEST_F(Srv6NextHopResourceAccountantTest, srv6UcmpDistinctWeights) {
  FLAGS_enable_srv6_nexthop_resource_protection = true;
  const int limit = asicTable_->getHwAsic(SwitchID(0))
                        ->getMaxSrv6UcmpDistinctWeights()
                        .value();

  auto makeSrv6Nhops = [limit](int group, int maxWeight, NextHopRole lastRole) {
    RouteNextHopSet nhops;
    for (int i = 1; i <= limit + 1; ++i) {
      nhops.insert(ResolvedNextHop(
          folly::IPAddress(fmt::format("1.1.{}.{}", group, i)),
          InterfaceID(1),
          std::min(i, maxWeight),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::vector<folly::IPAddressV6>{folly::IPAddressV6(
              fmt::format("3001:db8:{:x}:{:x}::", group, i))},
          TunnelType::SRV6_ENCAP,
          std::string("srv6Tunnel0"),
          std::nullopt,
          i == limit + 1 ? lastRole : NextHopRole::PRIMARY));
    }
    return nhops;
  };
  auto checkAdd = [&](int idx, const RouteNextHopSet& nhops) {
    RouteNextHopEntry entry(nhops, AdminDistance::EBGP);
    auto route = makeV6Route(
        {folly::IPAddressV6(fmt::format("2800:{:x}::", idx)), 48},
        entry,
        nhops);
    return this->accountant_->checkAndUpdateSrv6NextHopResource(
        route, true /* add */, state_);
  };

  // 1) limit + 1 distinct weights is rejected
  EXPECT_FALSE(checkAdd(0, makeSrv6Nhops(0, limit + 1, NextHopRole::PRIMARY)));
  // 2) limit distinct weights over limit + 1 next hops is accepted
  EXPECT_TRUE(checkAdd(1, makeSrv6Nhops(1, limit, NextHopRole::PRIMARY)));
  // 3) Protection groups are not limited
  EXPECT_TRUE(checkAdd(2, makeSrv6Nhops(2, limit + 1, NextHopRole::BACKUP)));
  // 4) Non-SRv6 groups are not limited
  RouteNextHopSet regular;
  for (int i = 1; i <= limit + 1; ++i) {
    regular.insert(ResolvedNextHop(
        folly::IPAddress(fmt::format("2.2.2.{}", i)), InterfaceID(2), i));
  }
  EXPECT_TRUE(checkAdd(3, regular));
}

} // namespace facebook::fboss
