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
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RoutingInformationBase.h"

#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>
#include <gtest/gtest.h>
#include <memory>
#include <unordered_set>

using namespace facebook::fboss;

folly::IPAddressV4 ip4_0("0.0.0.0");
folly::IPAddressV4 ip4_48("48.0.0.0");
folly::IPAddressV4 ip4_64("64.0.0.0");
folly::IPAddressV4 ip4_72("72.0.0.0");
folly::IPAddressV4 ip4_80("80.0.0.0");
folly::IPAddressV4 ip4_128("128.0.0.0");
folly::IPAddressV4 ip4_160("160.0.0.0");

folly::IPAddressV6 ip6_0("::");
folly::IPAddressV6 ip6_48("3000::");
folly::IPAddressV6 ip6_64("4000::");
folly::IPAddressV6 ip6_72("4800::");
folly::IPAddressV6 ip6_80("5000::");
folly::IPAddressV6 ip6_128("8000::");
folly::IPAddressV6 ip6_160("A000::");

const auto kRid0 = RouterID(0);
constexpr uint32_t kEcmpWidth = 64;

template <typename AddressT>
void addRoute(
    NetworkToRouteMap<AddressT>& rib,
    const std::shared_ptr<Route<AddressT>>& route) {
  rib.insert(route->prefix().network, route->prefix().mask, route);
}

void addRoute(RoutingInformationBase& rib, const UnicastRoute& route) {
  rib.update(
      nullptr,
      kRid0,
      ClientID::BGPD,
      AdminDistance::EBGP,
      {route},
      {},
      false,
      "Rib only update",
      noopFibUpdate,
      nullptr);
}

class V4LpmTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib.ensureVrf(kRid0);
    // The following prefixes are inserted into the FIB:
    // 0
    // 0000
    // 00110
    // 010
    // 01001
    // 0101
    // 10
    // 101
    addRoute(rib, makeDropUnicastRoute({ip4_0, 1}));
    addRoute(rib, makeDropUnicastRoute({ip4_128, 2}));
    addRoute(rib, makeDropUnicastRoute({ip4_64, 3}));
    addRoute(rib, makeDropUnicastRoute({ip4_160, 3}));
    addRoute(rib, makeDropUnicastRoute({ip4_80, 4}));
    addRoute(rib, makeDropUnicastRoute({ip4_0, 4}));
    addRoute(rib, makeDropUnicastRoute({ip4_48, 5}));
    addRoute(rib, makeDropUnicastRoute({ip4_72, 6}));
  }

  std::shared_ptr<Route<folly::IPAddressV4>> longestMatch(
      folly::IPAddressV4 addr) {
    return rib.longestMatch(addr, kRid0);
  }

  RoutingInformationBase rib;
};

class V6LpmTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rib.ensureVrf(kRid0);
    // The following prefixes are inserted into the FIB:
    // 0
    // 0000
    // 00110
    // 010
    // 01001
    // 0101
    // 10
    // 101
    addRoute(rib, makeDropUnicastRoute({ip6_0, 1}));
    addRoute(rib, makeDropUnicastRoute({ip6_128, 2}));
    addRoute(rib, makeDropUnicastRoute({ip6_64, 3}));
    addRoute(rib, makeDropUnicastRoute({ip6_160, 3}));
    addRoute(rib, makeDropUnicastRoute({ip6_80, 4}));
    addRoute(rib, makeDropUnicastRoute({ip6_0, 4}));
    addRoute(rib, makeDropUnicastRoute({ip6_48, 5}));
    addRoute(rib, makeDropUnicastRoute({ip6_72, 6}));
  }
  std::shared_ptr<Route<folly::IPAddressV6>> longestMatch(
      folly::IPAddressV6 addr) {
    return rib.longestMatch(addr, kRid0);
  }

  RoutingInformationBase rib;
};

template <typename AddressT>
void CHECK_LPM(
    const std::shared_ptr<facebook::fboss::Route<AddressT>>& route,
    AddressT address,
    uint8_t mask) {
  facebook::fboss::RoutePrefix<AddressT> routePrefix{address, mask};
  facebook::fboss::RouteFields<AddressT> fields(routePrefix);

  EXPECT_EQ(route->prefix().network(), address);
  EXPECT_EQ(route->prefix().mask(), mask);
}

TEST_F(V4LpmTest, SparseLPM) {
  // Candidate prefixes: 0/1, 0/4
  CHECK_LPM(longestMatch(folly::IPAddressV4("0.0.0.0")), ip4_0, 4);

  // Candidate prefixes: 0/1, 64/3
  CHECK_LPM(longestMatch(folly::IPAddressV4("64.1.0.1")), ip4_64, 3);

  // Candidate prefixes: 128/2, 160/3
  CHECK_LPM(longestMatch(folly::IPAddressV4("161.16.8.1")), ip4_160, 3);
}

TEST_F(V6LpmTest, SparseLPM) {
  // Candidate prefixes: ::/1, ::/4
  CHECK_LPM(longestMatch(folly::IPAddressV6("::")), ip6_0, 4);

  // Candidate prefixes: ::/1, 4000::/3
  CHECK_LPM(longestMatch(folly::IPAddressV6("4001:1::")), ip6_64, 3);

  // Candidate prefixes: 8000::/2, A000::/3
  CHECK_LPM(longestMatch(folly::IPAddressV6("A110:801::")), ip6_160, 3);
}

TEST_F(V4LpmTest, LPMDoesNotExist) {
  folly::IPAddressV4 address("192.0.0.0");

  EXPECT_EQ(nullptr, longestMatch(address));
}

TEST_F(V6LpmTest, LPMDoesNotExist) {
  folly::IPAddressV6 address("C000::");

  EXPECT_EQ(nullptr, longestMatch(address));
}

TEST_F(V4LpmTest, IncreasingLPMSequence) {
  folly::IPAddressV4 address("255.255.255.255");
  for (uint8_t mask = 0; mask <= address.bitCount(); ++mask) {
    auto addressWithCurrentMask = address.mask(mask);
    addRoute(rib, makeDropUnicastRoute({addressWithCurrentMask, mask}));
    CHECK_LPM(longestMatch(address), addressWithCurrentMask, mask);
  }
  auto ribBack = RoutingInformationBase::fromThrift(rib.toThrift(), kEcmpWidth);
  EXPECT_EQ(ribBack->toThrift(), rib.toThrift());
}

TEST_F(V6LpmTest, IncreasingLPMSequence) {
  folly::IPAddressV6 address("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF");
  for (uint16_t mask = 0; mask <= address.bitCount(); ++mask) {
    auto addressWithCurrentMask = address.mask(mask);
    addRoute(rib, makeDropUnicastRoute({addressWithCurrentMask, mask}));
    CHECK_LPM(longestMatch(address), addressWithCurrentMask, mask);
  }
  auto ribBack = RoutingInformationBase::fromThrift(rib.toThrift(), kEcmpWidth);
  EXPECT_EQ(ribBack->toThrift(), rib.toThrift());
}

TEST_F(V4LpmTest, DecreasingLPMSequence) {
  folly::IPAddressV4 address("255.255.255.255");
  for (int8_t mask = address.bitCount(); mask >= 0; --mask) {
    auto addressWithCurrentMask = address.mask(mask);
    addRoute(rib, makeDropUnicastRoute({addressWithCurrentMask, mask}));
    CHECK_LPM(longestMatch(address), address, address.bitCount());
  }
  auto ribBack = RoutingInformationBase::fromThrift(rib.toThrift(), kEcmpWidth);
  EXPECT_EQ(ribBack->toThrift(), rib.toThrift());
}

TEST_F(V6LpmTest, DecreasingLPMSequence) {
  folly::IPAddressV6 address("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF");
  for (int16_t mask = address.bitCount(); mask >= 0; --mask) {
    auto addressWithCurrentMask = address.mask(mask);
    addRoute(rib, makeDropUnicastRoute({addressWithCurrentMask, mask}));
    CHECK_LPM(longestMatch(address), address, address.bitCount());
  }
  auto ribBack = RoutingInformationBase::fromThrift(rib.toThrift(), kEcmpWidth);
  EXPECT_EQ(ribBack->toThrift(), rib.toThrift());
}

namespace {

std::map<int64_t, cfg::SwitchInfo> getTestSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> map;
  cfg::SwitchInfo info{};
  info.switchType() = cfg::SwitchType::NPU;
  info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  info.switchIndex() = 0;
  map.emplace(0, info);
  return map;
}

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kSwitchIdScopeResolver(
      getTestSwitchInfo());
  return &kSwitchIdScopeResolver;
}

} // namespace

template <typename AddrT, bool ResolveFromId, bool Normalized = true>
struct TestParam {
  using AddressType = AddrT;
  static constexpr bool resolveFromId = ResolveFromId;
  static constexpr bool normalized = Normalized;
};

template <typename AddrT>
struct NHopsTestData;

template <>
struct NHopsTestData<folly::IPAddressV4> {
  static std::vector<folly::CIDRNetwork> dropRoutes() {
    return {
        {ip4_0, 1},
        {ip4_128, 2},
        {ip4_64, 3},
        {ip4_160, 3},
        {ip4_80, 4},
        {ip4_0, 4},
        {ip4_48, 5},
        {ip4_72, 6}};
  }
  static folly::IPAddressV4 noMatchAddr() {
    return folly::IPAddressV4("192.0.0.0");
  }
  static folly::IPAddressV4 resolvedAddr() {
    return folly::IPAddressV4("0.0.0.0");
  }
  static folly::IPAddressV4 resolvedRouteNetwork() {
    return ip4_0;
  }
  static uint8_t resolvedRouteMask() {
    return 4;
  }
  static folly::CIDRNetwork testPrefix() {
    return {folly::IPAddressV4("192.168.0.0"), 16};
  }
  static folly::IPAddressV4 testLookup() {
    return folly::IPAddressV4("192.168.1.1");
  }
  static folly::IPAddress unreachableNhop() {
    return folly::IPAddress("200.0.0.1");
  }
  static folly::CIDRNetwork intfPrefix() {
    return {folly::IPAddress("10.0.0.0"), 24};
  }
  static folly::IPAddress intfAddr() {
    return folly::IPAddress("10.0.0.1");
  }
  static folly::CIDRNetwork nhopRoutePrefix() {
    return {folly::IPAddress("20.0.0.0"), 8};
  }
  static folly::IPAddress nhopRouteNhop() {
    return folly::IPAddress("10.0.0.2");
  }
  static folly::IPAddressV4 nhopLookup() {
    return folly::IPAddressV4("20.0.0.1");
  }
  static folly::IPAddressV4 nhopRouteNetwork() {
    return folly::IPAddressV4("20.0.0.0");
  }
  static uint8_t nhopRouteMask() {
    return 8;
  }
};

template <>
struct NHopsTestData<folly::IPAddressV6> {
  static std::vector<folly::CIDRNetwork> dropRoutes() {
    return {
        {ip6_0, 1},
        {ip6_128, 2},
        {ip6_64, 3},
        {ip6_160, 3},
        {ip6_80, 4},
        {ip6_0, 4},
        {ip6_48, 5},
        {ip6_72, 6}};
  }
  static folly::IPAddressV6 noMatchAddr() {
    return folly::IPAddressV6("C000::");
  }
  static folly::IPAddressV6 resolvedAddr() {
    return folly::IPAddressV6("::");
  }
  static folly::IPAddressV6 resolvedRouteNetwork() {
    return ip6_0;
  }
  static uint8_t resolvedRouteMask() {
    return 4;
  }
  static folly::CIDRNetwork testPrefix() {
    return {folly::IPAddressV6("C000::"), 16};
  }
  static folly::IPAddressV6 testLookup() {
    return folly::IPAddressV6("C000::1");
  }
  static folly::IPAddress unreachableNhop() {
    return folly::IPAddress("D000::1");
  }
  static folly::CIDRNetwork intfPrefix() {
    return {folly::IPAddress("1::"), 48};
  }
  static folly::IPAddress intfAddr() {
    return folly::IPAddress("1::1");
  }
  static folly::CIDRNetwork nhopRoutePrefix() {
    return {folly::IPAddress("2000::"), 16};
  }
  static folly::IPAddress nhopRouteNhop() {
    return folly::IPAddress("1::2");
  }
  static folly::IPAddressV6 nhopLookup() {
    return folly::IPAddressV6("2000::1");
  }
  static folly::IPAddressV6 nhopRouteNetwork() {
    return folly::IPAddressV6("2000::");
  }
  static uint8_t nhopRouteMask() {
    return 16;
  }
};

template <typename ParamT>
class GetRouteAndNextHopsTest : public ::testing::Test {
 protected:
  using AddrT = typename ParamT::AddressType;
  using TD = NHopsTestData<AddrT>;
  static constexpr bool kResolveFromId = ParamT::resolveFromId;
  static constexpr bool kNormalized = ParamT::normalized;

  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    FLAGS_resolve_nexthops_from_id = kResolveFromId;

    rib_ = std::make_unique<RoutingInformationBase>();
    switchState_ = makeSwitchState();

    // Add interface route so nexthops can resolve
    RibRouteTables::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
    RibRouteTables::PrefixToInterfaceIDAndIP intfMap;
    intfMap[TD::intfPrefix()] = {InterfaceID(1), TD::intfAddr()};
    interfaceRoutes[kRid0] = intfMap;

    rib_->reconfigure(
        scopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {} /* staticMySids */,
        noopFibUpdate,
        &switchState_);

    // Add DROP routes
    for (const auto& prefix : TD::dropRoutes()) {
      addRoute(*rib_, makeDropUnicastRoute(prefix));
    }

    // Add a route with a nexthop that resolves via the interface route
    rib_->update(
        scopeResolver(),
        kRid0,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {makeUnicastRoute(TD::nhopRoutePrefix(), {TD::nhopRouteNhop()})},
        {},
        false,
        "add route with nexthops",
        noopFibUpdate,
        &switchState_);
  }

  std::shared_ptr<SwitchState> makeSwitchState() const {
    auto switchState = std::make_shared<SwitchState>();
    const HwSwitchMatcher matcher(std::unordered_set<SwitchID>{SwitchID(0)});

    auto intf = std::make_shared<Interface>(
        InterfaceID(1),
        kRid0,
        std::optional<VlanID>(VlanID(1)),
        folly::StringPiece("intf1"),
        folly::MacAddress("00:00:00:00:00:01"),
        9000,
        false,
        false);
    intf->setAddresses({{TD::intfAddr(), TD::intfPrefix().second}});

    auto intfs = std::make_shared<MultiSwitchInterfaceMap>();
    intfs->addNode(intf, matcher);
    switchState->resetIntfs(intfs);

    switchState->publish();
    return switchState;
  }

  // When resolve_nexthops_from_id is true, verify that toggling the flag
  // produces the same nexthops (ID path vs normalizedNextHops() path agree).
  void verifyIdPathConsistency(const AddrT& lookupAddr) {
    if (!kResolveFromId) {
      return;
    }
    auto resultWithId =
        rib_->getRouteAndNextHops(lookupAddr, kRid0, kNormalized);
    FLAGS_resolve_nexthops_from_id = false;
    auto resultWithoutId =
        rib_->getRouteAndNextHops(lookupAddr, kRid0, kNormalized);
    FLAGS_resolve_nexthops_from_id = true;
    ASSERT_EQ(resultWithId.has_value(), resultWithoutId.has_value());
    if (resultWithId.has_value()) {
      EXPECT_EQ(resultWithId->second, resultWithoutId->second);
    }
  }

  std::unique_ptr<RoutingInformationBase> rib_;
  std::shared_ptr<SwitchState> switchState_;
};

using NHopsTestTypes = ::testing::Types<
    TestParam<folly::IPAddressV4, false, true>,
    TestParam<folly::IPAddressV4, true, true>,
    TestParam<folly::IPAddressV6, false, true>,
    TestParam<folly::IPAddressV6, true, true>,
    TestParam<folly::IPAddressV4, false, false>,
    TestParam<folly::IPAddressV4, true, false>,
    TestParam<folly::IPAddressV6, false, false>,
    TestParam<folly::IPAddressV6, true, false>>;
TYPED_TEST_SUITE(GetRouteAndNextHopsTest, NHopsTestTypes);

TYPED_TEST(GetRouteAndNextHopsTest, NoRoute) {
  using TD = typename TestFixture::TD;
  auto result = this->rib_->getRouteAndNextHops(
      TD::noMatchAddr(), kRid0, TestFixture::kNormalized);
  EXPECT_FALSE(result.has_value());
}

TYPED_TEST(GetRouteAndNextHopsTest, UnresolvedRoute) {
  using TD = typename TestFixture::TD;
  addRoute(
      *this->rib_, makeUnicastRoute(TD::testPrefix(), {TD::unreachableNhop()}));
  auto result = this->rib_->getRouteAndNextHops(
      TD::testLookup(), kRid0, TestFixture::kNormalized);
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(result->first->isResolved());
  this->verifyIdPathConsistency(TD::testLookup());
}

TYPED_TEST(GetRouteAndNextHopsTest, ResolvedDropRoute) {
  using TD = typename TestFixture::TD;
  auto result = this->rib_->getRouteAndNextHops(
      TD::resolvedAddr(), kRid0, TestFixture::kNormalized);
  ASSERT_TRUE(result.has_value());
  auto& [route, nhops] = *result;
  EXPECT_EQ(route->prefix().network(), TD::resolvedRouteNetwork());
  EXPECT_EQ(route->prefix().mask(), TD::resolvedRouteMask());
  EXPECT_TRUE(route->isResolved());
  EXPECT_TRUE(nhops.empty());
  this->verifyIdPathConsistency(TD::resolvedAddr());
}

TYPED_TEST(GetRouteAndNextHopsTest, RouteDeleted) {
  using TD = typename TestFixture::TD;
  addRoute(*this->rib_, makeDropUnicastRoute(TD::testPrefix()));

  auto result = this->rib_->getRouteAndNextHops(
      TD::testLookup(), kRid0, TestFixture::kNormalized);
  ASSERT_TRUE(result.has_value());

  this->rib_->update(
      nullptr,
      kRid0,
      ClientID::BGPD,
      AdminDistance::EBGP,
      std::vector<UnicastRoute>{},
      {toIpPrefix(TD::testPrefix())},
      false,
      "delete route",
      noopFibUpdate,
      nullptr);

  result = this->rib_->getRouteAndNextHops(
      TD::testLookup(), kRid0, TestFixture::kNormalized);
  EXPECT_FALSE(result.has_value());
}

TYPED_TEST(GetRouteAndNextHopsTest, ResolvedRouteWithNexthops) {
  using TD = typename TestFixture::TD;
  auto result = this->rib_->getRouteAndNextHops(
      TD::nhopLookup(), kRid0, TestFixture::kNormalized);
  ASSERT_TRUE(result.has_value());
  auto& [route, nhops] = *result;
  EXPECT_EQ(route->prefix().network(), TD::nhopRouteNetwork());
  EXPECT_EQ(route->prefix().mask(), TD::nhopRouteMask());
  EXPECT_TRUE(route->isResolved());
  EXPECT_FALSE(nhops.empty());
  this->verifyIdPathConsistency(TD::nhopLookup());
}
