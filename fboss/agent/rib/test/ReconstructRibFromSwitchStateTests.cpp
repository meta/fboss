// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/RouteTypes.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;

DECLARE_bool(enable_nexthop_id_manager);
DECLARE_bool(resolve_nexthops_from_id);

namespace {

constexpr uint32_t kEcmpWidth = 64;

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
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

template <typename AddrT>
std::shared_ptr<Route<AddrT>> makeUnresolvedRoute(
    const RoutePrefix<AddrT>& prefix) {
  auto thrift = Route<AddrT>::makeThrift(prefix);
  auto route = std::make_shared<Route<AddrT>>(thrift);
  // Route starts unresolved by default (no setResolved call)
  route->publish();
  return route;
}

RoutePrefixV4 makeV4Prefix(const std::string& addr, uint8_t mask) {
  return RoutePrefixV4{folly::IPAddressV4(addr), mask};
}

RoutePrefixV6 makeV6Prefix(const std::string& addr, uint8_t mask) {
  return RoutePrefixV6{folly::IPAddressV6(addr), mask};
}

constexpr int64_t kOff = 1LL << 62; // NextHopSetID allocation base.

NextHop mkNh(const std::string& addr) {
  return makeResolvedNextHop(InterfaceID(1), addr, UCMP_DEFAULT_WEIGHT);
}

// Adds a resolved route carrying both resolvedNextHopSetID and
// normalizedResolvedNextHopSetID to a FIB. The route's own nexthops are
// irrelevant to reconstruct (which rebuilds from the id maps); only the
// persisted SetIDs matter. normalizedSetId defaults to setId; pass a distinct
// value to point the normalized ref at a different set than the resolved ref.
template <typename AddrT, typename FibT>
void addResolvedRoute(
    const std::shared_ptr<FibT>& fib,
    const RoutePrefix<AddrT>& prefix,
    NextHopSetID setId,
    const std::optional<NextHopSetID>& normalizedSetId = std::nullopt) {
  RouteNextHopSet nhops{mkNh("10.0.0.1")};
  auto route = std::make_shared<Route<AddrT>>(Route<AddrT>::makeThrift(prefix));
  // Stamp the per-client ID so backfillNextHopIds treats the entry as already
  // ID'd and does not mint a spurious set for these placeholder nexthops.
  RouteNextHopEntry clientEntry(nhops, AdminDistance::EBGP);
  std::optional<NextHopSetID> clientSetIdOpt(setId);
  clientEntry.setClientNextHopSetID(clientSetIdOpt);
  route->update(ClientID::BGPD, clientEntry);
  auto fwdInfo = route->getForwardInfo().toThrift();
  fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(setId);
  fwdInfo.normalizedResolvedNextHopSetID() =
      static_cast<uint64_t>(normalizedSetId.value_or(setId));
  route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  route->publish();
  fib->addNode(route);
}

// Adds a V4 route whose BGPD per-client entry carries clientNextHopSetID, to
// exercise the per-client reference repoint.
void addV4ClientRoute(
    const std::shared_ptr<ForwardingInformationBaseV4>& fibV4,
    const std::string& prefixStr,
    NextHopSetID clientSetId) {
  RouteNextHopSet nhops{mkNh("10.0.0.1")};
  auto route =
      std::make_shared<RouteV4>(RouteV4::makeThrift(makePrefixV4(prefixStr)));
  RouteNextHopEntry entry(
      nhops,
      AdminDistance::EBGP,
      std::optional<RouteCounterID>(std::nullopt),
      std::optional<cfg::AclLookupClass>(std::nullopt),
      std::optional<cfg::SwitchingMode>(std::nullopt),
      std::optional<RouteNextHopEntry::NextHopSet>(std::nullopt),
      std::optional<NextHopSetID>(std::nullopt),
      std::optional<NextHopSetID>(std::nullopt),
      std::optional<NextHopSetID>(clientSetId));
  route->update(ClientID::BGPD, entry);
  // Empty forward (no resolved nexthops) so backfillNextHopIds does not stamp a
  // resolved set for the placeholder nexthops; the per-client
  // clientNextHopSetID is the reference under test.
  auto fwdInfo = route->getForwardInfo().toThrift();
  route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  route->publish();
  fibV4->addNode(route);
}

std::shared_ptr<ForwardingInformationBaseMap> makeFibsMap() {
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto container =
      std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));
  container->ref<switch_state_tags::fibV4>() =
      std::make_shared<ForwardingInformationBaseV4>();
  container->ref<switch_state_tags::fibV6>() =
      std::make_shared<ForwardingInformationBaseV6>();
  fibsMap->updateForwardingInformationBaseContainer(container);
  return fibsMap;
}

} // namespace

namespace {

using V4UnresolvedRouteMap = std::unordered_map<
    folly::CIDRNetworkV4,
    std::shared_ptr<Route<folly::IPAddressV4>>>;
using V6UnresolvedRouteMap = std::unordered_map<
    folly::CIDRNetworkV6,
    std::shared_ptr<Route<folly::IPAddressV6>>>;

template <typename AddrT>
auto toKey(const RoutePrefix<AddrT>& p) {
  return std::make_pair(p.network(), p.mask());
}

} // namespace

TEST(ReconstructRibFromFib, EmptyFibEmptyRib) {
  auto fib = std::make_shared<ForwardingInformationBaseV4>();
  IPv4NetworkToRouteMap rib;
  V4UnresolvedRouteMap index;

  reconstructRib(fib, &rib, index);

  EXPECT_EQ(rib.size(), 0);
}

TEST(ReconstructRibFromFib, FibPopulatesEmptyRibV4) {
  auto fib = std::make_shared<ForwardingInformationBaseV4>();

  auto prefix1 = makeV4Prefix("10.0.0.0", 24);
  auto prefix2 = makeV4Prefix("192.168.1.0", 16);
  auto route1 = makeResolvedRoute(prefix1);
  auto route2 = makeResolvedRoute(prefix2);
  fib->addNode(route1);
  fib->addNode(route2);

  IPv4NetworkToRouteMap rib;
  V4UnresolvedRouteMap index;
  reconstructRib(fib, &rib, index);

  EXPECT_EQ(rib.size(), 2);
}

TEST(ReconstructRibFromFib, FibPopulatesEmptyRibV6) {
  auto fib = std::make_shared<ForwardingInformationBaseV6>();

  auto prefix1 = makeV6Prefix("2001:db8::", 32);
  auto prefix2 = makeV6Prefix("fc00:100::", 48);
  auto route1 = makeResolvedRoute(prefix1);
  auto route2 = makeResolvedRoute(prefix2);
  fib->addNode(route1);
  fib->addNode(route2);

  IPv6NetworkToRouteMap rib;
  V6UnresolvedRouteMap index;
  reconstructRib(fib, &rib, index);

  EXPECT_EQ(rib.size(), 2);
}

TEST(ReconstructRibFromFib, UnresolvedRoutesPreserved) {
  // Index has an unresolved route; FIB has a resolved route at a different
  // prefix. After reconstruction, both should be present.
  auto fib = std::make_shared<ForwardingInformationBaseV4>();

  auto resolvedPrefix = makeV4Prefix("10.0.0.0", 24);
  auto resolvedRoute = makeResolvedRoute(resolvedPrefix);
  fib->addNode(resolvedRoute);

  IPv4NetworkToRouteMap rib;
  auto unresolvedPrefix = makeV4Prefix("172.16.0.0", 12);
  auto unresolvedRoute = makeUnresolvedRoute(unresolvedPrefix);
  V4UnresolvedRouteMap index;
  index[toKey(unresolvedPrefix)] = unresolvedRoute;

  reconstructRib(fib, &rib, index);

  // Both FIB resolved route and indexed unresolved route should be present
  EXPECT_EQ(rib.size(), 2);
}

TEST(ReconstructRibFromFib, ResolvedRoutesReplacedByFib) {
  // RIB has resolved routes that differ from FIB.
  // After reconstruction, FIB routes should replace RIB resolved routes.
  auto fib = std::make_shared<ForwardingInformationBaseV4>();

  auto prefix = makeV4Prefix("10.0.0.0", 24);
  auto fibRoute = makeResolvedRoute(prefix);
  fib->addNode(fibRoute);

  IPv4NetworkToRouteMap rib;
  // Add a different resolved route for the same prefix to RIB
  auto ribRoute = makeResolvedRoute(prefix);
  rib.insert(prefix, ribRoute);
  // Also add a route not in FIB (resolved)
  auto extraPrefix = makeV4Prefix("192.168.0.0", 16);
  auto extraRoute = makeResolvedRoute(extraPrefix);
  rib.insert(extraPrefix, extraRoute);

  V4UnresolvedRouteMap index;
  reconstructRib(fib, &rib, index);

  // Only the FIB route should remain (extra resolved route was cleared)
  EXPECT_EQ(rib.size(), 1);
}

TEST(ReconstructRibFromFib, OnlyUnresolvedRoutesWithEmptyFib) {
  // Index has only unresolved routes, FIB is empty.
  // After reconstruction, unresolved routes from the index should be present.
  auto fib = std::make_shared<ForwardingInformationBaseV4>();

  IPv4NetworkToRouteMap rib;
  auto prefix1 = makeV4Prefix("10.0.0.0", 24);
  auto prefix2 = makeV4Prefix("172.16.0.0", 12);
  V4UnresolvedRouteMap index;
  index[toKey(prefix1)] = makeUnresolvedRoute(prefix1);
  index[toKey(prefix2)] = makeUnresolvedRoute(prefix2);

  reconstructRib(fib, &rib, index);

  // Unresolved routes should appear
  EXPECT_EQ(rib.size(), 2);
}

TEST(ReconstructRibFromFib, MixedRibWithFibOverwrite) {
  // RIB has resolved routes that don't overlap with FIB.
  // Index has an unresolved route.
  // After reconstruction:
  //   - Old resolved routes from RIB are gone
  //   - FIB resolved routes are imported
  //   - Unresolved route from index is added
  auto fib = std::make_shared<ForwardingInformationBaseV6>();

  auto fibPrefix1 = makeV6Prefix("2001:db8::", 32);
  auto fibPrefix2 = makeV6Prefix("2001:db9::", 32);
  fib->addNode(makeResolvedRoute(fibPrefix1));
  fib->addNode(makeResolvedRoute(fibPrefix2));

  IPv6NetworkToRouteMap rib;
  // Resolved route in RIB that is NOT in FIB — should be removed
  auto oldResolvedPrefix = makeV6Prefix("fc00::", 16);
  rib.insert(oldResolvedPrefix, makeResolvedRoute(oldResolvedPrefix));
  // Unresolved route lives in the index — should be preserved
  auto unresolvedPrefix = makeV6Prefix("fe80::", 10);
  V6UnresolvedRouteMap index;
  index[toKey(unresolvedPrefix)] = makeUnresolvedRoute(unresolvedPrefix);

  reconstructRib(fib, &rib, index);

  // 2 FIB routes + 1 unresolved route = 3
  EXPECT_EQ(rib.size(), 3);
}

TEST(ReconstructRibFromFib, UnresolvedRouteOverwritesFibRouteForSamePrefix) {
  // If an unresolved route has the same prefix as a FIB route,
  // the unresolved route is re-inserted after FIB import,
  // so it overwrites the FIB route in the RIB.
  auto fib = std::make_shared<ForwardingInformationBaseV4>();

  auto sharedPrefix = makeV4Prefix("10.0.0.0", 24);
  fib->addNode(makeResolvedRoute(sharedPrefix));

  IPv4NetworkToRouteMap rib;
  auto unresolvedRoute = makeUnresolvedRoute(sharedPrefix);
  V4UnresolvedRouteMap index;
  index[toKey(sharedPrefix)] = unresolvedRoute;

  reconstructRib(fib, &rib, index);

  // The unresolved route overwrites the FIB route for the same prefix
  EXPECT_EQ(rib.size(), 1);
}

// --- Tests for reconstructMySidTableFromSwitchState ---

TEST(ReconstructMySidTable, EmptyMapEmptyTable) {
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  MySidTable table;

  reconstructMySidTableFromSwitchState(mySidMap, &table);

  EXPECT_EQ(table.size(), 0);
}

TEST(ReconstructMySidTable, PopulatesEmptyTable) {
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();

  auto prefix1 = makeSidPrefix("fc00:100::1", 48);
  auto prefix2 = makeSidPrefix("fc00:200::1", 64);
  mySidMap->addNode(makeMySid(prefix1), scope());
  mySidMap->addNode(makeMySid(prefix2), scope());

  MySidTable table;
  reconstructMySidTableFromSwitchState(mySidMap, &table);

  EXPECT_EQ(table.size(), 2);
  EXPECT_NE(table.find(prefix1), table.end());
  EXPECT_NE(table.find(prefix2), table.end());
}

TEST(ReconstructMySidTable, ClearsExistingEntries) {
  // MySidTable has entries not in the state map.
  // After reconstruction, only entries from the state map should remain.
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();

  auto statePrefix = makeSidPrefix("fc00:100::1", 48);
  mySidMap->addNode(makeMySid(statePrefix), scope());

  MySidTable table;
  auto oldPrefix = makeSidPrefix("fc00:999::1", 48);
  table[oldPrefix] = makeMySid(oldPrefix);

  reconstructMySidTableFromSwitchState(mySidMap, &table);

  EXPECT_EQ(table.size(), 1);
  EXPECT_NE(table.find(statePrefix), table.end());
  EXPECT_EQ(table.find(oldPrefix), table.end());
}

TEST(ReconstructMySidTable, ReplacesTableWithStateEntries) {
  // MySidTable has some overlapping entries with state map.
  // After reconstruction, table should exactly match state map.
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();

  auto prefix1 = makeSidPrefix("fc00:100::1", 48);
  auto prefix2 = makeSidPrefix("fc00:200::1", 64);
  mySidMap->addNode(makeMySid(prefix1), scope());
  mySidMap->addNode(makeMySid(prefix2), scope());

  MySidTable table;
  // Overlapping entry
  table[prefix1] = makeMySid(prefix1);
  // Extra entry not in state
  auto extraPrefix = makeSidPrefix("fc00:300::1", 48);
  table[extraPrefix] = makeMySid(extraPrefix);

  reconstructMySidTableFromSwitchState(mySidMap, &table);

  EXPECT_EQ(table.size(), 2);
  EXPECT_NE(table.find(prefix1), table.end());
  EXPECT_NE(table.find(prefix2), table.end());
  EXPECT_EQ(table.find(extraPrefix), table.end());
}

TEST(ReconstructMySidTable, EmptyMapClearsTable) {
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();

  MySidTable table;
  auto prefix1 = makeSidPrefix("fc00:100::1", 48);
  auto prefix2 = makeSidPrefix("fc00:200::1", 64);
  table[prefix1] = makeMySid(prefix1);
  table[prefix2] = makeMySid(prefix2);

  reconstructMySidTableFromSwitchState(mySidMap, &table);

  EXPECT_EQ(table.size(), 0);
}

TEST(ReconstructMySidTable, PreservesMySidType) {
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();

  auto prefix = makeSidPrefix("fc00:100::1", 48);
  auto mySid = makeMySid(prefix, MySidType::DECAPSULATE_AND_LOOKUP);
  mySidMap->addNode(mySid, scope());

  MySidTable table;
  reconstructMySidTableFromSwitchState(mySidMap, &table);

  ASSERT_EQ(table.size(), 1);
  EXPECT_EQ(table[prefix]->getType(), MySidType::DECAPSULATE_AND_LOOKUP);
}

// --- Tests for fromThrift with mySidMap ---

TEST(FromThriftWithMySid, NullMySidMap) {
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, nullptr, nullptr, nullptr, kEcmpWidth, nullptr);

  EXPECT_EQ(rib->getMySidTableCopy().size(), 0);
}

TEST(FromThriftWithMySid, EmptyMySidMap) {
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, nullptr, nullptr, mySidMap, kEcmpWidth, nullptr);

  EXPECT_EQ(rib->getMySidTableCopy().size(), 0);
}

TEST(FromThriftWithMySid, PopulatesMySidTable) {
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  auto prefix1 = makeSidPrefix("fc00:100::1", 48);
  auto prefix2 = makeSidPrefix("fc00:200::1", 64);
  mySidMap->addNode(makeMySid(prefix1), scope());
  mySidMap->addNode(makeMySid(prefix2), scope());

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, nullptr, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto mySidTableCopy = rib->getMySidTableCopy();
  EXPECT_EQ(mySidTableCopy.size(), 2);
  EXPECT_NE(mySidTableCopy.find(prefix1), mySidTableCopy.end());
  EXPECT_NE(mySidTableCopy.find(prefix2), mySidTableCopy.end());
}

TEST(FromThriftWithMySid, PreservesMySidType) {
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  auto prefix = makeSidPrefix("fc00:100::1", 48);
  mySidMap->addNode(
      makeMySid(prefix, MySidType::DECAPSULATE_AND_LOOKUP), scope());

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, nullptr, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto mySidTableCopy = rib->getMySidTableCopy();
  ASSERT_EQ(mySidTableCopy.size(), 1);
  EXPECT_EQ(*mySidTableCopy[prefix].type(), MySidType::DECAPSULATE_AND_LOOKUP);
}

// --- Tests for fromThrift initializing nextHopIDManager_ via fibsInfoMap ---

// Verifies that fromThrift with a real fibsInfoMap (containing nexthop id maps)
// and a mySidMap (with a MySid referencing a nexthop set) correctly populates
// the nextHopIDManager via reconstructFromSwitchStateMaps. This covers the
// warm-boot path where MySid entries have nexthop sets not referenced by
// routes.
TEST(FromThriftWithFibsInfoMap, NextHopIDManagerPopulatedViaMySid) {
  FLAGS_enable_nexthop_id_manager = true;
  constexpr int64_t kNhopId = 1;
  constexpr int64_t kSetId = 1LL << 62;

  // Build FibInfo with id maps containing one nexthop and one nexthop set
  NextHop nhop =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  idToNhopMap->addNextHop(kNhopId, nhop.toThrift());

  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kSetId, {kNhopId});

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(std::make_shared<ForwardingInformationBaseMap>());
  fibInfo->setIdToNextHopMap(idToNhopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNhopIdSetMap);

  auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  fibsInfoMap->addNode("id=0", fibInfo);

  // Build MySid whose resolvedNextHopsId references the nexthop set above
  auto mySid = makeMySid();
  mySid->setResolvedNextHopsId(NextHopSetID(kSetId));
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(mySid, scope());

  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, fibsInfoMap, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto idManager = rib->getNextHopIDManagerCopy();
  ASSERT_NE(idManager, nullptr);
  EXPECT_NE(
      idManager->getIdToNextHop().find(NextHopID(kNhopId)),
      idManager->getIdToNextHop().end());
  EXPECT_NE(
      idManager->getIdToNextHopIdSet().find(NextHopSetID(kSetId)),
      idManager->getIdToNextHopIdSet().end());
}

// MySid Layer-2 repoint, FRESH-id variant: the unresolved set canonicalizes to
// a NEW member set (not equal to the resolved set), so a fresh SetID is minted.
// The MySid's unresolveNextHopsId must be repointed onto that fresh id.
TEST(
    FromThriftWithFibsInfoMap,
    MySidNextHopIdsFreshSetIdAfterCanonicalization) {
  FLAGS_enable_nexthop_id_manager = true;
  constexpr int64_t kIdA1 = 1; // nhA
  constexpr int64_t kIdB = 2; // nhB
  constexpr int64_t kIdA2 = 3; // duplicate of nhA
  constexpr int64_t kIdC = 4; // nhC
  constexpr int64_t kSetId1 = 1LL << 62; // resolved   {A1, B}
  constexpr int64_t kSetId2 = (1LL << 62) + 1; // unresolved {A2, C}

  NextHop nhA =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nhB =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nhC =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  idToNhopMap->addNextHop(kIdA1, nhA.toThrift());
  idToNhopMap->addNextHop(kIdB, nhB.toThrift());
  idToNhopMap->addNextHop(kIdA2, nhA.toThrift()); // dup of nhA post-strip
  idToNhopMap->addNextHop(kIdC, nhC.toThrift());

  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kSetId1, {kIdA1, kIdB});
  idToNhopIdSetMap->addNextHopIdSet(kSetId2, {kIdA2, kIdC});

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(std::make_shared<ForwardingInformationBaseMap>());
  fibInfo->setIdToNextHopMap(idToNhopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNhopIdSetMap);
  auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  fibsInfoMap->addNode("id=0", fibInfo);

  auto mySid = makeMySid();
  mySid->setResolvedNextHopsId(NextHopSetID(kSetId1));
  mySid->setUnresolveNextHopsId(NextHopSetID(kSetId2));
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(mySid, scope());

  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, fibsInfoMap, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto idManager = rib->getNextHopIDManagerCopy();
  ASSERT_NE(idManager, nullptr);
  // Duplicate nhA id reclaimed: A,B,C survive.
  EXPECT_EQ(idManager->getIdToNextHop().size(), 3);

  auto mySidCopy = rib->getMySidTableCopy();
  ASSERT_EQ(mySidCopy.size(), 1);
  const auto& fields = mySidCopy.begin()->second;
  ASSERT_TRUE(fields.resolvedNextHopsId().has_value());
  ASSERT_TRUE(fields.unresolveNextHopsId().has_value());
  // resolved stays kSetId1; unresolved repointed onto the fresh id. Both must
  // exist in the manager.
  EXPECT_EQ(
      idManager->getIdToNextHopIdSet().count(
          NextHopSetID(*fields.resolvedNextHopsId())),
      1);
  EXPECT_EQ(
      idManager->getIdToNextHopIdSet().count(
          NextHopSetID(*fields.unresolveNextHopsId())),
      1);
  // The two SetIDs must now be distinct survivors (resolved != unresolved,
  // since their canonical member sets differ).
  EXPECT_NE(*fields.resolvedNextHopsId(), *fields.unresolveNextHopsId());
}

// Two distinct MySid entries whose resolved sets each reference a duplicate of
// the same nexthop. After canonicalization both must repoint onto a surviving
// SetID -- neither left dangling.
TEST(FromThriftWithFibsInfoMap, MultipleMySidEntriesCanonicalizeConsistently) {
  FLAGS_enable_nexthop_id_manager = true;
  constexpr int64_t kIdA1 = 1;
  constexpr int64_t kIdA2 = 2; // duplicate of nhop1 post-strip
  constexpr int64_t kSetId1 = 1LL << 62;
  constexpr int64_t kSetId2 = (1LL << 62) + 1;

  NextHop nhop =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  idToNhopMap->addNextHop(kIdA1, nhop.toThrift());
  idToNhopMap->addNextHop(kIdA2, nhop.toThrift());
  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kSetId1, {kIdA1});
  idToNhopIdSetMap->addNextHopIdSet(kSetId2, {kIdA2});

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(std::make_shared<ForwardingInformationBaseMap>());
  fibInfo->setIdToNextHopMap(idToNhopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNhopIdSetMap);
  auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  fibsInfoMap->addNode("id=0", fibInfo);

  auto mySid1 = makeMySid(makeSidPrefix("fc00:100::1", 48));
  mySid1->setResolvedNextHopsId(NextHopSetID(kSetId1));
  auto mySid2 = makeMySid(makeSidPrefix("fc00:200::1", 48));
  mySid2->setResolvedNextHopsId(NextHopSetID(kSetId2));
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(mySid1, scope());
  mySidMap->addNode(mySid2, scope());

  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, fibsInfoMap, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto idManager = rib->getNextHopIDManagerCopy();
  ASSERT_NE(idManager, nullptr);
  EXPECT_EQ(idManager->getIdToNextHop().size(), 1);

  auto mySidCopy = rib->getMySidTableCopy();
  ASSERT_EQ(mySidCopy.size(), 2);
  for (const auto& [_prefix, fields] : mySidCopy) {
    ASSERT_TRUE(fields.resolvedNextHopsId().has_value());
    EXPECT_EQ(
        idManager->getIdToNextHopIdSet().count(
            NextHopSetID(*fields.resolvedNextHopsId())),
        1)
        << "a MySid entry points at a retired SetID";
  }
}

// --- Full end-to-end canonicalization through fromThrift() ---

// The big one: a single cross-version warm boot (cost stripped) that drives
// EVERY canonicalization outcome through the real RoutingInformationBase::
// fromThrift() entry point, across EVERY reference kind. The persisted state
// has 7 NextHopIDs that deserialize to 4 distinct NextHops (ids 5/6/7 are
// post-strip duplicates of 1/2/3), and 7 persisted SetIDs:
//
//   S1 = {1,2}      healthy                        -> unchanged
//   S2 = {1,3}      healthy (collapse target)      -> unchanged
//   S3 = {5,7}      -> canon {1,3} == S2           -> COLLAPSE onto S2
//   S4 = {5,4}      -> canon {1,4}, matches none   -> FRESH mint
//   S5 = {6,7}      -> canon {2,3}, matches none   -> FRESH mint
//   S6 = {7}        -> canon {3},   matches none   -> FRESH mint
//   S7 = {1,5,2}    intra-set dup (5->1), canon {1,2} == S1 -> COLLAPSE onto S1
//
// References exercised: V4 resolved routes (S1/S3/S4), a V6 resolved route
// (S2), a V4 per-client route (S5), a named next-hop group (S6), and a MySid
// entry (resolved S2 + unresolved S7). After fromThrift every reference must
// point at a SURVIVING SetID, no set may contain a reclaimed member, and
// healthy sets must be left exactly as persisted.
TEST(FromThriftWithFibsInfoMap, FullCanonicalizationAcrossAllReferenceKinds) {
  FLAGS_enable_nexthop_id_manager = true;

  // 4 distinct nexthops; ids 5/6/7 are duplicates of 1/2/3 after cost strip.
  NextHop nhA = mkNh("10.0.0.1");
  NextHop nhB = mkNh("10.0.0.2");
  NextHop nhC = mkNh("10.0.0.3");
  NextHop nhD = mkNh("10.0.0.4");
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<int64_t, NextHop>>{
           {1, nhA},
           {2, nhB},
           {3, nhC},
           {4, nhD},
           {5, nhA},
           {6, nhB},
           {7, nhC}}) {
    idToNhopMap->addNextHop(id, nh.toThrift());
  }

  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kOff + 1, {1, 2}); // S1 healthy
  idToNhopIdSetMap->addNextHopIdSet(kOff + 2, {1, 3}); // S2 healthy / survivor
  idToNhopIdSetMap->addNextHopIdSet(kOff + 3, {5, 7}); // S3 -> S2 collapse
  idToNhopIdSetMap->addNextHopIdSet(kOff + 4, {5, 4}); // S4 fresh
  idToNhopIdSetMap->addNextHopIdSet(kOff + 5, {6, 7}); // S5 fresh (per-client)
  idToNhopIdSetMap->addNextHopIdSet(kOff + 6, {7}); // S6 fresh (named group)
  idToNhopIdSetMap->addNextHopIdSet(kOff + 7, {1, 5, 2}); // S7 intra-dup -> S1

  auto fibsMap = makeFibsMap();
  auto fibV4 = fibsMap->getNodeIf(RouterID(0))->getFibV4();
  auto fibV6 = fibsMap->getNodeIf(RouterID(0))->getFibV6();
  addResolvedRoute(
      fibV4, makePrefixV4("10.1.0.0/24"), NextHopSetID(kOff + 1)); // healthy
  // 10.3: resolved S3 (collapses onto S2 {A,C}); normalized S4 (fresh {A,D}) so
  // the resolved and normalized refs repoint to DIFFERENT survivors.
  addResolvedRoute(
      fibV4,
      makePrefixV4("10.3.0.0/24"),
      NextHopSetID(kOff + 3),
      NextHopSetID(kOff + 4));
  addResolvedRoute(
      fibV4, makePrefixV4("10.4.0.0/24"), NextHopSetID(kOff + 4)); // fresh
  addV4ClientRoute(fibV4, "10.5.0.0/24", NextHopSetID(kOff + 5)); // per-client
  addResolvedRoute(
      fibV6, makePrefixV6("2001:db8::/32"), NextHopSetID(kOff + 2)); // healthy

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(fibsMap);
  fibInfo->setIdToNextHopMap(idToNhopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNhopIdSetMap);
  fibInfo->setNextHopSetIdForName("ng", kOff + 6); // named group -> S6 (fresh)
  auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  fibsInfoMap->addNode("id=0", fibInfo);

  // MySid: resolved on a healthy set, unresolved on the intra-dup set.
  auto mySid = makeMySid();
  mySid->setResolvedNextHopsId(NextHopSetID(kOff + 2)); // S2 healthy
  mySid->setUnresolveNextHopsId(NextHopSetID(kOff + 7)); // S7 -> S1
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(mySid, scope());

  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});

  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, fibsInfoMap, nullptr, mySidMap, kEcmpWidth, nullptr);

  auto idm = rib->getNextHopIDManagerCopy();
  ASSERT_NE(idm, nullptr);

  auto idToSet = idm->getIdToNextHopIdSet();
  auto idToNhop = idm->getIdToNextHop();

  // 7 persisted ids reclaim down to 4 canonical nexthops.
  EXPECT_EQ(idToNhop.size(), 4);
  // 7 persisted SetIDs collapse to 5 distinct canonical member sets:
  //   {A,B}(S1==S7) {A,C}(S2==S3) {A,D}(S4) {B,C}(S5) {C}(S6).
  EXPECT_EQ(idToSet.size(), 5);

  // Global invariant: no surviving set references a reclaimed member.
  for (const auto& [sid, members] : idToSet) {
    for (const auto& m : members) {
      EXPECT_EQ(idToNhop.count(m), 1)
          << "set " << sid << " has dangling member " << m;
    }
  }

  // The canonical id chosen for a nexthop is order-dependent (first persisted
  // id seen wins), so we assert on resolved NEXTHOP membership, not raw
  // ids/SetIDs. resolve() maps a surviving SetID to its member NextHops via the
  // manager.
  auto resolve = [&](const NextHopSetID& sid) {
    auto it = idToSet.find(sid);
    EXPECT_NE(it, idToSet.end()) << "SetID " << sid << " not in manager";
    std::vector<NextHop> nhops;
    if (it != idToSet.end()) {
      for (const auto& id : it->second) {
        auto nhIt = idToNhop.find(id);
        if (nhIt != idToNhop.end()) {
          nhops.push_back(nhIt->second.nextHop);
        }
      }
    }
    // Bulk-construct the flat_set from a range rather than per-element insert
    // (facebook-expensive-flat-container-operation).
    return RouteNextHopSet(nhops.begin(), nhops.end());
  };
  const RouteNextHopSet AB{nhA, nhB};
  const RouteNextHopSet AC{nhA, nhC};
  const RouteNextHopSet AD{nhA, nhD};
  const RouteNextHopSet BC{nhB, nhC};
  const RouteNextHopSet C{nhC};

  auto resolvedSetIdOf = [](const auto& route) {
    return NextHopSetID(*route->getForwardInfo().getResolvedNextHopSetID());
  };
  auto normalizedSetIdOf = [](const auto& route) {
    return NextHopSetID(
        *route->getForwardInfo().getNormalizedResolvedNextHopSetID());
  };

  // V4/V6 resolved routes: each must now resolve to its canonical member set.
  auto r1 = rib->longestMatch(folly::IPAddressV4("10.1.0.1"), RouterID(0));
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(resolve(resolvedSetIdOf(r1)), AB); // S1 healthy

  auto r3 = rib->longestMatch(folly::IPAddressV4("10.3.0.1"), RouterID(0));
  ASSERT_NE(r3, nullptr);
  EXPECT_EQ(resolve(resolvedSetIdOf(r3)), AC); // S3 {5,7} -> {A,C}
  // Normalized ref repointed independently of resolved: S4 {5,4} -> {A,D}.
  EXPECT_EQ(resolve(normalizedSetIdOf(r3)), AD);

  auto r4 = rib->longestMatch(folly::IPAddressV4("10.4.0.1"), RouterID(0));
  ASSERT_NE(r4, nullptr);
  EXPECT_EQ(resolve(resolvedSetIdOf(r4)), AD); // S4 fresh {A,D}

  auto r2 = rib->longestMatch(folly::IPAddressV6("2001:db8::1"), RouterID(0));
  ASSERT_NE(r2, nullptr);
  EXPECT_EQ(resolve(resolvedSetIdOf(r2)), AC); // S2 healthy {A,C}

  // Per-client route: clientNextHopSetID repointed, resolves to {B,C}.
  auto r5 = rib->longestMatch(folly::IPAddressV4("10.5.0.1"), RouterID(0));
  ASSERT_NE(r5, nullptr);
  bool sawClientSetId = false;
  for (const auto& entry : r5->getEntryForClients()) {
    if (auto clientSetId = entry.second->getClientNextHopSetID()) {
      sawClientSetId = true;
      EXPECT_EQ(resolve(NextHopSetID(*clientSetId)), BC); // S5 {6,7} -> {B,C}
    }
  }
  EXPECT_TRUE(sawClientSetId) << "per-client clientNextHopSetID was lost";

  // Named group resolves to its canonical single-member set {C}.
  const auto& nameToSetId = idm->getNameToNextHopSetID();
  ASSERT_EQ(nameToSetId.count("ng"), 1);
  EXPECT_EQ(resolve(nameToSetId.at("ng")), C); // S6 {7} -> {C}

  // MySid entry repointed: resolved -> {A,C}, unresolved (intra-dup S7) ->
  // {A,B}.
  auto mySidCopy = rib->getMySidTableCopy();
  ASSERT_EQ(mySidCopy.size(), 1);
  const auto& fields = mySidCopy.begin()->second;
  ASSERT_TRUE(fields.resolvedNextHopsId().has_value());
  ASSERT_TRUE(fields.unresolveNextHopsId().has_value());
  EXPECT_EQ(resolve(NextHopSetID(*fields.resolvedNextHopsId())), AC);
  EXPECT_EQ(resolve(NextHopSetID(*fields.unresolveNextHopsId())), AB);
}

// --- Applying the RIB's warm boot NextHop ids to the switch state ---

namespace {

const SwitchIdScopeResolver* nextHopSyncResolver() {
  static const SwitchIdScopeResolver kResolver([] {
    cfg::SwitchInfo info;
    info.switchType() = cfg::SwitchType::NPU;
    info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
    info.switchIndex() = 0;
    return std::map<int64_t, cfg::SwitchInfo>{{0, info}};
  }());
  return &kResolver;
}

struct WarmbootRibAndState {
  std::unique_ptr<RoutingInformationBase> rib;
  std::shared_ptr<SwitchState> state;
};

// Warm boot: the RIB is rebuilt from the persisted state.
WarmbootRibAndState warmboot(
    const std::shared_ptr<ForwardingInformationBaseMap>& fibsMap,
    const std::shared_ptr<IdToNextHopMap>& idToNhopMap,
    const std::shared_ptr<IdToNextHopIdSetMap>& idToNhopIdSetMap,
    const std::shared_ptr<MultiSwitchMySidMap>& mySidMap = nullptr) {
  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(fibsMap);
  fibInfo->setIdToNextHopMap(idToNhopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNhopIdSetMap);
  auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  fibsInfoMap->addNode("id=0", fibInfo);
  auto state = std::make_shared<SwitchState>();
  state->resetFibsInfoMap(fibsInfoMap);
  if (mySidMap) {
    state->resetMySids(mySidMap);
  }
  state->publish();
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, state::RouteTableFields{});
  auto rib = RoutingInformationBase::fromThrift(
      ribThrift, fibsInfoMap, nullptr, mySidMap, kEcmpWidth, nullptr);
  return {std::move(rib), state};
}

// Resolved route over nexthops A and B whose NextHopSetIDs all point at setId.
template <typename AddrT, typename FibT>
void addRouteOnSet(
    const std::shared_ptr<FibT>& fib,
    const RoutePrefix<AddrT>& prefix,
    NextHopSetID setId) {
  RouteNextHopSet nhops{mkNh("10.0.0.1"), mkNh("10.0.0.2")};
  auto route = std::make_shared<Route<AddrT>>(Route<AddrT>::makeThrift(prefix));
  RouteNextHopEntry clientEntry(nhops, AdminDistance::EBGP);
  std::optional<NextHopSetID> clientSetId(setId);
  clientEntry.setClientNextHopSetID(clientSetId);
  route->update(ClientID::BGPD, clientEntry);
  auto fwd = RouteNextHopEntry(nhops, AdminDistance::EBGP).toThrift();
  fwd.resolvedNextHopSetID() = static_cast<int64_t>(setId);
  fwd.normalizedResolvedNextHopSetID() = static_cast<int64_t>(setId);
  route->setResolved(RouteNextHopEntry(std::move(fwd)));
  route->publish();
  fib->addNode(route);
}

std::shared_ptr<SwitchState> applyRib(
    RoutingInformationBase& rib,
    const std::shared_ptr<SwitchState>& state) {
  auto newState = state;
  rib.syncStateFromRib(
      nextHopSyncResolver(), &ribToSwitchStateUpdate, &newState);
  return newState;
}

std::shared_ptr<ForwardingInformationBaseContainer> fibContainer(
    const std::shared_ptr<SwitchState>& state) {
  return state->getFibsInfoMap()->cbegin()->second->getFibContainerIf(
      RouterID(0));
}

template <typename Fn>
void forEachRoute(const std::shared_ptr<SwitchState>& state, Fn fn) {
  auto container = fibContainer(state);
  for (const auto& [_, route] : std::as_const(*container->getFibV4())) {
    fn(route);
  }
  for (const auto& [_, route] : std::as_const(*container->getFibV6())) {
    fn(route);
  }
}

// The state's NextHop id maps equal the RIB's, and every route and MySid in the
// state carries the RIB's NextHopSetIDs.
void expectStateMatchesRib(
    const RoutingInformationBase& rib,
    const std::shared_ptr<SwitchState>& state) {
  auto manager = rib.getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);
  auto synced = SwitchStateNextHopIdUpdater(manager.get())(state);
  const auto& fibInfo = state->getFibsInfoMap()->cbegin()->second;
  const auto& syncedFibInfo = synced->getFibsInfoMap()->cbegin()->second;
  EXPECT_EQ(
      fibInfo->getIdToNextHopMap()->toThrift(),
      syncedFibInfo->getIdToNextHopMap()->toThrift());
  EXPECT_EQ(
      fibInfo->getIdToNextHopIdSetMap()->toThrift(),
      syncedFibInfo->getIdToNextHopIdSetMap()->toThrift());
  forEachRoute(state, [&](const auto& route) {
    const auto& fwd = route->getForwardInfo();
    const auto& ribFwd =
        rib.longestMatch(route->prefix().network(), RouterID(0))
            ->getForwardInfo();
    EXPECT_EQ(fwd.getResolvedNextHopSetID(), ribFwd.getResolvedNextHopSetID())
        << route->str();
    EXPECT_EQ(
        fwd.getNormalizedResolvedNextHopSetID(),
        ribFwd.getNormalizedResolvedNextHopSetID())
        << route->str();
  });
  auto ribMySids = rib.getMySidTableCopy();
  EXPECT_EQ(state->getMySids()->numNodes(), ribMySids.size());
  for (const auto& [_, mySids] : std::as_const(*state->getMySids())) {
    for (const auto& [__, mySid] : std::as_const(*mySids)) {
      auto cidr = mySid->getMySid();
      auto itr =
          ribMySids.find(folly::CIDRNetworkV6(cidr.first.asV6(), cidr.second));
      ASSERT_NE(itr, ribMySids.end());
      EXPECT_EQ(
          mySid->getResolvedNextHopsId(),
          itr->second.resolvedNextHopsId().to_optional());
    }
  }
}

// Every route forwards exactly as before; only NextHop ids may differ.
void expectForwardingUnchanged(
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState) {
  auto newContainer = fibContainer(newState);
  EXPECT_EQ(
      newContainer->getFibV4()->size() + newContainer->getFibV6()->size(),
      fibContainer(oldState)->getFibV4()->size() +
          fibContainer(oldState)->getFibV6()->size());
  forEachRoute(oldState, [&](const auto& oldRoute) {
    using RouteT = std::decay_t<decltype(*oldRoute)>;
    std::shared_ptr<RouteT> newRoute;
    if constexpr (std::is_same_v<RouteT, RouteV4>) {
      newRoute = newContainer->getFibV4()->getRouteIf(oldRoute->prefix());
    } else {
      newRoute = newContainer->getFibV6()->getRouteIf(oldRoute->prefix());
    }
    ASSERT_NE(newRoute, nullptr) << oldRoute->str();
    const auto& oldFwd = oldRoute->getForwardInfo();
    const auto& newFwd = newRoute->getForwardInfo();
    EXPECT_EQ(newFwd.getAction(), oldFwd.getAction()) << oldRoute->str();
    EXPECT_EQ(newFwd.getAdminDistance(), oldFwd.getAdminDistance())
        << oldRoute->str();
    EXPECT_EQ(newFwd.getNextHopSet(), oldFwd.getNextHopSet())
        << oldRoute->str();
  });
}

} // namespace

// Same-version warm boot: the RIB changes no NextHop id, so applying it leaves
// the state untouched.
TEST(WarmbootNextHopIdSync, CleanStateUnchanged) {
  FLAGS_enable_nexthop_id_manager = true;
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  idToNhopMap->addNextHop(1, mkNh("10.0.0.1").toThrift());
  idToNhopMap->addNextHop(2, mkNh("10.0.0.2").toThrift());
  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kOff + 1, {1, 2});
  auto fibsMap = makeFibsMap();
  addRouteOnSet(
      fibsMap->getNodeIf(RouterID(0))->getFibV4(),
      makePrefixV4("10.1.0.0/24"),
      NextHopSetID(kOff + 1));

  auto [rib, state] = warmboot(fibsMap, idToNhopMap, idToNhopIdSetMap);
  EXPECT_FALSE(rib->nextHopIdsChangedAtWarmboot());
  EXPECT_EQ(applyRib(*rib, state), state);
}

// Two persisted NextHop ids read as one NextHop: the state keeps one of them,
// and every route and MySid on either set moves onto one surviving set.
TEST(WarmbootNextHopIdSync, CollapsedNextHopsSyncedIntoState) {
  FLAGS_enable_nexthop_id_manager = true;
  auto idToNhopMap = std::make_shared<IdToNextHopMap>();
  idToNhopMap->addNextHop(1, mkNh("10.0.0.1").toThrift());
  idToNhopMap->addNextHop(2, mkNh("10.0.0.2").toThrift());
  idToNhopMap->addNextHop(3, mkNh("10.0.0.1").toThrift());
  auto idToNhopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNhopIdSetMap->addNextHopIdSet(kOff + 1, {1, 2});
  idToNhopIdSetMap->addNextHopIdSet(kOff + 2, {3, 2});
  auto fibsMap = makeFibsMap();
  auto container = fibsMap->getNodeIf(RouterID(0));
  addRouteOnSet(
      container->getFibV4(),
      makePrefixV4("10.1.0.0/24"),
      NextHopSetID(kOff + 1));
  addRouteOnSet(
      container->getFibV4(),
      makePrefixV4("10.2.0.0/24"),
      NextHopSetID(kOff + 2));
  addRouteOnSet(
      container->getFibV6(),
      makePrefixV6("2001:db8::/32"),
      NextHopSetID(kOff + 2));
  auto mySid = makeMySid();
  mySid->setResolvedNextHopsId(NextHopSetID(kOff + 2));
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(mySid, scope());

  auto [rib, state] =
      warmboot(fibsMap, idToNhopMap, idToNhopIdSetMap, mySidMap);
  ASSERT_TRUE(rib->nextHopIdsChangedAtWarmboot());
  auto newState = applyRib(*rib, state);
  expectStateMatchesRib(*rib, newState);
  expectForwardingUnchanged(state, newState);

  // Which persisted id survives depends on reconstruct order; exactly one does.
  auto idToNhop = newState->getFibsInfoMap()
                      ->cbegin()
                      ->second->getIdToNextHopMap()
                      ->toThrift();
  EXPECT_NE(idToNhop.contains(1), idToNhop.contains(3));
  auto newContainer = fibContainer(newState);
  auto setId = newContainer->getFibV4()
                   ->getRouteIf(makePrefixV4("10.1.0.0/24"))
                   ->getForwardInfo()
                   .getResolvedNextHopSetID();
  ASSERT_TRUE(setId.has_value());
  EXPECT_EQ(
      newContainer->getFibV4()
          ->getRouteIf(makePrefixV4("10.2.0.0/24"))
          ->getForwardInfo()
          .getResolvedNextHopSetID(),
      setId);
  EXPECT_EQ(
      newContainer->getFibV6()
          ->getRouteIf(makePrefixV6("2001:db8::/32"))
          ->getForwardInfo()
          .getResolvedNextHopSetID(),
      setId);
  auto newMySid = newState->getMySids()->getNodeIf(mySid->getID());
  ASSERT_NE(newMySid, nullptr);
  EXPECT_EQ(newMySid->getResolvedNextHopsId(), setId);
}

// State written with the NextHop id manager off: the state gets the NextHop
// ids the RIB backfilled.
TEST(WarmbootNextHopIdSync, BackfilledNextHopIdsSyncedIntoState) {
  gflags::FlagSaver saver;
  FLAGS_enable_nexthop_id_manager = true;
  FLAGS_resolve_nexthops_from_id = false;
  RouteNextHopSet nhops{mkNh("10.0.0.1"), mkNh("10.0.0.2")};
  auto fibsMap = makeFibsMap();
  auto route = std::make_shared<RouteV4>(
      RouteV4::makeThrift(makePrefixV4("10.1.0.0/24")));
  route->update(ClientID::BGPD, RouteNextHopEntry(nhops, AdminDistance::EBGP));
  route->setResolved(RouteNextHopEntry(nhops, AdminDistance::EBGP));
  route->publish();
  fibsMap->getNodeIf(RouterID(0))->getFibV4()->addNode(route);

  auto [rib, state] = warmboot(
      fibsMap,
      std::make_shared<IdToNextHopMap>(),
      std::make_shared<IdToNextHopIdSetMap>());
  ASSERT_TRUE(rib->nextHopIdsChangedAtWarmboot());
  auto newState = applyRib(*rib, state);
  expectStateMatchesRib(*rib, newState);
  expectForwardingUnchanged(state, newState);
  const auto& fibInfo = newState->getFibsInfoMap()->cbegin()->second;
  EXPECT_EQ(fibInfo->getIdToNextHopMap()->toThrift().size(), 2);
  auto syncedRoute = fibContainer(newState)->getFibV4()->getRouteIf(
      makePrefixV4("10.1.0.0/24"));
  ASSERT_NE(syncedRoute, nullptr);
  EXPECT_TRUE(
      syncedRoute->getForwardInfo().getResolvedNextHopSetID().has_value());
}
