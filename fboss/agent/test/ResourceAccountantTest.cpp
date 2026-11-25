/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/core.h>
#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/ResourceAccountant.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/test/ResourceAccountantTestBase.h"

namespace facebook::fboss {

class ResourceAccountantTest : public ResourceAccountantTestBase {
 public:
  void SetUp() override {
    ResourceAccountantTestBase::SetUp();
    makeResourceAccountant();
  }
  void makeResourceAccountant() {
    resourceAccountant_ = std::make_unique<ResourceAccountant>(
        asicTable_.get(), scopeResolver_.get());
  }

  std::unique_ptr<ResourceAccountant> resourceAccountant_;
};

TEST_F(ResourceAccountantTest, getMemberCountForEcmpGroup) {
  const auto ecmpWidth = (getMaxEcmpWidth() / 2) + 1;

  RouteNextHopSet ecmpNexthops;
  for (int i = 0; i < ecmpWidth; i++) {
    ecmpNexthops.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }
  RouteNextHopEntry ecmpNextHopEntry =
      RouteNextHopEntry(ecmpNexthops, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(ecmpNextHopEntry, ecmpNexthops);
  EXPECT_EQ(
      ecmpWidth,
      this->resourceAccountant_->getMemberCountForEcmpGroup(
          ecmpNextHopEntry, state_));

  RouteNextHopSet ucmpNexthops;
  for (int i = 0; i < ecmpWidth; i++) {
    ucmpNexthops.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
        InterfaceID(i + 1),
        i + 1));
  }
  RouteNextHopEntry ucmpNextHopEntry =
      RouteNextHopEntry(ucmpNexthops, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(ucmpNextHopEntry, ucmpNexthops);
  auto expectedTotalMember = 0;
  for (const auto& nhop : ucmpNextHopEntry.normalizedNextHops()) {
    expectedTotalMember += nhop.weight();
  }
  EXPECT_EQ(
      expectedTotalMember,
      this->resourceAccountant_->getMemberCountForEcmpGroup(
          ucmpNextHopEntry, state_));
}

TEST_F(ResourceAccountantTest, checkArsResource) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  // MockAsic is configured to support 7 DLB groups
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));

  auto addEcmp = [](int i, std::vector<RouteNextHopSet>& ecmpList) {
    ecmpList.push_back(
        RouteNextHopSet{
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
                InterfaceID(i + 1),
                ecmpWeight),
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 2)),
                InterfaceID(i + 2),
                ecmpWeight)});
  };
  int i = 0;
  std::vector<RouteNextHopSet> ecmpNexthopsList;
  ecmpNexthopsList.reserve(getMaxArsGroups());
  for (i = 0; i < getMaxArsGroups() - 2; i++) {
    addEcmp(i, ecmpNexthopsList);
  }
  for (const auto& nhopSet : ecmpNexthopsList) {
    this->resourceAccountant_->arsEcmpGroupRefMap_[nhopSet] = 1;
  }
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      false /* intermediateState */));

  // add 1 more should take it over dlb resource percentage
  addEcmp(i++, ecmpNexthopsList);
  for (const auto& nhopSet : ecmpNexthopsList) {
    this->resourceAccountant_->arsEcmpGroupRefMap_[nhopSet] = 1;
  }
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkArsResource(
      false /* intermediateState */));

  // add 2 more should take it over 100%
  addEcmp(i++, ecmpNexthopsList);
  addEcmp(i++, ecmpNexthopsList);
  for (const auto& nhopSet : ecmpNexthopsList) {
    this->resourceAccountant_->arsEcmpGroupRefMap_[nhopSet] = 1;
  }
  EXPECT_FALSE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkArsResource(
      false /* intermediateState */));
}

TEST_F(ResourceAccountantTest, checkEcmpResource) {
  // MockAsic is configured to support 20 group and 128 members.
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add one ECMP group with max ECMP membere
  const auto ecmpWidth = getMaxEcmpWidth();

  // Add 3 groups of max ecmp width. This consumes 75% of ECMP members
  std::vector<RouteNextHopSet> ecmpNexthops;
  for (int i = 0; i < 3; ++i) {
    RouteNextHopSet ecmpNexthopsCur;
    for (int j = 0; j < ecmpWidth; j++) {
      ecmpNexthopsCur.insert(ResolvedNextHop(
          folly::IPAddress(folly::to<std::string>(i + 1, ".1.1.", j + 1)),
          InterfaceID(i + 1),
          ecmpWeight));
    }
    this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthopsCur] = {
        .refCountNonVirtual = 1};
    this->resourceAccountant_->ecmpMemberUsage_ += ecmpWidth;
    ecmpNexthops.push_back(std::move(ecmpNexthopsCur));
  }
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add a ecmp groups with 2 members. So resource usage now exceeds
  // 75% of max ecmp members

  RouteNextHopSet ecmpNexthops4;
  for (int i = 0; i < 2; i++) {
    ecmpNexthops4.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("100.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops4] = {
      .refCountNonVirtual = 1};
  this->resourceAccountant_->ecmpMemberUsage_ += 2;
  // Intermediate limit is 100%, which is not violated
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  // End limit is 75% which is now violated
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add another ECMP group with max ECMP width
  RouteNextHopSet ecmpNexthops5;
  for (int i = 0; i < ecmpWidth; i++) {
    ecmpNexthops4.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("4.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops5] = {
      .refCountNonVirtual = 1};
  this->resourceAccountant_->ecmpMemberUsage_ += ecmpWidth;
  // Both intermediate and end limit are violated
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Remove ecmpGroup4, 5:
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops4);
  this->resourceAccountant_->ecmpMemberUsage_ -= 2;
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops5);
  this->resourceAccountant_->ecmpMemberUsage_ -= ecmpWidth;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add three groups (with width = 2 each) and remove ecmpGroup0
  std::vector<RouteNextHopSet> ecmpNexthopsList;
  ecmpNexthopsList.reserve(
      (getMaxEcmpGroups() * FLAGS_ecmp_resource_percentage / 100.0));
  for (int i = ecmpNexthops.size();
       i < (getMaxEcmpGroups() * FLAGS_ecmp_resource_percentage / 100.0);
       i++) {
    ecmpNexthopsList.push_back(
        RouteNextHopSet{
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
                InterfaceID(i + 1),
                ecmpWeight),
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 2)),
                InterfaceID(i + 2),
                ecmpWeight)});
  }
  for (const auto& nhopSet : ecmpNexthopsList) {
    this->resourceAccountant_->ecmpGroupRefMap_[nhopSet] = {
        .refCountNonVirtual = 1};
    this->resourceAccountant_->ecmpMemberUsage_ += 2;
  }
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops[0]);
  this->resourceAccountant_->ecmpMemberUsage_ -= ecmpWidth;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add one more group to exceed group limit
  RouteNextHopSet ecmpNexthops2 = RouteNextHopSet{
      ResolvedNextHop(
          folly::IPAddress(folly::to<std::string>("3.1.1.1")),
          InterfaceID(1),
          ecmpWeight),
      ResolvedNextHop(
          folly::IPAddress(folly::to<std::string>("3.1.1.2")),
          InterfaceID(2),
          ecmpWeight)};
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops2] = {
      .refCountNonVirtual = 1};
  this->resourceAccountant_->ecmpMemberUsage_ += 2;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));
}

TEST_F(ResourceAccountantTest, checkEcmpResourceForUcmpWeights) {
  // MockAsic is configured to support 20 group and 128 members.
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  const auto ecmpWidth = getMaxEcmpWidth();

  // Add 3 groups of max ecmp width. This consumes 75% of UCMP members
  std::vector<RouteNextHopSet> ecmpNexthops =
      getUcmpNextHops(ecmpWidth, 3 /*numGroups*/, 0 /*seed*/);
  for (const auto& ecmpNexthopsCur : ecmpNexthops) {
    this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthopsCur] = {
        .refCountNonVirtual = 1};
    this->resourceAccountant_->ecmpMemberUsage_ += ecmpWidth;
  }
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add a ucmp groups with 2 members. So resource usage now exceeds
  // 75% of max ucmp members
  auto singleGroup = getUcmpNextHops(ecmpWidth, 1 /*numGroups*/, 1 /*seed*/);
  RouteNextHopSet ecmpNexthops2 = singleGroup[0];

  this->resourceAccountant_->ecmpMemberUsage_ += 5;
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops2] = {
      .refCountNonVirtual = 1};
  // Intermediate limit is 100%, which is not violated
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  // End limit is 75% which is now violated
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add another UCMP group with max ECMP width
  auto singleGroup2 = getUcmpNextHops(ecmpWidth, 1 /*numGroups*/, 2 /*seed*/);
  RouteNextHopSet ecmpNexthops3 = singleGroup2[0];
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops3] = {
      .refCountNonVirtual = 1};
  this->resourceAccountant_->ecmpMemberUsage_ += ecmpWidth;
  // Both intermediate and end limit are violated
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Remove ecmpGroup4, 5:
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops2);
  this->resourceAccountant_->ecmpMemberUsage_ -= 5;
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops3);
  this->resourceAccountant_->ecmpMemberUsage_ -= ecmpWidth;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add more 13 more groups (with width = 5 each) and remove ucmpGroup0
  std::vector<RouteNextHopSet> ecmpNexthopsList;
  ecmpNexthopsList.reserve(
      (getMaxEcmpGroups() * FLAGS_ecmp_resource_percentage / 100.0));
  ecmpNexthopsList = getUcmpNextHops(5, 13 /*numGroups*/, 3 /*seed*/);
  for (const auto& nhopSet : ecmpNexthopsList) {
    this->resourceAccountant_->ecmpGroupRefMap_[nhopSet] = {
        .refCountNonVirtual = 1};
    this->resourceAccountant_->ecmpMemberUsage_ += 5;
  }
  this->resourceAccountant_->ecmpGroupRefMap_.erase(ecmpNexthops[0]);
  this->resourceAccountant_->ecmpMemberUsage_ -= ecmpWidth;

  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add one more group to exceed group limit
  auto singleGroup3 = getUcmpNextHops(ecmpWidth, 1 /*numGroups*/, 4 /*seed*/);
  RouteNextHopSet ecmpNexthops4 = singleGroup3[0];
  this->resourceAccountant_->ecmpGroupRefMap_[ecmpNexthops4] = {
      .refCountNonVirtual = 1};
  this->resourceAccountant_->ecmpMemberUsage_ += 5;

  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Add 5 more groups with 2 members each and width 5. This consumes 100% of
  // UCMP groups
  std::vector<RouteNextHopSet> ecmpNexthopsListFinal =
      getUcmpNextHops(5, 5 /*numGroups*/, 5 /*seed*/);

  for (const auto& nhopSet : ecmpNexthopsListFinal) {
    this->resourceAccountant_->ecmpGroupRefMap_[nhopSet] = {
        .refCountNonVirtual = 1};
    this->resourceAccountant_->ecmpMemberUsage_ += 5;
  }

  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));
}

TEST_F(ResourceAccountantTest, checkAndUpdateGenericEcmpResource) {
  // Add one ECMP group with ECMP width limit
  const auto ecmpWidth = getMaxEcmpWidth();
  auto constexpr kEcmpGroupsOfHalfWidth = 7;
  std::array<RouteNextHopSet, kEcmpGroupsOfHalfWidth> ecmpNexthops;
  std::array<std::shared_ptr<RouteV6>, kEcmpGroupsOfHalfWidth> routes;
  for (int i = 0; i < kEcmpGroupsOfHalfWidth; ++i) {
    for (int j = 0; j < ecmpWidth / 2; j++) {
      ecmpNexthops[i].insert(ResolvedNextHop(
          folly::IPAddress(folly::to<std::string>(i + 1, "::", j + 1)),
          InterfaceID(j + 1),
          ecmpWeight));
    }
    routes[i] = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("100::", i + 1)), 128},
        {ecmpNexthops[i], AdminDistance::EBGP},
        ecmpNexthops[i]);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        routes[i], true /* add */, state_));
  }

  // Add another ECMP group with max ECMP width
  RouteNextHopSet ecmpNexthops8;
  for (int i = 0; i < ecmpWidth; i++) {
    ecmpNexthops8.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("2.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }
  const auto route8 = makeV6Route(
      {folly::IPAddressV6("200::1"), 128},
      {ecmpNexthops8, AdminDistance::EBGP},
      ecmpNexthops8);
  EXPECT_FALSE(this->resourceAccountant_
                   ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                       route8, true /* add */, state_));

  // Remove above route
  EXPECT_TRUE(this->resourceAccountant_
                  ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                      route8, false /* add */, state_));
  // Add more groups (with width = 2 each) and
  const auto groupsToAdd = getMaxEcmpGroups() - kEcmpGroupsOfHalfWidth + 1;
  for (int i = 0; i < groupsToAdd; i++) {
    auto ecmpNextHops = RouteNextHopSet{
        ResolvedNextHop(
            folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
            InterfaceID(i + 1),
            ecmpWeight),
        ResolvedNextHop(
            folly::IPAddress(folly::to<std::string>("1.1.1.", i + 2)),
            InterfaceID(i + 2),
            ecmpWeight)};
    const auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("300::", i + 1)), 128},
        {ecmpNextHops, AdminDistance::EBGP},
        ecmpNextHops);
    if (i < groupsToAdd - 1) {
      EXPECT_TRUE(this->resourceAccountant_
                      ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                          route, true /* add */, state_));
    } else {
      EXPECT_FALSE(this->resourceAccountant_
                       ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                           route, true /* add */, state_));
    }
  }

  EXPECT_TRUE(this->resourceAccountant_
                  ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                      routes[0], false /* add */, state_));
}

TEST_F(
    ResourceAccountantTest,
    checkAndUpdateGenericEcmpResourceForUcmpWeights) {
  // Add one ECMP group with ECMP width limit
  const auto ecmpWidth = getMaxEcmpWidth();
  auto constexpr kUcmpGroupsWithWeights = 7;
  auto constexpr kTotalWeightForUCMP = 60;
  std::array<RouteNextHopSet, kUcmpGroupsWithWeights> ecmpNexthops;
  std::array<std::shared_ptr<RouteV6>, kUcmpGroupsWithWeights> routes;

  // Configure kUcmpGroupsWithWeights number of UCMP groups with
  // kTotalWeightForUCMP each
  std::vector<RouteNextHopSet> ecmpNextHops = getUcmpNextHops(
      kTotalWeightForUCMP, kUcmpGroupsWithWeights /*numGroups*/, 0 /*seed*/);
  for (int i = 0; i < kUcmpGroupsWithWeights; i++) {
    ecmpNexthops[i] = ecmpNextHops[i]; // Copy the generated groups
    routes[i] = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("100::", i + 1)), 128},
        {ecmpNexthops[i], AdminDistance::EBGP},
        ecmpNexthops[i]);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                        routes[i], true /* add */, state_));
  }

  // Add another UCMP group where total weights of all members in all groups  >
  // maxEcmpMembers
  auto singleGroup8 = getUcmpNextHops(ecmpWidth, 1 /*numGroups*/, 1 /*seed*/);
  RouteNextHopSet ecmpNexthops8 = singleGroup8[0];
  const auto route8 = makeV6Route(
      {folly::IPAddressV6("200::1"), 128},
      {ecmpNexthops8, AdminDistance::EBGP},
      ecmpNexthops8);
  EXPECT_FALSE(this->resourceAccountant_
                   ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                       route8, true /* add */, state_));

  // Remove above route
  EXPECT_TRUE(this->resourceAccountant_
                  ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                      route8, false /* add */, state_));

  //  Add more groups (with width = 5 each) and
  // Testing max UCMP groups > MaxEcmpGroups returns False.
  const auto groupsToAdd = getMaxEcmpGroups() - kUcmpGroupsWithWeights + 1;
  std::vector<RouteNextHopSet> ecmpNextHopsRemaining =
      getUcmpNextHops(5, groupsToAdd /*numGroups*/, 2 /*seed*/);
  for (int i = 0; i < groupsToAdd; i++) {
    const auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("300::", i + 1)), 128},
        {ecmpNextHopsRemaining[i], AdminDistance::EBGP},
        ecmpNextHopsRemaining[i]);
    if (i < groupsToAdd - 1) {
      EXPECT_TRUE(this->resourceAccountant_
                      ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                          route, true /* add */, state_));
    } else {
      EXPECT_FALSE(this->resourceAccountant_
                       ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                           route, true /* add */, state_));
    }
  }

  EXPECT_TRUE(this->resourceAccountant_
                  ->checkAndUpdateGenericEcmpResource<folly::IPAddressV6>(
                      routes[0], false /* add */, state_));
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceAccounting) {
  std::vector<NextHop> protectionNextHopList{
      roleNextHop("1.1.1.1", 1, NextHopRole::PRIMARY)};
  for (int i = 0; i < 8; ++i) {
    protectionNextHopList.emplace_back(roleNextHop(
        folly::to<std::string>("1.1.2.", i + 1), i + 2, NextHopRole::BACKUP));
  }
  const RouteNextHopSet protectionNextHops(
      protectionNextHopList.begin(), protectionNextHopList.end());
  const RouteNextHopEntry entry(protectionNextHops, AdminDistance::EBGP);
  const auto route1 = makeV6Route(
      {folly::IPAddressV6("400::1"), 128}, entry, protectionNextHops);
  const auto route2 = makeV6Route(
      {folly::IPAddressV6("400::2"), 128}, entry, protectionNextHops);
  const auto initialMemberUsage = resourceAccountant_->ecmpMemberUsage_;

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route1, true /* add */, state_));
  // One group keyed by the full set, charged only for its single primary.
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  EXPECT_EQ(
      resourceAccountant_->ecmpGroupRefMap_.find(protectionNextHops)
          ->second.refCountNonVirtual,
      1);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 1);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route2, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  EXPECT_EQ(
      resourceAccountant_->ecmpGroupRefMap_.find(protectionNextHops)
          ->second.refCountNonVirtual,
      2);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 1);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route1, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route2, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceSharing) {
  const auto initialMemberUsage = resourceAccountant_->ecmpMemberUsage_;
  const RouteNextHopSet protectionNextHops1{
      roleNextHop("2.1.1.1", 1, NextHopRole::BACKUP),
      roleNextHop("2.1.1.2", 2, NextHopRole::BACKUP),
      roleNextHop("2.1.2.1", 3, NextHopRole::PRIMARY)};
  const RouteNextHopSet protectionNextHops2{
      roleNextHop("2.1.1.1", 1, NextHopRole::BACKUP),
      roleNextHop("2.1.1.2", 2, NextHopRole::BACKUP),
      roleNextHop("2.1.2.2", 4, NextHopRole::PRIMARY)};
  const RouteNextHopEntry entry1(protectionNextHops1, AdminDistance::EBGP);
  const RouteNextHopEntry entry2(protectionNextHops2, AdminDistance::EBGP);
  const auto route1 = makeV6Route(
      {folly::IPAddressV6("500::1"), 128}, entry1, protectionNextHops1);
  const auto route2 = makeV6Route(
      {folly::IPAddressV6("500::2"), 128}, entry2, protectionNextHops2);
  const auto route3 = makeV6Route(
      {folly::IPAddressV6("500::3"), 128}, entry2, protectionNextHops2);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route1, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route2, true /* add */, state_));
  // Distinct protection sets are independent groups, each charged for its one
  // primary. The backups they have in common cost nothing either way.
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 2);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 2);

  // A second route on the same set only bumps the reference count.
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route3, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 2);
  EXPECT_EQ(
      resourceAccountant_->ecmpGroupRefMap_.find(protectionNextHops2)
          ->second.refCountNonVirtual,
      2);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 2);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route1, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route2, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      route3, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceEdgeCases) {
  const auto initialMemberUsage = resourceAccountant_->ecmpMemberUsage_;
  const RouteNextHopSet singletonBackup{
      roleNextHop("3.1.1.1", 1, NextHopRole::BACKUP)};
  const auto singletonRoute = makeV6Route(
      {folly::IPAddressV6("600::1"), 128},
      {singletonBackup, AdminDistance::EBGP},
      singletonBackup);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      singletonRoute, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);

  const RouteNextHopSet multiplePrimaries{
      roleNextHop("3.1.3.1", 5, NextHopRole::PRIMARY),
      roleNextHop("3.1.3.2", 6, NextHopRole::PRIMARY),
      roleNextHop("3.1.3.3", 7, NextHopRole::BACKUP)};
  const auto multiPrimaryRoute = makeV6Route(
      {folly::IPAddressV6("600::3"), 128},
      {multiplePrimaries, AdminDistance::EBGP},
      multiplePrimaries);
  // Only the two primaries are charged as members.
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      multiPrimaryRoute, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 2);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      multiPrimaryRoute, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);

  // The same weighted primaries without a backup are an ordinary ECMP group
  // and still pay the asic's UCMP cost.
  const RouteNextHopSet weightedPrimariesOnly{
      roleNextHop("3.1.4.1", 8, NextHopRole::PRIMARY, 2),
      roleNextHop("3.1.4.2", 9, NextHopRole::PRIMARY, 2)};
  const auto weightedPrimariesRoute = makeV6Route(
      {folly::IPAddressV6("600::4"), 128},
      {weightedPrimariesOnly, AdminDistance::EBGP},
      weightedPrimariesOnly);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      weightedPrimariesRoute, true /* add */, state_));
  const auto weightedPrimariesUsage =
      resourceAccountant_->ecmpMemberUsage_ - initialMemberUsage;
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      weightedPrimariesRoute, false /* add */, state_));

  const RouteNextHopSet weightedProtection{
      roleNextHop("3.1.4.1", 8, NextHopRole::PRIMARY, 2),
      roleNextHop("3.1.4.2", 9, NextHopRole::PRIMARY, 2),
      roleNextHop("3.1.4.3", 10, NextHopRole::BACKUP, 1)};
  const auto weightedRoute = makeV6Route(
      {folly::IPAddressV6("600::5"), 128},
      {weightedProtection, AdminDistance::EBGP},
      weightedProtection);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      weightedRoute, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  // Sai leaves protection members unweighted, so the two primaries cost one
  // member each rather than the asic's UCMP cost.
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 2);
  EXPECT_GT(weightedPrimariesUsage, 2);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      weightedRoute, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceLimits) {
  const auto initialMemberUsage = resourceAccountant_->ecmpMemberUsage_;
  std::vector<std::shared_ptr<RouteV6>> routes;
  // Each protection route is one group holding its single primary, so filling
  // to the group limit takes as many routes as the asic allows groups.
  for (uint64_t i = 0; i < getMaxEcmpGroups(); ++i) {
    const RouteNextHopSet protectionNextHops{
        roleNextHop(
            folly::to<std::string>("10.1.", i + 1, ".1"),
            (2 * i) + 1,
            NextHopRole::PRIMARY),
        roleNextHop(
            folly::to<std::string>("10.1.", i + 1, ".2"),
            (2 * i) + 2,
            NextHopRole::BACKUP)};
    routes.push_back(makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("800::", i + 1)), 128},
        {protectionNextHops, AdminDistance::EBGP},
        protectionNextHops));
    EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
        routes.back(), true /* add */, state_));
  }

  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), getMaxEcmpGroups());
  EXPECT_EQ(
      resourceAccountant_->ecmpMemberUsage_,
      initialMemberUsage + getMaxEcmpGroups());
  // Sitting exactly on the asic limit passes the intermediate check but not
  // the percentage based one.
  EXPECT_TRUE(
      resourceAccountant_->checkEcmpResource(true /* intermediateState */));
  EXPECT_FALSE(
      resourceAccountant_->checkEcmpResource(false /* intermediateState */));

  for (const auto& route : routes) {
    EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
        route, false /* add */, state_));
  }
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceLifecycle) {
  const auto initialMemberUsage = resourceAccountant_->ecmpMemberUsage_;
  const RouteNextHopSet ordinaryNextHops{
      roleNextHop("11.1.1.1", 1, NextHopRole::PRIMARY),
      roleNextHop("11.1.1.2", 2, NextHopRole::PRIMARY)};
  const RouteNextHopSet protectionNextHops{
      roleNextHop("11.1.2.1", 3, NextHopRole::PRIMARY),
      roleNextHop("11.1.2.2", 4, NextHopRole::BACKUP)};
  const auto ordinaryRoute = makeV6Route(
      {folly::IPAddressV6("900::1"), 128},
      {ordinaryNextHops, AdminDistance::EBGP},
      ordinaryNextHops);
  const auto protectionRoute = makeV6Route(
      {folly::IPAddressV6("900::1"), 128},
      {protectionNextHops, AdminDistance::EBGP},
      protectionNextHops);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      ordinaryRoute, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      protectionRoute, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      ordinaryRoute, false /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 1);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      ordinaryRoute, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      protectionRoute, false /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage + 2);
  EXPECT_TRUE(resourceAccountant_->checkAndUpdateGenericEcmpResource(
      ordinaryRoute, false /* add */, state_));
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, initialMemberUsage);
}

TEST_F(ResourceAccountantTest, protectionEcmpResourceArsExcluded) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  resourceAccountant_->setMinWidthForArsVirtualGroup(2);
  resourceAccountant_->setMaxArsVirtualGroups(256);
  resourceAccountant_->setMaxArsVirtualGroupWidth(256);
  const RouteNextHopSet protectionNextHops{
      roleNextHop("4.1.1.1", 1, NextHopRole::PRIMARY),
      roleNextHop("4.1.1.2", 2, NextHopRole::BACKUP),
      roleNextHop("4.1.1.3", 3, NextHopRole::BACKUP)};
  const auto route = makeV6Route(
      {folly::IPAddressV6("700::1"), 128},
      {protectionNextHops, AdminDistance::EBGP},
      protectionNextHops);

  EXPECT_TRUE(resourceAccountant_->checkAndUpdateEcmpResource(
      route, true /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->arsEcmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->virtualArsGroupCount_, 0);
  EXPECT_TRUE(resourceAccountant_->virtualArsSuperGroupMemberRefMap_.empty());
}

TEST_F(ResourceAccountantTest, arsEcmpResourceSourcePortPrune) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  const RouteNextHopSet ecmpNextHops{
      ResolvedNextHop(folly::IPAddress("20.1.1.1"), InterfaceID(1), ecmpWeight),
      ResolvedNextHop(
          folly::IPAddress("20.1.1.2"), InterfaceID(2), ecmpWeight)};
  const auto route = makeV6Route(
      {folly::IPAddressV6("b00::1"), 128},
      {ecmpNextHops, AdminDistance::EBGP},
      ecmpNextHops);

  // Without split horizon: one DLB group and one ECMP group, sharing the two
  // members between them.
  EXPECT_TRUE(
      resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->arsEcmpGroupRefMap_.size(), 1);
  EXPECT_EQ(resourceAccountant_->getEcmpGroupUsage(), 1);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, 2);

  // Split horizon pairs the DLB group with a secondary plain ECMP group, each
  // holding its own copy of the members. The DLB group count is unchanged.
  makeResourceAccountant();
  setSplitHorizon(cfg::EcmpGroupType::ARS, true);
  EXPECT_TRUE(
      resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route, true /* add */, state_));
  EXPECT_EQ(resourceAccountant_->arsEcmpGroupRefMap_.size(), 1);
  EXPECT_EQ(resourceAccountant_->ecmpGroupRefMap_.size(), 1);
  // The DLB group and its secondary plain ECMP group.
  EXPECT_EQ(resourceAccountant_->getEcmpGroupUsage(), 2);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, 4);

  EXPECT_TRUE(
      resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route, false /* add */, state_));
  EXPECT_TRUE(resourceAccountant_->arsEcmpGroupRefMap_.empty());
  EXPECT_TRUE(resourceAccountant_->ecmpGroupRefMap_.empty());
  EXPECT_EQ(resourceAccountant_->getEcmpGroupUsage(), 0);
  EXPECT_EQ(resourceAccountant_->ecmpMemberUsage_, 0);
}

TEST_F(ResourceAccountantTest, checkAndUpdateArsEcmpResource) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  FLAGS_enable_ecmp_resource_manager = true;
  auto addRoute =
      [this](
          int i,
          std::vector<std::shared_ptr<Route<folly::IPAddressV6>>>& routes,
          std::optional<cfg::SwitchingMode> switchingMode) {
        auto ecmpNextHops = RouteNextHopSet{
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
                InterfaceID(i + 1),
                ecmpWeight),
            ResolvedNextHop(
                folly::IPAddress(folly::to<std::string>("1.1.1.", i + 2)),
                InterfaceID(i + 2),
                ecmpWeight)};
        const RouteNextHopEntry routeNextHopEntry{
            ecmpNextHops,
            AdminDistance::EBGP,
            std::optional<RouteCounterID>(std::nullopt),
            std::optional<cfg::AclLookupClass>(std::nullopt),
            switchingMode};
        auto route = makeV6Route(
            {folly::IPAddressV6(folly::to<std::string>(i + 1, "00::1")), 128},
            routeNextHopEntry,
            ecmpNextHops);
        routes.push_back(std::move(route));
      };
  int i = 0;
  std::vector<std::shared_ptr<Route<folly::IPAddressV6>>> routes;
  // add upto limit of DLB groups
  for (i = 0; i < getMaxArsGroups(); i++) {
    addRoute(i, routes, std::nullopt);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateArsEcmpResource<folly::IPAddressV6>(
                        routes.back(), true /* add */, state_));
  }

  // add backup ECMP groups, still should be OK
  for (; i < 10; i++) {
    addRoute(i, routes, cfg::SwitchingMode::PER_PACKET_RANDOM);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateArsEcmpResource<folly::IPAddressV6>(
                        routes.back(), true /* add */, state_));
  }
}

TEST_F(ResourceAccountantTest, computeWeightedEcmpMemberCount) {
  RouteNextHopSet ecmpNexthops;
  for (int i = 0; i < getMaxEcmpMembers() - 1; i++) {
    ecmpNexthops.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1::", i + 1)),
        InterfaceID(i + 1),
        i + 1));
  }
  RouteNextHopEntry ecmpNextHopEntry =
      RouteNextHopEntry(ecmpNexthops, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(ecmpNextHopEntry, ecmpNexthops);
  EXPECT_EQ(
      this->resourceAccountant_->computeWeightedEcmpMemberCount(
          ecmpNextHopEntry, cfg::AsicType::ASIC_TYPE_TOMAHAWK4, state_),
      4 * ecmpNextHopEntry.normalizedNextHops().size());
  // Assume ECMP replication for devices without specifying UCMP computation.
  uint32_t totalWeight = 0;
  for (const auto& nhop : ecmpNextHopEntry.normalizedNextHops()) {
    totalWeight += nhop.weight() ? nhop.weight() : 1;
    XLOG(INFO) << "Weighted ECMP member count: " << nhop.weight();
  }
  EXPECT_EQ(
      this->resourceAccountant_->computeWeightedEcmpMemberCount(
          ecmpNextHopEntry, cfg::AsicType::ASIC_TYPE_MOCK, state_),
      totalWeight);
}

TEST_F(ResourceAccountantTest, routeWithAdjustedWeightZero) {
  // Create route with ECMP next hops where one has adjustedWeight of 0
  // The next hop with adjustedWeight 0 should not be counted towards ECMP
  // member usage since ResourceAccountant tracks normalized next hops

  // Create ECMP next hops: 3 next hops total, but one with adjustedWeight=0
  RouteNextHopSet ecmpNexthops;
  // Next hop 1: normal ECMP member with weight 1
  ecmpNexthops.insert(ResolvedNextHop(
      folly::IPAddress("1.1.1.1"),
      InterfaceID(1),
      ecmpWeight,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt));

  // Next hop 2: normal ECMP member with weight 1
  ecmpNexthops.insert(ResolvedNextHop(
      folly::IPAddress("1.1.1.2"),
      InterfaceID(2),
      ecmpWeight,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt));

  // Next hop 3: ECMP member with adjustedWeight=0 (should be filtered out)
  ecmpNexthops.insert(ResolvedNextHop(
      folly::IPAddress("1.1.1.3"),
      InterfaceID(3),
      ecmpWeight,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      NextHopWeight(0)));

  RouteNextHopEntry ecmpNextHopEntry =
      RouteNextHopEntry(ecmpNexthops, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(ecmpNextHopEntry, ecmpNexthops);

  // Only 2 next hops should be counted (the one with adjustedWeight=0 is
  // filtered)
  EXPECT_EQ(
      2,
      this->resourceAccountant_->getMemberCountForEcmpGroup(
          ecmpNextHopEntry, state_));

  // Add route with the ECMP next hop entry
  auto route = makeV6Route(
      {folly::IPAddressV6("2001:db8::1"), 128}, ecmpNextHopEntry, ecmpNexthops);

  auto initialEcmpMemberUsage = this->resourceAccountant_->ecmpMemberUsage_;

  // Validate the route via ResourceAccountant
  EXPECT_TRUE(this->resourceAccountant_->checkAndUpdateEcmpResource(
      route, true, state_));
  // Check that we tracked only 2 ECMP members (not 3)
  EXPECT_EQ(
      initialEcmpMemberUsage + 2, this->resourceAccountant_->ecmpMemberUsage_);

  // Test removing the route
  EXPECT_TRUE(this->resourceAccountant_->checkAndUpdateEcmpResource(
      route, false, state_));
  EXPECT_EQ(
      initialEcmpMemberUsage, this->resourceAccountant_->ecmpMemberUsage_);
}

// This test verifies that routeAndEcmpStateChangedImpl correctly handles
// resolved and unresolved routes.
// 1. Add resolved and unresolved routes - only resolved are tracked
// 2. Delete resolved and unresolved routes - only resolved affect usage
// 3. Change resolved routes to unresolved - remove from tracking
// 4. Change unresolved routes to resolved - add to tracking
TEST_F(ResourceAccountantTest, resolvedAndUnresolvedRoutes) {
  FLAGS_enable_route_resource_protection = true;

  // Create nexthops for resolved routes
  RouteNextHopSet ecmpNexthops;
  ecmpNexthops.insert(
      ResolvedNextHop(folly::IPAddress("1.1.1.1"), InterfaceID(1), ecmpWeight));
  ecmpNexthops.insert(
      ResolvedNextHop(folly::IPAddress("1.1.1.2"), InterfaceID(2), ecmpWeight));
  RouteNextHopEntry ecmpNextHopEntry =
      RouteNextHopEntry(ecmpNexthops, AdminDistance::EBGP);

  auto initialEcmpMemberUsage = this->resourceAccountant_->ecmpMemberUsage_;
  auto initialEcmpGroupCount =
      this->resourceAccountant_->ecmpGroupRefMap_.size();
  auto initialRouteUsage =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();

  // Helper to build SwitchState with V6 routes
  auto makeSwitchStateWithV6Routes =
      [this](const std::vector<std::shared_ptr<RouteV6>>& routes) {
        auto state = std::make_shared<SwitchState>();
        auto fibContainer =
            std::make_shared<ForwardingInformationBaseContainer>(RouterID(0));
        ForwardingInformationBaseV6 fibV6;

        for (const auto& route : routes) {
          route->publish();
          fibV6.addNode(route);
        }

        fibContainer->setFib(fibV6.clone());

        // Create ForwardingInformationBaseMap and add the container
        auto fibsMap = std::make_shared<ForwardingInformationBaseMap>();
        fibsMap->updateForwardingInformationBaseContainer(fibContainer);

        HwSwitchMatcher scope(std::unordered_set<SwitchID>({SwitchID(0)}));
        auto fibInfo = std::make_shared<FibInfo>();
        fibInfo->ref<switch_state_tags::fibsMap>() = fibsMap;

        // Populate ID maps for ID-based resolution
        auto id2Nhop = std::make_shared<IdToNextHopMap>();
        for (const auto& [id, nhopEntry] :
             nextHopIDManager_->getIdToNextHop()) {
          id2Nhop->addNextHop(id, nhopEntry.nextHop.toThrift());
        }
        auto id2NhopSetIds = std::make_shared<IdToNextHopIdSetMap>();
        for (const auto& [id, nhopIdSet] :
             nextHopIDManager_->getIdToNextHopIdSet()) {
          std::set<int64_t> nhopIds;
          for (const auto& nhopId : nhopIdSet) {
            nhopIds.insert(static_cast<int64_t>(nhopId));
          }
          id2NhopSetIds->addNextHopIdSet(id, nhopIds);
        }
        fibInfo->setIdToNextHopMap(id2Nhop);
        fibInfo->setIdToNextHopIdSetMap(id2NhopSetIds);

        auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
        fibInfoMap->updateFibInfo(fibInfo, scope);
        state->resetFibsInfoMap(fibInfoMap);
        return state;
      };

  // Test 1: Add resolved and unresolved routes
  // Only resolved routes should increase usage
  auto resolvedRoute1 = makeV6Route(
      {folly::IPAddressV6("100::1"), 128}, ecmpNextHopEntry, ecmpNexthops);
  auto resolvedRoute2 = makeV6Route(
      {folly::IPAddressV6("100::2"), 128}, ecmpNextHopEntry, ecmpNexthops);

  // Create unresolved route
  auto unresolvedRoute1 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("200::1"), 128}));

  auto oldState1 = makeSwitchStateWithV6Routes({});
  auto newState1 = makeSwitchStateWithV6Routes(
      {resolvedRoute1, resolvedRoute2, unresolvedRoute1});
  StateDelta delta1(oldState1, newState1);

  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta1));

  // Only 2 resolved routes should be counted
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      initialRouteUsage + 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_.size(),
      initialEcmpGroupCount + 1);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpMemberUsage_, initialEcmpMemberUsage + 2);

  // Test 2: Delete resolved and unresolved routes
  // Only resolved route deletions should decrease usage
  auto oldState2 = newState1;
  auto newState2 = makeSwitchStateWithV6Routes({});
  StateDelta delta2(oldState2, newState2);

  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta2));

  // Should go back to initial state
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      initialRouteUsage);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_.size(),
      initialEcmpGroupCount);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpMemberUsage_, initialEcmpMemberUsage);

  // Test 3: Change resolved routes to unresolved
  // First add resolved routes
  auto oldState3a = makeSwitchStateWithV6Routes({});
  auto newState3a =
      makeSwitchStateWithV6Routes({resolvedRoute1, resolvedRoute2});
  StateDelta delta3a(oldState3a, newState3a);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta3a));

  auto usageBeforeUnresolve =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();
  auto ecmpUsageBeforeUnresolve = this->resourceAccountant_->ecmpMemberUsage_;

  // Change to unresolved (same prefixes, but unresolved)
  auto unresolvedRoute2 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("100::1"), 128}));
  auto unresolvedRoute3 = std::make_shared<RouteV6>(RouteV6::makeThrift(
      RoutePrefix<folly::IPAddressV6>{folly::IPAddressV6("100::2"), 128}));

  auto oldState3b = newState3a;
  auto newState3b =
      makeSwitchStateWithV6Routes({unresolvedRoute2, unresolvedRoute3});
  StateDelta delta3b(oldState3b, newState3b);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta3b));

  // Usage should decrease when routes become unresolved
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      usageBeforeUnresolve - 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpMemberUsage_,
      ecmpUsageBeforeUnresolve - 2);

  // Test 4: Change unresolved routes to resolved
  auto usageBeforeResolve =
      this->resourceAccountant_->routeResourceAccountant_.getUsage();
  auto ecmpUsageBeforeResolve = this->resourceAccountant_->ecmpMemberUsage_;

  auto oldState4 = newState3b;
  auto newState4 =
      makeSwitchStateWithV6Routes({resolvedRoute1, resolvedRoute2});
  StateDelta delta4(oldState4, newState4);
  EXPECT_TRUE(this->resourceAccountant_->routeAndEcmpStateChangedImpl(delta4));

  // Usage should increase when routes become resolved
  EXPECT_EQ(
      this->resourceAccountant_->routeResourceAccountant_.getUsage(),
      usageBeforeResolve + 2);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpMemberUsage_, ecmpUsageBeforeResolve + 2);
}

TEST_F(ResourceAccountantTest, virtualArsGroups) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  this->resourceAccountant_->setMinWidthForArsVirtualGroup(5);
  this->resourceAccountant_->setMaxArsVirtualGroups(256);
  this->resourceAccountant_->setMaxArsVirtualGroupWidth(256);

  // Helper to create a nexthop set of given width
  auto makeNhSet = [](int width, int seed) {
    RouteNextHopSet nhSet;
    for (int i = 0; i < width; i++) {
      nhSet.insert(ResolvedNextHop(
          folly::IPAddress(
              folly::to<std::string>(
                  (seed / 250) + 1, ".", (seed % 250) + 1, ".1.", i + 1)),
          InterfaceID(i + 1),
          ecmpWeight));
    }
    return nhSet;
  };

  // Scenario (scaled to fit mock ASIC limits):
  //   5 routes use 1 shared ARS ECMP group with 4 members (non-virtual)
  //   8 routes use 1 shared ARS ECMP group with 6 members (virtual)
  //   2 routes use unique ARS ECMP groups with 3 members each (non-virtual)
  //   3 routes use unique ARS ECMP groups with 7 members each (virtual)

  // Build shared groups
  auto sharedNonVirtualNhSet = makeNhSet(4, 0);
  RouteNextHopEntry sharedNonVirtualEntry(
      sharedNonVirtualNhSet, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(sharedNonVirtualEntry, sharedNonVirtualNhSet);

  auto sharedVirtualNhSet = makeNhSet(6, 1);
  RouteNextHopEntry sharedVirtualEntry(sharedVirtualNhSet, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(sharedVirtualEntry, sharedVirtualNhSet);

  // getMemberCountForEcmpGroup: returns actual width for both virtual and
  // non-virtual.
  EXPECT_EQ(
      4,
      this->resourceAccountant_->getMemberCountForEcmpGroup(
          sharedNonVirtualEntry, state_));
  EXPECT_EQ(
      6,
      this->resourceAccountant_->getMemberCountForEcmpGroup(
          sharedVirtualEntry, state_));

  // Add 5 routes sharing 1 non-virtual group (4 members)
  for (int i = 0; i < 5; i++) {
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("1::", i + 1)), 128},
        sharedNonVirtualEntry,
        sharedNonVirtualNhSet);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        route, true, state_));
  }

  // Add 8 routes sharing 1 virtual group (6 members)
  for (int i = 0; i < 8; i++) {
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("2::", i + 1)), 128},
        sharedVirtualEntry,
        sharedVirtualNhSet);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        route, true, state_));
  }

  // Add 2 routes with unique non-virtual groups (3 members each)
  std::vector<RouteNextHopEntry> uniqueNonVirtualEntries;
  for (int i = 0; i < 2; i++) {
    auto nhSet = makeNhSet(3, i + 100);
    uniqueNonVirtualEntries.emplace_back(nhSet, AdminDistance::EBGP);
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("3::", i + 1)), 128},
        uniqueNonVirtualEntries.back(),
        nhSet);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        route, true, state_));
  }

  // Add 3 routes with unique virtual groups (7 members each)
  std::vector<RouteNextHopEntry> uniqueVirtualEntries;
  for (int i = 0; i < 3; i++) {
    auto nhSet = makeNhSet(7, i + 200);
    uniqueVirtualEntries.emplace_back(nhSet, AdminDistance::EBGP);
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("4::", i + 1)), 128},
        uniqueVirtualEntries.back(),
        nhSet);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        route, true, state_));
  }

  // ecmpGroupRefMap_: all 7 unique groups (1+1+2+3)
  EXPECT_EQ(this->resourceAccountant_->ecmpGroupRefMap_.size(), 7);

  // arsEcmpGroupRefMap_: only 3 non-virtual groups (1 shared + 2 unique)
  EXPECT_EQ(this->resourceAccountant_->arsEcmpGroupRefMap_.size(), 3);

  // virtualArsGroupCount_: 4 unique virtual groups (1 shared + 3 unique)
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 4);

  // ecmpMemberUsage_: 4 + 2*3 (non-virtual) + 6 + 3*7 (virtual) = 37
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 37);

  // Ref counts for shared groups (split by classification)
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_[sharedNonVirtualNhSet]
          .refCountNonVirtual,
      5);
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_[sharedVirtualNhSet]
          .refCountVirtual,
      8);
  EXPECT_EQ(
      this->resourceAccountant_->arsEcmpGroupRefMap_[sharedNonVirtualNhSet], 5);
  EXPECT_EQ(
      this->resourceAccountant_->arsEcmpGroupRefMap_.count(sharedVirtualNhSet),
      0);

  // checkArsResource: totalDlbUsage = 3 (non-virtual) + 4 (collective) = 7
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));

  // checkEcmpResource: ecmpGroupUsage = 7 (unique nhSets),
  //   totalEcmpMemberUsage = 37 + 256 = 293
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      true /* intermediateState */));

  // Remove one of 5 routes sharing non-virtual group - ref count decreases
  auto removeRoute1 = makeV6Route(
      {folly::IPAddressV6("1::1"), 128},
      sharedNonVirtualEntry,
      sharedNonVirtualNhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          removeRoute1, false, state_));
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_[sharedNonVirtualNhSet]
          .refCountNonVirtual,
      4);
  EXPECT_EQ(this->resourceAccountant_->ecmpGroupRefMap_.size(), 7);
  EXPECT_EQ(this->resourceAccountant_->arsEcmpGroupRefMap_.size(), 3);

  // Remove one of 8 routes sharing virtual group - ref count decreases
  auto removeRoute2 = makeV6Route(
      {folly::IPAddressV6("2::1"), 128},
      sharedVirtualEntry,
      sharedVirtualNhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          removeRoute2, false, state_));
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_[sharedVirtualNhSet]
          .refCountVirtual,
      7);
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 4);

  // Remove all remaining routes sharing virtual group
  for (int i = 1; i < 8; i++) {
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("2::", i + 1)), 128},
        sharedVirtualEntry,
        sharedVirtualNhSet);
    EXPECT_TRUE(this->resourceAccountant_
                    ->checkAndUpdateEcmpResource<folly::IPAddressV6>(
                        route, false, state_));
  }
  EXPECT_EQ(
      this->resourceAccountant_->ecmpGroupRefMap_.count(sharedVirtualNhSet), 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpGroupRefMap_.size(), 6);
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 3);

  // Verify virtualArsGroupCount_ > kMaxVirtualArsGroups (256) is rejected
  this->resourceAccountant_->virtualArsGroupCount_ = 257;
  EXPECT_FALSE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));

  // Verify DLB collective overhead: fill arsEcmpGroupRefMap_ to limit - 4,
  // then virtual overhead should push to exactly at limit
  this->resourceAccountant_->virtualArsGroupCount_ = 0;
  this->resourceAccountant_->arsEcmpGroupRefMap_.clear();
  for (int i = 0; i < static_cast<int>(getMaxArsGroups()) - 4; i++) {
    auto nhSet = makeNhSet(2, i + 1000);
    this->resourceAccountant_->arsEcmpGroupRefMap_[nhSet] = 1;
  }
  this->resourceAccountant_->virtualArsGroupCount_ = 10;
  // totalDlbUsage = (maxArsGroups - 4) + 4 = maxArsGroups
  EXPECT_TRUE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));

  // One more non-virtual group should exceed limit
  auto extraNhSet = makeNhSet(2, 999);
  this->resourceAccountant_->arsEcmpGroupRefMap_[extraNhSet] = 1;
  EXPECT_FALSE(this->resourceAccountant_->checkArsResource(
      true /* intermediateState */));

  // Verify ECMP member overhead: virtualArsEcmpMemberUsage =
  // maxArsVirtualGroupWidth_ (flat, regardless of group count). Fill members
  // to just below the final-state limit so the virtual overhead tips it over.
  const auto memberLimit = static_cast<uint32_t>(
      getMaxEcmpMembers() * FLAGS_ecmp_resource_percentage / 100);
  const int32_t maxVirtualGroupWidth =
      this->resourceAccountant_->maxArsVirtualGroupWidth_.value();
  this->resourceAccountant_->ecmpMemberUsage_ =
      memberLimit - static_cast<uint32_t>(maxVirtualGroupWidth) + 1;

  this->resourceAccountant_->virtualArsGroupCount_ = 0;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  this->resourceAccountant_->virtualArsGroupCount_ = 1;
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  // Overhead is flat — same result with many virtual groups.
  this->resourceAccountant_->virtualArsGroupCount_ = 100;
  EXPECT_FALSE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));

  this->resourceAccountant_->virtualArsGroupCount_ = 0;
  EXPECT_TRUE(this->resourceAccountant_->checkEcmpResource(
      false /* intermediateState */));
}

TEST_F(ResourceAccountantTest, virtualArsSuperGroupMemberLimit) {
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  // minWidth=2 so any nhSet with >=2 nexthops qualifies as virtual.
  // Supergroup limit = 5 unique nexthop members across all virtual groups.
  this->resourceAccountant_->setMinWidthForArsVirtualGroup(2);
  this->resourceAccountant_->setMaxArsVirtualGroups(256);
  this->resourceAccountant_->setMaxArsVirtualGroupWidth(5);

  // Build nexthop sets with partial overlap so the union grows incrementally:
  //   A = {nh1, nh2, nh3}  -> unique total = 3
  //   B = {nh2, nh3, nh4}  -> unique total = 4
  //   C = {nh3, nh4, nh5}  -> unique total = 5  (at limit)
  //   D = {nh4, nh5, nh6}  -> unique total = 6  (exceeds limit -> rejected)
  auto makeNh = [](int id) {
    return ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("10.0.0.", id)),
        InterfaceID(id),
        ecmpWeight);
  };

  auto makeNhSet = [&](std::initializer_list<int> ids) {
    RouteNextHopSet nhSet;
    for (int id : ids) {
      nhSet.insert(makeNh(id));
    }
    return nhSet;
  };

  auto nhSetA = makeNhSet({1, 2, 3});
  auto nhSetB = makeNhSet({2, 3, 4});
  auto nhSetC = makeNhSet({3, 4, 5});
  auto nhSetD = makeNhSet({4, 5, 6});

  RouteNextHopEntry entryA(nhSetA, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryA, nhSetA);
  RouteNextHopEntry entryB(nhSetB, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryB, nhSetB);
  RouteNextHopEntry entryC(nhSetC, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryC, nhSetC);
  RouteNextHopEntry entryD(nhSetD, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryD, nhSetD);

  auto addRoute = [&](int prefix,
                      const RouteNextHopEntry& entry,
                      const RouteNextHopSet& nhSet) {
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("::", prefix)), 128},
        entry,
        nhSet);
    return this->resourceAccountant_
        ->checkAndUpdateEcmpResource<folly::IPAddressV6>(route, true, state_);
  };

  auto removeRoute = [&](int prefix,
                         const RouteNextHopEntry& entry,
                         const RouteNextHopSet& nhSet) {
    auto route = makeV6Route(
        {folly::IPAddressV6(folly::to<std::string>("::", prefix)), 128},
        entry,
        nhSet);
    return this->resourceAccountant_
        ->checkAndUpdateEcmpResource<folly::IPAddressV6>(route, false, state_);
  };

  // Add A: unique members = {nh1, nh2, nh3} = 3
  EXPECT_TRUE(addRoute(1, entryA, nhSetA));
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 3);

  // Add B: union gains nh4 -> 4 unique
  EXPECT_TRUE(addRoute(2, entryB, nhSetB));
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 4);

  // Add C: union gains nh5 -> 5 unique (exactly at limit)
  EXPECT_TRUE(addRoute(3, entryC, nhSetC));
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 5);

  // Add D: would add nh6 -> 6 unique, exceeds limit -> rejected
  EXPECT_FALSE(addRoute(4, entryD, nhSetD));

  // Remove A: nh1 erased (refcount 0); nh2 still ref'd by B; nh3 still ref'd by
  // B and C
  removeRoute(1, entryA, nhSetA);
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 4);

  // D is now accepted: nh6 brings unique count to 5 = limit
  EXPECT_TRUE(addRoute(4, entryD, nhSetD));
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 5);

  // A second route sharing nhSetB doesn't change the supergroup unique count
  auto nhSetB2 = makeNhSet({2, 3, 4});
  RouteNextHopEntry entryB2(nhSetB2, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryB2, nhSetB2);
  EXPECT_TRUE(addRoute(5, entryB2, nhSetB2));
  EXPECT_EQ(
      this->resourceAccountant_->virtualArsSuperGroupMemberRefMap_.size(), 5);
}

TEST_F(ResourceAccountantTest, virtualArsGroupOverrideExcluded) {
  // ERM-overridden (backup) routes must not count against
  // virtualArsGroupCount_.
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  FLAGS_enable_ecmp_resource_manager = true;
  this->resourceAccountant_->setMinWidthForArsVirtualGroup(2);
  this->resourceAccountant_->setMaxArsVirtualGroups(256);
  this->resourceAccountant_->setMaxArsVirtualGroupWidth(256);

  auto makeNhSet = [](int width, int seed) {
    RouteNextHopSet nhSet;
    for (int i = 0; i < width; i++) {
      nhSet.insert(ResolvedNextHop(
          folly::IPAddress(
              folly::to<std::string>(
                  (seed / 250) + 1, ".", (seed % 250) + 1, ".1.", i + 1)),
          InterfaceID(i + 1),
          ecmpWeight));
    }
    return nhSet;
  };

  // Wide route with no override: counted as virtual.
  auto nhSet1 = makeNhSet(4, 0);
  RouteNextHopEntry entry1(nhSet1, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entry1, nhSet1);
  auto route1 = makeV6Route({folly::IPAddressV6("1::1"), 128}, entry1, nhSet1);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route1, true /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 1);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 4);

  // Wide route WITH override switching mode (e.g. ERM moved it to backup
  // ECMP): must NOT increment virtualArsGroupCount_, and must contribute
  // real ECMP members.
  auto nhSet2 = makeNhSet(4, 1);
  RouteNextHopEntry entry2(nhSet2, AdminDistance::EBGP);
  entry2.setOverrideEcmpSwitchingMode(cfg::SwitchingMode::PER_PACKET_RANDOM);
  this->allocAndUpdateIdMaps(entry2, nhSet2);
  auto route2 = makeV6Route({folly::IPAddressV6("1::2"), 128}, entry2, nhSet2);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route2, true /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 1);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 8);

  // Removing the override route must not decrement virtualArsGroupCount_.
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route2, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 1);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 4);

  // Removing the non-override route does decrement.
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          route1, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 0);
}

TEST_F(ResourceAccountantTest, virtualArsGroupOverrideFlipLifecycle) {
  // ERM flipping a virtual route to backup must decrement virtualArsGroupCount_
  // and post member cost; deleting it while still in backup must unwind
  // cleanly.
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  FLAGS_enable_ecmp_resource_manager = true;
  this->resourceAccountant_->setMinWidthForArsVirtualGroup(2);
  this->resourceAccountant_->setMaxArsVirtualGroups(256);
  this->resourceAccountant_->setMaxArsVirtualGroupWidth(256);

  RouteNextHopSet nhSet;
  for (int i = 0; i < 4; i++) {
    nhSet.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }

  // 1. Add route with no override (qualifies as virtual).
  RouteNextHopEntry entryNoOverride(nhSet, AdminDistance::EBGP);
  this->allocAndUpdateIdMaps(entryNoOverride, nhSet);
  auto routeNoOverride =
      makeV6Route({folly::IPAddressV6("1::1"), 128}, entryNoOverride, nhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeNoOverride, true /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 1);

  // 2. ERM flips route to backup: add(override) + remove(no-override).
  RouteNextHopEntry entryWithOverride(nhSet, AdminDistance::EBGP);
  entryWithOverride.setOverrideEcmpSwitchingMode(
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  auto routeWithOverride =
      makeV6Route({folly::IPAddressV6("1::1"), 128}, entryWithOverride, nhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeWithOverride, true /* add */, state_));
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeNoOverride, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 4);

  // 3. Delete while still in backup: members unwind, entry removed.
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeWithOverride, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpGroupRefMap_.count(nhSet), 0);
}

TEST_F(
    ResourceAccountantTest,
    virtualArsGroupOverrideDropPromotesClassification) {
  // Route arrives with override (warmboot replay / over-cap). ERM later
  // drops the override; virtualArsGroupCount_ must increment. Member usage
  // stays unchanged (the underlying ECMP object persists). Subsequent
  // deletion unwinds both cleanly.
  FLAGS_dlbResourceCheckEnable = true;
  FLAGS_flowletSwitchingEnable = true;
  FLAGS_enable_ecmp_resource_manager = true;
  this->resourceAccountant_->setMinWidthForArsVirtualGroup(2);
  this->resourceAccountant_->setMaxArsVirtualGroups(256);
  this->resourceAccountant_->setMaxArsVirtualGroupWidth(256);

  RouteNextHopSet nhSet;
  for (int i = 0; i < 4; i++) {
    nhSet.insert(ResolvedNextHop(
        folly::IPAddress(folly::to<std::string>("1.1.1.", i + 1)),
        InterfaceID(i + 1),
        ecmpWeight));
  }

  RouteNextHopEntry entryWithOverride(nhSet, AdminDistance::EBGP);
  entryWithOverride.setOverrideEcmpSwitchingMode(
      cfg::SwitchingMode::PER_PACKET_RANDOM);
  this->allocAndUpdateIdMaps(entryWithOverride, nhSet);
  auto routeWithOverride =
      makeV6Route({folly::IPAddressV6("1::1"), 128}, entryWithOverride, nhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeWithOverride, true /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 4);

  // 2. ERM drops override: add(no-override) + remove(with-override).
  RouteNextHopEntry entryNoOverride(nhSet, AdminDistance::EBGP);
  auto routeNoOverride =
      makeV6Route({folly::IPAddressV6("1::1"), 128}, entryNoOverride, nhSet);
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeNoOverride, true /* add */, state_));
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeWithOverride, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 1);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 4);

  // 3. Delete unwinds cleanly.
  EXPECT_TRUE(
      this->resourceAccountant_->checkAndUpdateEcmpResource<folly::IPAddressV6>(
          routeNoOverride, false /* add */, state_));
  EXPECT_EQ(this->resourceAccountant_->virtualArsGroupCount_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpMemberUsage_, 0);
  EXPECT_EQ(this->resourceAccountant_->ecmpGroupRefMap_.count(nhSet), 0);
}

TEST_F(ResourceAccountantTest, mySidResourceExceeded) {
  FLAGS_enable_mysid_resource_protection = true;

  // MockAsic supports 8 MySID entries, 75% = 6 allowed
  HwSwitchMatcher mySidScope(std::unordered_set<SwitchID>{SwitchID(10)});

  auto makeMySidEntry = [](const std::string& addr, uint8_t len) {
    state::MySidFields fields;
    fields.type() = MySidType::NODE_MICRO_SID;
    facebook::network::thrift::IPPrefix thriftPrefix;
    thriftPrefix.prefixAddress() =
        facebook::network::toBinaryAddress(folly::IPAddress(addr));
    thriftPrefix.prefixLength() = len;
    fields.mySid() = thriftPrefix;
    return std::make_shared<MySid>(fields);
  };

  // Add 6 entries (exactly 75% of 8) — should be accepted
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();

  auto stateWith6 = oldState->clone();
  auto mySids6 = stateWith6->getMySids()->modify(&stateWith6);
  for (int i = 0; i < 6; ++i) {
    mySids6->addNode(
        makeMySidEntry(fmt::format("fc00:{:x}::1", i + 1), 48), mySidScope);
  }
  stateWith6->publish();

  StateDelta delta6(oldState, stateWith6);
  EXPECT_TRUE(this->resourceAccountant_->isValidUpdate(delta6));

  // Add 1 more entry (7/8 = 87.5%, exceeds 75%) — should be rejected
  auto stateWith7 = stateWith6->clone();
  auto mySids7 = stateWith7->getMySids()->modify(&stateWith7);
  mySids7->addNode(makeMySidEntry("fc00:7::1", 48), mySidScope);
  stateWith7->publish();

  StateDelta delta7(stateWith6, stateWith7);
  EXPECT_FALSE(this->resourceAccountant_->isValidUpdate(delta7));
}

} // namespace facebook::fboss
