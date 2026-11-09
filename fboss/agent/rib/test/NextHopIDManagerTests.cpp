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
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
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
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {

constexpr int64_t kSetIdOffset = 1LL << 62;

namespace {

constexpr uint32_t kEcmpWidth = 64;
const std::string kSrv6Tunnel0{"srv6Tunnel0"};

// Helper function to create a V4 route with resolvedNextHopSetID and add to FIB
void addV4RouteWithSetId(
    const std::shared_ptr<ForwardingInformationBaseV4>& fibV4,
    const std::string& prefixStr,
    const RouteNextHopSet& nhops,
    std::optional<NextHopSetID> setId = std::nullopt) {
  auto route =
      std::make_shared<RouteV4>(RouteV4::makeThrift(makePrefixV4(prefixStr)));
  route->update(ClientID::BGPD, RouteNextHopEntry(nhops, AdminDistance::EBGP));
  if (setId.has_value()) {
    auto fwdInfo = route->getForwardInfo().toThrift();
    fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(*setId);
    route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  } else {
    route->setResolved(RouteNextHopEntry(nhops, AdminDistance::EBGP));
  }
  route->publish();
  fibV4->addNode(route);
}

// Helper function to create a V6 route with resolvedNextHopSetID and add to FIB
void addV6RouteWithSetId(
    const std::shared_ptr<ForwardingInformationBaseV6>& fibV6,
    const std::string& prefixStr,
    const RouteNextHopSet& nhops,
    std::optional<NextHopSetID> setId = std::nullopt) {
  auto route =
      std::make_shared<RouteV6>(RouteV6::makeThrift(makePrefixV6(prefixStr)));
  route->update(ClientID::BGPD, RouteNextHopEntry(nhops, AdminDistance::EBGP));
  if (setId.has_value()) {
    auto fwdInfo = route->getForwardInfo().toThrift();
    fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(*setId);
    route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  } else {
    route->setResolved(RouteNextHopEntry(nhops, AdminDistance::EBGP));
  }
  route->publish();
  fibV6->addNode(route);
}

// Helper function to create a V4 route with a per-client entry that carries
// clientNextHopSetID, and add it to a FIB. Used to exercise the per-client
// reconstruction path in NextHopIDManager::reconstructFromSwitchStateMaps.
// Also stamps resolvedNextHopSetID + normalizedResolvedNextHopSetID on the
// fwd info so the backfill pass in RibRouteTables::fromThrift is a no-op
// (the test focuses on reconstruct behavior, not backfill).
void addV4RouteWithClientId(
    const std::shared_ptr<ForwardingInformationBaseV4>& fibV4,
    const std::string& prefixStr,
    const RouteNextHopSet& nhops,
    ClientID clientId,
    NextHopSetID clientSetId) {
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
  route->update(clientId, entry);
  auto fwdInfo = RouteNextHopEntry(nhops, AdminDistance::EBGP).toThrift();
  fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(clientSetId);
  fwdInfo.normalizedResolvedNextHopSetID() = static_cast<uint64_t>(clientSetId);
  route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  route->publish();
  fibV4->addNode(route);
}

// V6 counterpart of addV4RouteWithClientId.
void addV6RouteWithClientId(
    const std::shared_ptr<ForwardingInformationBaseV6>& fibV6,
    const std::string& prefixStr,
    const RouteNextHopSet& nhops,
    ClientID clientId,
    NextHopSetID clientSetId) {
  auto route =
      std::make_shared<RouteV6>(RouteV6::makeThrift(makePrefixV6(prefixStr)));
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
  route->update(clientId, entry);
  auto fwdInfo = RouteNextHopEntry(nhops, AdminDistance::EBGP).toThrift();
  fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(clientSetId);
  fwdInfo.normalizedResolvedNextHopSetID() = static_cast<uint64_t>(clientSetId);
  route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  route->publish();
  fibV6->addNode(route);
}

// Helper function to create a ForwardingInformationBaseMap with empty FIBs
std::shared_ptr<ForwardingInformationBaseMap> createFibsMap(
    RouterID vrf = RouterID(0)) {
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto fibContainer = std::make_shared<ForwardingInformationBaseContainer>(vrf);
  fibContainer->ref<switch_state_tags::fibV4>() =
      std::make_shared<ForwardingInformationBaseV4>();
  fibContainer->ref<switch_state_tags::fibV6>() =
      std::make_shared<ForwardingInformationBaseV6>();
  fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  return fibsMap;
}

// Helper function to get FibV4 from FibsMap
std::shared_ptr<ForwardingInformationBaseV4> getFibV4(
    const std::shared_ptr<ForwardingInformationBaseMap>& fibsMap,
    RouterID vrf = RouterID(0)) {
  auto fibContainer = fibsMap->getNodeIf(vrf);
  return fibContainer ? fibContainer->getFibV4() : nullptr;
}

// Helper function to get FibV6 from FibsMap
std::shared_ptr<ForwardingInformationBaseV6> getFibV6(
    const std::shared_ptr<ForwardingInformationBaseMap>& fibsMap,
    RouterID vrf = RouterID(0)) {
  auto fibContainer = fibsMap->getNodeIf(vrf);
  return fibContainer ? fibContainer->getFibV6() : nullptr;
}

// Helper function to create a MultiSwitchFibInfoMap with FIB and ID maps
std::shared_ptr<MultiSwitchFibInfoMap> createMultiSwitchFibInfoMap(
    const std::shared_ptr<ForwardingInformationBaseMap>& fibsMap,
    const std::shared_ptr<IdToNextHopMap>& idToNextHopMap = nullptr,
    const std::shared_ptr<IdToNextHopIdSetMap>& idToNextHopIdSetMap = nullptr) {
  auto multiSwitchFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto fibInfo = std::make_shared<FibInfo>();

  // Set the FIBs map using the resetFibsMap method
  if (fibsMap) {
    fibInfo->resetFibsMap(fibsMap);
  }

  // Set the ID maps if provided
  if (idToNextHopMap) {
    fibInfo->setIdToNextHopMap(idToNextHopMap);
  }
  if (idToNextHopIdSetMap) {
    fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);
  }

  // Add the FibInfo to the map using a default switch matcher string key
  multiSwitchFibInfoMap->addNode("id=0", fibInfo);
  return multiSwitchFibInfoMap;
}

// Helper function to add FibInfo to an existing MultiSwitchFibInfoMap
void addFibInfoToMultiSwitchMap(
    const std::shared_ptr<MultiSwitchFibInfoMap>& multiSwitchFibInfoMap,
    const std::string& switchId,
    const std::shared_ptr<ForwardingInformationBaseMap>& fibsMap,
    const std::shared_ptr<IdToNextHopMap>& idToNextHopMap = nullptr,
    const std::shared_ptr<IdToNextHopIdSetMap>& idToNextHopIdSetMap = nullptr) {
  auto fibInfo = std::make_shared<FibInfo>();

  if (fibsMap) {
    fibInfo->resetFibsMap(fibsMap);
  }
  if (idToNextHopMap) {
    fibInfo->setIdToNextHopMap(idToNextHopMap);
  }
  if (idToNextHopIdSetMap) {
    fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);
  }

  multiSwitchFibInfoMap->addNode(switchId, fibInfo);
}

std::shared_ptr<MySid> makeMySidEntry(
    std::optional<NextHopSetID> resolvedId,
    std::optional<NextHopSetID> unresolvedId) {
  state::MySidFields fields;
  fields.type() = MySidType::NODE_MICRO_SID;
  facebook::network::thrift::IPPrefix thriftPrefix;
  thriftPrefix.prefixAddress() =
      facebook::network::toBinaryAddress(folly::IPAddress("fc00:100::1"));
  thriftPrefix.prefixLength() = 48;
  fields.mySid() = thriftPrefix;
  auto mySid = std::make_shared<MySid>(fields);
  mySid->setResolvedNextHopsId(resolvedId);
  mySid->setUnresolveNextHopsId(unresolvedId);
  return mySid;
}

std::shared_ptr<MultiSwitchMySidMap> makeMySidMap(
    const std::shared_ptr<MySid>& mySid) {
  auto mySidMap = std::make_shared<MultiSwitchMySidMap>();
  mySidMap->addNode(
      mySid, HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}});
  return mySidMap;
}

} // namespace

class NextHopIDManagerTest : public ::testing::Test {
 public:
  NextHopIDManagerTest() = default;

  void SetUp() override {
    manager_ = std::make_unique<NextHopIDManager>();
  }

 protected:
  std::unique_ptr<NextHopIDManager> manager_;
};

TEST_F(NextHopIDManagerTest, getOrAllocateNextHopID) {
  // Test that IDs are allocated sequentially
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  auto id1 = manager_->getOrAllocateNextHopID(nh1)->second;
  auto id2 = manager_->getOrAllocateNextHopID(nh2)->second;
  auto id3 = manager_->getOrAllocateNextHopID(nh3)->second;

  EXPECT_EQ(id1, NextHopID(1));
  EXPECT_EQ(id2, NextHopID(2));
  EXPECT_EQ(id3, NextHopID(3));

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);

  EXPECT_EQ(manager_->getIdToNextHop().at(id1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(id2).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(id3).nextHop, nh3);

  // Test reusing existing ID

  auto id4 = manager_->getOrAllocateNextHopID(nh1)->second;
  EXPECT_EQ(id1, id4);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().at(id4).nextHop, nh1);

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
}

TEST_F(NextHopIDManagerTest, AllocatesDifferentIDsForDifferentRoles) {
  NextHop primary =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  auto backupThrift = primary.toThrift();
  *backupThrift.role() = NextHopRole::BACKUP;
  NextHop backup = util::fromThrift(backupThrift);

  const auto primaryID = manager_->getOrAllocateNextHopID(primary)->second;
  const auto backupID = manager_->getOrAllocateNextHopID(backup)->second;

  EXPECT_NE(primaryID, backupID);
  EXPECT_EQ(manager_->getOrAllocateNextHopID(primary)->second, primaryID);
  EXPECT_EQ(manager_->getOrAllocateNextHopID(backup)->second, backupID);
}

TEST_F(NextHopIDManagerTest, getOrAllocateNextHopSetID) {
  // Test empty set throws exception
  NextHopIDSet emptySet;
  auto emptySetIter = manager_->getOrAllocateNextHopSetID(emptySet);
  EXPECT_EQ(emptySetIter->second.id, NextHopSetID(kSetIdOffset));
  EXPECT_EQ(emptySetIter->first, emptySet);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);

  // Test that IDs are allocated sequentially starting from 2^63
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  auto nhID1 = manager_->getOrAllocateNextHopID(nh1)->second;
  auto nhID2 = manager_->getOrAllocateNextHopID(nh2)->second;
  auto nhID3 = manager_->getOrAllocateNextHopID(nh3)->second;

  NextHopIDSet set1 = {nhID1};
  NextHopIDSet set2 = {nhID1, nhID2};
  NextHopIDSet set3 = {nhID1, nhID2, nhID3};

  auto setIter1 = manager_->getOrAllocateNextHopSetID(set1);
  auto setIter2 = manager_->getOrAllocateNextHopSetID(set2);
  auto setIter3 = manager_->getOrAllocateNextHopSetID(set3);

  EXPECT_EQ(setIter1->second.id, NextHopSetID(kSetIdOffset + 1));
  EXPECT_EQ(setIter2->second.id, NextHopSetID(kSetIdOffset + 2));
  EXPECT_EQ(setIter3->second.id, NextHopSetID(kSetIdOffset + 3));

  EXPECT_EQ(setIter1->first, set1);
  EXPECT_EQ(setIter2->first, set2);
  EXPECT_EQ(setIter3->first, set3);

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set1), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set2), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set3), 1);

  // Reuse existing ID
  auto setIter4 = manager_->getOrAllocateNextHopSetID(set1);
  EXPECT_EQ(setIter1->second.id, setIter4->second.id);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 4);
  EXPECT_EQ(setIter4->first, set1);

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set1), 2);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set2), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set3), 1);
}

TEST_F(NextHopIDManagerTest, getOrAllocateNextHopSetIDOrderIndependence) {
  // Test that sets with same elements but different order get same ID
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  auto nhID1 = manager_->getOrAllocateNextHopID(nh1)->second;
  auto nhID2 = manager_->getOrAllocateNextHopID(nh2)->second;
  auto nhID3 = manager_->getOrAllocateNextHopID(nh3)->second;

  NextHopIDSet set1 = {nhID1, nhID2, nhID3};
  NextHopIDSet set2 = {nhID3, nhID1, nhID2};
  NextHopIDSet set3 = {nhID2, nhID3, nhID1};

  auto setIter1 = manager_->getOrAllocateNextHopSetID(set1);
  auto setIter2 = manager_->getOrAllocateNextHopSetID(set2);
  auto setIter3 = manager_->getOrAllocateNextHopSetID(set3);

  EXPECT_EQ(setIter1->second.id, setIter2->second.id);
  EXPECT_EQ(setIter2->second.id, setIter3->second.id);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(setIter1->first, set1);

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set1), 3);
}

TEST_F(NextHopIDManagerTest, decrOrDeallocateNextHop) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  // Test throws exception when decrementing/deallocating non-existent ID
  EXPECT_THROW(manager_->decrOrDeallocateNextHop(nh1), FbossError);

  // allocate IDs to Nexthops
  manager_->getOrAllocateNextHopID(nh1);
  auto id2 = manager_->getOrAllocateNextHopID(nh2)->second;
  manager_->getOrAllocateNextHopID(nh3);
  auto id4 = manager_->getOrAllocateNextHopID(nh1)->second;

  // Test decrementing ref count works
  manager_->decrOrDeallocateNextHop(nh1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().at(id4).nextHop, nh1);

  // Test deallocation works
  manager_->decrOrDeallocateNextHop(nh2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 0);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHop().count(id2), 0);
  EXPECT_EQ(manager_->nextHopToID_.count(nh2), 0);

  // Test allocating new ID after deallocating continues from last ID
  auto id5 = manager_->getOrAllocateNextHopID(nh2)->second;
  EXPECT_EQ(id5, NextHopID(4));
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().at(id5).nextHop, nh2);
}

TEST_F(NextHopIDManagerTest, decrOrDeallocateNextHopByID) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  // Test throws exception when decrementing non-existent NextHopID
  EXPECT_THROW(
      manager_->decrOrDeallocateNextHopByID(NextHopID(999)), FbossError);

  // Allocate IDs
  auto id1 = manager_->getOrAllocateNextHopID(nh1)->second;
  auto id2 = manager_->getOrAllocateNextHopID(nh2)->second;
  manager_->getOrAllocateNextHopID(nh3);
  // Bump nh1 refcount to 2
  manager_->getOrAllocateNextHopID(nh1);

  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Decrement by ID — refcount goes from 2 to 1
  EXPECT_FALSE(manager_->decrOrDeallocateNextHopByID(id1));
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Deallocate by ID — refcount goes from 1 to 0, entry removed
  EXPECT_TRUE(manager_->decrOrDeallocateNextHopByID(id2));
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 0);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHop().count(id2), 0);
  EXPECT_EQ(manager_->nextHopToID_.count(nh2), 0);

  // Re-allocating after deallocation gets a new ID
  auto id4 = manager_->getOrAllocateNextHopID(nh2)->second;
  EXPECT_EQ(id4, NextHopID(4));
  EXPECT_EQ(manager_->getIdToNextHop().at(id4).nextHop, nh2);
}

TEST_F(NextHopIDManagerTest, decrOrDeallocateNextHopIDSet) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  auto nhID1 = manager_->getOrAllocateNextHopID(nh1)->second;
  auto nhID2 = manager_->getOrAllocateNextHopID(nh2)->second;
  auto nhID3 = manager_->getOrAllocateNextHopID(nh3)->second;

  NextHopIDSet set1 = {nhID1};
  NextHopIDSet set2 = {nhID1, nhID2};
  NextHopIDSet set3 = {nhID1, nhID2, nhID3};
  NextHopIDSet set4 = {};

  // Test throws exception when decrementing/deallocating non-existent set
  EXPECT_THROW(manager_->decrOrDeallocateNextHopIDSet(set1), FbossError);

  manager_->getOrAllocateNextHopSetID(set1);
  auto setIter2 = manager_->getOrAllocateNextHopSetID(set2);
  auto setID2 = setIter2->second.id;
  manager_->getOrAllocateNextHopSetID(set3);
  auto setIter4 = manager_->getOrAllocateNextHopSetID(set1);
  auto setID4 = setIter4->second.id;
  auto setIter5 = manager_->getOrAllocateNextHopSetID(set4);
  auto setID5 = setIter5->second.id;

  // Test decrementing ref count works
  manager_->decrOrDeallocateNextHopIDSet(set1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set1), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID4), set1);

  // Test deallocation works
  manager_->decrOrDeallocateNextHopIDSet(set2);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set2), 0);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setID2), 0);
  EXPECT_EQ(manager_->nextHopIdSetToIDInfo_.count(set2), 0);

  // Test deallocating empty set works
  manager_->decrOrDeallocateNextHopIDSet(set4);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(set4), 0);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setID5), 0);
  EXPECT_EQ(manager_->nextHopIdSetToIDInfo_.count(set4), 0);

  // Test allocating new ID after deallocating continues from last ID
  auto setIter6 = manager_->getOrAllocateNextHopSetID(set2);
  EXPECT_EQ(setIter6->second.id, NextHopSetID(kSetIdOffset + 4));
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
  EXPECT_EQ(setIter6->first, set2);
}

TEST_F(NextHopIDManagerTest, getOrAllocRouteNextHopSetIDWithEmptySet) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);

  // Add nhSet1 with 2 nexthops (nh1, nh2)
  RouteNextHopSet nhSet1 = {nh1, nh2};

  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);

  EXPECT_EQ(result1.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset));
  // Verify 2 new NextHopIDs were allocated
  EXPECT_EQ(result1.addedNextHopIds.size(), 2);

  // Verify individual NextHop IDs were allocated
  auto nhID1 = manager_->getNextHopID(nh1);
  auto nhID2 = manager_->getNextHopID(nh2);
  ASSERT_TRUE(nhID1.has_value());
  ASSERT_TRUE(nhID2.has_value());
  EXPECT_EQ(nhID1.value(), NextHopID(1));
  EXPECT_EQ(nhID2.value(), NextHopID(2));

  // Verify idToNextHop map
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID1.value()).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID2.value()).nextHop, nh2);

  // Verify NextHopIDSet was created
  NextHopIDSet expectedIDSet1 = {nhID1.value(), nhID2.value()};
  auto actualSetID1 = manager_->getNextHopSetID(expectedIDSet1);
  ASSERT_TRUE(actualSetID1.has_value());
  EXPECT_EQ(actualSetID1.value(), result1.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result1.nextHopIdSetIter->second.id),
      expectedIDSet1);

  // Make NhSet2 with 3 nexthops (nh1, nh2, nh3)
  RouteNextHopSet nhSet2 = {nh1, nh2, nh3};

  auto result2 = manager_->getOrAllocRouteNextHopSetID(nhSet2);

  EXPECT_EQ(
      result2.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset + 1));
  // Verify only 1 new NextHopID was allocated (nh3)
  EXPECT_EQ(result2.addedNextHopIds.size(), 1);
  EXPECT_EQ(result2.addedNextHopIds[0], NextHopID(3));

  // Verify NextHop IDs - nh3 should get a new ID
  auto nhID3 = manager_->getNextHopID(nh3);
  ASSERT_TRUE(nhID3.has_value());
  EXPECT_EQ(nhID3.value(), NextHopID(3));

  // Verify idToNextHop map now has 3 entries
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID3.value()).nextHop, nh3);

  // Verify NextHopIDSet was created for nhSet2
  NextHopIDSet expectedIDSet2 = {nhID1.value(), nhID2.value(), nhID3.value()};
  auto actualSetID2 = manager_->getNextHopSetID(expectedIDSet2);
  ASSERT_TRUE(actualSetID2.has_value());
  EXPECT_EQ(actualSetID2.value(), result2.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map has 2 entries
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result2.nextHopIdSetIter->second.id),
      expectedIDSet2);

  // Add nhSet3 with empty set {}
  RouteNextHopSet nhSet3;

  // Call getOrAllocRouteNextHopSetID for empty set
  auto result3 = manager_->getOrAllocRouteNextHopSetID(nhSet3);

  EXPECT_EQ(
      result3.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset + 2));
  // Verify no new NextHopIDs were allocated
  EXPECT_EQ(result3.addedNextHopIds.size(), 0);

  // Verify no new NextHop IDs were allocated (empty set)
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Verify NextHopIDSet was created for empty nhSet3
  NextHopIDSet expectedIDSet3 = {};
  auto actualSetID3 = manager_->getNextHopSetID(expectedIDSet3);
  ASSERT_TRUE(actualSetID3.has_value());
  EXPECT_EQ(actualSetID3.value(), result3.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map has 3 entries
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result3.nextHopIdSetIter->second.id),
      expectedIDSet3);

  // Call getOrAllocRouteNextHopSetID on empty set again to verify ID reuse
  RouteNextHopSet nhSet4;
  auto result4 = manager_->getOrAllocRouteNextHopSetID(nhSet4);

  // Verify the same ID is returned
  EXPECT_EQ(
      result3.nextHopIdSetIter->second.id, result4.nextHopIdSetIter->second.id);
  // Verify no new NextHopIDs were allocated
  EXPECT_EQ(result4.addedNextHopIds.size(), 0);

  // Verify total number of NextHopIDSets remains 3
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
}

TEST_F(
    NextHopIDManagerTest,
    getOrAllocRouteNextHopSetIDSubSetSuperSetNextHops) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);

  // Add nhSet1 with 3 nexthops (nh1, nh2, nh3)
  RouteNextHopSet nhSet1 = {nh1, nh2, nh3};

  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);

  EXPECT_EQ(result1.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset));
  // Verify 3 new NextHopIDs were allocated
  EXPECT_EQ(result1.addedNextHopIds.size(), 3);

  // Verify individual NextHop IDs were allocated
  auto nhID1 = manager_->getNextHopID(nh1);
  auto nhID2 = manager_->getNextHopID(nh2);
  auto nhID3 = manager_->getNextHopID(nh3);

  // Verify idToNextHop map
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID1.value()).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID2.value()).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID3.value()).nextHop, nh3);

  // Verify NextHopIDSet was created
  NextHopIDSet expectedIDSet1 = {nhID1.value(), nhID2.value(), nhID3.value()};
  auto actualSetID1 = manager_->getNextHopSetID(expectedIDSet1);
  ASSERT_TRUE(actualSetID1.has_value());
  EXPECT_EQ(actualSetID1.value(), result1.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result1.nextHopIdSetIter->second.id),
      expectedIDSet1);

  // Make NhSet2 with 2 nexthops (nh1, nh2) - subset of nhSet1
  RouteNextHopSet nhSet2 = {nh1, nh2};

  auto result2 = manager_->getOrAllocRouteNextHopSetID(nhSet2);

  EXPECT_EQ(
      result2.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset + 1));
  // Verify no new NextHopIDs were allocated (reusing existing IDs)
  EXPECT_EQ(result2.addedNextHopIds.size(), 0);

  // Verify no new NextHop IDs were allocated (reusing existing IDs)
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Verify NextHopIDSet was created for nhSet2
  NextHopIDSet expectedIDSet2 = {nhID1.value(), nhID2.value()};
  auto actualSetID2 = manager_->getNextHopSetID(expectedIDSet2);
  ASSERT_TRUE(actualSetID2.has_value());
  EXPECT_EQ(actualSetID2.value(), result2.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map has 2 entries
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result2.nextHopIdSetIter->second.id),
      expectedIDSet2);

  // Add nhSet3 with 4 nexthops (nh1, nh2, nh3, nh4) - superset of nhSet1
  RouteNextHopSet nhSet3 = {nh1, nh2, nh3, nh4};

  auto result3 = manager_->getOrAllocRouteNextHopSetID(nhSet3);

  EXPECT_EQ(
      result3.nextHopIdSetIter->second.id, NextHopSetID(kSetIdOffset + 2));
  // Verify only 1 new NextHopID was allocated (nh4)
  EXPECT_EQ(result3.addedNextHopIds.size(), 1);

  // Verify NextHop IDs - nh4 should get a new ID
  auto nhID4 = manager_->getNextHopID(nh4);
  ASSERT_TRUE(nhID4.has_value());
  EXPECT_EQ(nhID4.value(), NextHopID(4));
  EXPECT_EQ(result3.addedNextHopIds[0], NextHopID(4));

  // Verify idToNextHop map has 4 entries
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhID4.value()).nextHop, nh4);

  // Verify NextHopIDSet was created for nhSet3
  NextHopIDSet expectedIDSet3 = {
      nhID1.value(), nhID2.value(), nhID3.value(), nhID4.value()};
  auto actualSetID3 = manager_->getNextHopSetID(expectedIDSet3);
  ASSERT_TRUE(actualSetID3.has_value());
  EXPECT_EQ(actualSetID3.value(), result3.nextHopIdSetIter->second.id);

  // Verify idToNextHopIdSet map has 3 entries
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(result3.nextHopIdSetIter->second.id),
      expectedIDSet3);

  // Call getOrAllocRouteNextHopSetID on nhSet4 = nhSet2 again to verify ID
  // reuse
  RouteNextHopSet nhSet4 = {nh1, nh2};
  auto result4 = manager_->getOrAllocRouteNextHopSetID(nhSet4);

  // Verify the same ID is returned
  EXPECT_EQ(
      result2.nextHopIdSetIter->second.id, result4.nextHopIdSetIter->second.id);
  // Verify no new NextHopIDs were allocated
  EXPECT_EQ(result4.addedNextHopIds.size(), 0);

  // Verify total number of NextHopIDSets remains 3
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);

  // Verify total number of NextHops remains 4
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
}

TEST_F(NextHopIDManagerTest, delOrDecrRouteNextHopSetID) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);

  //  Allocate setID id1 for Nh1, nh2, nh3
  RouteNextHopSet nhSet1 = {nh1, nh2, nh3};
  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);
  NextHopSetID setID1 = result1.nextHopIdSetIter->second.id;

  // Allocate setID id2 for Nh1, nh2
  RouteNextHopSet nhSet2 = {nh1, nh2};
  auto result2 = manager_->getOrAllocRouteNextHopSetID(nhSet2);
  NextHopSetID setID2 = result2.nextHopIdSetIter->second.id;

  // Allocate setID id3 for Nh2, nh4
  RouteNextHopSet nhSet3 = {nh2, nh4};
  auto result3 = manager_->getOrAllocRouteNextHopSetID(nhSet3);
  NextHopSetID setID3 = result3.nextHopIdSetIter->second.id;

  // Allocate setID id4 for empty set {}
  RouteNextHopSet nhSet4;
  auto result4 = manager_->getOrAllocRouteNextHopSetID(nhSet4);
  NextHopSetID setID4 = result4.nextHopIdSetIter->second.id;

  // Verify initial state
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 4);

  // Verify NextHop reference counts
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 3);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh4), 1);

  // Get NextHopIDs for verification
  auto nhID1 = manager_->getNextHopID(nh1);
  auto nhID2 = manager_->getNextHopID(nh2);
  auto nhID3 = manager_->getNextHopID(nh3);
  auto nhID4 = manager_->getNextHopID(nh4);

  NextHopIDSet expectedIDSet1 = {nhID1.value(), nhID2.value(), nhID3.value()};
  NextHopIDSet expectedIDSet2 = {nhID1.value(), nhID2.value()};
  NextHopIDSet expectedIDSet3 = {nhID2.value(), nhID4.value()};
  NextHopIDSet expectedIDSet4 = {};

  // Verify idToNextHopIdSet map
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID1), expectedIDSet1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID2), expectedIDSet2);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID3), expectedIDSet3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID4), expectedIDSet4);

  //  Call decrOrDeallocRouteNextHopSetID on id3
  auto deallocResult3 = manager_->decrOrDeallocRouteNextHopSetID(setID3);

  // Verify the deallocation result
  EXPECT_TRUE(deallocResult3.removedSetId.has_value());
  EXPECT_EQ(deallocResult3.removedSetId.value(), setID3);
  // nh4 should be deallocated (refcount was 1)
  EXPECT_EQ(deallocResult3.removedNextHopIds.size(), 1);
  EXPECT_EQ(deallocResult3.removedNextHopIds[0], nhID4.value());

  // NextHopSetID id3 should be deallocated
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setID3), 0);
  EXPECT_EQ(manager_->nextHopIdSetToIDInfo_.count(expectedIDSet3), 0);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhID4.value()), 0);
  EXPECT_EQ(manager_->nextHopToID_.count(nh4), 0);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 2);

  // Call delOrDecrRouteNextHopSetID on id2
  auto deallocResult2 = manager_->decrOrDeallocRouteNextHopSetID(setID2);

  // Verify the deallocation result
  EXPECT_TRUE(deallocResult2.removedSetId.has_value());
  EXPECT_EQ(deallocResult2.removedSetId.value(), setID2);
  // No NextHops should be deallocated (nh1 and nh2 still have refcounts from
  // set1)
  EXPECT_EQ(deallocResult2.removedNextHopIds.size(), 0);

  // NextHopSetID id2 should be deallocated
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);

  // All NextHops should still exist
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Allocate id1 again with same set of nexthops
  auto result1Again = manager_->getOrAllocRouteNextHopSetID(nhSet1);
  EXPECT_EQ(result1Again.nextHopIdSetIter->second.id, setID1);

  // Call delOrDecrRouteNextHopSetID on id1 which has been allocated twice
  auto deallocResult1 = manager_->decrOrDeallocRouteNextHopSetID(setID1);

  // Set was not deallocated (refcount decremented but not to 0)
  EXPECT_FALSE(deallocResult1.removedSetId.has_value());
  // No NextHops should be deallocated
  EXPECT_EQ(deallocResult1.removedNextHopIds.size(), 0);

  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);
  // Verify remaining NextHopSetIDs (id1 and id4 should still exist)
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setID1), expectedIDSet1);
  EXPECT_EQ(manager_->nextHopIdSetToIDInfo_.count(expectedIDSet1), 1);

  // Deallocate setID4 (empty set) and verify state
  auto deallocResult4 = manager_->decrOrDeallocRouteNextHopSetID(setID4);

  // Verify the deallocation result
  EXPECT_TRUE(deallocResult4.removedSetId.has_value());
  EXPECT_EQ(deallocResult4.removedSetId.value(), setID4);
  // No NextHops to deallocate (empty set)
  EXPECT_EQ(deallocResult4.removedNextHopIds.size(), 0);

  // NextHopSetID id4 should be deallocated
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setID4), 0);
  EXPECT_EQ(manager_->nextHopIdSetToIDInfo_.count(expectedIDSet4), 0);

  // No NextHops should be affected (still 3 NextHops)
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);

  // Call decrOrDeallocRouteNextHopSetID for non-existent id and check it
  // throws FbossError
  NextHopSetID nonExistentID = NextHopSetID(999999);
  EXPECT_THROW(
      manager_->decrOrDeallocRouteNextHopSetID(nonExistentID), FbossError);
}

TEST_F(NextHopIDManagerTest, updateRouteNextHopSetID) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);
  NextHop nh5 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.5", UCMP_DEFAULT_WEIGHT);

  // Update with same nexthop set
  RouteNextHopSet nhSet1 = {nh1, nh2};
  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);
  NextHopSetID setID1 = result1.nextHopIdSetIter->second.id;
  EXPECT_EQ(setID1, NextHopSetID(kSetIdOffset));
  // Verify initial allocation
  EXPECT_EQ(result1.addedNextHopIds.size(), 2);

  auto updateResult1 = manager_->updateRouteNextHopSetID(setID1, nhSet1);
  NextHopSetID updatedSetID1 =
      updateResult1.allocation.nextHopIdSetIter->second.id;
  EXPECT_EQ(updatedSetID1, NextHopSetID(kSetIdOffset + 1));
  // Old set was deallocated
  EXPECT_TRUE(updateResult1.deallocation.removedSetId.has_value());
  EXPECT_EQ(updateResult1.deallocation.removedSetId.value(), setID1);
  // NextHops ARE deallocated first (dealloc happens before alloc)
  EXPECT_EQ(updateResult1.deallocation.removedNextHopIds.size(), 2);
  // NextHops are reallocated (since they were deallocated first)
  EXPECT_EQ(updateResult1.allocation.addedNextHopIds.size(), 2);

  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);

  auto nhID1 = manager_->getNextHopID(nh1);
  auto nhID2 = manager_->getNextHopID(nh2);
  NextHopIDSet expectedIDSet1 = {nhID1.value(), nhID2.value()};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(updatedSetID1), expectedIDSet1);

  // Update with completely different nexthops
  RouteNextHopSet nhSet2 = {nh3, nh4};
  auto updateResult2 = manager_->updateRouteNextHopSetID(updatedSetID1, nhSet2);
  NextHopSetID updatedSetID2 =
      updateResult2.allocation.nextHopIdSetIter->second.id;
  EXPECT_EQ(updatedSetID2, NextHopSetID(kSetIdOffset + 2));

  // Verify deallocation result
  EXPECT_TRUE(updateResult2.deallocation.removedSetId.has_value());
  EXPECT_EQ(updateResult2.deallocation.removedSetId.value(), updatedSetID1);
  // nh1 and nh2 should be deallocated (refcounts were 1)
  EXPECT_EQ(updateResult2.deallocation.removedNextHopIds.size(), 2);

  // Verify allocation result
  // nh3 and nh4 should be newly allocated
  EXPECT_EQ(updateResult2.allocation.addedNextHopIds.size(), 2);

  // Old nexthops should be deallocated
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 0);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 0);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhID1.value()), 0);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhID2.value()), 0);

  // New nexthops should be allocated
  auto nhID3 = manager_->getNextHopID(nh3);
  auto nhID4 = manager_->getNextHopID(nh4);
  ASSERT_TRUE(nhID3.has_value());
  ASSERT_TRUE(nhID4.has_value());

  NextHopIDSet expectedIDSet2 = {nhID3.value(), nhID4.value()};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(updatedSetID2), expectedIDSet2);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);

  // Update with partial overlap
  // Note: Since dealloc happens before alloc, nh3 will be deallocated then
  // reallocated
  RouteNextHopSet nhSet3 = {nh3, nh5};
  auto updateResult3 = manager_->updateRouteNextHopSetID(updatedSetID2, nhSet3);
  NextHopSetID updatedSetID3 =
      updateResult3.allocation.nextHopIdSetIter->second.id;
  EXPECT_EQ(updatedSetID3, NextHopSetID(kSetIdOffset + 3));

  // Verify deallocation result - both nh3 and nh4 deallocated (dealloc before
  // alloc)
  EXPECT_TRUE(updateResult3.deallocation.removedSetId.has_value());
  EXPECT_EQ(updateResult3.deallocation.removedNextHopIds.size(), 2);

  // Verify allocation result - both nh3 and nh5 allocated
  EXPECT_EQ(updateResult3.allocation.addedNextHopIds.size(), 2);

  // nh3 gets a new ID (was deallocated, then reallocated)
  auto nhID3New = manager_->getNextHopID(nh3);
  ASSERT_TRUE(nhID3New.has_value());
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh4), 0);

  // nh5 should be newly allocated
  auto nhID5 = manager_->getNextHopID(nh5);
  ASSERT_TRUE(nhID5.has_value());
  EXPECT_EQ(manager_->getNextHopRefCount(nh5), 1);

  NextHopIDSet expectedIDSet3 = {nhID3New.value(), nhID5.value()};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(updatedSetID3), expectedIDSet3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);

  // Update to empty set
  RouteNextHopSet emptySet;
  auto updateResult4 =
      manager_->updateRouteNextHopSetID(updatedSetID3, emptySet);
  NextHopSetID updatedSetID4 =
      updateResult4.allocation.nextHopIdSetIter->second.id;
  EXPECT_EQ(updatedSetID4, NextHopSetID(kSetIdOffset + 4));

  // Verify deallocation result
  EXPECT_TRUE(updateResult4.deallocation.removedSetId.has_value());
  // nh3 and nh5 should be deallocated
  EXPECT_EQ(updateResult4.deallocation.removedNextHopIds.size(), 2);

  // Verify allocation result - empty set allocated
  EXPECT_EQ(updateResult4.allocation.addedNextHopIds.size(), 0);

  // Previous nexthops should be deallocated
  EXPECT_EQ(manager_->getIdToNextHop().size(), 0);

  NextHopIDSet expectedEmptySet = {};
  EXPECT_EQ(
      manager_->getIdToNextHopIdSet().at(updatedSetID4), expectedEmptySet);

  // Update from empty set
  RouteNextHopSet nhSet4 = {nh1, nh2};
  auto updateResult5 = manager_->updateRouteNextHopSetID(updatedSetID4, nhSet4);
  NextHopSetID updatedSetID5 =
      updateResult5.allocation.nextHopIdSetIter->second.id;
  EXPECT_EQ(updatedSetID5, NextHopSetID(kSetIdOffset + 5));

  // Verify deallocation result - empty set deallocated
  EXPECT_TRUE(updateResult5.deallocation.removedSetId.has_value());
  EXPECT_EQ(updateResult5.deallocation.removedNextHopIds.size(), 0);

  // Verify allocation result
  EXPECT_EQ(updateResult5.allocation.addedNextHopIds.size(), 2);

  // New nexthops should be allocated
  nhID1 = manager_->getNextHopID(nh1);
  nhID2 = manager_->getNextHopID(nh2);
  ASSERT_TRUE(nhID1.has_value());
  ASSERT_TRUE(nhID2.has_value());
  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);

  NextHopIDSet expectedIDSet5 = {nhID1.value(), nhID2.value()};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(updatedSetID5), expectedIDSet5);

  // Update with invalid/non-existent old NextHopSetID
  NextHopSetID nonExistentID = NextHopSetID(999999);
  RouteNextHopSet nhSet8 = {nh1, nh2};
  EXPECT_THROW(
      manager_->updateRouteNextHopSetID(nonExistentID, nhSet8), FbossError);
}

// This tests the single-phase reconstructFromSwitchStateMaps which builds ID
// maps and refcounts in a single pass through FIB routes
TEST_F(NextHopIDManagerTest, reconstructFromSwitchStateMaps) {
  // Create IdToNextHopMap
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "2001:db8::1", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);

  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId3), nh3.toThrift());

  // Create IdToNextHopIdSetMap
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);
  NextHopSetID setId2 = NextHopSetID(kSetIdOffset + 2);

  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  std::set<state::NextHopIdType> set2{
      static_cast<state::NextHopIdType>(nhId2),
      static_cast<state::NextHopIdType>(nhId3)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2), set2);

  // Create FIB with routes using helper functions
  auto fibsMap = createFibsMap();
  auto fibV4 = getFibV4(fibsMap);
  auto fibV6 = getFibV6(fibsMap);

  RouteNextHopSet nhops1 = {nh1, nh2};
  RouteNextHopSet nhops2 = {nh2, nh3};

  // Create V4 routes using setId1
  addV4RouteWithSetId(fibV4, "10.0.0.0/24", nhops1, setId1);
  addV4RouteWithSetId(fibV4, "10.1.0.0/24", nhops1, setId1);

  // Create V6 routes using setId2
  addV6RouteWithSetId(fibV6, "2001:db8::/32", nhops2, setId2);
  addV6RouteWithSetId(fibV6, "2001:db8:1::/48", nhops2, setId2);

  // Create MultiSwitchFibInfoMap with ID maps and FIBs
  auto multiSwitchFibInfoMap =
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap);

  // Call reconstructFromSwitchStateMaps with no MySid map (FIB-only case)
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, nullptr, nullptr, nullptr);

  // Verify maps are populated
  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId2).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId3).nextHop, nh3);

  NextHopIDSet expectedSet1 = {nhId1, nhId2};
  NextHopIDSet expectedSet2 = {nhId2, nhId3};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expectedSet1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId2), expectedSet2);

  // Verify ref counts are correct
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 2);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet2), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 4);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 2);

  // Verify next available IDs are set correctly
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);
  auto newNhId = manager_->getOrAllocateNextHopID(nh4)->second;
  EXPECT_EQ(newNhId, NextHopID(4));

  NextHopIDSet newSet = {nhId1};
  auto newSetIter = manager_->getOrAllocateNextHopSetID(newSet);
  EXPECT_EQ(newSetIter->second.id, NextHopSetID(kSetIdOffset + 3));
}

// Simulates a cross-version warm boot where the persisted state was written by
// a build that distinguished two nexthops this build treats as equal (e.g. they
// differ only by a field this build ignores), so the same NextHop appears under
// two different persisted NextHopIDs. Here ids {1,4} both map to nh1 and {2,5}
// both map to nh2:
//   SID1 -> {1,2,3}   (route R1)
//   SID2 -> {4,5,6}   (route R2)
// On reconstruct the duplicate ids 4,5 dedup onto 1,2, so SID2's
// primary membership becomes {1,2,6} -- different from what was persisted
// under SID2, but NOT equal to SID1's {1,2,3} (nh3 != nh6), so the two sets do
// NOT collapse. Because a SetID's membership is immutable, SID2 must not be
// rebound to {1,2,6}: reconstruct mints a FRESH SetID for {1,2,6}, retires
// SID2, and records SID2 -> fresh in the remap so callers repoint routes.
TEST_F(NextHopIDManagerTest, reconstructMemberDedupMintsFreshSetId) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh6 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.6", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);
  NextHopID nhId4 = NextHopID(4);
  NextHopID nhId5 = NextHopID(5);
  NextHopID nhId6 = NextHopID(6);

  // ids 4,5 are duplicates of 1,2 (same NextHop after this build's fromThrift).
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId3), nh3.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId4), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId5), nh2.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId6), nh6.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);
  NextHopSetID setId2 = NextHopSetID(kSetIdOffset + 2);
  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2),
      static_cast<state::NextHopIdType>(nhId3)};
  std::set<state::NextHopIdType> set2{
      static_cast<state::NextHopIdType>(nhId4),
      static_cast<state::NextHopIdType>(nhId5),
      static_cast<state::NextHopIdType>(nhId6)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2), set2);

  auto fibsMap = createFibsMap();
  auto fibV4 = getFibV4(fibsMap);
  addV4RouteWithSetId(fibV4, "10.0.0.0/24", {nh1, nh2, nh3}, setId1);
  addV4RouteWithSetId(fibV4, "10.1.0.0/24", {nh1, nh2, nh6}, setId2);

  auto multiSwitchFibInfoMap =
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap);
  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap,
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  // Duplicate ids 4,5 reclaimed: only 1,2,3,6 survive.
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId2).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId3).nextHop, nh3);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId6).nextHop, nh6);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhId4), 0);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhId5), 0);

  // SID2 was retired and remapped onto a fresh id minted above the highest
  // persisted SetID (setId2 == kSetIdOffset + 2 -> fresh == kSetIdOffset + 3).
  NextHopSetID freshSetId = NextHopSetID(kSetIdOffset + 3);
  EXPECT_EQ(remap.size(), 1);
  ASSERT_EQ(remap.count(setId2), 1);
  EXPECT_EQ(remap.at(setId2), freshSetId);

  // SID1 keeps its own (unchanged) members; SID2 is gone; fresh holds {1,2,6}.
  const auto& idToSet = manager_->getIdToNextHopIdSet();
  EXPECT_EQ(idToSet.size(), 2);
  EXPECT_EQ(idToSet.count(setId2), 0);
  NextHopIDSet expectedSet1 = {nhId1, nhId2, nhId3};
  NextHopIDSet expectedFreshSet = {nhId1, nhId2, nhId6};
  EXPECT_EQ(idToSet.at(setId1), expectedSet1);
  EXPECT_EQ(idToSet.at(freshSetId), expectedFreshSet);

  // No set references a member absent from idToNextHop_ (the invariant the
  // dedup protects).
  for (const auto& [_setId, members] : idToSet) {
    for (const auto& member : members) {
      EXPECT_EQ(manager_->getIdToNextHop().count(member), 1);
    }
  }

  // Refcounts: nh1/nh2 referenced by both sets, nh3/nh6 by one each.
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedFreshSet), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh6), 1);

  // Runtime allocation resumes above every minted/persisted SetID.
  auto newSetIter = manager_->getOrAllocateNextHopSetID(NextHopIDSet{nhId3});
  EXPECT_EQ(newSetIter->second.id, NextHopSetID(kSetIdOffset + 4));
}

TEST_F(NextHopIDManagerTest, idTypeRanges) {
  // NextHopSetIDs predate the type nibble; persisted ids must stay in range.
  EXPECT_EQ(nextHopIdTypeStart(NextHopIdType::NEXTHOP_SET), 1LL << 62);
  EXPECT_EQ(nextHopIdTypeEnd(NextHopIdType::NEXTHOP), 1LL << 60);
  EXPECT_EQ(
      nextHopIdTypeEnd(NextHopIdType::NEXTHOP_SET), (1LL << 62) + (1LL << 60));

  EXPECT_EQ(nextHopIdType(1), NextHopIdType::NEXTHOP);
  EXPECT_EQ(nextHopIdType((1LL << 60) - 1), NextHopIdType::NEXTHOP);
  EXPECT_NE(nextHopIdType(1LL << 60), NextHopIdType::NEXTHOP);

  EXPECT_NE(nextHopIdType((1LL << 62) - 1), NextHopIdType::NEXTHOP_SET);
  EXPECT_EQ(nextHopIdType(1LL << 62), NextHopIdType::NEXTHOP_SET);
  EXPECT_EQ(
      nextHopIdType((1LL << 62) + (1LL << 60) - 1), NextHopIdType::NEXTHOP_SET);
  EXPECT_NE(
      nextHopIdType((1LL << 62) + (1LL << 60)), NextHopIdType::NEXTHOP_SET);
}

TEST_F(NextHopIDManagerTest, reconstructRejectsOutOfRangeIds) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  auto reconstruct = [&](int64_t nhId, int64_t setId) {
    auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
    idToNextHopMap->addNextHop(nhId, nh1.toThrift());
    auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
    idToNextHopIdSetMap->addNextHopIdSet(
        setId, std::set<state::NextHopIdType>{nhId});
    auto fibsMap = createFibsMap();
    addV4RouteWithSetId(
        getFibV4(fibsMap), "10.0.0.0/24", {nh1}, NextHopSetID(setId));
    NextHopIDManager manager;
    manager.reconstructFromSwitchStateMaps(
        createMultiSwitchFibInfoMap(
            fibsMap, idToNextHopMap, idToNextHopIdSetMap),
        nullptr,
        nullptr,
        nullptr);
  };

  EXPECT_NO_THROW(reconstruct(1, 1LL << 62));
  EXPECT_THROW(reconstruct(1, (1LL << 62) + (1LL << 60)), FbossError);
  EXPECT_THROW(reconstruct(1LL << 60, 1LL << 62), FbossError);
}

// Verifies that reconstructFromSwitchStateMaps rebuilds per-client
// clientNextHopSetID refcounts from BOTH sources of truth:
//   (a) resolved routes carried in the FIB (the per-client entry walk
//       inside the FIB pass), and
//   (b) unresolved routes carried only in the RIB (the unresolved-RIB
//       pass, fed by the RibRouteTables* the manager receives).
//
// Setup: 4 nexthops, 5 distinct setIds with distinct underlying NextHopIDSets
// so the manager doesn't dedup them into one bucket:
//   setIdA -> {nhId1, nhId2}   shared by 2 resolved routes (V4 + V6)
//   setIdB -> {nhId1, nhId3}   on 1 resolved V4
//   setIdC -> {nhId1, nhId4}   on 1 resolved V6
//   setIdD -> {nhId2, nhId3}   on 1 unresolved V4 (RIB only)
//   setIdE -> {nhId2, nhId4}   on 1 unresolved V6 (RIB only)
//
// Expected after reconstruct:
//   set refcounts: A=2, B=1, C=1, D=1, E=1
//   nh refcounts:  nh1=4 (A*2+B+C), nh2=4 (A*2+D+E), nh3=2 (B+D), nh4=2 (C+E)
TEST_F(NextHopIDManagerTest, reconstructFromSwitchStateMapsClientNextHopSetID) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);
  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);
  NextHopID nhId4 = NextHopID(4);
  NextHopSetID setIdA = NextHopSetID(kSetIdOffset + 1);
  NextHopSetID setIdB = NextHopSetID(kSetIdOffset + 2);
  NextHopSetID setIdC = NextHopSetID(kSetIdOffset + 3);
  NextHopSetID setIdD = NextHopSetID(kSetIdOffset + 4);
  NextHopSetID setIdE = NextHopSetID(kSetIdOffset + 5);

  // FibInfo id maps must know every setId we reference (including the
  // unresolved-RIB ones), because reconstruct looks up nh members through
  // fibId2NhopIdSetMap regardless of which walk discovered the setId.
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {nhId1, nh1}, {nhId2, nh2}, {nhId3, nh3}, {nhId4, nh4}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  auto makeIdSet = [](std::initializer_list<NextHopID> ids) {
    std::set<state::NextHopIdType> s;
    for (auto id : ids) {
      s.insert(static_cast<state::NextHopIdType>(id));
    }
    return s;
  };
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdA), makeIdSet({nhId1, nhId2}));
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdB), makeIdSet({nhId1, nhId3}));
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdC), makeIdSet({nhId1, nhId4}));
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdD), makeIdSet({nhId2, nhId3}));
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdE), makeIdSet({nhId2, nhId4}));

  // FIB: 4 resolved routes with per-client clientNextHopSetIDs. The route's
  // own `nhops` parameter is irrelevant for refcount reconstruction — only
  // the clientNextHopSetID matters (it's the key into idToNextHopIdSetMap).
  auto fibsMap = createFibsMap();
  auto fibV4 = getFibV4(fibsMap);
  auto fibV6 = getFibV6(fibsMap);
  RouteNextHopSet placeholderNhops = {nh1, nh2};
  addV4RouteWithClientId(
      fibV4, "10.0.0.0/24", placeholderNhops, ClientID::BGPD, setIdA);
  addV4RouteWithClientId(
      fibV4, "10.0.1.0/24", placeholderNhops, ClientID::BGPD, setIdB);
  addV6RouteWithClientId(
      fibV6, "2001:db8::/32", placeholderNhops, ClientID::BGPD, setIdA);
  addV6RouteWithClientId(
      fibV6, "2001:db9::/32", placeholderNhops, ClientID::BGPD, setIdC);
  auto fibInfoMap =
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap);

  // RIB: 2 unresolved routes (V4 + V6), each with a per-client entry that
  // carries a distinct setId. Built directly via thrift so fromThrift drives
  // the unresolved-RIB walk on reconstruct.
  auto makePerClient = [&](NextHopSetID setId) {
    state::RouteNextHopEntry e;
    *e.adminDistance() = AdminDistance::EBGP;
    *e.action() = RouteForwardAction::NEXTHOPS;
    *e.nexthops() = util::fromRouteNextHopSet(placeholderNhops);
    e.clientNextHopSetID() = static_cast<int64_t>(setId);
    return e;
  };
  state::RouteFields v4Unresolved;
  *v4Unresolved.prefix() = makePrefixV4("20.0.0.0/24").toThrift();
  *v4Unresolved.flags() = 0;
  v4Unresolved.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, makePerClient(setIdD));
  state::RouteFields v6Unresolved;
  *v6Unresolved.prefix() = makePrefixV6("2001:dba::/32").toThrift();
  *v6Unresolved.flags() = 0;
  v6Unresolved.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, makePerClient(setIdE));

  state::RouteTableFields vrfTable;
  vrfTable.v4NetworkToRoute()->emplace("20.0.0.0/24", v4Unresolved);
  vrfTable.v6NetworkToRoute()->emplace("2001:dba::/32", v6Unresolved);
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, vrfTable);

  // Drives the full reconstruct path (FIB walk + unresolved-RIB walk).
  auto rib = RoutingInformationBase::fromThrift(
      ribThrift,
      fibInfoMap,
      std::make_shared<MultiLabelForwardingInformationBase>(),
      std::make_shared<MultiSwitchMySidMap>(),
      kEcmpWidth,
      nullptr);
  auto manager = rib->getNextHopIDManagerCopy();
  ASSERT_NE(manager, nullptr);

  // All 4 nexthops and all 5 sets must be present.
  EXPECT_EQ(manager->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager->getIdToNextHopIdSet().size(), 5);

  NextHopIDSet setA = {nhId1, nhId2};
  NextHopIDSet setB = {nhId1, nhId3};
  NextHopIDSet setC = {nhId1, nhId4};
  NextHopIDSet setD = {nhId2, nhId3};
  NextHopIDSet setE = {nhId2, nhId4};
  EXPECT_EQ(manager->getIdToNextHopIdSet().at(setIdA), setA);
  EXPECT_EQ(manager->getIdToNextHopIdSet().at(setIdB), setB);
  EXPECT_EQ(manager->getIdToNextHopIdSet().at(setIdC), setC);
  EXPECT_EQ(manager->getIdToNextHopIdSet().at(setIdD), setD);
  EXPECT_EQ(manager->getIdToNextHopIdSet().at(setIdE), setE);

  // Set refcounts:
  //   Each FIB route contributes 3 bumps to its setId (resolved +
  //   normalized + per-client). Each unresolved RIB route contributes 1
  //   bump (per-client only). A is on 2 FIB routes (V4+V6), so A=6.
  //   B, C are each on 1 FIB route, so each=3. D, E are each on 1
  //   unresolved RIB route, so each=1.
  EXPECT_EQ(manager->getNextHopIDSetRefCount(setA), 6);
  EXPECT_EQ(manager->getNextHopIDSetRefCount(setB), 3);
  EXPECT_EQ(manager->getNextHopIDSetRefCount(setC), 3);
  EXPECT_EQ(manager->getNextHopIDSetRefCount(setD), 1);
  EXPECT_EQ(manager->getNextHopIDSetRefCount(setE), 1);

  // Per-nexthop refcounts: each set-ref bumps every member nh once.
  //   nh1: in A(*6) + B(*3) + C(*3) = 12
  //   nh2: in A(*6) + D(*1) + E(*1) = 8
  //   nh3: in B(*3) + D(*1) = 4
  //   nh4: in C(*3) + E(*1) = 4
  EXPECT_EQ(manager->getNextHopRefCount(nh1), 12);
  EXPECT_EQ(manager->getNextHopRefCount(nh2), 8);
  EXPECT_EQ(manager->getNextHopRefCount(nh3), 4);
  EXPECT_EQ(manager->getNextHopRefCount(nh4), 4);
}

// Member dedup driven by an UNRESOLVED route. The retired SetID is referenced
// only by an unresolved RIB route's per-client clientNextHopSetID, so the
// unresolved-RIB walk in reconstructFromSwitchStateMaps -- not the FIB walk --
// must trigger the dedup, mint the fresh SetID, and record the remap.
//
// Mirrors reconstructMemberDedupMintsFreshSetId, but setId2 lives on an
// unresolved RIB route (fed via the ribTables arg) instead of a resolved FIB
// route:
//   ids 4,5 are cross-version duplicates of 1,2 (same NextHop this build).
//   setId1 {1,2,3} on a resolved FIB route    -> binds primaries 1,2,3 first.
//   setId2 {4,5,6} on an unresolved RIB route  -> dedups to {1,2,6}, retired.
TEST_F(NextHopIDManagerTest, reconstructMemberDedupUnresolvedRoute) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh6 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.6", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);
  NextHopID nhId4 = NextHopID(4);
  NextHopID nhId5 = NextHopID(5);
  NextHopID nhId6 = NextHopID(6);

  // ids 4,5 are duplicates of 1,2 (same NextHop after this build's fromThrift).
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {nhId1, nh1},
           {nhId2, nh2},
           {nhId3, nh3},
           {nhId4, nh1},
           {nhId5, nh2},
           {nhId6, nh6}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  auto makeIdSet = [](std::initializer_list<NextHopID> ids) {
    std::set<state::NextHopIdType> s;
    for (auto id : ids) {
      s.insert(static_cast<state::NextHopIdType>(id));
    }
    return s;
  };
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);
  NextHopSetID setId2 = NextHopSetID(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      makeIdSet({nhId1, nhId2, nhId3}));
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      makeIdSet({nhId4, nhId5, nhId6}));

  // FIB: one resolved route on setId1 so primaries 1,2,3 are bound before the
  // unresolved-RIB walk processes setId2 (the FIB pass runs first).
  auto fibsMap = createFibsMap();
  auto fibV4 = getFibV4(fibsMap);
  addV4RouteWithSetId(fibV4, "10.0.0.0/24", {nh1, nh2, nh3}, setId1);
  auto multiSwitchFibInfoMap =
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap);

  // RIB: one unresolved V4 route whose per-client entry references setId2. The
  // single-arg fromThrift only deserializes route tables (no reconstruct, no
  // repoint), so setId2 reaches the unresolved-RIB walk unmodified.
  state::RouteNextHopEntry perClient;
  *perClient.adminDistance() = AdminDistance::EBGP;
  *perClient.action() = RouteForwardAction::NEXTHOPS;
  *perClient.nexthops() = util::fromRouteNextHopSet({nh1, nh2, nh6});
  perClient.clientNextHopSetID() = static_cast<int64_t>(setId2);
  state::RouteFields v4Unresolved;
  *v4Unresolved.prefix() = makePrefixV4("20.0.0.0/24").toThrift();
  *v4Unresolved.flags() = 0;
  v4Unresolved.nexthopsmulti()->client2NextHopEntry()->emplace(
      ClientID::BGPD, perClient);
  state::RouteTableFields vrfTable;
  vrfTable.v4NetworkToRoute()->emplace("20.0.0.0/24", v4Unresolved);
  std::map<int32_t, state::RouteTableFields> ribThrift;
  ribThrift.emplace(0, vrfTable);
  auto ribTables = RibRouteTables::fromThrift(ribThrift, kEcmpWidth);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap,
      nullptr,
      nullptr,
      &ribTables,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  // Duplicate ids 4,5 reclaimed: only 1,2,3,6 survive.
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId2).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId3).nextHop, nh3);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId6).nextHop, nh6);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhId4), 0);
  EXPECT_EQ(manager_->getIdToNextHop().count(nhId5), 0);

  // setId2 (referenced only by the unresolved route) was retired and remapped
  // onto a fresh id minted above the highest persisted SetID (setId2 ==
  // kSetIdOffset + 2 -> fresh == kSetIdOffset + 3).
  NextHopSetID freshSetId = NextHopSetID(kSetIdOffset + 3);
  EXPECT_EQ(remap.size(), 1);
  ASSERT_EQ(remap.count(setId2), 1);
  EXPECT_EQ(remap.at(setId2), freshSetId);

  // SID1 keeps its own (unchanged) members; SID2 is gone; fresh holds {1,2,6}.
  const auto& idToSet = manager_->getIdToNextHopIdSet();
  EXPECT_EQ(idToSet.size(), 2);
  EXPECT_EQ(idToSet.count(setId2), 0);
  NextHopIDSet expectedSet1 = {nhId1, nhId2, nhId3};
  NextHopIDSet expectedFreshSet = {nhId1, nhId2, nhId6};
  EXPECT_EQ(idToSet.at(setId1), expectedSet1);
  EXPECT_EQ(idToSet.at(freshSetId), expectedFreshSet);

  // No set references a member absent from idToNextHop_.
  for (const auto& [_setId, members] : idToSet) {
    for (const auto& member : members) {
      EXPECT_EQ(manager_->getIdToNextHop().count(member), 1);
    }
  }

  // setId1 referenced by the FIB route, the fresh set by the unresolved route.
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedFreshSet), 1);
  // nh1/nh2 are shared (setId1 + fresh); nh3 only in setId1; nh6 only in fresh.
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh6), 1);
}

// This tests reconstructFromFib with multiple switches.
// In a multi-switch environment, each switch has its own FibInfo with its own
// routes and ID maps, but the NextHopIDManager is common across all switches.
// The reconstruction should consolidate maps and refcounts from ALL switches.
//
// Test setup with partial overlap across switches:
//   NextHops: nh1, nh2, nh3, nh4
//   Switch 0: setId1 = {nh1, nh2} (2 routes), setId2 = {nh2, nh3} (1 route)
//   Switch 1: setId2 = {nh2, nh3} (2 routes), setId3 = {nh3, nh4} (1 route)
//
// Expected refcounts:
//   nh1: 2 (from setId1 x2 on switch0)
//   nh2: 5 (from setId1 x2 on switch0 + setId2 x1 on switch0 + setId2 x2 on
//   switch1) nh3: 4 (from setId2 x1 on switch0 + setId2 x2 on switch1 + setId3
//   x1 on switch1) nh4: 1 (from setId3 x1 on switch1)
TEST_F(NextHopIDManagerTest, reconstructFromSwitchStateMapsMultiSwitch) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.4", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);
  NextHopID nhId4 = NextHopID(4);

  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);
  NextHopSetID setId2 = NextHopSetID(kSetIdOffset + 2);
  NextHopSetID setId3 = NextHopSetID(kSetIdOffset + 3);

  // In production, all switches share the same complete id maps (synced from
  // the global RIB). Build one complete map covering all nexthops and sets.
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId3), nh3.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId4), nh4.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  std::set<state::NextHopIdType> set2{
      static_cast<state::NextHopIdType>(nhId2),
      static_cast<state::NextHopIdType>(nhId3)};
  std::set<state::NextHopIdType> set3{
      static_cast<state::NextHopIdType>(nhId3),
      static_cast<state::NextHopIdType>(nhId4)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2), set2);
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId3), set3);

  // Create FIB for switch 0
  auto fibsMapSwitch0 = createFibsMap();
  auto fibV4Switch0 = getFibV4(fibsMapSwitch0);

  RouteNextHopSet nhops1 = {nh1, nh2};
  RouteNextHopSet nhops2 = {nh2, nh3};
  // 2 routes using setId1, 1 route using setId2
  addV4RouteWithSetId(fibV4Switch0, "10.0.0.0/24", nhops1, setId1);
  addV4RouteWithSetId(fibV4Switch0, "10.1.0.0/24", nhops1, setId1);
  addV4RouteWithSetId(fibV4Switch0, "10.2.0.0/24", nhops2, setId2);

  // Create FIB for switch 1
  auto fibsMapSwitch1 = createFibsMap();
  auto fibV4Switch1 = getFibV4(fibsMapSwitch1);
  auto fibV6Switch1 = getFibV6(fibsMapSwitch1);

  RouteNextHopSet nhops3 = {nh3, nh4};
  // 2 routes using setId2, 1 route using setId3
  addV4RouteWithSetId(fibV4Switch1, "10.3.0.0/24", nhops2, setId2);
  addV6RouteWithSetId(fibV6Switch1, "2001:db8::/32", nhops2, setId2);
  addV6RouteWithSetId(fibV6Switch1, "2001:db8:1::/48", nhops3, setId3);

  // ============ CREATE MULTI-SWITCH FIB INFO MAP ============
  // Both switches use the same complete id maps, as in production.
  auto multiSwitchFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();

  addFibInfoToMultiSwitchMap(
      multiSwitchFibInfoMap,
      "id=0",
      fibsMapSwitch0,
      idToNextHopMap,
      idToNextHopIdSetMap);

  addFibInfoToMultiSwitchMap(
      multiSwitchFibInfoMap,
      "id=1",
      fibsMapSwitch1,
      idToNextHopMap,
      idToNextHopIdSetMap);

  EXPECT_EQ(multiSwitchFibInfoMap->size(), 2);

  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, nullptr, nullptr, nullptr);

  // Verify all 4 NextHops are in idToNextHop map
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId2).nextHop, nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId3).nextHop, nh3);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId4).nextHop, nh4);

  // Verify all 3 NextHopIDSets are in idToNextHopIdSet map
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 3);

  NextHopIDSet expectedSet1 = {nhId1, nhId2};
  NextHopIDSet expectedSet2 = {nhId2, nhId3};
  NextHopIDSet expectedSet3 = {nhId3, nhId4};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expectedSet1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId2), expectedSet2);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId3), expectedSet3);

  // Verify NextHopIDSet ref counts
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 2);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet2), 3);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet3), 1);

  // Verify NextHop ref counts
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 5);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 4);
  EXPECT_EQ(manager_->getNextHopRefCount(nh4), 1);

  // Verify next available IDs are set correctly (max ID + 1)
  NextHop nh5 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.5", UCMP_DEFAULT_WEIGHT);
  auto newNhId = manager_->getOrAllocateNextHopID(nh5)->second;
  EXPECT_EQ(newNhId, NextHopID(5)); // max was 4, so next is 5

  NextHopIDSet newSet = {nhId1};
  auto newSetIter = manager_->getOrAllocateNextHopSetID(newSet);
  EXPECT_EQ(
      newSetIter->second.id,
      NextHopSetID(kSetIdOffset + 4)); // max was +3, so next is +4
}

TEST_F(NextHopIDManagerTest, Srv6NextHopGetDistinctIDs) {
  // Verify that SRv6 nexthops with different SRv6 fields get distinct IDs
  const std::vector<folly::IPAddressV6> segList1{
      folly::IPAddressV6("2001:db8::1"), folly::IPAddressV6("2001:db8::2")};
  const std::vector<folly::IPAddressV6> segList2{
      folly::IPAddressV6("2001:db8::3")};
  const std::vector<folly::IPAddressV6> segList3{
      folly::IPAddressV6("2001:db8::4")};

  // SRv6 nexthop with segList1
  NextHop srv6Nh1 = ResolvedNextHop(
      folly::IPAddress("10.0.0.1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt, // action
      std::nullopt, // disableTTLDecrement
      std::nullopt, // topologyInfo
      std::nullopt, // adjustedWeight
      segList1,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);

  // SRv6 nexthop with same IP but different segment list
  NextHop srv6Nh2 = ResolvedNextHop(
      folly::IPAddress("10.0.0.1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList2,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);

  // SRv6 nexthop with same IP but a third, distinct segment list
  NextHop srv6Nh3 = ResolvedNextHop(
      folly::IPAddress("10.0.0.1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList3,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);

  // Regular nexthop with same IP (no SRv6 fields)
  NextHop regularNh =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);

  // Allocate IDs for all nexthops
  auto id1 = manager_->getOrAllocateNextHopID(srv6Nh1)->second;
  auto id2 = manager_->getOrAllocateNextHopID(srv6Nh2)->second;
  auto id3 = manager_->getOrAllocateNextHopID(srv6Nh3)->second;
  auto id4 = manager_->getOrAllocateNextHopID(regularNh)->second;

  // All should get distinct IDs since SRv6 fields differ
  EXPECT_NE(id1, id2);
  EXPECT_NE(id1, id3);
  EXPECT_NE(id1, id4);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id2, id4);
  EXPECT_NE(id3, id4);

  // 4 distinct nexthops allocated
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);

  // Re-allocating same SRv6 nexthop returns same ID
  auto idDup = manager_->getOrAllocateNextHopID(srv6Nh1)->second;
  EXPECT_EQ(idDup, id1);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 4);

  // Verify idToNextHop map stores SRv6 nexthops correctly
  EXPECT_EQ(manager_->getIdToNextHop().at(id1).nextHop, srv6Nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(id2).nextHop, srv6Nh2);
  EXPECT_EQ(manager_->getIdToNextHop().at(id3).nextHop, srv6Nh3);
  EXPECT_EQ(manager_->getIdToNextHop().at(id4).nextHop, regularNh);
}

TEST_F(NextHopIDManagerTest, Srv6NextHopSetID) {
  // Verify that sets of SRv6 nexthops get proper set IDs
  const std::vector<folly::IPAddressV6> segList{
      folly::IPAddressV6("2001:db8::1")};

  NextHop srv6Nh1 = ResolvedNextHop(
      folly::IPAddress("10.0.0.1"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);

  NextHop srv6Nh2 = ResolvedNextHop(
      folly::IPAddress("10.0.0.2"),
      InterfaceID(1),
      UCMP_DEFAULT_WEIGHT,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      segList,
      TunnelType::SRV6_ENCAP,
      kSrv6Tunnel0);

  // Allocate individual NextHop IDs
  auto nhId1 = manager_->getOrAllocateNextHopID(srv6Nh1)->second;
  auto nhId2 = manager_->getOrAllocateNextHopID(srv6Nh2)->second;

  // Allocate a set containing both SRv6 nexthops
  NextHopIDSet srv6Set = {nhId1, nhId2};
  auto setIter = manager_->getOrAllocateNextHopSetID(srv6Set);
  auto setId = setIter->second.id;

  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId), srv6Set);

  // Re-allocating the same set returns the same ID
  auto setIterDup = manager_->getOrAllocateNextHopSetID(srv6Set);
  EXPECT_EQ(setIterDup->second.id, setId);
}
TEST_F(NextHopIDManagerTest, getNextHops) {
  // Test with resolved v4 nexthops
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);

  RouteNextHopSet nhSet1 = {nh1, nh2};
  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);
  NextHopSetID setID1 = result1.nextHopIdSetIter->second.id;

  auto retrievedSet = manager_->getNextHops(setID1);
  EXPECT_EQ(retrievedSet, nhSet1);

  // Test with unresolved v6 nexthops
  NextHop unh1 = UnresolvedNextHop(folly::IPAddress("2001:db8::1"), 1);
  NextHop unh2 = UnresolvedNextHop(folly::IPAddress("2001:db8::2"), 1);

  RouteNextHopSet nhSet2 = {unh1, unh2};
  auto result2 = manager_->getOrAllocRouteNextHopSetID(nhSet2);
  NextHopSetID setID2 = result2.nextHopIdSetIter->second.id;

  auto retrievedSet2 = manager_->getNextHops(setID2);
  EXPECT_EQ(retrievedSet2, nhSet2);

  // Test with mixed resolved v4, resolved v6, and unresolved nexthops
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "2001:db8::3", UCMP_DEFAULT_WEIGHT);
  NextHop unh3 = UnresolvedNextHop(folly::IPAddress("10.1.0.3"), 1);

  RouteNextHopSet nhSet3 = {nh3, unh3};
  auto result3 = manager_->getOrAllocRouteNextHopSetID(nhSet3);
  NextHopSetID setID3 = result3.nextHopIdSetIter->second.id;

  auto retrievedSet3 = manager_->getNextHops(setID3);
  EXPECT_EQ(retrievedSet3, nhSet3);

  // getNextHops with non-existent ID should throw
  NextHopSetID nonExistentID = NextHopSetID(999999);
  EXPECT_THROW(manager_->getNextHops(nonExistentID), FbossError);
}

TEST_F(NextHopIDManagerTest, getNextHopsIf) {
  // Test with resolved v4 nexthops
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);

  RouteNextHopSet nhSet1 = {nh1, nh2};
  auto result1 = manager_->getOrAllocRouteNextHopSetID(nhSet1);
  NextHopSetID setID1 = result1.nextHopIdSetIter->second.id;

  auto retrievedOpt1 = manager_->getNextHopsIf(setID1);
  ASSERT_TRUE(retrievedOpt1.has_value());
  EXPECT_EQ(*retrievedOpt1, nhSet1);

  // Test with unresolved v6 nexthops
  NextHop unh1 = UnresolvedNextHop(folly::IPAddress("2001:db8::1"), 1);
  NextHop unh2 = UnresolvedNextHop(folly::IPAddress("2001:db8::2"), 1);

  RouteNextHopSet nhSet2 = {unh1, unh2};
  auto result2 = manager_->getOrAllocRouteNextHopSetID(nhSet2);
  NextHopSetID setID2 = result2.nextHopIdSetIter->second.id;

  auto retrievedOpt2 = manager_->getNextHopsIf(setID2);
  ASSERT_TRUE(retrievedOpt2.has_value());
  EXPECT_EQ(*retrievedOpt2, nhSet2);

  // Test with resolved v6 nexthops
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "2001:db8::3", UCMP_DEFAULT_WEIGHT);
  NextHop nh4 =
      makeResolvedNextHop(InterfaceID(1), "2001:db8::4", UCMP_DEFAULT_WEIGHT);

  RouteNextHopSet nhSet3 = {nh3, nh4};
  auto result3 = manager_->getOrAllocRouteNextHopSetID(nhSet3);
  NextHopSetID setID3 = result3.nextHopIdSetIter->second.id;

  auto retrievedOpt3 = manager_->getNextHopsIf(setID3);
  ASSERT_TRUE(retrievedOpt3.has_value());
  EXPECT_EQ(*retrievedOpt3, nhSet3);

  // getNextHopsIf with non-existent ID should return std::nullopt
  NextHopSetID nonExistentID = NextHopSetID(999999);
  auto notFoundOpt = manager_->getNextHopsIf(nonExistentID);
  EXPECT_FALSE(notFoundOpt.has_value());
}

TEST_F(NextHopIDManagerTest, allocateNamedNextHopGroup) {
  // Test basic allocation of named next-hop groups
  RouteNextHopSet nhSet1;
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));

  // Allocate a new named next-hop group
  auto result1 = manager_->allocateNamedNextHopGroup("group1", nhSet1);
  EXPECT_TRUE(result1.isNew);
  EXPECT_EQ(result1.name, "group1");
  EXPECT_EQ(result1.allocation.addedNextHopIds.size(), 2);

  // Verify the group exists and can be looked up
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group1"));
  auto setIdOpt = manager_->getNextHopSetIDForName("group1");
  ASSERT_TRUE(setIdOpt.has_value());
  EXPECT_EQ(*setIdOpt, result1.allocation.nextHopIdSetIter->second.id);

  // Verify the nexthops can be retrieved
  auto nextHopsOpt = manager_->getNextHopsForName("group1");
  ASSERT_TRUE(nextHopsOpt.has_value());
  EXPECT_EQ(*nextHopsOpt, nhSet1);

  // Allocate another group with different nexthops
  RouteNextHopSet nhSet2;
  nhSet2.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.3"), InterfaceID(3), 1));

  auto result2 = manager_->allocateNamedNextHopGroup("group2", nhSet2);
  EXPECT_TRUE(result2.isNew);
  EXPECT_EQ(result2.name, "group2");

  // Verify both groups exist
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group1"));
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group2"));
  EXPECT_EQ(manager_->getNameToNextHopSetMap().size(), 2);

  // Allocate an existing group with different nexthops (should update)
  RouteNextHopSet nhSet3;
  nhSet3.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.4"), InterfaceID(4), 1));

  auto result3 = manager_->allocateNamedNextHopGroup("group1", nhSet3);
  EXPECT_FALSE(result3.isNew); // existing group
  EXPECT_EQ(result3.name, "group1");

  // Verify the group was updated with new nexthops
  nextHopsOpt = manager_->getNextHopsForName("group1");
  ASSERT_TRUE(nextHopsOpt.has_value());
  EXPECT_EQ(*nextHopsOpt, nhSet3);
}

TEST_F(NextHopIDManagerTest, updateNamedNextHopGroup) {
  // First allocate a named next-hop group
  RouteNextHopSet nhSet1;
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));

  manager_->allocateNamedNextHopGroup("group1", nhSet1);

  // Update the group with new nexthops
  RouteNextHopSet nhSet2;
  nhSet2.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.3"), InterfaceID(3), 1));
  nhSet2.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.4"), InterfaceID(4), 1));
  nhSet2.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.5"), InterfaceID(5), 1));

  auto updateResult = manager_->updateNamedNextHopGroup("group1", nhSet2);
  EXPECT_EQ(updateResult.name, "group1");

  // Verify the nexthops were updated
  auto nextHopsOpt = manager_->getNextHopsForName("group1");
  ASSERT_TRUE(nextHopsOpt.has_value());
  EXPECT_EQ(*nextHopsOpt, nhSet2);

  // Verify the old nexthops were deallocated
  EXPECT_EQ(updateResult.deallocation.removedNextHopIds.size(), 2);

  // Try to update a non-existent group - should throw
  RouteNextHopSet nhSet3;
  nhSet3.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.6"), InterfaceID(6), 1));

  EXPECT_THROW(
      manager_->updateNamedNextHopGroup("non_existent_group", nhSet3),
      FbossError);
}

TEST_F(NextHopIDManagerTest, deallocateNamedNextHopGroup) {
  // Allocate some named next-hop groups
  RouteNextHopSet nhSet1;
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));

  RouteNextHopSet nhSet2;
  nhSet2.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));

  manager_->allocateNamedNextHopGroup("group1", nhSet1);
  manager_->allocateNamedNextHopGroup("group2", nhSet2);

  EXPECT_EQ(manager_->getNameToNextHopSetMap().size(), 2);
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group1"));
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group2"));

  // Deallocate group1
  auto deallocResult = manager_->deallocateNamedNextHopGroup("group1");

  // Verify group1 no longer exists
  EXPECT_FALSE(manager_->hasNamedNextHopGroup("group1"));
  EXPECT_FALSE(manager_->getNextHopSetIDForName("group1").has_value());
  EXPECT_FALSE(manager_->getNextHopsForName("group1").has_value());

  // Verify group2 still exists
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("group2"));
  EXPECT_EQ(manager_->getNameToNextHopSetMap().size(), 1);

  // Try to deallocate a non-existent group - should throw
  EXPECT_THROW(
      manager_->deallocateNamedNextHopGroup("non_existent_group"), FbossError);
}

TEST_F(NextHopIDManagerTest, namedNextHopGroupWarmBoot) {
  // Test that named next-hop groups work with warm boot reconstruction
  // For now this is a placeholder - full warm boot test requires
  // properly constructing FibInfo with named group mappings
  RouteNextHopSet nhSet1;
  nhSet1.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));

  auto result = manager_->allocateNamedNextHopGroup("testGroup", nhSet1);
  EXPECT_TRUE(result.isNew);
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("testGroup"));

  auto setId = manager_->getNextHopSetIDForName("testGroup");
  ASSERT_TRUE(setId.has_value());

  auto nhOpt = manager_->getNextHopsForName("testGroup");
  ASSERT_TRUE(nhOpt.has_value());
  EXPECT_EQ(*nhOpt, nhSet1);
}

// Warm boot restores a named NHG's normalized id from the policies naming it,
// not by re-normalizing.
TEST_F(NextHopIDManagerTest, namedNextHopGroupNormalizedIdSurvivesWarmBoot) {
  RouteNextHopSet nhSet;
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));
  manager_->allocateNamedNextHopGroup("warmGroup", nhSet);
  ClassBasedPolicy prebootPolicy;
  prebootPolicy.name() = "warmPolicy";
  prebootPolicy.defaultNexthopGroup() = "warmGroup";
  manager_->addOrUpdatePolicy(prebootPolicy);
  // These next hops already carry an interface, so RIB resolution passes them
  // through untouched.
  manager_->updateNormalizedNamedNhgId("warmGroup", nhSet, kEcmpWidth);

  const auto rawId = manager_->getNextHopSetIDForName("warmGroup");
  const auto normalizedId =
      manager_->getNormalizedNextHopSetIDForPolicyNhg("warmGroup");
  ASSERT_TRUE(rawId.has_value());
  ASSERT_TRUE(normalizedId.has_value());

  // Persist what the pre-boot manager published, then reconstruct from it.
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, entry] : manager_->getIdToNextHop()) {
    idToNextHopMap->addNextHop(
        static_cast<NextHopId>(id), entry.nextHop.toThrift());
  }
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  for (const auto& [id, idSet] : manager_->getIdToNextHopIdSet()) {
    std::set<NextHopId> ids;
    for (auto nhId : idSet) {
      ids.insert(static_cast<NextHopId>(nhId));
    }
    idToNextHopIdSetMap->addNextHopIdSet(static_cast<NextHopSetId>(id), ids);
  }
  auto fibInfoMap =
      createMultiSwitchFibInfoMap(nullptr, idToNextHopMap, idToNextHopIdSetMap);
  auto fibInfo = fibInfoMap->getNodeIf("id=0");
  ASSERT_NE(fibInfo, nullptr);
  fibInfo->setNameToNextHopSetId(
      {{"warmGroup", static_cast<NextHopSetId>(*rawId)}});

  state::ClassBasedPolicyFields policyFields;
  policyFields.name() = "warmPolicy";
  policyFields.defaultNextHopGroup()->name() = "warmGroup";
  policyFields.defaultNextHopGroup()->id() =
      static_cast<int64_t>(*normalizedId);
  policyFields.referenced() = true;
  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  policyMap->addPolicy(std::make_shared<ClassBasedPolicyNode>(policyFields));
  auto policyMaps = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  policyMaps->addMapNode(policyMap, HwSwitchMatcher("id=0"));

  NextHopIDManager rebooted;
  rebooted.reconstructFromSwitchStateMaps(
      fibInfoMap, nullptr, nullptr, nullptr, policyMaps);

  EXPECT_EQ(rebooted.getNextHopSetIDForName("warmGroup"), rawId);
  EXPECT_EQ(
      rebooted.getNormalizedNextHopSetIDForPolicyNhg("warmGroup"),
      normalizedId);
}

// Reconstruction has to re-establish one reference per policy: registering a
// shared name once would underflow the refcount when the later policies go.
TEST_F(NextHopIDManagerTest, warmBootRefcountsSharedNamedNhgOncePerPolicy) {
  RouteNextHopSet nhSet;
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  manager_->allocateNamedNextHopGroup("sharedGroup", nhSet);

  const std::vector<std::string> policyNames{"polA", "polB", "polC"};
  for (const auto& name : policyNames) {
    ClassBasedPolicy policy;
    policy.name() = name;
    policy.defaultNexthopGroup() = "sharedGroup";
    manager_->addOrUpdatePolicy(policy);
  }
  manager_->updateNormalizedNamedNhgId("sharedGroup", nhSet, kEcmpWidth);

  const auto rawId = manager_->getNextHopSetIDForName("sharedGroup");
  const auto normalizedId =
      manager_->getNormalizedNextHopSetIDForPolicyNhg("sharedGroup");
  ASSERT_TRUE(rawId.has_value());
  ASSERT_TRUE(normalizedId.has_value());

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, entry] : manager_->getIdToNextHop()) {
    idToNextHopMap->addNextHop(
        static_cast<NextHopId>(id), entry.nextHop.toThrift());
  }
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  for (const auto& [id, idSet] : manager_->getIdToNextHopIdSet()) {
    std::set<NextHopId> ids;
    for (auto nhId : idSet) {
      ids.insert(static_cast<NextHopId>(nhId));
    }
    idToNextHopIdSetMap->addNextHopIdSet(static_cast<NextHopSetId>(id), ids);
  }
  auto fibInfoMap =
      createMultiSwitchFibInfoMap(nullptr, idToNextHopMap, idToNextHopIdSetMap);
  auto fibInfo = fibInfoMap->getNodeIf("id=0");
  ASSERT_NE(fibInfo, nullptr);
  fibInfo->setNameToNextHopSetId(
      {{"sharedGroup", static_cast<NextHopSetId>(*rawId)}});

  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  for (const auto& name : policyNames) {
    state::ClassBasedPolicyFields fields;
    fields.name() = name;
    fields.defaultNextHopGroup()->name() = "sharedGroup";
    fields.defaultNextHopGroup()->id() = static_cast<int64_t>(*normalizedId);
    fields.referenced() = true;
    policyMap->addPolicy(std::make_shared<ClassBasedPolicyNode>(fields));
  }
  auto policyMaps = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  policyMaps->addMapNode(policyMap, HwSwitchMatcher("id=0"));

  NextHopIDManager rebooted;
  rebooted.reconstructFromSwitchStateMaps(
      fibInfoMap, nullptr, nullptr, nullptr, policyMaps);
  EXPECT_EQ(
      rebooted.getNormalizedNextHopSetIDForPolicyNhg("sharedGroup"),
      normalizedId);

  // Dropping all three must not underflow, and must leave nothing behind once
  // the raw reference goes too.
  for (const auto& name : policyNames) {
    rebooted.removePolicy(name);
  }
  rebooted.deallocateNamedNextHopGroup("sharedGroup");
  EXPECT_EQ(rebooted.getIdToNextHopIdSet().count(*normalizedId), 0);
}

TEST_F(NextHopIDManagerTest, namedNextHopGroupSharesSetIdWithRoutes) {
  // Test that named next-hop groups and routes share the same NextHopSetID
  // when they have the same nexthops (via reference counting)

  RouteNextHopSet nhSet;
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));

  // First, allocate via route API (simulates addRoute)
  auto routeAllocResult = manager_->getOrAllocRouteNextHopSetID(nhSet);
  auto routeSetId = routeAllocResult.nextHopIdSetIter->second.id;
  EXPECT_EQ(routeAllocResult.addedNextHopIds.size(), 2); // 2 new nexthops

  // Now allocate a named next-hop group with the same nexthops
  auto namedGroupResult =
      manager_->allocateNamedNextHopGroup("sharedGroup", nhSet);

  // The named group should reuse the same NextHopSetID
  EXPECT_EQ(
      namedGroupResult.allocation.nextHopIdSetIter->second.id, routeSetId);
  // No new nexthops should be added (they're already allocated)
  EXPECT_EQ(namedGroupResult.allocation.addedNextHopIds.size(), 0);

  // Verify via lookup
  auto setIdForName = manager_->getNextHopSetIDForName("sharedGroup");
  ASSERT_TRUE(setIdForName.has_value());
  EXPECT_EQ(*setIdForName, routeSetId);

  // Referenced twice: once by the route, once by the named group's raw set.
  // The normalized id is assigned by RIB resolution, which does not run here.
  EXPECT_EQ(
      manager_->getNextHopIDSetRefCount(
          manager_->idToNextHopIdSet_[routeSetId]),
      2);

  // Deallocate the route - named group should still have the set
  auto routeDeallocResult =
      manager_->decrOrDeallocRouteNextHopSetID(routeSetId);
  EXPECT_FALSE(routeDeallocResult.removedSetId.has_value()); // Not removed yet

  // Named group still has the set
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("sharedGroup"));
  EXPECT_EQ(manager_->getNextHopSetIDForName("sharedGroup"), routeSetId);

  // Deallocate the named group - now the set should be removed
  auto namedDeallocResult =
      manager_->deallocateNamedNextHopGroup("sharedGroup");
  EXPECT_TRUE(namedDeallocResult.removedSetId.has_value());
  EXPECT_EQ(*namedDeallocResult.removedSetId, routeSetId);
}

TEST_F(NextHopIDManagerTest, routeReusesNamedNextHopGroupSetId) {
  // Test the reverse: named group is created first, then route reuses it

  RouteNextHopSet nhSet;
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.1"), InterfaceID(1), 1));
  nhSet.emplace(
      ResolvedNextHop(folly::IPAddress("10.0.0.2"), InterfaceID(2), 1));

  // First, allocate a named next-hop group
  auto namedGroupResult = manager_->allocateNamedNextHopGroup("myGroup", nhSet);
  auto namedSetId = namedGroupResult.allocation.nextHopIdSetIter->second.id;
  EXPECT_TRUE(namedGroupResult.isNew);
  EXPECT_EQ(namedGroupResult.allocation.addedNextHopIds.size(), 2);

  // Now allocate via route API with the same nexthops
  auto routeAllocResult = manager_->getOrAllocRouteNextHopSetID(nhSet);

  // The route should reuse the same NextHopSetID
  EXPECT_EQ(routeAllocResult.nextHopIdSetIter->second.id, namedSetId);
  // No new nexthops should be added
  EXPECT_EQ(routeAllocResult.addedNextHopIds.size(), 0);

  // Referenced twice: once by the named group's raw set, once by the route.
  // The normalized id is assigned by RIB resolution, which does not run here.
  EXPECT_EQ(
      manager_->getNextHopIDSetRefCount(
          manager_->idToNextHopIdSet_[namedSetId]),
      2);
}
// Verify that reconstructFromSwitchStateMaps passes the assertNextHopIdMapsSame
// DCHECK when both switches carry identical id maps (the production invariant).
TEST_F(NextHopIDManagerTest, assertNextHopIdMapsSameCheck) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);

  // Build a single shared id map covering both nexthops and one set.
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);

  // Both switches share the same id maps — this is the production invariant.
  RouteNextHopSet nhops = {nh1, nh2};
  auto fibsMapSwitch0 = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMapSwitch0), "10.0.0.0/24", nhops, setId1);

  auto fibsMapSwitch1 = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMapSwitch1), "10.1.0.0/24", nhops, setId1);

  auto multiSwitchFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  addFibInfoToMultiSwitchMap(
      multiSwitchFibInfoMap,
      "id=0",
      fibsMapSwitch0,
      idToNextHopMap,
      idToNextHopIdSetMap);
  addFibInfoToMultiSwitchMap(
      multiSwitchFibInfoMap,
      "id=1",
      fibsMapSwitch1,
      idToNextHopMap,
      idToNextHopIdSetMap);

  // Reconstruction should succeed: identical maps pass the DCHECK.
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, nullptr, nullptr, nullptr);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId1).nextHop, nh1);
  EXPECT_EQ(manager_->getIdToNextHop().at(nhId2).nextHop, nh2);

  NextHopIDSet expectedSet1 = {nhId1, nhId2};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expectedSet1);
}

// Verify that lookupRouteNextHopSetID returns nullopt when any nexthop in the
// set has not been allocated, rather than crashing.
TEST_F(NextHopIDManagerTest, lookupRouteNextHopSetIDReturnsNulloptIfMissing) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nhUnknown =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.99", UCMP_DEFAULT_WEIGHT);

  // Allocate nh1 and nh2, but not nhUnknown.
  manager_->getOrAllocRouteNextHopSetID({nh1, nh2});

  // Looking up a set containing an unallocated nexthop returns nullopt.
  EXPECT_FALSE(manager_->lookupRouteNextHopSetID({nh1, nhUnknown}).has_value());

  // Looking up the fully allocated set succeeds.
  EXPECT_TRUE(manager_->lookupRouteNextHopSetID({nh1, nh2}).has_value());
}

// MySid entry with only resolvedNextHopsId set: its nexthop set must be
// registered even when no FIB routes reference it.
TEST_F(
    NextHopIDManagerTest,
    reconstructFromSwitchStateMaps_MySidResolvedNextHopsId) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);

  // Empty FIB — setId1 is referenced only by the MySid entry.
  auto multiSwitchFibInfoMap = createMultiSwitchFibInfoMap(
      createFibsMap(), idToNextHopMap, idToNextHopIdSetMap);

  auto mySid = makeMySidEntry(setId1, std::nullopt);
  auto mySidMap = makeMySidMap(mySid);

  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, mySidMap, nullptr, nullptr);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 2);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);

  const NextHopIDSet expectedSet1 = {nhId1, nhId2};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expectedSet1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 1);
}

// MySid entry with both resolvedNextHopsId and unresolveNextHopsId set: both
// nexthop sets must be registered even when no FIB routes reference them.
TEST_F(
    NextHopIDManagerTest,
    reconstructFromSwitchStateMaps_MySidBothNextHopIds) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopID nhId3 = NextHopID(3);
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1); // resolved
  NextHopSetID setId2 = NextHopSetID(kSetIdOffset + 2); // unresolved

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId3), nh3.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  std::set<state::NextHopIdType> set1{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  std::set<state::NextHopIdType> set2{static_cast<state::NextHopIdType>(nhId3)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), set1);
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2), set2);

  // Empty FIB — both sets are referenced only by the MySid entry.
  auto multiSwitchFibInfoMap = createMultiSwitchFibInfoMap(
      createFibsMap(), idToNextHopMap, idToNextHopIdSetMap);

  auto mySid = makeMySidEntry(setId1, setId2);
  auto mySidMap = makeMySidMap(mySid);

  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, mySidMap, nullptr, nullptr);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 3);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);

  const NextHopIDSet expectedSet1 = {nhId1, nhId2};
  const NextHopIDSet expectedSet2 = {nhId3};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expectedSet1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId2), expectedSet2);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet1), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expectedSet2), 1);
}

TEST_F(NextHopIDManagerTest, namedNhgRouteReverseMapping) {
  auto prefix1 = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);
  auto prefix2 = folly::CIDRNetwork(folly::IPAddress("10.1.0.0"), 24);
  auto prefix3 = folly::CIDRNetwork(folly::IPAddress("2001:db8::"), 48);
  RouterID rid0(0);

  EXPECT_FALSE(manager_->hasRoutesForNamedNhg("nhg1"));
  EXPECT_TRUE(manager_->getRoutesForNamedNhg("nhg1").empty());

  manager_->addRouteForNamedNhg("nhg1", rid0, prefix1);
  EXPECT_TRUE(manager_->hasRoutesForNamedNhg("nhg1"));
  EXPECT_EQ(manager_->getRoutesForNamedNhg("nhg1").size(), 1);

  manager_->addRouteForNamedNhg("nhg1", rid0, prefix2);
  EXPECT_EQ(manager_->getRoutesForNamedNhg("nhg1").size(), 2);

  manager_->addRouteForNamedNhg("nhg2", rid0, prefix3);
  EXPECT_TRUE(manager_->hasRoutesForNamedNhg("nhg2"));
  EXPECT_EQ(manager_->getRoutesForNamedNhg("nhg2").size(), 1);

  manager_->removeRouteForNamedNhg("nhg1", rid0, prefix1);
  EXPECT_EQ(manager_->getRoutesForNamedNhg("nhg1").size(), 1);
  EXPECT_TRUE(manager_->hasRoutesForNamedNhg("nhg1"));

  manager_->removeRouteForNamedNhg("nhg1", rid0, prefix2);
  EXPECT_FALSE(manager_->hasRoutesForNamedNhg("nhg1"));
  EXPECT_TRUE(manager_->getRoutesForNamedNhg("nhg1").empty());

  EXPECT_TRUE(manager_->hasRoutesForNamedNhg("nhg2"));

  manager_->removeRouteForNamedNhg("nonexistent", rid0, prefix1);
  EXPECT_FALSE(manager_->hasRoutesForNamedNhg("nonexistent"));
}

TEST_F(NextHopIDManagerTest, namedNhgMySidReverseMapping) {
  const folly::CIDRNetworkV6 mySid1{folly::IPAddressV6("fc00:100::1"), 48};
  const folly::CIDRNetworkV6 mySid2{folly::IPAddressV6("fc00:200::1"), 64};

  EXPECT_FALSE(manager_->hasMySidsForNamedNhg("nhg1"));
  EXPECT_TRUE(manager_->getMySidsForNamedNhg("nhg1").empty());

  manager_->addMySidForNamedNhg("nhg1", mySid1);
  EXPECT_TRUE(manager_->hasMySidsForNamedNhg("nhg1"));
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg1").count(mySid1), 1);

  manager_->addMySidForNamedNhg("nhg1", mySid2);
  manager_->addMySidForNamedNhg("nhg2", mySid1);
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg1").size(), 2);
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg2").count(mySid1), 1);

  manager_->removeMySidForNamedNhg("nhg1", mySid1);
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg1").size(), 1);
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg1").count(mySid2), 1);
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg2").count(mySid1), 1);

  manager_->removeMySidFromNamedNhgs(mySid1);
  EXPECT_FALSE(manager_->hasMySidsForNamedNhg("nhg2"));
  EXPECT_EQ(manager_->getMySidsForNamedNhg("nhg1").count(mySid2), 1);

  manager_->removeMySidForNamedNhg("nhg1", mySid2);
  EXPECT_FALSE(manager_->hasMySidsForNamedNhg("nhg1"));
}

TEST_F(NextHopIDManagerTest, deallocateNamedNhgBlockedByMySidReference) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  const RouteNextHopSet nhops{nh1};
  manager_->allocateNamedNextHopGroup("nhg1", nhops);

  const folly::CIDRNetworkV6 mySid{folly::IPAddressV6("fc00:100::1"), 48};
  manager_->addMySidForNamedNhg("nhg1", mySid);

  EXPECT_THROW(manager_->deallocateNamedNextHopGroup("nhg1"), FbossError);
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("nhg1"));

  manager_->removeMySidForNamedNhg("nhg1", mySid);
  EXPECT_NO_THROW(manager_->deallocateNamedNextHopGroup("nhg1"));
  EXPECT_FALSE(manager_->hasNamedNextHopGroup("nhg1"));
}

TEST_F(NextHopIDManagerTest, namedNhgMySidReverseMappingWarmBoot) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHopID nhId1 = NextHopID(1);
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(nhId1)});

  auto multiSwitchFibInfoMap = createMultiSwitchFibInfoMap(
      createFibsMap(), idToNextHopMap, idToNextHopIdSetMap);

  auto mySid = makeMySidEntry(setId1, std::nullopt);
  mySid->setNamedNextHopGroup("my-nhg");
  auto mySidMap = makeMySidMap(mySid);

  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, mySidMap, nullptr, nullptr);

  EXPECT_TRUE(manager_->hasMySidsForNamedNhg("my-nhg"));
  const folly::CIDRNetworkV6 expectedMySid{
      folly::IPAddressV6("fc00:100::1"), 48};
  EXPECT_EQ(manager_->getMySidsForNamedNhg("my-nhg").count(expectedMySid), 1);
  EXPECT_FALSE(manager_->hasMySidsForNamedNhg("other-nhg"));
}

TEST_F(NextHopIDManagerTest, namedNhgRouteReverseMappingWarmBoot) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(2), "10.0.0.2", UCMP_DEFAULT_WEIGHT);

  NextHopID nhId1 = NextHopID(1);
  NextHopID nhId2 = NextHopID(2);
  NextHopSetID setId1 = NextHopSetID(kSetIdOffset + 1);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(nhId2), nh2.toThrift());

  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  std::set<state::NextHopIdType> nhIdSet{
      static_cast<state::NextHopIdType>(nhId1),
      static_cast<state::NextHopIdType>(nhId2)};
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1), nhIdSet);

  RouteNextHopSet nhops = {nh1, nh2};

  auto fibsMap = createFibsMap();
  auto fibV4 = getFibV4(fibsMap);

  auto route = std::make_shared<RouteV4>(
      RouteV4::makeThrift(makePrefixV4("10.0.0.0/24")));
  auto nhEntry = RouteNextHopEntry(nhops, AdminDistance::EBGP);
  nhEntry.setNamedNextHopGroup(std::string("my-nhg"));
  route->update(ClientID::BGPD, nhEntry);
  auto fwdInfo = route->getForwardInfo().toThrift();
  fwdInfo.resolvedNextHopSetID() = static_cast<uint64_t>(setId1);
  route->setResolved(RouteNextHopEntry(std::move(fwdInfo)));
  route->publish();
  fibV4->addNode(route);

  auto multiSwitchFibInfoMap =
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap);

  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap, nullptr, nullptr, nullptr);

  EXPECT_TRUE(manager_->hasRoutesForNamedNhg("my-nhg"));
  const auto& routes = manager_->getRoutesForNamedNhg("my-nhg");
  EXPECT_EQ(routes.size(), 1);
  auto expectedPrefix = folly::CIDRNetwork(folly::IPAddress("10.0.0.0"), 24);
  EXPECT_EQ(routes.count({RouterID(0), expectedPrefix}), 1);

  EXPECT_FALSE(manager_->hasRoutesForNamedNhg("other-nhg"));
}

// Full collapse: every member of SID2 is a duplicate of SID1's members, so
// SID2's primary set equals the already-registered SID1. SID1 survives and
// SID2 collapses onto it -- NO fresh id is minted (this is the existing
// duplicate-set path, which member dedup must still route through).
TEST_F(NextHopIDManagerTest, reconstructFullCollapseRemapsToSurvivor) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2), id3(3), id4(4), id5(5), id6(6);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {id1, nh1},
           {id2, nh2},
           {id3, nh3},
           {id4, nh1},
           {id5, nh2},
           {id6, nh3}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  NextHopSetID setId1(kSetIdOffset + 1), setId2(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2),
       static_cast<state::NextHopIdType>(id3)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id4),
       static_cast<state::NextHopIdType>(id5),
       static_cast<state::NextHopIdType>(id6)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.0.0.0/24", {nh1, nh2, nh3}, setId1);
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.1.0.0/24", {nh1, nh2, nh3}, setId2);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  ASSERT_EQ(remap.size(), 1);
  EXPECT_EQ(remap.at(setId2), setId1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setId2), 0);
  NextHopIDSet expected = {id1, id2, id3};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(setId1), expected);

  // Both routes reference the survivor: set count 2, each member rc 2.
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expected), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 2);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 2);

  // Both references drain cleanly.
  EXPECT_FALSE(manager_->decrOrDeallocRouteNextHopSetID(setId1)
                   .removedSetId.has_value());
  EXPECT_TRUE(manager_->decrOrDeallocRouteNextHopSetID(setId1)
                  .removedSetId.has_value());
  EXPECT_TRUE(manager_->getIdToNextHop().empty());
  EXPECT_TRUE(manager_->getIdToNextHopIdSet().empty());
}

// A retired SetID referenced by MULTIPLE routes (the common ECMP case where
// several prefixes share one nexthop group). setId2's members dedup to a
// new set, so setId2 is retired; because two routes reference setId2,
// reconstruct processes it twice. Verifies that:
//   - the first reference mints the fresh SetID and the second COLLAPSES onto
//     that same fresh id (no second/duplicate set is created), and
//   - the fresh set's refcount (2) and the member refcounts (nh1/nh2 = 3 from
//     setId1 + two fresh refs, nh6 = 2 from two fresh refs) accumulate across
//     both references, so a later delete drains cleanly.
TEST_F(
    NextHopIDManagerTest,
    reconstructMultipleRoutesToRetiredSetIdAccumulateFreshCount) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh6 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.6", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2), id3(3), id4(4), id5(5), id6(6);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {id1, nh1},
           {id2, nh2},
           {id3, nh3},
           {id4, nh1},
           {id5, nh2},
           {id6, nh6}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  NextHopSetID setId1(kSetIdOffset + 1), setId2(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2),
       static_cast<state::NextHopIdType>(id3)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id4),
       static_cast<state::NextHopIdType>(id5),
       static_cast<state::NextHopIdType>(id6)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.0.0.0/24", {nh1, nh2, nh3}, setId1);
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.1.0.0/24", {nh1, nh2, nh6}, setId2);
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.2.0.0/24", {nh1, nh2, nh6}, setId2);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  NextHopSetID freshSetId(kSetIdOffset + 3);
  ASSERT_EQ(remap.size(), 1);
  EXPECT_EQ(remap.at(setId2), freshSetId);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 2);

  NextHopIDSet freshSet = {id1, id2, id6};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(freshSetId), freshSet);
  // Fresh set referenced by 2 routes.
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(freshSet), 2);
  // nh1/nh2: SID1 (1) + fresh x2 (2) = 3; nh3: SID1 only; nh6: fresh x2.
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 3);
  EXPECT_EQ(manager_->getNextHopRefCount(nh2), 3);
  EXPECT_EQ(manager_->getNextHopRefCount(nh3), 1);
  EXPECT_EQ(manager_->getNextHopRefCount(nh6), 2);
}

// Intra-set duplicate: a single persisted set contains two member ids that this
// build maps to the same NextHop. The primary set shrinks (the duplicate is
// dropped), a fresh id is minted, and -- critically -- the surviving member is
// reffed only ONCE so dealloc drains it with no leak.
TEST_F(
    NextHopIDManagerTest,
    reconstructIntraSetDuplicateShrinksGroupAndDeallocsCleanly) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id1), nh1.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id2), nh1.toThrift()); // duplicate

  NextHopSetID setId1(kSetIdOffset + 1);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMap), "10.0.0.0/24", {nh1}, setId1);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  // SID1 retired onto a fresh id; the group shrank to a single member. Which of
  // id1/id2 survives as primary depends on persisted-set iteration order, so
  // assert structurally rather than on a specific id.
  NextHopSetID freshSetId(kSetIdOffset + 2);
  ASSERT_EQ(remap.size(), 1);
  EXPECT_EQ(remap.at(setId1), freshSetId);
  EXPECT_EQ(manager_->getIdToNextHop().size(), 1);
  ASSERT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  const auto& freshSet = manager_->getIdToNextHopIdSet().at(freshSetId);
  EXPECT_EQ(freshSet.size(), 1);
  // The surviving member must exist in idToNextHop_ (the invariant).
  EXPECT_EQ(manager_->getIdToNextHop().count(*freshSet.begin()), 1);

  // Leak guard: nh1 is reffed exactly once despite appearing twice in the
  // persisted set, and the fresh set is reffed once.
  EXPECT_EQ(manager_->getNextHopRefCount(nh1), 1);
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(freshSet), 1);

  // Dealloc drains everything -- no stranded member.
  auto result = manager_->decrOrDeallocRouteNextHopSetID(freshSetId);
  ASSERT_TRUE(result.removedSetId.has_value());
  EXPECT_EQ(*result.removedSetId, freshSetId);
  EXPECT_TRUE(manager_->getIdToNextHop().empty());
  EXPECT_TRUE(manager_->getIdToNextHopIdSet().empty());
}

// MySid pass: a MySid entry's unresolved set is a duplicate of its resolved set
// (members dedup), so it collapses onto the resolved set's id and the
// collapse is recorded in the remap -- exercised with an empty FIB.
TEST_F(NextHopIDManagerTest, reconstructMySidSetDedupRecordsRemap) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2), id3(3), id4(4);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {id1, nh1}, {id2, nh2}, {id3, nh1}, {id4, nh2}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  NextHopSetID setId1(kSetIdOffset + 1); // resolved
  NextHopSetID setId2(kSetIdOffset + 2); // unresolved (duplicate of resolved)
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id3),
       static_cast<state::NextHopIdType>(id4)});

  auto multiSwitchFibInfoMap = createMultiSwitchFibInfoMap(
      createFibsMap(), idToNextHopMap, idToNextHopIdSetMap);
  auto mySid = makeMySidEntry(setId1, setId2);
  auto mySidMap = makeMySidMap(mySid);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap,
      mySidMap,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  ASSERT_EQ(remap.size(), 1);
  EXPECT_EQ(remap.at(setId2), setId1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().size(), 1);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(setId2), 0);
  NextHopIDSet expected = {id1, id2};
  // Both the resolved and unresolved references land on setId1.
  EXPECT_EQ(manager_->getNextHopIDSetRefCount(expected), 2);
}

// When a retired SetID is remapped to a freshly minted id, that fresh id must
// be seeded from the HIGHEST SetID anywhere in the persisted state, so it can
// never collide with a healthy set that kept its id. Here setId2 (offset+2) is
// retired, but a healthy setIdHigh (offset+50) is still alive at its high
// number. The fresh id must therefore be offset+51 (global max + 1), NOT
// offset+3 (retired set + 1) -- otherwise the next allocation could re-issue an
// id setIdHigh already owns.
TEST_F(NextHopIDManagerTest, reconstructFreshSetIdClearsGlobalMaxSetId) {
  NextHop nh1 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nh2 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nh3 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHop nh6 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.6", UCMP_DEFAULT_WEIGHT);
  NextHop nh7 =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.7", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2), id3(3), id4(4), id5(5), id6(6), id7(7);

  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  for (const auto& [id, nh] : std::vector<std::pair<NextHopID, NextHop>>{
           {id1, nh1},
           {id2, nh2},
           {id3, nh3},
           {id4, nh1},
           {id5, nh2},
           {id6, nh6},
           {id7, nh7}}) {
    idToNextHopMap->addNextHop(
        static_cast<state::NextHopIdType>(id), nh.toThrift());
  }
  NextHopSetID setId1(kSetIdOffset + 1);
  NextHopSetID setId2(kSetIdOffset + 2); // dedups
  NextHopSetID setIdHigh(kSetIdOffset + 50); // healthy, defines global max
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2),
       static_cast<state::NextHopIdType>(id3)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id4),
       static_cast<state::NextHopIdType>(id5),
       static_cast<state::NextHopIdType>(id6)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdHigh),
      {static_cast<state::NextHopIdType>(id7)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.0.0.0/24", {nh1, nh2, nh3}, setId1);
  addV4RouteWithSetId(
      getFibV4(fibsMap), "10.1.0.0/24", {nh1, nh2, nh6}, setId2);
  addV4RouteWithSetId(getFibV4(fibsMap), "10.2.0.0/24", {nh7}, setIdHigh);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  // Fresh id is global-max (kSetIdOffset + 50) + 1, NOT setId2 + 1.
  ASSERT_EQ(remap.size(), 1);
  EXPECT_EQ(remap.at(setId2), NextHopSetID(kSetIdOffset + 51));
}

// Named-group dedup: a route registers the primary id under SID1,
// and a named group "grp" is persisted pointing at SID2 whose only member is a
// duplicate of the same nexthop (as produced by a cost-aware writer read by a
// cost-unaware reader). On reconstruct SID2 collapses onto SID1; the
// named-group path must resolve through the remap so "grp" ends up on the
// surviving SID1 -- without the remap resolution the post-processNhopSetId
// CHECK would fire.
TEST_F(NextHopIDManagerTest, reconstructNamedGroupSetIdDedupResolves) {
  NextHop nh =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2);
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id1), nh.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id2), nh.toThrift()); // duplicate

  NextHopSetID setId1(kSetIdOffset + 1), setId2(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id2)});

  // A route references setId1 (processed first in the FIB pass) so id1 is the
  // primary id by the time the named-group pass processes setId2.
  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMap), "10.0.0.0/24", {nh}, setId1);

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(fibsMap);
  fibInfo->setIdToNextHopMap(idToNextHopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);
  fibInfo->setNameToNextHopSetId({{"grp", static_cast<NextHopSetId>(setId2)}});
  auto multiSwitchFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  multiSwitchFibInfoMap->addNode("id=0", fibInfo);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap,
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 1);
  ASSERT_EQ(remap.count(setId2), 1);
  EXPECT_EQ(remap.at(setId2), setId1);

  ASSERT_TRUE(manager_->hasNamedNextHopGroup("grp"));
  auto nameSetId = manager_->getNextHopSetIDForName("grp");
  ASSERT_TRUE(nameSetId.has_value());
  EXPECT_EQ(*nameSetId, setId1);
  // The named group resolves to a SetID that still exists in the manager.
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(*nameSetId), 1);
}

// Named-group dedup, FRESH-id variant: the named group's set, after
// member dedup, becomes a set that is NOT already registered (it
// differs from the route's set), so a fresh SetID is minted. The named-group
// path must resolve "grp" onto the fresh id.
TEST_F(NextHopIDManagerTest, reconstructNamedGroupSetIdDedupFreshId) {
  NextHop nhA =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nhB =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  NextHop nhC =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2), id3(3), id4(4);
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id1), nhA.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id2), nhA.toThrift()); // dup of nhA
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id3), nhB.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id4), nhC.toThrift());

  NextHopSetID setId1(kSetIdOffset + 1), setId2(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id3)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id2),
       static_cast<state::NextHopIdType>(id4)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMap), "10.0.0.0/24", {nhA, nhB}, setId1);

  auto fibInfo = std::make_shared<FibInfo>();
  fibInfo->resetFibsMap(fibsMap);
  fibInfo->setIdToNextHopMap(idToNextHopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);
  fibInfo->setNameToNextHopSetId({{"grp", static_cast<NextHopSetId>(setId2)}});
  auto multiSwitchFibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  multiSwitchFibInfoMap->addNode("id=0", fibInfo);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      multiSwitchFibInfoMap,
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  // setId2 deduped to {id1,id4} (new) -> retired onto fresh id.
  NextHopSetID freshSetId(kSetIdOffset + 3);
  ASSERT_EQ(remap.count(setId2), 1);
  EXPECT_EQ(remap.at(setId2), freshSetId);

  ASSERT_TRUE(manager_->hasNamedNextHopGroup("grp"));
  auto nameSetId = manager_->getNextHopSetIDForName("grp");
  ASSERT_TRUE(nameSetId.has_value());
  EXPECT_EQ(*nameSetId, freshSetId);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(*nameSetId), 1);
  NextHopIDSet expectedFresh = {id1, id4};
  EXPECT_EQ(manager_->getIdToNextHopIdSet().at(freshSetId), expectedFresh);
}

// A reclaimed (duplicate) NextHopID must NOT be reused by a later allocation.
// nextAvailableNextHopID_ is seeded from the highest PERSISTED id (4 here), not
// the highest surviving id (2), even though the duplicate ids 3/4 were dropped
// from idToNextHop_. If it regressed to the surviving max, a new nexthop would
// reuse id 3 -- but the persisted FibInfo still maps id3->nhA, and the key-only
// diff in SwitchStateNextHopIdUpdater would never overwrite it, leaving the
// manager and switch state permanently inconsistent. (NextHopID-space mirror of
// reconstructFreshSetIdClearsGlobalMaxSetId.)
TEST_F(NextHopIDManagerTest, reconstructReclaimedNextHopIdNotReused) {
  NextHop nhA =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHop nhB =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT);
  // Persisted: id1=nhA, id2=nhB, id3=nhA(dup), id4=nhB(dup). 3,4 reclaimed.
  NextHopID id1(1), id2(2), id3(3), id4(4);
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id1), nhA.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id2), nhB.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id3), nhA.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id4), nhB.toThrift());

  NextHopSetID setId1(kSetIdOffset + 1), setId2(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId1),
      {static_cast<state::NextHopIdType>(id1),
       static_cast<state::NextHopIdType>(id2)});
  // setId2 = {id3,id4} dedups to {id1,id2} == setId1 -> collapse.
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setId2),
      {static_cast<state::NextHopIdType>(id3),
       static_cast<state::NextHopIdType>(id4)});

  auto fibsMap = createFibsMap();
  addV4RouteWithSetId(getFibV4(fibsMap), "10.0.0.0/24", {nhA, nhB}, setId1);
  addV4RouteWithSetId(getFibV4(fibsMap), "10.1.0.0/24", {nhA, nhB}, setId2);

  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 2); // id3,id4 reclaimed

  // Allocating a brand-new nexthop must yield an id ABOVE the highest persisted
  // id (4), never reusing the reclaimed 3 or 4.
  NextHop nhC =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.3", UCMP_DEFAULT_WEIGHT);
  auto result = manager_->getOrAllocRouteNextHopSetID({nhC});
  ASSERT_EQ(result.addedNextHopIds.size(), 1);
  EXPECT_EQ(result.addedNextHopIds[0], NextHopID(5));
}

// Per-client clientNextHopSetID cost collision: two routes carry per-client
// entries whose clientNextHopSetIDs reference duplicate-of-the-same-nexthop
// sets. Reconstruct must dedup via the per-client walk and collapse the
// duplicate SetID with no dangling member.
TEST_F(NextHopIDManagerTest, reconstructPerClientCostCollisionCollapse) {
  NextHop nh =
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT);
  NextHopID id1(1), id2(2);
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id1), nh.toThrift());
  idToNextHopMap->addNextHop(
      static_cast<state::NextHopIdType>(id2), nh.toThrift()); // duplicate

  NextHopSetID setIdA(kSetIdOffset + 1), setIdB(kSetIdOffset + 2);
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdA),
      {static_cast<state::NextHopIdType>(id1)});
  idToNextHopIdSetMap->addNextHopIdSet(
      static_cast<state::NextHopSetIdType>(setIdB),
      {static_cast<state::NextHopIdType>(id2)});

  auto fibsMap = createFibsMap();
  addV4RouteWithClientId(
      getFibV4(fibsMap), "10.0.0.0/24", {nh}, ClientID::BGPD, setIdA);
  addV4RouteWithClientId(
      getFibV4(fibsMap), "10.1.0.0/24", {nh}, ClientID::BGPD, setIdB);

  std::unordered_map<NextHopSetID, NextHopSetID> remap;
  manager_->reconstructFromSwitchStateMaps(
      createMultiSwitchFibInfoMap(fibsMap, idToNextHopMap, idToNextHopIdSetMap),
      nullptr,
      nullptr,
      nullptr,
      nullptr /*classBasedPolicyMaps*/,
      &remap);

  EXPECT_EQ(manager_->getIdToNextHop().size(), 1);
  ASSERT_EQ(remap.count(setIdB), 1);
  EXPECT_EQ(remap.at(setIdB), setIdA);
  for (const auto& [_sid, members] : manager_->getIdToNextHopIdSet()) {
    for (const auto& m : members) {
      EXPECT_EQ(manager_->getIdToNextHop().count(m), 1);
    }
  }
}

namespace {
state::NamedNextHopGroupAndID makeNamedNhg(
    const std::string& name,
    int64_t id) {
  state::NamedNextHopGroupAndID nhg;
  nhg.name() = name;
  nhg.id() = id;
  return nhg;
}
} // namespace

TEST_F(NextHopIDManagerTest, reconstructClassBasedPolicyPassRebuildsStore) {
  auto matcher0 = HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
  auto matcher1 = HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(1)}};

  // 1. A persisted policy, replicated across two matchers as config scoping
  // produces.
  state::ClassBasedPolicyFields fields;
  fields.name() = "pol1";
  fields.defaultNextHopGroup() = makeNamedNhg("defaultNhg", 10);
  fields.class2NextHopGroup() = {
      {ForwardingClass::CLASS_1, makeNamedNhg("classNhg1", 11)},
      {ForwardingClass::CLASS_2, makeNamedNhg("classNhg2", 12)}};
  auto policyMaps = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  for (const auto& matcher : {matcher0, matcher1}) {
    auto policyMap = std::make_shared<ClassBasedPolicyMap>();
    policyMap->addNode(std::make_shared<ClassBasedPolicyNode>(fields));
    policyMaps->addMapNode(policyMap, matcher);
  }

  // 2. Reconstruct rebuilds the store from the persisted policies.
  manager_->reconstructFromSwitchStateMaps(
      nullptr, nullptr, nullptr, nullptr, policyMaps);

  const auto* stored = manager_->getPolicy("pol1");
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->defaultNexthopGroup, "defaultNhg");
  const std::map<ForwardingClass, std::string> expected = {
      {ForwardingClass::CLASS_1, "classNhg1"},
      {ForwardingClass::CLASS_2, "classNhg2"}};
  EXPECT_EQ(stored->class2NextHopGroup, expected);

  // 3. The same policy under a second matcher is deduped, not duplicated.
  EXPECT_EQ(manager_->getPbrPolicyToNamedNhg().size(), 1);
}

// Class-based policy store: add / get / remove. The RIB ingest path reads
// this store (getPolicy) to translate a policyName route into its default
// named-NHG route, so the store must round-trip the config policy verbatim.
TEST_F(NextHopIDManagerTest, ClassBasedPolicyStore) {
  ClassBasedPolicy policy;
  policy.name() = "pol1";
  policy.defaultNexthopGroup() = "defaultNhg";
  NextHopGroup redirectNhg;
  redirectNhg.name() = "class1Nhg";
  policy.class2NextHopGroup()[ForwardingClass::CLASS_1] = redirectNhg;

  EXPECT_EQ(manager_->getPolicy("pol1"), nullptr);

  manager_->addOrUpdatePolicy(policy);

  const auto* stored = manager_->getPolicy("pol1");
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->defaultNexthopGroup, "defaultNhg");
  EXPECT_EQ(
      stored->class2NextHopGroup.at(ForwardingClass::CLASS_1), "class1Nhg");

  // Update in place: a changed default group replaces the stored policy, and
  // the redirects it did not touch survive.
  policy.defaultNexthopGroup() = "defaultNhg2";
  manager_->addOrUpdatePolicy(policy);
  const auto* updated = manager_->getPolicy("pol1");
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->defaultNexthopGroup, "defaultNhg2");
  EXPECT_EQ(
      updated->class2NextHopGroup.at(ForwardingClass::CLASS_1), "class1Nhg");
  EXPECT_EQ(manager_->getPbrPolicyToNamedNhg().size(), 1);

  manager_->removePolicy("pol1");
  EXPECT_EQ(manager_->getPolicy("pol1"), nullptr);
  EXPECT_TRUE(manager_->getPbrPolicyToNamedNhg().empty());
}

TEST_F(NextHopIDManagerTest, policyUpdateRepointsNamedNhgIndex) {
  ClassBasedPolicy policy;
  policy.name() = "pol1";
  policy.defaultNexthopGroup() = "defaultNhg";
  NextHopGroup redirectNhg;
  redirectNhg.name() = "redirectNhg";
  policy.class2NextHopGroup()[ForwardingClass::CLASS_1] = redirectNhg;
  manager_->addOrUpdatePolicy(policy);

  EXPECT_EQ(manager_->getPoliciesForNamedNhg("defaultNhg").count("pol1"), 1);
  EXPECT_EQ(manager_->getPoliciesForNamedNhg("redirectNhg").count("pol1"), 1);

  // Repoint the default and drop the redirect entirely.
  policy.defaultNexthopGroup() = "defaultNhg2";
  policy.class2NextHopGroup()->clear();
  manager_->addOrUpdatePolicy(policy);

  EXPECT_FALSE(manager_->hasPoliciesForNamedNhg("defaultNhg"));
  EXPECT_FALSE(manager_->hasPoliciesForNamedNhg("redirectNhg"));
  EXPECT_TRUE(manager_->hasPoliciesForNamedNhg("defaultNhg2"));

  // Two policies on one group: removing one must leave the other's reference.
  ClassBasedPolicy other;
  other.name() = "pol2";
  other.defaultNexthopGroup() = "defaultNhg2";
  manager_->addOrUpdatePolicy(other);
  EXPECT_EQ(manager_->getPoliciesForNamedNhg("defaultNhg2").size(), 2);

  manager_->removePolicy("pol1");
  EXPECT_EQ(manager_->getPoliciesForNamedNhg("defaultNhg2").size(), 1);

  manager_->removePolicy("pol2");
  EXPECT_FALSE(manager_->hasPoliciesForNamedNhg("defaultNhg2"));
  EXPECT_TRUE(manager_->getPoliciesForNamedNhg("defaultNhg2").empty());
}

// Each policy naming a group holds its own reference to the group's normalized
// set, so repointing one of two must not free the id the other still matches
// on.
TEST_F(NextHopIDManagerTest, sharedNamedNhgIdSurvivesOnePolicyRepointing) {
  RouteNextHopSet shared{
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT)};
  RouteNextHopSet other{
      makeResolvedNextHop(InterfaceID(2), "10.0.0.2", UCMP_DEFAULT_WEIGHT)};
  manager_->allocateNamedNextHopGroup("shared", shared);
  manager_->allocateNamedNextHopGroup("other", other);

  auto bindPolicy = [&](const std::string& name, const std::string& nhgName) {
    ClassBasedPolicy policy;
    policy.name() = name;
    policy.defaultNexthopGroup() = nhgName;
    manager_->addOrUpdatePolicy(policy);
  };
  bindPolicy("polA", "shared");
  bindPolicy("polB", "shared");
  manager_->updateNormalizedNamedNhgId("shared", shared, kEcmpWidth);

  auto sharedId = manager_->getNormalizedNextHopSetIDForPolicyNhg("shared");
  ASSERT_TRUE(sharedId.has_value());

  // 1. Repoint polB away. polA still names the group, so its id must hold.
  bindPolicy("polB", "other");
  EXPECT_EQ(
      manager_->getNormalizedNextHopSetIDForPolicyNhg("shared"), sharedId);
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(*sharedId), 1);

  // 2. With the last policy and the group itself gone, nothing holds the set.
  manager_->removePolicy("polA");
  manager_->deallocateNamedNextHopGroup("shared");
  EXPECT_EQ(manager_->getIdToNextHopIdSet().count(*sharedId), 0);
}

// A policy's default NHG cannot be deleted out from under it. Without the
// policy guard this is only caught when the policy has routes.
TEST_F(NextHopIDManagerTest, deleteNamedNhgReferencedAsPolicyDefaultBlocked) {
  RouteNextHopSet nhops{
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT)};
  manager_->allocateNamedNextHopGroup("defaultNhg", nhops);

  ClassBasedPolicy policy;
  policy.name() = "pol1";
  policy.defaultNexthopGroup() = "defaultNhg";
  manager_->addOrUpdatePolicy(policy);

  EXPECT_TRUE(manager_->hasPoliciesForNamedNhg("defaultNhg"));
  EXPECT_THROW(manager_->deallocateNamedNextHopGroup("defaultNhg"), FbossError);
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("defaultNhg"));

  // Dropping the policy releases the reference.
  manager_->removePolicy("pol1");
  EXPECT_FALSE(manager_->hasPoliciesForNamedNhg("defaultNhg"));
  manager_->deallocateNamedNextHopGroup("defaultNhg");
  EXPECT_FALSE(manager_->hasNamedNextHopGroup("defaultNhg"));
}

// A redirect-only NHG has no routes pointing at it, so the route guard never
// fires; the policy guard is the only thing protecting it.
TEST_F(NextHopIDManagerTest, deleteNamedNhgReferencedAsPolicyRedirectBlocked) {
  RouteNextHopSet defaultNhops{
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT)};
  RouteNextHopSet redirectNhops{
      makeResolvedNextHop(InterfaceID(1), "10.0.0.2", UCMP_DEFAULT_WEIGHT)};
  manager_->allocateNamedNextHopGroup("defaultNhg", defaultNhops);
  manager_->allocateNamedNextHopGroup("redirectNhg", redirectNhops);

  ClassBasedPolicy policy;
  policy.name() = "pol1";
  policy.defaultNexthopGroup() = "defaultNhg";
  NextHopGroup redirectNhg;
  redirectNhg.name() = "redirectNhg";
  policy.class2NextHopGroup()[ForwardingClass::CLASS_1] = redirectNhg;
  manager_->addOrUpdatePolicy(policy);

  EXPECT_FALSE(manager_->hasRoutesForNamedNhg("redirectNhg"));
  EXPECT_TRUE(manager_->hasPoliciesForNamedNhg("redirectNhg"));
  EXPECT_THROW(
      manager_->deallocateNamedNextHopGroup("redirectNhg"), FbossError);
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("redirectNhg"));

  manager_->removePolicy("pol1");
  manager_->deallocateNamedNextHopGroup("redirectNhg");
  EXPECT_FALSE(manager_->hasNamedNextHopGroup("redirectNhg"));
}

// An NHG no policy names stays deletable.
TEST_F(NextHopIDManagerTest, deleteUnreferencedNamedNhgWithPoliciesPresent) {
  RouteNextHopSet nhops{
      makeResolvedNextHop(InterfaceID(1), "10.0.0.1", UCMP_DEFAULT_WEIGHT)};
  manager_->allocateNamedNextHopGroup("defaultNhg", nhops);
  manager_->allocateNamedNextHopGroup("unusedNhg", nhops);

  ClassBasedPolicy policy;
  policy.name() = "pol1";
  policy.defaultNexthopGroup() = "defaultNhg";
  manager_->addOrUpdatePolicy(policy);

  EXPECT_FALSE(manager_->hasPoliciesForNamedNhg("unusedNhg"));
  manager_->deallocateNamedNextHopGroup("unusedNhg");
  EXPECT_FALSE(manager_->hasNamedNextHopGroup("unusedNhg"));
  EXPECT_TRUE(manager_->hasNamedNextHopGroup("defaultNhg"));
}

} // namespace facebook::fboss
