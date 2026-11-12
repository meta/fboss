/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/IPAddress.h>
#include <gtest/gtest.h>
#include <memory>

namespace facebook::fboss {

namespace {

std::shared_ptr<ForwardingInformationBaseContainer> createFibContainer(
    RouterID vrf) {
  return std::make_shared<ForwardingInformationBaseContainer>(vrf);
}

std::shared_ptr<FibInfo> createFibInfo(RouterID vrf) {
  auto fibInfo = std::make_shared<FibInfo>();
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto fibContainer = createFibContainer(vrf);
  fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  fibInfo->resetFibsMap(fibsMap);
  return fibInfo;
}

HwSwitchMatcher createMatcher(int64_t switchId) {
  return HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(switchId)});
}

} // namespace

class FibInfoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_shared<SwitchState>();
  }

  std::shared_ptr<SwitchState> state_;
};

TEST_F(FibInfoTest, SerializationAndGetfibsMap) {
  auto fibInfo = createFibInfo(RouterID(0));

  // Test serialization
  validateNodeSerialization(*fibInfo);

  // Test getting fibsMap
  auto fibsMap = fibInfo->getfibsMap();
  EXPECT_EQ(fibsMap->size(), 1);
  EXPECT_EQ(fibsMap->getNodeIf(RouterID(0))->getID(), RouterID(0));
}

TEST_F(FibInfoTest, GetFibContainer) {
  // Test getting existing FIB container
  auto fibInfo = createFibInfo(RouterID(5));
  auto fibContainer = fibInfo->getFibContainerIf(RouterID(5));
  EXPECT_NE(fibContainer, nullptr);
  EXPECT_EQ(fibContainer->getID(), RouterID(5));

  // Test getting non-existent FIB container
  auto nonExistentContainer = fibInfo->getFibContainerIf(RouterID(10));
  EXPECT_EQ(nonExistentContainer, nullptr);
}

TEST_F(FibInfoTest, ModifyPublished) {
  auto fibInfo = createFibInfo(RouterID(0));

  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto matcher = createMatcher(10);
  fibInfoMap->addNode(matcher.matcherString(), fibInfo);
  state_->resetFibsInfoMap(fibInfoMap);
  state_->publish();

  auto modifiedFibInfo = fibInfo->modify(&state_);
  EXPECT_NE(modifiedFibInfo, fibInfo.get());
  EXPECT_EQ(
      state_->getFibsInfoMap()->getFibInfo(matcher).get(), modifiedFibInfo);
}

TEST_F(FibInfoTest, UpdateFibContainer) {
  // Setup initial state with RouterID(0) container
  auto fibInfo = createFibInfo(RouterID(0));
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto matcher = createMatcher(10);
  fibInfoMap->addNode(matcher.matcherString(), fibInfo);
  state_->resetFibsInfoMap(fibInfoMap);
  state_->publish();

  // Verify initial container exists
  EXPECT_NE(fibInfo->getFibContainerIf(RouterID(0)), nullptr);
  EXPECT_EQ(fibInfo->getFibContainerIf(RouterID(1)), nullptr);

  // Add a new container with RouterID(1)
  auto newFibContainer = createFibContainer(RouterID(1));
  fibInfo->updateFibContainer(newFibContainer, &state_);

  // Verify the FibInfo was cloned
  auto updatedFibInfo =
      state_->getFibsInfoMap()->getNodeIf(matcher.matcherString());
  EXPECT_NE(updatedFibInfo, nullptr);
  EXPECT_NE(updatedFibInfo, fibInfo);

  // Verify both containers exist in the updated FibInfo
  EXPECT_NE(updatedFibInfo->getFibContainerIf(RouterID(0)), nullptr);
  EXPECT_NE(updatedFibInfo->getFibContainerIf(RouterID(1)), nullptr);
}

class MultiSwitchFibInfoMapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_shared<SwitchState>();
  }

  std::shared_ptr<SwitchState> state_;
};

TEST_F(MultiSwitchFibInfoMapTest, SerializationAndGetFibInfoTest) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto fibInfo1 = createFibInfo(RouterID(0));
  auto fibInfo2 = createFibInfo(RouterID(1));

  auto matcher1 = createMatcher(10);
  auto matcher2 = createMatcher(20);
  fibInfoMap->addNode(matcher1.matcherString(), fibInfo1);
  fibInfoMap->addNode(matcher2.matcherString(), fibInfo2);

  // Test serialization
  validateThriftMapMapSerialization(*fibInfoMap);

  // Test getting existing FibInfo
  auto retrievedFibInfo1 = fibInfoMap->getFibInfo(matcher1);
  EXPECT_NE(retrievedFibInfo1, nullptr);
  EXPECT_EQ(retrievedFibInfo1, fibInfo1);

  auto retrievedFibInfo2 = fibInfoMap->getFibInfo(matcher2);
  EXPECT_EQ(retrievedFibInfo2, fibInfo2);

  // Test getting non-existent FibInfo
  auto matcher3 = createMatcher(30);
  auto nonExistentFibInfo = fibInfoMap->getFibInfo(matcher3);
  EXPECT_EQ(nonExistentFibInfo, nullptr);
}

TEST_F(MultiSwitchFibInfoMapTest, GetNodeByVrfAndHwSwitchMatcherTest) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto fibInfo1 = createFibInfo(RouterID(5));
  auto fibInfo2 = createFibInfo(RouterID(10));

  auto matcher1 = createMatcher(10);
  auto matcher2 = createMatcher(20);
  fibInfoMap->addNode(matcher1.matcherString(), fibInfo1);
  fibInfoMap->addNode(matcher2.matcherString(), fibInfo2);

  // Test getNode searches across all switches
  auto fibContainer5 = fibInfoMap->getFibContainerIf(RouterID(5));
  EXPECT_NE(fibContainer5, nullptr);
  EXPECT_EQ(fibContainer5->getID(), RouterID(5));

  auto fibContainer10 = fibInfoMap->getFibContainerIf(RouterID(10));
  EXPECT_NE(fibContainer10, nullptr);
  EXPECT_EQ(fibContainer10->getID(), RouterID(10));

  // Test getting non-existent FIB container by VRF
  auto nonExistentContainer = fibInfoMap->getFibContainerIf(RouterID(15));
  EXPECT_EQ(nonExistentContainer, nullptr);

  // Test getFibContainerIf(matcher, RouterID vrf) gets from specific switch
  auto fibContainer5FromMatcher1 =
      fibInfoMap->getFibContainerIf(matcher1, RouterID(5));
  EXPECT_EQ(fibContainer5FromMatcher1->getID(), RouterID(5));

  auto fibContainer10FromMatcher2 =
      fibInfoMap->getFibContainerIf(matcher2, RouterID(10));
  EXPECT_EQ(fibContainer10FromMatcher2->getID(), RouterID(10));

  // Test getting non-existent VRF from existing switch
  auto nonExistentVrf = fibInfoMap->getFibContainerIf(matcher1, RouterID(99));
  EXPECT_EQ(nonExistentVrf, nullptr);

  // Test getting VRF from wrong switch
  auto wrongSwitch = fibInfoMap->getFibContainerIf(matcher1, RouterID(10));
  EXPECT_EQ(wrongSwitch, nullptr);

  // Test getting from non-existent switch
  auto matcher3 = createMatcher(30);
  auto nonExistentSwitch = fibInfoMap->getFibContainerIf(matcher3, RouterID(5));
  EXPECT_EQ(nonExistentSwitch, nullptr);
}

TEST_F(MultiSwitchFibInfoMapTest, UpdateFibInfoTest) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto fibInfo1 = createFibInfo(RouterID(0));
  auto fibInfo2 = createFibInfo(RouterID(1));
  auto matcher = createMatcher(10);

  // Test adding new FibInfo
  fibInfoMap->updateFibInfo(fibInfo1, matcher);
  auto retrievedFibInfo = fibInfoMap->getFibInfo(matcher);
  EXPECT_EQ(retrievedFibInfo, fibInfo1);
  EXPECT_EQ(fibInfoMap->size(), 1);

  // Test updating existing FibInfo
  fibInfoMap->updateFibInfo(fibInfo2, matcher);
  EXPECT_EQ(fibInfoMap->size(), 1);

  retrievedFibInfo = fibInfoMap->getFibInfo(matcher);
  EXPECT_EQ(retrievedFibInfo, fibInfo2);
  EXPECT_NE(retrievedFibInfo, fibInfo1);
}

TEST_F(MultiSwitchFibInfoMapTest, ModifyPublishedStateTest) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
  auto fibInfo = createFibInfo(RouterID(0));
  auto matcher = createMatcher(0);

  fibInfoMap->addNode(matcher.matcherString(), fibInfo);
  state_->resetFibsInfoMap(fibInfoMap);
  state_->publish();

  auto modifiedMap = fibInfoMap->modify(&state_);
  EXPECT_NE(modifiedMap, fibInfoMap.get());
  EXPECT_EQ(state_->getFibsInfoMap().get(), modifiedMap);
}

TEST_F(MultiSwitchFibInfoMapTest, MultipleNodesAndEmptyMapTest) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();

  // Test empty map behavior
  EXPECT_EQ(fibInfoMap->size(), 0);
  EXPECT_EQ(fibInfoMap->getFibContainerIf(RouterID(0)), nullptr);
  EXPECT_EQ(fibInfoMap->getFibInfo(createMatcher(10)), nullptr);

  // Add multiple nodes and test
  auto fibInfo1 = createFibInfo(RouterID(0));
  auto fibInfo2 = createFibInfo(RouterID(1));
  auto fibInfo3 = createFibInfo(RouterID(2));

  auto matcher1 = createMatcher(10);
  auto matcher2 = createMatcher(20);
  auto matcher3 = createMatcher(30);

  fibInfoMap->updateFibInfo(fibInfo1, matcher1);
  fibInfoMap->updateFibInfo(fibInfo2, matcher2);
  fibInfoMap->updateFibInfo(fibInfo3, matcher3);

  // Test multiple nodes behavior
  EXPECT_EQ(fibInfoMap->size(), 3);
  EXPECT_EQ(fibInfoMap->getFibInfo(matcher1), fibInfo1);
  EXPECT_EQ(fibInfoMap->getFibInfo(matcher2), fibInfo2);
  EXPECT_EQ(fibInfoMap->getFibInfo(matcher3), fibInfo3);
}

TEST_F(MultiSwitchFibInfoMapTest, GetRouteCount) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();

  // Verify initial route count is 0 for empty map
  auto [emptyV4Count, emptyV6Count] = fibInfoMap->getRouteCount();
  EXPECT_EQ(emptyV4Count, 0);
  EXPECT_EQ(emptyV6Count, 0);

  // Create FibInfo with RouterID(0)
  auto fibInfo = std::make_shared<FibInfo>();
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto fibContainer =
      std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));

  // Create and add 2 IPv4 routes
  ForwardingInformationBaseV4 fibV4;
  RouteFields<folly::IPAddressV4> routeV4_1(
      RoutePrefixV4{folly::IPAddressV4("10.0.0.0"), 24});
  RouteFields<folly::IPAddressV4> routeV4_2(
      RoutePrefixV4{folly::IPAddressV4("192.168.1.0"), 24});
  fibV4.addNode(std::make_shared<RouteV4>(routeV4_1.toThrift()));
  fibV4.addNode(std::make_shared<RouteV4>(routeV4_2.toThrift()));
  fibContainer->setFib(fibV4.clone());

  // Create and add 3 IPv6 routes
  ForwardingInformationBaseV6 fibV6;
  RouteFields<folly::IPAddressV6> routeV6_1(
      RoutePrefixV6{folly::IPAddressV6("2001:db8::1"), 64});
  RouteFields<folly::IPAddressV6> routeV6_2(
      RoutePrefixV6{folly::IPAddressV6("2001:db8:1::"), 64});
  RouteFields<folly::IPAddressV6> routeV6_3(
      RoutePrefixV6{folly::IPAddressV6("2001:db8:2::"), 64});
  fibV6.addNode(std::make_shared<RouteV6>(routeV6_1.toThrift()));
  fibV6.addNode(std::make_shared<RouteV6>(routeV6_2.toThrift()));
  fibV6.addNode(std::make_shared<RouteV6>(routeV6_3.toThrift()));
  fibContainer->setFib(fibV6.clone());

  fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  fibInfo->resetFibsMap(fibsMap);

  // Add FibInfo to FibInfoMap
  auto matcher = createMatcher(10);
  fibInfoMap->updateFibInfo(fibInfo, matcher);

  // Verify route count after adding routes: 2 IPv4 and 3 IPv6
  auto [v4Count, v6Count] = fibInfoMap->getRouteCount();
  EXPECT_EQ(v4Count, 2);
  EXPECT_EQ(v6Count, 3);
}

TEST_F(MultiSwitchFibInfoMapTest, GetAllFibNodes) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();

  // Test empty map returns empty merged map
  auto emptyMergedMap = fibInfoMap->getAllFibNodes();
  EXPECT_NE(emptyMergedMap, nullptr);
  EXPECT_EQ(emptyMergedMap->size(), 0);

  // Create FibInfo objects for different switches with different RouterIDs
  auto fibInfo1 = createFibInfo(RouterID(0));
  auto fibInfo2 = createFibInfo(RouterID(1));
  auto fibInfo3 = createFibInfo(RouterID(2));

  // Add FibInfos to the map
  auto matcher1 = createMatcher(10);
  auto matcher2 = createMatcher(20);
  auto matcher3 = createMatcher(30);
  fibInfoMap->updateFibInfo(fibInfo1, matcher1);
  fibInfoMap->updateFibInfo(fibInfo2, matcher2);
  fibInfoMap->updateFibInfo(fibInfo3, matcher3);

  // Get merged map and verify it contains all FibContainers from all switches
  auto mergedMap = fibInfoMap->getAllFibNodes();
  EXPECT_EQ(mergedMap->size(), 3);

  // Verify all FibContainers are present in the merged map
  auto mergedContainer0 = mergedMap->getFibContainerIf(RouterID(0));
  EXPECT_EQ(mergedContainer0->getID(), RouterID(0));

  auto mergedContainer1 = mergedMap->getFibContainerIf(RouterID(1));
  EXPECT_EQ(mergedContainer1->getID(), RouterID(1));

  auto mergedContainer2 = mergedMap->getFibContainerIf(RouterID(2));
  EXPECT_EQ(mergedContainer2->getID(), RouterID(2));
}

TEST_F(MultiSwitchFibInfoMapTest, GetVrfCount) {
  auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();

  // Test empty map
  EXPECT_EQ(fibInfoMap->getVrfCount(), 0);

  // Add FibInfo with null fibsMap (empty VRF)
  auto emptyFibInfo = std::make_shared<FibInfo>();
  auto matcher1 = createMatcher(10);
  fibInfoMap->updateFibInfo(emptyFibInfo, matcher1);
  EXPECT_EQ(fibInfoMap->getVrfCount(), 0);

  // Add FibInfo with 1 VRF
  auto fibInfo1 = createFibInfo(RouterID(0));
  auto matcher2 = createMatcher(20);
  fibInfoMap->updateFibInfo(fibInfo1, matcher2);
  EXPECT_EQ(fibInfoMap->getVrfCount(), 1);

  // Add FibInfo with 2 VRFs
  auto fibInfo2 = std::make_shared<FibInfo>();
  auto fibsMap2 = std::make_shared<ForwardingInformationBaseMap>();
  fibsMap2->updateForwardingInformationBaseContainer(
      createFibContainer(RouterID(1)));
  fibsMap2->updateForwardingInformationBaseContainer(
      createFibContainer(RouterID(2)));
  fibInfo2->resetFibsMap(fibsMap2);
  auto matcher3 = createMatcher(30);
  fibInfoMap->updateFibInfo(fibInfo2, matcher3);
  EXPECT_EQ(fibInfoMap->getVrfCount(), 3);
}

TEST_F(FibInfoTest, SetAndGetIdToNextHopMap) {
  auto fibInfo = std::make_shared<FibInfo>();

  // Create and populate IdToNextHopMap
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  NextHopThrift nh1, nh2;
  nh1.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.1"));
  nh2.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.2"));
  idToNextHopMap->addNextHop(100, nh1);
  idToNextHopMap->addNextHop(200, nh2);

  // Set and verify
  fibInfo->setIdToNextHopMap(idToNextHopMap);
  auto retrieved = fibInfo->getIdToNextHopMap();
  EXPECT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size(), 2);
  EXPECT_NE(retrieved->getNextHopIf(100), nullptr);
  EXPECT_NE(retrieved->getNextHopIf(200), nullptr);
}

TEST_F(FibInfoTest, SetAndGetIdToNextHopIdSetMap) {
  auto fibInfo = std::make_shared<FibInfo>();

  // Create and populate IdToNextHopIdSetMap
  // NextHopSetIds must be >= 2^63 per spec
  constexpr NextHopSetId setId1 = (1ULL << 63) + 1;
  constexpr NextHopSetId setId2 = (1ULL << 63) + 2;
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  idToNextHopIdSetMap->addNextHopIdSet(setId1, {100, 200});
  idToNextHopIdSetMap->addNextHopIdSet(setId2, {300});

  // Set and verify
  fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);
  auto retrieved = fibInfo->getIdToNextHopIdSetMap();
  EXPECT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size(), 2);
  EXPECT_NE(retrieved->getNextHopIdSetIf(setId1), nullptr);
  EXPECT_NE(retrieved->getNextHopIdSetIf(setId2), nullptr);
  EXPECT_EQ(
      retrieved->getNextHopIdSet(setId1)->toThrift(),
      (std::set<NextHopId>{100, 200}));
  EXPECT_EQ(
      retrieved->getNextHopIdSet(setId2)->toThrift(),
      (std::set<NextHopId>{300}));
}

TEST_F(FibInfoTest, SetAndGetOverrideIdToNextHopIdSetMap) {
  auto fibInfo = std::make_shared<FibInfo>();
  const NextHopSetId overrideId =
      nextHopIdTypeStart(NextHopIdType::OVERRIDE_SET) + 1;
  auto overrideMap = std::make_shared<IdToNextHopIdSetMap>();
  overrideMap->addNextHopIdSet(overrideId, {100, 200});

  fibInfo->setOverrideIdToNextHopIdSetMap(overrideMap);

  auto retrieved = fibInfo->getOverrideIdToNextHopIdSetMap();
  ASSERT_NE(retrieved, nullptr);
  EXPECT_EQ(retrieved->size(), 1);
  EXPECT_EQ(
      retrieved->getNextHopIdSet(overrideId)->toThrift(),
      (std::set<NextHopId>{100, 200}));
  // Independent of the RIB owned map.
  EXPECT_EQ(fibInfo->getIdToNextHopIdSetMap()->size(), 0);
  validateNodeSerialization(*fibInfo);
}

TEST_F(FibInfoTest, ResolveNextHopSetFromId) {
  auto fibInfo = std::make_shared<FibInfo>();

  // Create IPv4 NextHops with interface names for resolution
  NextHopThrift nhV4_1, nhV4_2, nhV4_3;
  nhV4_1.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.1"));
  nhV4_1.address()->ifName() = "fboss1";
  *nhV4_1.weight() = UCMP_DEFAULT_WEIGHT;
  nhV4_2.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.2"));
  nhV4_2.address()->ifName() = "fboss2";
  *nhV4_2.weight() = UCMP_DEFAULT_WEIGHT;
  nhV4_3.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.3"));
  nhV4_3.address()->ifName() = "fboss3";
  *nhV4_3.weight() = UCMP_DEFAULT_WEIGHT;

  // Create IPv6 NextHops with interface names for resolution
  NextHopThrift nhV6_1, nhV6_2;
  nhV6_1.address() = network::toBinaryAddress(folly::IPAddress("2001:db8::1"));
  nhV6_1.address()->ifName() = "fboss4";
  *nhV6_1.weight() = UCMP_DEFAULT_WEIGHT;
  nhV6_2.address() = network::toBinaryAddress(folly::IPAddress("2001:db8::2"));
  nhV6_2.address()->ifName() = "fboss5";
  *nhV6_2.weight() = UCMP_DEFAULT_WEIGHT;

  // Assign NextHopIds
  NextHopId nhIdV4_1 = 1;
  NextHopId nhIdV4_2 = 2;
  NextHopId nhIdV4_3 = 3;
  NextHopId nhIdV6_1 = 4;
  NextHopId nhIdV6_2 = 5;

  // Create and populate IdToNextHopMap with both IPv4 and IPv6 nexthops
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  idToNextHopMap->addNextHop(nhIdV4_1, nhV4_1);
  idToNextHopMap->addNextHop(nhIdV4_2, nhV4_2);
  idToNextHopMap->addNextHop(nhIdV4_3, nhV4_3);
  idToNextHopMap->addNextHop(nhIdV6_1, nhV6_1);
  idToNextHopMap->addNextHop(nhIdV6_2, nhV6_2);

  // Create and populate IdToNextHopIdSetMap
  constexpr int64_t kSetIdOffset = 1LL << 62;
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();

  // Set with IPv4 nexthops only
  NextHopSetId setIdV4 = kSetIdOffset + 1;
  idToNextHopIdSetMap->addNextHopIdSet(setIdV4, {nhIdV4_1, nhIdV4_2, nhIdV4_3});

  // Set with IPv6 nexthops only
  NextHopSetId setIdV6 = kSetIdOffset + 2;
  idToNextHopIdSetMap->addNextHopIdSet(setIdV6, {nhIdV6_1, nhIdV6_2});

  // Set the maps on FibInfo
  fibInfo->setIdToNextHopMap(idToNextHopMap);
  fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);

  // Test resolveNextHopSetFromId with IPv4 set
  auto resolvedV4 = fibInfo->resolveNextHopSetFromId(setIdV4);
  EXPECT_EQ(resolvedV4.size(), 3);

  // Verify the actual IPv4 nexthops by sorting and comparing vectors
  std::sort(resolvedV4.begin(), resolvedV4.end());
  std::vector<NextHop> expectedV4{
      ResolvedNextHop(
          folly::IPAddress("10.0.0.1"), InterfaceID(1), UCMP_DEFAULT_WEIGHT),
      ResolvedNextHop(
          folly::IPAddress("10.0.0.2"), InterfaceID(2), UCMP_DEFAULT_WEIGHT),
      ResolvedNextHop(
          folly::IPAddress("10.0.0.3"), InterfaceID(3), UCMP_DEFAULT_WEIGHT)};
  std::sort(expectedV4.begin(), expectedV4.end());
  EXPECT_EQ(resolvedV4, expectedV4);

  // Test resolveNextHopSetFromId with IPv6 set
  auto resolvedV6 = fibInfo->resolveNextHopSetFromId(setIdV6);
  EXPECT_EQ(resolvedV6.size(), 2);

  // Verify the actual IPv6 nexthops by sorting and comparing vectors
  std::sort(resolvedV6.begin(), resolvedV6.end());
  std::vector<NextHop> expectedV6{
      ResolvedNextHop(
          folly::IPAddress("2001:db8::1"), InterfaceID(4), UCMP_DEFAULT_WEIGHT),
      ResolvedNextHop(
          folly::IPAddress("2001:db8::2"),
          InterfaceID(5),
          UCMP_DEFAULT_WEIGHT)};
  std::sort(expectedV6.begin(), expectedV6.end());
  EXPECT_EQ(resolvedV6, expectedV6);
}

TEST_F(FibInfoTest, ResolveNextHopSetFromIdNonExistentId) {
  constexpr int64_t kSetIdOffset = 1LL << 62;
  auto fibInfo = std::make_shared<FibInfo>();

  // Set up IdToNextHopIdSetMap with one valid set
  auto idToNextHopIdSetMap = std::make_shared<IdToNextHopIdSetMap>();
  NextHopSetId validSetId = kSetIdOffset + 1;
  NextHopSetId nonExistentSetId = kSetIdOffset + 999;
  idToNextHopIdSetMap->addNextHopIdSet(validSetId, {1, 2});
  fibInfo->setIdToNextHopIdSetMap(idToNextHopIdSetMap);

  // Set up IdToNextHopMap but only add NextHopId 1, not 2
  auto idToNextHopMap = std::make_shared<IdToNextHopMap>();
  NextHopThrift nh1;
  nh1.address() = network::toBinaryAddress(folly::IPAddress("10.0.0.1"));
  *nh1.weight() = UCMP_DEFAULT_WEIGHT;
  idToNextHopMap->addNextHop(1, nh1);
  fibInfo->setIdToNextHopMap(idToNextHopMap);

  // Throws FbossError when NextHopSetId is not found
  EXPECT_THROW(fibInfo->resolveNextHopSetFromId(nonExistentSetId), FbossError);

  // Throws FbossError when NextHopId is not found in IdToNextHopMap
  EXPECT_THROW(fibInfo->resolveNextHopSetFromId(validSetId), FbossError);
}

TEST_F(FibInfoTest, GetNextHopSetIdRefCountsFromRoutesEmpty) {
  auto fibInfo = std::make_shared<FibInfo>();
  auto refCounts = fibInfo->getNextHopSetIdRefCountsFromRoutes();
  EXPECT_TRUE(refCounts.empty());

  fibInfo = createFibInfo(RouterID(0));
  refCounts = fibInfo->getNextHopSetIdRefCountsFromRoutes();
  EXPECT_TRUE(refCounts.empty());
}

TEST_F(FibInfoTest, GetNextHopSetIdRefCountsFromRoutes) {
  auto fibInfo = std::make_shared<FibInfo>();
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto fibContainer =
      std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));

  NextHopSetID clientId1(10);
  NextHopSetID resolvedId1(100);
  NextHopSetID normalizedId1(100);
  NextHopSetID clientId2(20);
  NextHopSetID resolvedId2(200);
  NextHopSetID normalizedId2(100);
  NextHopSetID clientId3(30);
  NextHopSetID resolvedId3(300);
  NextHopSetID normalizedId3(300);

  auto makeNextHop = [](const std::string& ip) {
    return ResolvedNextHop(
        folly::IPAddress(ip), InterfaceID(1), UCMP_DEFAULT_WEIGHT);
  };

  auto makeFwdInfo = [&makeNextHop](
                         const std::string& ip,
                         std::optional<NextHopSetID> resolvedId,
                         std::optional<NextHopSetID> normalizedId) {
    RouteNextHopEntry fwd(
        RouteNextHopEntry::NextHopSet{makeNextHop(ip)}, AdminDistance::EBGP);
    fwd.setResolvedNextHopSetID(resolvedId);
    fwd.setNormalizedResolvedNextHopSetID(normalizedId);
    return fwd;
  };
  auto setBestEntry = [&makeNextHop](
                          const auto& route,
                          const std::string& ip,
                          std::optional<NextHopSetID> clientId) {
    RouteNextHopEntry nonPreferredEntry(
        RouteNextHopEntry::NextHopSet{makeNextHop(ip)}, AdminDistance::IBGP);
    std::optional<NextHopSetID> nonPreferredId{NextHopSetID(999)};
    nonPreferredEntry.setClientNextHopSetID(nonPreferredId);
    route->update(ClientID::OPENR, nonPreferredEntry);
    RouteNextHopEntry entry(
        RouteNextHopEntry::NextHopSet{makeNextHop(ip)}, AdminDistance::EBGP);
    entry.setClientNextHopSetID(clientId);
    route->update(ClientID::BGPD, entry);
  };

  auto fwd1 = makeFwdInfo("10.0.0.1", resolvedId1, normalizedId1);
  auto fwd2 = makeFwdInfo("10.0.0.2", resolvedId2, normalizedId2);
  auto fwd3 = makeFwdInfo("2001:db8::1", resolvedId3, normalizedId3);

  ForwardingInformationBaseV4 fibV4;
  auto route1 = std::make_shared<RouteV4>(
      RouteFields<folly::IPAddressV4>(
          RoutePrefixV4{folly::IPAddressV4("10.0.0.0"), 24})
          .toThrift());
  setBestEntry(route1, "10.0.0.1", clientId1);
  route1->setResolved(fwd1);
  fibV4.addNode(route1);

  auto route2 = std::make_shared<RouteV4>(
      RouteFields<folly::IPAddressV4>(
          RoutePrefixV4{folly::IPAddressV4("192.168.1.0"), 24})
          .toThrift());
  setBestEntry(route2, "10.0.0.2", clientId2);
  route2->setResolved(fwd2);
  fibV4.addNode(route2);
  fibContainer->setFib(fibV4.clone());

  ForwardingInformationBaseV6 fibV6;
  auto route3 = std::make_shared<RouteV6>(
      RouteFields<folly::IPAddressV6>(
          RoutePrefixV6{folly::IPAddressV6("2001:db8::"), 64})
          .toThrift());
  setBestEntry(route3, "2001:db8::1", clientId3);
  route3->setResolved(fwd3);
  fibV6.addNode(route3);
  fibContainer->setFib(fibV6.clone());

  fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  fibInfo->resetFibsMap(fibsMap);

  auto refCounts = fibInfo->getNextHopSetIdRefCountsFromRoutes();
  EXPECT_EQ(refCounts.count(NextHopSetID(999)), 0);
  // clientId1(10): client(route1) = 1
  EXPECT_EQ(refCounts[clientId1], 1);
  // clientId2(20): client(route2) = 1
  EXPECT_EQ(refCounts[clientId2], 1);
  // clientId3(30): client(route3) = 1
  EXPECT_EQ(refCounts[clientId3], 1);
  // resolvedId1(100): resolved(route1) + normalized(route1) +
  // normalized(route2) = 3
  EXPECT_EQ(refCounts[resolvedId1], 3);
  // resolvedId2(200): resolved(route2) = 1
  EXPECT_EQ(refCounts[resolvedId2], 1);
  // resolvedId3(300): resolved(route3) + normalized(route3) = 2
  EXPECT_EQ(refCounts[resolvedId3], 2);
}

TEST_F(FibInfoTest, GetNextHopSetIdRefCountsFromRoutesRecursiveResolution) {
  auto fibInfo = std::make_shared<FibInfo>();
  auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
  auto fibContainer =
      std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));

  // Simulate named NHG scenario:
  // Named NHG "nhg1" has clientNextHopSetID=42 (unresolved loopback nexthops)
  // After recursive resolution, resolvedNextHopSetID=500 (link-local nexthops)
  NextHopSetID namedNhgClientId(42);
  NextHopSetID resolvedLinkLocalId(500);

  auto makeNextHop = [](const std::string& ip) {
    return ResolvedNextHop(
        folly::IPAddress(ip), InterfaceID(1), UCMP_DEFAULT_WEIGHT);
  };

  RouteNextHopEntry fwd(
      RouteNextHopEntry::NextHopSet{makeNextHop("fe80::1")},
      AdminDistance::EBGP);
  std::optional<NextHopSetID> clientOpt = namedNhgClientId;
  std::optional<NextHopSetID> resolvedOpt = resolvedLinkLocalId;
  fwd.setResolvedNextHopSetID(resolvedOpt);
  fwd.setNormalizedResolvedNextHopSetID(resolvedOpt);

  ForwardingInformationBaseV6 fibV6;
  auto route = std::make_shared<RouteV6>(
      RouteFields<folly::IPAddressV6>(
          RoutePrefixV6{folly::IPAddressV6("2001:db8::"), 48})
          .toThrift());
  RouteNextHopEntry clientEntry(
      RouteNextHopEntry::NextHopSet{makeNextHop("2001:db8::1")},
      AdminDistance::EBGP);
  clientEntry.setClientNextHopSetID(clientOpt);
  clientEntry.setNamedNextHopGroup("nhg1");
  route->update(ClientID::BGPD, clientEntry);
  route->setResolved(fwd);
  fibV6.addNode(route);
  fibContainer->setFib(fibV6.clone());

  fibsMap->updateForwardingInformationBaseContainer(fibContainer);
  fibInfo->resetFibsMap(fibsMap);

  auto refCounts = fibInfo->getNextHopSetIdRefCountsFromRoutes();
  // The named NHG's clientNextHopSetID should be in the refcounts
  EXPECT_EQ(refCounts[namedNhgClientId], 1);
  // The resolved link-local ID should also be counted (resolved + normalized)
  EXPECT_EQ(refCounts[resolvedLinkLocalId], 2);
}

TEST_F(FibInfoTest, GetNextHopSetIdRefCountsFromMySidEmpty) {
  std::unordered_map<NextHopSetID, uint32_t> refCounts;
  FibInfo::getNextHopSetIdRefCountsFromMySid(state_, refCounts);
  EXPECT_TRUE(refCounts.empty());
}

TEST_F(FibInfoTest, GetNextHopSetIdRefCountsFromMySid) {
  NextHopSetID setId1(100);
  NextHopSetID setId2(200);

  auto makeMySidEntry = [](const std::string& ip, uint8_t len) {
    state::MySidFields fields;
    fields.type() = MySidType::NODE_MICRO_SID;
    facebook::network::thrift::IPPrefix prefix;
    prefix.prefixAddress() =
        facebook::network::toBinaryAddress(folly::IPAddress(ip));
    prefix.prefixLength() = len;
    fields.mySid() = prefix;
    return std::make_shared<MySid>(fields);
  };

  auto mySid1 = makeMySidEntry("fc00:100::1", 48);
  mySid1->setResolvedNextHopsId(setId1);

  auto mySid2 = makeMySidEntry("fc00:200::1", 48);
  mySid2->setResolvedNextHopsId(setId1);
  mySid2->setUnresolveNextHopsId(setId2);

  auto mySid3 = makeMySidEntry("fc00:300::1", 48);
  mySid3->setResolvedNextHopsId(setId2);

  auto matcher = createMatcher(0);
  auto mySids = state_->getMySids()->modify(&state_);
  mySids->addNode(mySid1, matcher);
  mySids->addNode(mySid2, matcher);
  mySids->addNode(mySid3, matcher);

  std::unordered_map<NextHopSetID, uint32_t> refCounts;
  FibInfo::getNextHopSetIdRefCountsFromMySid(state_, refCounts);
  // setId1: resolved(mySid1) + resolved(mySid2) = 2
  EXPECT_EQ(refCounts[setId1], 2);
  // setId2: unresolved(mySid2) + resolved(mySid3) = 2
  EXPECT_EQ(refCounts[setId2], 2);
}

} // namespace facebook::fboss
