/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <chrono>

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/ThriftHandler.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

class NamedNextHopGroupRibTest : public ::testing::Test {
 public:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    auto config = initialConfig();
    handle_ = createTestHandle(&config);
    sw_ = handle_->getSw();
  }

  cfg::SwitchConfig initialConfig() const {
    cfg::SwitchConfig config;
    config.switchSettings()->switchIdToSwitchInfo() = {
        {0, createSwitchInfo(cfg::SwitchType::NPU)}};
    config.vlans()->resize(2);
    config.vlans()[0].id() = 1;
    config.vlans()[1].id() = 2;

    config.interfaces()->resize(2);
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
    return config;
  }

 protected:
  RouteNextHopSet makeResolvedNextHops(
      const std::vector<std::string>& nexthopStrs) {
    RouteNextHopSet nhops;
    for (size_t i = 0; i < nexthopStrs.size(); ++i) {
      nhops.emplace(UnresolvedNextHop(folly::IPAddress(nexthopStrs[i]), 1));
    }
    return nhops;
  }

  void addOrUpdateGroups(
      const std::vector<std::pair<std::string, RouteNextHopSet>>& groups) {
    auto rib = sw_->getRib();
    rib->addOrUpdateNamedNextHopGroups(
        sw_->getScopeResolver(), groups, createRibToSwitchStateFunction(), sw_);
  }

  SwSwitch* sw_ = nullptr;
  std::unique_ptr<HwTestHandle> handle_;
};

TEST_F(NamedNextHopGroupRibTest, AddNamedNextHopGroups) {
  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("group1", makeResolvedNextHops({"1.1.1.10", "2.2.2.10"}));
  groups.emplace_back("group2", makeResolvedNextHops({"1.1.1.20"}));

  addOrUpdateGroups(groups);

  auto managerCopy = rib->getNextHopIDManagerCopy();
  ASSERT_NE(managerCopy, nullptr);
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("group1"));
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("group2"));
  auto setId1 = managerCopy->getNextHopSetIDForName("group1");
  auto setId2 = managerCopy->getNextHopSetIDForName("group2");
  EXPECT_TRUE(setId1.has_value());
  EXPECT_TRUE(setId2.has_value());
  EXPECT_NE(*setId1, *setId2);
}

TEST_F(NamedNextHopGroupRibTest, UpdateNamedNextHopGroup) {
  auto rib = sw_->getRib();

  // First add a group
  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("group1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  // Update with new nexthops (allocateNamedNextHopGroup handles both
  // add/update)
  std::vector<std::pair<std::string, RouteNextHopSet>> updatedGroups;
  updatedGroups.emplace_back(
      "group1", makeResolvedNextHops({"1.1.1.10", "2.2.2.10"}));

  addOrUpdateGroups(updatedGroups);

  auto managerCopy = rib->getNextHopIDManagerCopy();
  ASSERT_NE(managerCopy, nullptr);
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("group1"));
  auto nhops = managerCopy->getNextHopsForName("group1");
  ASSERT_TRUE(nhops.has_value());
  EXPECT_EQ(nhops->size(), 2);
}

TEST_F(NamedNextHopGroupRibTest, DeleteNamedNextHopGroups) {
  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  // Add groups first
  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("group1", makeResolvedNextHops({"1.1.1.10"}));
  groups.emplace_back("group2", makeResolvedNextHops({"2.2.2.10"}));
  addOrUpdateGroups(groups);

  // Delete one group
  bool deleteCalled = false;
  rib->deleteNamedNextHopGroups(
      {"group1"}, [&](const NextHopIDManager* manager) {
        deleteCalled = true;
        ASSERT_NE(manager, nullptr);
        EXPECT_FALSE(manager->hasNamedNextHopGroup("group1"));
        EXPECT_TRUE(manager->hasNamedNextHopGroup("group2"));
      });

  EXPECT_TRUE(deleteCalled);

  // Verify through copy
  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_FALSE(managerCopy->hasNamedNextHopGroup("group1"));
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("group2"));
}

TEST_F(NamedNextHopGroupRibTest, DeleteNonExistentGroupIsNoOp) {
  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  // Delete a group that doesn't exist — should not throw
  bool stateUpdateCalled = false;
  rib->deleteNamedNextHopGroups(
      {"nonexistent"}, [&](const NextHopIDManager* manager) {
        stateUpdateCalled = true;
        EXPECT_NE(manager, nullptr);
      });

  EXPECT_TRUE(stateUpdateCalled);
}

TEST_F(NamedNextHopGroupRibTest, AddAndDeleteMultipleGroups) {
  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  // Add multiple groups
  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("g1", makeResolvedNextHops({"1.1.1.10"}));
  groups.emplace_back("g2", makeResolvedNextHops({"2.2.2.10"}));
  groups.emplace_back("g3", makeResolvedNextHops({"1.1.1.10", "2.2.2.10"}));
  addOrUpdateGroups(groups);

  // Delete all at once
  bool deleteCalled = false;
  rib->deleteNamedNextHopGroups(
      {"g1", "g2", "g3"}, [&](const NextHopIDManager* manager) {
        deleteCalled = true;
        EXPECT_FALSE(manager->hasNamedNextHopGroup("g1"));
        EXPECT_FALSE(manager->hasNamedNextHopGroup("g2"));
        EXPECT_FALSE(manager->hasNamedNextHopGroup("g3"));
      });

  EXPECT_TRUE(deleteCalled);
}

TEST_F(NamedNextHopGroupRibTest, invalidEntryInBatchDoesNotPartiallyMutate) {
  // Verify that if any entry in the batch is invalid, pre-validation throws
  // before any state is mutated. This matches the MySid batch validation
  // pattern (invalidEntryInBatchDoesNotPartiallyMutateMySidTable).
  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);

  // Seed the table with a known-good entry
  std::vector<std::pair<std::string, RouteNextHopSet>> seed;
  seed.emplace_back("existing", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(seed);

  // Batch: valid group first, then invalid group with empty nexthop set.
  // Without pre-validation, the valid group would be allocated before the
  // invalid group throws, leaving partial state.
  std::vector<std::pair<std::string, RouteNextHopSet>> batch;
  batch.emplace_back("validNew", makeResolvedNextHops({"2.2.2.10"}));
  batch.emplace_back("badGroup", RouteNextHopSet{}); // invalid: empty

  EXPECT_THROW(addOrUpdateGroups(batch), FbossError);

  // Only the seed entry should exist — the valid entry from the failed
  // batch must not have been inserted (no partial mutation)
  auto manager = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(manager->hasNamedNextHopGroup("existing"));
  EXPECT_FALSE(manager->hasNamedNextHopGroup("validNew"));
  EXPECT_FALSE(manager->hasNamedNextHopGroup("badGroup"));
}

TEST_F(NamedNextHopGroupRibTest, emptyNameInBatchDoesNotPartiallyMutate) {
  auto rib = sw_->getRib();

  // Seed the table with a known-good entry
  std::vector<std::pair<std::string, RouteNextHopSet>> seed;
  seed.emplace_back("existing", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(seed);

  // Batch: valid group first, then invalid group with empty name
  std::vector<std::pair<std::string, RouteNextHopSet>> batch;
  batch.emplace_back("validNew", makeResolvedNextHops({"2.2.2.10"}));
  batch.emplace_back(
      "", makeResolvedNextHops({"1.1.1.20"})); // invalid: empty name

  EXPECT_THROW(addOrUpdateGroups(batch), FbossError);

  // Only the seed entry should exist — no partial mutation
  auto manager = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(manager->hasNamedNextHopGroup("existing"));
  EXPECT_FALSE(manager->hasNamedNextHopGroup("validNew"));
}

TEST_F(NamedNextHopGroupRibTest, AddRouteWithNamedNhg) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10", "2.2.2.10"}));
  addOrUpdateGroups(groups);

  UnicastRoute route;
  route.dest()->ip() =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
  route.dest()->prefixLength() = 24;
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = "nhg1";
  route.namedRouteDestination() = namedDest;

  auto updater = sw_->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  updater.program();

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  auto expectedPrefix = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);
  EXPECT_EQ(
      managerCopy->getRoutesForNamedNhg("nhg1").count(
          {RouterID(0), expectedPrefix}),
      1);
}

TEST_F(
    NamedNextHopGroupRibTest,
    MixedNextHopsAfterNamedGroupRecursiveResolutionRollsBack) {
  gflags::FlagSaver flagSaver;
  FLAGS_srv6 = true;

  sw_->initialConfigApplied(std::chrono::steady_clock::now());
  waitForStateUpdates(sw_);

  constexpr auto kNhgName = "srv6_fboss11_1";
  const auto rid = RouterID(0);
  const auto srv6BranchPrefix =
      folly::CIDRNetwork(folly::IPAddress("100::"), 64);
  const auto plainBranchPrefix =
      folly::CIDRNetwork(folly::IPAddress("200::"), 64);
  const auto failedPrefix = folly::CIDRNetwork(folly::IPAddress("300::"), 64);
  const std::vector<folly::IPAddressV6> sidListA{
      folly::IPAddressV6("3001:db8:1::")};
  const std::vector<folly::IPAddressV6> sidListB{
      folly::IPAddressV6("3001:db8:2::")};

  // Model the CSCO experiment: the named group behind one BGP next-hop branch
  // is homogeneous SRv6; the sibling branch resolves to an ordinary IP next
  // hop. Only the recursively resolved BGP route is mixed.
  RouteNextHopSet srv6NextHops{
      UnresolvedNextHop(
          folly::IPAddress("1::10"),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidListA,
          TunnelType::SRV6_ENCAP,
          "srv6Tunnel0"),
      UnresolvedNextHop(
          folly::IPAddress("2::10"),
          ECMP_WEIGHT,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          sidListB,
          TunnelType::SRV6_ENCAP,
          "srv6Tunnel0")};
  ASSERT_FALSE(hasSrv6AndNonSrv6NextHops(srv6NextHops));

  ThriftHandler handler(sw_);
  NextHopGroup srv6Group;
  srv6Group.name() = kNhgName;
  srv6Group.nexthops() = util::fromRouteNextHopSet(srv6NextHops);
  auto groups = std::make_unique<std::vector<NextHopGroup>>();
  groups->push_back(std::move(srv6Group));
  handler.addNamedNextHopGroups(std::move(groups));

  const auto addRouteViaThrift = [&handler](
                                     ClientID client, UnicastRoute route) {
    auto routes = std::make_unique<std::vector<UnicastRoute>>();
    routes->push_back(std::move(route));
    handler.addUnicastRoutes(static_cast<int16_t>(client), std::move(routes));
  };

  // Install the two routes that resolve the BGP next-hop addresses. The TE
  // branch points at the SRv6-only named group; the other branch is plain IP.
  UnicastRoute srv6BranchRoute;
  srv6BranchRoute.dest()->ip() =
      facebook::network::toBinaryAddress(srv6BranchPrefix.first);
  srv6BranchRoute.dest()->prefixLength() = srv6BranchPrefix.second;
  srv6BranchRoute.adminDistance() = AdminDistance::TE_AGENT;
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = kNhgName;
  srv6BranchRoute.namedRouteDestination() = namedDest;
  addRouteViaThrift(ClientID::TE_AGENT, std::move(srv6BranchRoute));

  UnicastRoute plainBranchRoute;
  plainBranchRoute.dest()->ip() =
      facebook::network::toBinaryAddress(plainBranchPrefix.first);
  plainBranchRoute.dest()->prefixLength() = plainBranchPrefix.second;
  plainBranchRoute.adminDistance() = AdminDistance::OPENR;
  plainBranchRoute.nextHops() = util::fromRouteNextHopSet(
      RouteNextHopSet{
          UnresolvedNextHop(folly::IPAddress("2::20"), ECMP_WEIGHT)});
  addRouteViaThrift(ClientID::OPENR, std::move(plainBranchRoute));

  auto rib = sw_->getRib();
  ASSERT_NE(rib, nullptr);
  const auto stateBeforeUpdate = sw_->getState();
  const auto ribRoutesBeforeUpdate = rib->getRouteTableDetails(rid);
  const auto managerBeforeUpdate = rib->getNextHopIDManagerCopy();
  ASSERT_NE(managerBeforeUpdate, nullptr);
  ASSERT_TRUE(managerBeforeUpdate->hasNamedNextHopGroup(kNhgName));
  ASSERT_TRUE(managerBeforeUpdate->hasRoutesForNamedNhg(kNhgName));
  const auto namedGroupRoutesBeforeUpdate =
      managerBeforeUpdate->getRoutesForNamedNhg(kNhgName);
  ASSERT_EQ(namedGroupRoutesBeforeUpdate.count({rid, srv6BranchPrefix}), 1);
  const auto namedSetIdBeforeUpdate =
      managerBeforeUpdate->getNextHopSetIDForName(kNhgName);
  ASSERT_TRUE(namedSetIdBeforeUpdate.has_value());
  const auto namedNextHopsBeforeUpdate =
      managerBeforeUpdate->getNextHopsForName(kNhgName);
  const auto nextHopSetsBeforeUpdate =
      managerBeforeUpdate->getIdToNextHopIdSet();

  RouteNextHopSet bgpNextHops{
      UnresolvedNextHop(folly::IPAddress("100::10"), ECMP_WEIGHT),
      UnresolvedNextHop(folly::IPAddress("200::10"), ECMP_WEIGHT)};
  ASSERT_FALSE(hasSrv6AndNonSrv6NextHops(bgpNextHops));

  UnicastRoute bgpRoute;
  bgpRoute.dest()->ip() =
      facebook::network::toBinaryAddress(failedPrefix.first);
  bgpRoute.dest()->prefixLength() = failedPrefix.second;
  bgpRoute.adminDistance() = AdminDistance::EBGP;
  bgpRoute.nextHops() = util::fromRouteNextHopSet(bgpNextHops);
  EXPECT_THROW(
      addRouteViaThrift(ClientID::BGPD, std::move(bgpRoute)),
      FbossFibUpdateError);

  // The failed update must leave both the applied switch state and RIB at
  // their pre-update snapshots.
  EXPECT_EQ(stateBeforeUpdate, sw_->getState());
  EXPECT_EQ(ribRoutesBeforeUpdate, rib->getRouteTableDetails(rid));
  EXPECT_EQ(
      findRoute<folly::IPAddressV6>(rid, failedPrefix, sw_->getState()),
      nullptr);

  // updateFib() must also reconstruct NextHopIDManager. The named group is
  // retained, but the failed route reference and its resolved allocations are
  // absent.
  const auto managerAfterRollback = rib->getNextHopIDManagerCopy();
  ASSERT_NE(managerAfterRollback, nullptr);
  EXPECT_TRUE(managerAfterRollback->hasNamedNextHopGroup(kNhgName));
  EXPECT_EQ(
      managerAfterRollback->getNextHopSetIDForName(kNhgName),
      namedSetIdBeforeUpdate);
  EXPECT_EQ(
      managerAfterRollback->getNextHopsForName(kNhgName),
      namedNextHopsBeforeUpdate);
  EXPECT_EQ(
      managerAfterRollback->getRoutesForNamedNhg(kNhgName),
      namedGroupRoutesBeforeUpdate);
  EXPECT_EQ(
      managerAfterRollback->getIdToNextHopIdSet(), nextHopSetsBeforeUpdate);
  EXPECT_EQ(
      managerAfterRollback->getIdToNextHop().size(),
      managerBeforeUpdate->getIdToNextHop().size());
  for (const auto& [id, entryBefore] : managerBeforeUpdate->getIdToNextHop()) {
    const auto& idToNextHopAfter = managerAfterRollback->getIdToNextHop();
    auto entryAfter = idToNextHopAfter.find(id);
    ASSERT_NE(entryAfter, idToNextHopAfter.end());
    EXPECT_EQ(entryAfter->second.nextHop, entryBefore.nextHop);
    EXPECT_EQ(entryAfter->second.refCount, entryBefore.refCount);
  }
}

TEST_F(NamedNextHopGroupRibTest, AddRouteWithNonExistentNhgFails) {
  UnicastRoute route;
  route.dest()->ip() =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
  route.dest()->prefixLength() = 24;
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = "nonexistent";
  route.namedRouteDestination() = namedDest;

  auto updater = sw_->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  EXPECT_THROW(updater.program(), FbossError);
}

TEST_F(NamedNextHopGroupRibTest, AddRouteWithBothNextHopsAndNamedNhgFails) {
  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  UnicastRoute route;
  route.dest()->ip() =
      facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
  route.dest()->prefixLength() = 24;

  NextHopThrift nh;
  nh.address() =
      facebook::network::toBinaryAddress(folly::IPAddress("1.1.1.10"));
  *nh.weight() = 1;
  route.nextHops() = {nh};

  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = "nhg1";
  route.namedRouteDestination() = namedDest;

  auto updater = sw_->getRouteUpdater();
  updater.addRoute(RouterID(0), ClientID::BGPD, route);
  EXPECT_THROW(updater.program(), FbossError);
}

TEST_F(NamedNextHopGroupRibTest, ReAddRouteWithDifferentNamedNhg) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  groups.emplace_back("nhg2", makeResolvedNextHops({"2.2.2.10"}));
  addOrUpdateGroups(groups);

  auto prefix = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);

  // Add route with nhg1
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  EXPECT_FALSE(managerCopy->hasRoutesForNamedNhg("nhg2"));

  // Re-add the same route with nhg2
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg2";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_FALSE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg2"));
  EXPECT_EQ(
      managerCopy->getRoutesForNamedNhg("nhg2").count({RouterID(0), prefix}),
      1);
}

TEST_F(NamedNextHopGroupRibTest, DeleteRouteWithNamedNhgCleansUpMapping) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  // Add route with named NHG
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));

  // Delete the route
  {
    auto updater = sw_->getRouteUpdater();
    updater.delRoute(
        RouterID(0), folly::IPAddress("10.0.0.0"), 24, ClientID::BGPD);
    updater.program();
  }

  managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_FALSE(managerCopy->hasRoutesForNamedNhg("nhg1"));
}

TEST_F(NamedNextHopGroupRibTest, SyncFibCleansUpNamedNhgMapping) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  groups.emplace_back("nhg2", makeResolvedNextHops({"2.2.2.10"}));
  addOrUpdateGroups(groups);

  // Add two routes with named NHGs
  {
    UnicastRoute route1;
    route1.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route1.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest1;
    namedDest1.nextHopGroup() = "nhg1";
    route1.namedRouteDestination() = namedDest1;

    UnicastRoute route2;
    route2.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.1.0.0"));
    route2.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest2;
    namedDest2.nextHopGroup() = "nhg2";
    route2.namedRouteDestination() = namedDest2;

    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route1);
    updater.addRoute(RouterID(0), ClientID::BGPD, route2);
    updater.program();
  }

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg2"));

  // syncFib with only route1 — route2 should be cleaned up
  {
    UnicastRoute route1;
    route1.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route1.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest1;
    namedDest1.nextHopGroup() = "nhg1";
    route1.namedRouteDestination() = namedDest1;

    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route1);
    RouteUpdateWrapper::SyncFibInfo syncInfo;
    syncInfo.ridAndClients.insert({RouterID(0), ClientID::BGPD});
    syncInfo.type = RouteUpdateWrapper::SyncFibInfo::SyncFibType::IP_ONLY;
    updater.program(syncInfo);
  }

  managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  EXPECT_FALSE(managerCopy->hasRoutesForNamedNhg("nhg2"));
}

TEST_F(NamedNextHopGroupRibTest, DeleteNhgWithRouteReferenceBlocked) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  // Add route referencing nhg1
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  // Deleting nhg1 should fail
  EXPECT_THROW(
      rib->deleteNamedNextHopGroups({"nhg1"}, [](const NextHopIDManager*) {}),
      FbossError);

  // nhg1 should still exist
  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("nhg1"));
}

TEST_F(NamedNextHopGroupRibTest, DeleteNhgAfterRouteRemovedSucceeds) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  // Add and then delete a route referencing nhg1
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }
  {
    auto updater = sw_->getRouteUpdater();
    updater.delRoute(
        RouterID(0), folly::IPAddress("10.0.0.0"), 24, ClientID::BGPD);
    updater.program();
  }

  // Now deletion should succeed
  rib->deleteNamedNextHopGroups({"nhg1"}, [](const NextHopIDManager*) {});

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_FALSE(managerCopy->hasNamedNextHopGroup("nhg1"));
}

TEST_F(NamedNextHopGroupRibTest, UpdateNhgReprogramsRoutes) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  auto prefix = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);

  // Add route referencing nhg1
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  // Update nhg1 with different nexthops
  std::vector<std::pair<std::string, RouteNextHopSet>> updatedGroups;
  updatedGroups.emplace_back("nhg1", makeResolvedNextHops({"2.2.2.10"}));
  addOrUpdateGroups(updatedGroups);

  // Verify the NHG was updated with new nexthops
  auto managerCopy = rib->getNextHopIDManagerCopy();
  auto nhops = managerCopy->getNextHopsForName("nhg1");
  ASSERT_TRUE(nhops.has_value());
  EXPECT_EQ(nhops->size(), 1);
  EXPECT_EQ(
      nhops->count(UnresolvedNextHop(folly::IPAddress("2.2.2.10"), 1)), 1);

  // Route should still reference nhg1
  EXPECT_EQ(
      managerCopy->getRoutesForNamedNhg("nhg1").count({RouterID(0), prefix}),
      1);
}

TEST_F(NamedNextHopGroupRibTest, UpdateNhgMultipleRoutesReprogrammed) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  auto prefix1 = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);
  auto prefix2 = folly::CIDRNetwork(folly::IPAddress("10.1.0.0"), 24);

  // Add two routes referencing nhg1
  {
    UnicastRoute route1;
    route1.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route1.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route1.namedRouteDestination() = namedDest;

    UnicastRoute route2;
    route2.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.1.0.0"));
    route2.dest()->prefixLength() = 24;
    route2.namedRouteDestination() = namedDest;

    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route1);
    updater.addRoute(RouterID(0), ClientID::BGPD, route2);
    updater.program();
  }

  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_EQ(managerCopy->getRoutesForNamedNhg("nhg1").size(), 2);

  // Update nhg1
  std::vector<std::pair<std::string, RouteNextHopSet>> updatedGroups;
  updatedGroups.emplace_back(
      "nhg1", makeResolvedNextHops({"2.2.2.10", "1.1.1.10"}));
  addOrUpdateGroups(updatedGroups);

  // Both routes should still reference nhg1 with correct prefixes
  managerCopy = rib->getNextHopIDManagerCopy();
  const auto& routes = managerCopy->getRoutesForNamedNhg("nhg1");
  EXPECT_EQ(routes.size(), 2);
  EXPECT_EQ(routes.count({RouterID(0), prefix1}), 1);
  EXPECT_EQ(routes.count({RouterID(0), prefix2}), 1);

  // Verify NHG nexthops were updated
  auto nhops = managerCopy->getNextHopsForName("nhg1");
  ASSERT_TRUE(nhops.has_value());
  EXPECT_EQ(nhops->size(), 2);
}

TEST_F(NamedNextHopGroupRibTest, BatchUpdateAndNewNhg) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  auto prefix = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);

  // Add route referencing nhg1
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  // Batch: update nhg1 (has route) + add nhg2 (new, no routes)
  std::vector<std::pair<std::string, RouteNextHopSet>> batchGroups;
  batchGroups.emplace_back("nhg1", makeResolvedNextHops({"2.2.2.10"}));
  batchGroups.emplace_back(
      "nhg2", makeResolvedNextHops({"1.1.1.20", "2.2.2.20"}));
  addOrUpdateGroups(batchGroups);

  auto managerCopy = rib->getNextHopIDManagerCopy();

  // nhg1 should be updated with new nexthops
  auto nhops1 = managerCopy->getNextHopsForName("nhg1");
  ASSERT_TRUE(nhops1.has_value());
  EXPECT_EQ(nhops1->size(), 1);
  EXPECT_EQ(
      nhops1->count(UnresolvedNextHop(folly::IPAddress("2.2.2.10"), 1)), 1);

  // Route should still reference nhg1
  EXPECT_EQ(
      managerCopy->getRoutesForNamedNhg("nhg1").count({RouterID(0), prefix}),
      1);

  // nhg2 should exist with its nexthops
  EXPECT_TRUE(managerCopy->hasNamedNextHopGroup("nhg2"));
  auto nhops2 = managerCopy->getNextHopsForName("nhg2");
  ASSERT_TRUE(nhops2.has_value());
  EXPECT_EQ(nhops2->size(), 2);
}

TEST_F(NamedNextHopGroupRibTest, RouteEntryReportsNamedNhg) {
  auto rib = sw_->getRib();

  std::vector<std::pair<std::string, RouteNextHopSet>> groups;
  groups.emplace_back("nhg1", makeResolvedNextHops({"1.1.1.10"}));
  addOrUpdateGroups(groups);

  // Add route with named NHG
  {
    UnicastRoute route;
    route.dest()->ip() =
        facebook::network::toBinaryAddress(folly::IPAddress("10.0.0.0"));
    route.dest()->prefixLength() = 24;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = "nhg1";
    route.namedRouteDestination() = namedDest;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::BGPD, route);
    updater.program();
  }

  // Verify the per-client entry has namedNextHopGroup set
  auto managerCopy = rib->getNextHopIDManagerCopy();
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg1"));

  // Verify ClientAndNextHops via toThriftLegacy includes namedRouteDestination
  // by checking the route in the RIB
  bool foundNhg = false;
  auto ribRoutes = rib->getRouteTableDetails(RouterID(0));
  for (const auto& rd : ribRoutes) {
    auto prefix = folly::IPAddress::createNetwork(
        fmt::format(
            "{}/{}",
            facebook::network::toIPAddress(*rd.dest()->ip()).str(),
            *rd.dest()->prefixLength()));
    if (prefix == folly::IPAddress::createNetwork("10.0.0.0/24")) {
      // Check ClientAndNextHops.namedRouteDestination
      for (const auto& clientNhops : *rd.nextHopMulti()) {
        if (clientNhops.namedRouteDestination().has_value()) {
          EXPECT_EQ(
              *clientNhops.namedRouteDestination()->nextHopGroup(), "nhg1");
          foundNhg = true;
        }
      }
      // Check RouteDetails.namedRouteDestination
      ASSERT_TRUE(rd.namedRouteDestination().has_value());
      EXPECT_EQ(*rd.namedRouteDestination()->nextHopGroup(), "nhg1");
    }
  }
  EXPECT_TRUE(foundNhg);
}

// A BGP route whose next hop is resolved recursively by an OpenR route, with
// TE_Agent steering the same destination through a named next hop group that
// carries SID lists and a counter. TE_Agent wins on admin distance, so the
// route takes its counter; repointing TE_Agent at a second group moves the
// route onto that group's counter.
TEST_F(NamedNextHopGroupRibTest, TeAgentNamedNhgUpdatesRouteCounterID) {
  const folly::IPAddress kChildPrefix{"2001::"};
  const folly::IPAddress kRecursiveNhop{"2001::1"};
  const folly::IPAddress kDest{"2800::"};
  constexpr uint8_t kPrefixLen{64};
  const RouteCounterID kCounter1{"teAgentCounter1"};
  const RouteCounterID kCounter2{"teAgentCounter2"};

  auto namedNhops = [&kRecursiveNhop](const folly::IPAddressV6& sid) {
    RouteNextHopSet nhops;
    nhops.emplace(UnresolvedNextHop(
        kRecursiveNhop,
        1,
        std::nullopt /*labelAction*/,
        std::nullopt /*disableTTLDecrement*/,
        std::nullopt /*topologyInfo*/,
        std::nullopt /*adjustedWeight*/,
        std::vector<folly::IPAddressV6>{sid},
        TunnelType::SRV6_ENCAP,
        std::string("srv6Tunnel0")));
    return nhops;
  };

  addOrUpdateGroups(
      {{"nhg1", namedNhops(folly::IPAddressV6("fdad:ffff:1::"))}});

  // OpenR resolves the address the BGP route points at, then BGP resolves
  // recursively through it.
  {
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(
        RouterID(0),
        kChildPrefix,
        kPrefixLen,
        ClientID::OPENR,
        RouteNextHopEntry(
            makeResolvedNextHops({"1::10", "2::10"}), AdminDistance::OPENR));
    updater.addRoute(
        RouterID(0),
        kDest,
        kPrefixLen,
        ClientID::BGPD,
        RouteNextHopEntry(
            RouteNextHopSet{UnresolvedNextHop(kRecursiveNhop, 1)},
            AdminDistance::EBGP));
    updater.program();
  }

  auto destCounterID =
      [this, &kDest, kPrefixLen]() -> std::optional<RouteCounterID> {
    auto route = findRoute<folly::IPAddressV6>(
        RouterID(0), folly::CIDRNetwork(kDest, kPrefixLen), sw_->getState());
    if (!route) {
      return std::nullopt;
    }
    EXPECT_TRUE(route->isResolved());
    return route->getForwardInfo().getCounterID();
  };

  // Only BGP and OpenR so far, neither of which is counted.
  EXPECT_EQ(destCounterID(), std::nullopt);

  auto programTeAgentRoute = [this, &kDest, kPrefixLen](
                                 const std::string& nhgName,
                                 const RouteCounterID& counterID) {
    UnicastRoute route;
    route.dest()->ip() = facebook::network::toBinaryAddress(kDest);
    route.dest()->prefixLength() = kPrefixLen;
    NamedRouteDestination namedDest;
    namedDest.nextHopGroup() = nhgName;
    route.namedRouteDestination() = namedDest;
    route.counterID() = counterID;
    auto updater = sw_->getRouteUpdater();
    updater.addRoute(RouterID(0), ClientID::TE_AGENT, route);
    updater.program();
  };

  programTeAgentRoute("nhg1", kCounter1);
  EXPECT_EQ(destCounterID(), kCounter1);

  // Repoint TE_Agent at a second group carrying a different SID list and a
  // different counter.
  addOrUpdateGroups(
      {{"nhg2", namedNhops(folly::IPAddressV6("fdad:ffff:2::"))}});
  programTeAgentRoute("nhg2", kCounter2);
  EXPECT_EQ(destCounterID(), kCounter2);

  auto managerCopy = sw_->getRib()->getNextHopIDManagerCopy();
  EXPECT_FALSE(managerCopy->hasRoutesForNamedNhg("nhg1"));
  EXPECT_TRUE(managerCopy->hasRoutesForNamedNhg("nhg2"));
}

// Driven off a bare RIB rather than SwSwitch: publishing a class-based policy
// drives PBR ACL synthesis, which a fake switch has no table group to program.
namespace {
const RouterID kRid(0);
const std::string kMatcher{"id=0"};

std::map<int64_t, cfg::SwitchInfo> normalizedIdSwitchInfo() {
  std::map<int64_t, cfg::SwitchInfo> map;
  cfg::SwitchInfo info{};
  info.switchType() = cfg::SwitchType::NPU;
  info.asicType() = cfg::AsicType::ASIC_TYPE_FAKE;
  info.switchIndex() = 0;
  map.emplace(0, info);
  return map;
}

const SwitchIdScopeResolver* normalizedIdScopeResolver() {
  static const SwitchIdScopeResolver kResolver(normalizedIdSwitchInfo());
  return &kResolver;
}
} // namespace

class NormalizedNamedNhgIdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    rib_.ensureVrf(kRid);
    switchState_ = std::make_shared<SwitchState>();
    auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    fibsInfoMap->addNode(kMatcher, std::make_shared<FibInfo>());
    switchState_->resetFibsInfoMap(fibsInfoMap);
    switchState_->publish();

    // Connected coverage for the next hops these tests use. 9.9.9.9 is
    // deliberately left uncovered so it never resolves.
    RoutingInformationBase::RouterIDAndNetworkToInterfaceRoutes interfaceRoutes;
    interfaceRoutes[kRid][{folly::IPAddress("1.1.1.0"), 24}] = {
        InterfaceID(1), folly::IPAddress("1.1.1.1")};
    interfaceRoutes[kRid][{folly::IPAddress("2.2.2.0"), 24}] = {
        InterfaceID(2), folly::IPAddress("2.2.2.2")};
    rib_.reconfigure(
        normalizedIdScopeResolver(),
        interfaceRoutes,
        {},
        {},
        {},
        {},
        {},
        {},
        {},
        {} /* staticMySids */,
        ribToSwitchStateUpdate,
        &switchState_);
  }

  RouteNextHopSet unresolvedNextHops(
      const std::vector<std::string>& nexthopStrs) const {
    std::vector<NextHop> nhops;
    nhops.reserve(nexthopStrs.size());
    for (const auto& nhip : nexthopStrs) {
      nhops.emplace_back(UnresolvedNextHop(folly::IPAddress(nhip), 1));
    }
    return RouteNextHopSet(nhops.begin(), nhops.end());
  }

  std::function<void(const NextHopIDManager*)> nextHopIdStateUpdate() {
    return [this](const NextHopIDManager* nextHopIDManager) {
      SwitchStateNextHopIdUpdater nhopUpdater(nextHopIDManager);
      ClassBasedPolicyUpdater policyUpdater(nextHopIDManager);
      switchState_ = policyUpdater(nhopUpdater(switchState_));
      switchState_->publish();
    };
  }

  void addOrUpdateGroups(
      const std::vector<std::pair<std::string, RouteNextHopSet>>& groups) {
    rib_.addOrUpdateNamedNextHopGroups(
        normalizedIdScopeResolver(),
        groups,
        ribToSwitchStateUpdate,
        &switchState_);
  }

  // Only a policy gives a named group a normalized id.
  void addPolicyNaming(
      const std::string& policyName,
      const std::string& nhgName) {
    ClassBasedPolicy policy;
    policy.name() = policyName;
    policy.defaultNexthopGroup() = nhgName;
    rib_.addOrUpdatePolicies({policy}, nextHopIdStateUpdate());
  }

  void removePolicy(const std::string& policyName) {
    rib_.removePolicies({policyName}, nextHopIdStateUpdate());
  }

  void addRoute(
      const std::string& prefix,
      const std::vector<std::string>& nhips) {
    auto network = folly::IPAddress::createNetwork(prefix);
    UnicastRoute route;
    route.dest()->ip() = facebook::network::toBinaryAddress(network.first);
    route.dest()->prefixLength() = network.second;
    for (const auto& nhip : nhips) {
      route.nextHopAddrs()->push_back(
          facebook::network::toBinaryAddress(folly::IPAddress(nhip)));
    }
    rib_.update(
        normalizedIdScopeResolver(),
        kRid,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {route},
        {},
        false,
        "add route",
        ribToSwitchStateUpdate,
        &switchState_);
  }

  void delRoute(const std::string& prefix) {
    auto network = folly::IPAddress::createNetwork(prefix);
    IpPrefix toDel;
    toDel.ip() = facebook::network::toBinaryAddress(network.first);
    toDel.prefixLength() = network.second;
    rib_.update(
        normalizedIdScopeResolver(),
        kRid,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {},
        {toDel},
        false,
        "del route",
        ribToSwitchStateUpdate,
        &switchState_);
  }

  std::optional<NextHopSetID> normalizedId(const std::string& nhgName) const {
    return rib_.getNextHopIDManagerCopy()
        ->getNormalizedNextHopSetIDForPolicyNhg(nhgName);
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(NormalizedNamedNhgIdTest, NormalizedIdAssignedFromResolvedNextHops) {
  addOrUpdateGroups({{"nhg1", unresolvedNextHops({"1.1.1.10", "2.2.2.10"})}});
  addPolicyNaming("pol1", "nhg1");

  EXPECT_TRUE(normalizedId("nhg1").has_value());
}

TEST_F(NormalizedNamedNhgIdTest, NormalizedIdSurvivesPartialResolution) {
  addOrUpdateGroups({{"mixed", unresolvedNextHops({"1.1.1.10", "9.9.9.9"})}});
  addPolicyNaming("mixedPol", "mixed");

  auto mixedId = normalizedId("mixed");
  ASSERT_TRUE(mixedId.has_value());

  // The id is the one the sole resolvable member alone would produce.
  addOrUpdateGroups({{"resolvableOnly", unresolvedNextHops({"1.1.1.10"})}});
  addPolicyNaming("resolvableOnlyPol", "resolvableOnly");
  EXPECT_EQ(normalizedId("resolvableOnly"), mixedId);
}

TEST_F(NormalizedNamedNhgIdTest, NormalizedIdDedupsWithRoute) {
  addOrUpdateGroups({{"nhg1", unresolvedNextHops({"1.1.1.10", "2.2.2.10"})}});
  addPolicyNaming("pol1", "nhg1");

  addRoute("10.0.0.0/24", {"1.1.1.10", "2.2.2.10"});

  auto ribRoute = findRoute<folly::IPAddressV4>(
      kRid, folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24), switchState_);
  ASSERT_NE(ribRoute, nullptr);
  auto routeNormalizedId =
      ribRoute->getForwardInfo().getNormalizedResolvedNextHopSetID();
  ASSERT_TRUE(routeNormalizedId.has_value());
  EXPECT_EQ(normalizedId("nhg1"), NextHopSetID(*routeNormalizedId));
}

// A group with no resolvable member still gets an id: the empty set interns
// like any other, and every such group shares it.
TEST_F(NormalizedNamedNhgIdTest, NormalizedIdEmptyWhenNothingResolves) {
  addOrUpdateGroups(
      {{"unresolvable", unresolvedNextHops({"9.9.9.9"})},
       {"alsoUnresolvable", unresolvedNextHops({"8.8.8.8"})}});
  addPolicyNaming("unresolvablePol", "unresolvable");
  addPolicyNaming("alsoUnresolvablePol", "alsoUnresolvable");

  auto id = normalizedId("unresolvable");
  ASSERT_TRUE(id.has_value());
  EXPECT_TRUE(rib_.getNextHopIDManagerCopy()->getNextHops(*id).empty());
  EXPECT_EQ(normalizedId("alsoUnresolvable"), id);
}

// Resolution moves with the RIB, so the id has to be recomputed every pass,
// not just when the group's own members change.
TEST_F(NormalizedNamedNhgIdTest, NormalizedIdRepointsWhenMemberLosesItsRoute) {
  // 1. 20.0.0.1 is only reachable via a BGP route, and out a different
  // interface than the connected member, so the two resolve to distinct sets.
  addRoute("20.0.0.0/24", {"2.2.2.10"});

  addOrUpdateGroups(
      {{"dyn", unresolvedNextHops({"20.0.0.1", "1.1.1.10"})},
       {"connectedOnly", unresolvedNextHops({"1.1.1.10"})}});
  addPolicyNaming("dynPol", "dyn");
  addPolicyNaming("connectedOnlyPol", "connectedOnly");

  auto bothId = normalizedId("dyn");
  auto connectedOnlyId = normalizedId("connectedOnly");
  ASSERT_TRUE(bothId.has_value());
  ASSERT_TRUE(connectedOnlyId.has_value());
  EXPECT_NE(bothId, connectedOnlyId);

  // 2. Withdraw the route 20.0.0.1 resolved through. The sweep at the tail of
  // this update must repoint "dyn" onto its one surviving member.
  delRoute("20.0.0.0/24");

  EXPECT_EQ(normalizedId("dyn"), connectedOnlyId);
}

// The sweep runs on every RIB pass, so a refcount that is not balanced on the
// nothing-moved path leaks once per pass and the set is never freed.
TEST_F(NormalizedNamedNhgIdTest, RepeatedResolutionLeavesNormalizedIdBalanced) {
  addOrUpdateGroups({{"steady", unresolvedNextHops({"1.1.1.10"})}});
  addPolicyNaming("steadyPol", "steady");
  auto id = normalizedId("steady");
  ASSERT_TRUE(id.has_value());

  // 1. Unrelated route churn: each update runs another resolution pass over
  // "steady" without changing it.
  for (int i = 0; i < 3; ++i) {
    addRoute(fmt::format("30.0.{}.0/24", i), {"2.2.2.10"});
  }

  // 2. The id must not have drifted across those passes.
  EXPECT_EQ(normalizedId("steady"), id);

  // 3. Drop the group. The policy has to go first: a named group a policy
  // references cannot be deleted.
  removePolicy("steadyPol");
  rib_.deleteNamedNextHopGroups({"steady"}, [](const NextHopIDManager*) {});
  EXPECT_EQ(
      rib_.getNextHopIDManagerCopy()->getIdToNextHopIdSet().count(*id), 0);
}

} // namespace facebook::fboss
