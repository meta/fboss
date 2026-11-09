// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;

namespace {

constexpr auto kSwitchId = 0;

std::map<int64_t, cfg::SwitchInfo> getTestSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> map;
  cfg::SwitchInfo info;
  info.switchType() = cfg::SwitchType::NPU;
  info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  info.switchIndex() = 0;
  map.emplace(kSwitchId, info);
  return map;
}

const SwitchIdScopeResolver* scopeResolver() {
  static const SwitchIdScopeResolver kSwitchIdScopeResolver(
      getTestSwitchInfo());
  return &kSwitchIdScopeResolver;
}

folly::CIDRNetworkV6 makeSidPrefix(
    const std::string& addr = "fc00:100::1",
    uint8_t len = 48) {
  return std::make_pair(folly::IPAddressV6(addr), len);
}

std::shared_ptr<MySid> makeMySid(
    const folly::CIDRNetworkV6& prefix = makeSidPrefix(),
    MySidType type = MySidType::DECAPSULATE_AND_LOOKUP) {
  state::MySidFields fields;
  fields.type() = type;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(prefix.first);
  thriftPrefix.prefixLength() = prefix.second;
  fields.mySid() = thriftPrefix;
  auto mySid = std::make_shared<MySid>(fields);
  mySid->setUnresolveNextHopsId(std::nullopt);
  mySid->setResolvedNextHopsId(std::nullopt);
  return mySid;
}

// Create a resolved DROP route and insert into the route map
template <typename AddrT>
std::shared_ptr<Route<AddrT>> makeResolvedRoute(
    const RoutePrefix<AddrT>& prefix) {
  auto thrift = Route<AddrT>::makeThrift(prefix);
  auto route = std::make_shared<Route<AddrT>>(thrift);
  route->setResolved(
      RouteNextHopEntry(RouteForwardAction::DROP, AdminDistance::STATIC_ROUTE));
  route->publish();
  return route;
}

void addResolvedV6Route(
    IPv6NetworkToRouteMap& v6Map,
    const std::string& addr,
    uint8_t mask) {
  RoutePrefixV6 prefix{folly::IPAddressV6(addr), mask};
  v6Map.insert(prefix, makeResolvedRoute(prefix));
}

void addResolvedV4Route(
    IPv4NetworkToRouteMap& v4Map,
    const std::string& addr,
    uint8_t mask) {
  RoutePrefixV4 prefix{folly::IPAddressV4(addr), mask};
  v4Map.insert(prefix, makeResolvedRoute(prefix));
}

} // namespace

TEST(RibToSwitchStateUpdater, EmptyRoutesAndEmptyMySidTable) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;

  RibToSwitchStateUpdater updater(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto newState = updater(state);

  // State changed because FIB container is created for new VRF
  auto delta = updater.getLastDelta();
  ASSERT_TRUE(delta.has_value());
}

TEST(RibToSwitchStateUpdater, RoutesOnlyNoMySid) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;

  addResolvedV4Route(v4Map, "10.0.0.0", 24);
  addResolvedV6Route(v6Map, "2001:db8::", 32);

  RibToSwitchStateUpdater updater(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);

  // Verify FIB has the routes
  auto fibContainer =
      newState->getFibsInfoMap()->getFibContainerIf(RouterID(0));
  ASSERT_NE(fibContainer, nullptr);
  EXPECT_EQ(fibContainer->getFibV4()->size(), 1);
  EXPECT_EQ(fibContainer->getFibV6()->size(), 1);

  // Verify MySid map is unchanged (empty)
  EXPECT_EQ(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);

  auto delta = updater.getLastDelta();
  ASSERT_TRUE(delta.has_value());
  EXPECT_NE(delta->oldState(), delta->newState());
}

TEST(RibToSwitchStateUpdater, MySidOnlyNoRoutes) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;

  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;
  mySidTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;

  RibToSwitchStateUpdater updater(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);

  // Verify MySid entry was added
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);

  // Verify FIB was created but is empty
  auto fibContainer =
      newState->getFibsInfoMap()->getFibContainerIf(RouterID(0));
  ASSERT_NE(fibContainer, nullptr);
  EXPECT_EQ(fibContainer->getFibV4()->size(), 0);
  EXPECT_EQ(fibContainer->getFibV6()->size(), 0);

  auto delta = updater.getLastDelta();
  ASSERT_TRUE(delta.has_value());
  EXPECT_NE(delta->oldState(), delta->newState());
}

TEST(RibToSwitchStateUpdater, BothRoutesAndMySid) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;

  addResolvedV4Route(v4Map, "10.0.0.0", 24);
  addResolvedV6Route(v6Map, "2001:db8::", 32);

  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  auto mySid1 = makeMySid(makeSidPrefix("fc00:200::1", 64));
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;
  mySidTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;
  mySidTable[makeSidPrefix("fc00:200::1", 64)] = mySid1;

  RibToSwitchStateUpdater updater(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto newState = updater(state);

  EXPECT_NE(newState, state);

  // Verify FIB has the routes
  auto fibContainer =
      newState->getFibsInfoMap()->getFibContainerIf(RouterID(0));
  ASSERT_NE(fibContainer, nullptr);
  EXPECT_EQ(fibContainer->getFibV4()->size(), 1);
  EXPECT_EQ(fibContainer->getFibV6()->size(), 1);

  // Verify MySid entries were added
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
  EXPECT_NE(newState->getMySids()->getNodeIf("fc00:200::1/64"), nullptr);

  auto delta = updater.getLastDelta();
  ASSERT_TRUE(delta.has_value());
  // Delta captures the original state as old and the combined update as new
  EXPECT_EQ(delta->oldState(), state);
  EXPECT_EQ(delta->newState(), newState);
}

TEST(RibToSwitchStateUpdater, LastDeltaSpansBothUpdaters) {
  // Verify that lastDelta captures the original state as oldState
  // and the final state (after both FIB and MySid updates) as newState
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;

  addResolvedV4Route(v4Map, "10.0.0.0", 24);

  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;
  mySidTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;

  RibToSwitchStateUpdater updater(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto newState = updater(state);

  auto delta = updater.getLastDelta();
  ASSERT_TRUE(delta.has_value());

  // oldState should be the original published state
  EXPECT_EQ(delta->oldState(), state);
  // newState should have both FIB routes and MySid entries
  EXPECT_EQ(delta->newState(), newState);

  // Verify newState has both changes
  auto fibContainer =
      delta->newState()->getFibsInfoMap()->getFibContainerIf(RouterID(0));
  ASSERT_NE(fibContainer, nullptr);
  EXPECT_EQ(fibContainer->getFibV4()->size(), 1);
  EXPECT_NE(
      delta->newState()->getMySids()->getNodeIf("fc00:100::1/48"), nullptr);
}

TEST(RibToSwitchStateUpdater, NoChangeOnSecondCallWithSameData) {
  auto state = std::make_shared<SwitchState>();
  state->publish();

  IPv4NetworkToRouteMap v4Map;
  IPv6NetworkToRouteMap v6Map;
  LabelToRouteMap labelMap;

  addResolvedV4Route(v4Map, "10.0.0.0", 24);

  auto mySid0 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  MySidTable mySidTable;
  NextHopIDManager nhopIdManager;
  mySidTable[makeSidPrefix("fc00:100::1", 48)] = mySid0;

  // First call: populates FIB and MySid
  RibToSwitchStateUpdater updater1(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto state1 = updater1(state);
  state1->publish();

  // Second call with same data: should produce no change
  RibToSwitchStateUpdater updater2(
      scopeResolver(),
      RouterID(0),
      v4Map,
      v6Map,
      labelMap,
      &nhopIdManager,
      mySidTable);
  auto state2 = updater2(state1);

  // Routes are the same pointers, MySid entries are equal
  // FIB updater returns same state when no change, MySid updater also
  EXPECT_EQ(state1, state2);

  auto delta = updater2.getLastDelta();
  ASSERT_TRUE(delta.has_value());
  EXPECT_EQ(delta->oldState(), delta->newState());
}
