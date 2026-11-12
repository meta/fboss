/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {

namespace {
constexpr uint32_t kEcmpWidth = 64;
const std::string kMatcher{"id=0"};
const std::string kMatcher2{"id=1"};
const std::string kDefaultNhgA{"defaultNhgA"};
const std::string kDefaultNhgB{"defaultNhgB"};
const std::string kSharedNhg{"sharedNhg"};
const std::string kPolicyA{"polA"};
const std::string kPolicyB{"polB"};
} // namespace

class ClassBasedPolicyUpdaterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    state_ = std::make_shared<SwitchState>();
    auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    fibsInfoMap->addNode(kMatcher, std::make_shared<FibInfo>());
    state_->resetFibsInfoMap(fibsInfoMap);
  }

  // These next hops already carry an interface, so RIB resolution would pass
  // them through untouched; stand in for it by assigning the id directly.
  void addOrUpdateNhg(const std::string& name, const std::string& nhip) {
    auto nhops = makeResolvedNextHops({{InterfaceID(1), nhip}});
    manager_.allocateNamedNextHopGroup(name, nhops);
    manager_.updateNormalizedNamedNhgId(name, nhops, kEcmpWidth);
  }

  void addPolicy(
      const std::string& name,
      const std::string& defaultNhg,
      const std::map<ForwardingClass, std::string>& class2Nhg = {}) {
    ClassBasedPolicy policy;
    policy.name() = name;
    policy.defaultNexthopGroup() = defaultNhg;
    for (const auto& [fc, nhgName] : class2Nhg) {
      NextHopGroup nhg;
      nhg.name() = nhgName;
      policy.class2NextHopGroup()[fc] = nhg;
    }
    manager_.addOrUpdatePolicy(policy);
    // Mirrors RibRouteTables::addOrUpdatePolicies: only a policy gives its
    // named groups a normalized id, so assign them once the policy exists.
    auto assign = [&](const std::string& nhgName) {
      if (auto nhops = manager_.getNextHopsForName(nhgName)) {
        manager_.updateNormalizedNamedNhgId(nhgName, *nhops, kEcmpWidth);
      }
    };
    assign(defaultNhg);
    for (const auto& [_fc, nhgName] : class2Nhg) {
      assign(nhgName);
    }
  }

  void addRouteForNhg(const std::string& nhgName, const std::string& prefix) {
    manager_.addRouteForNamedNhg(
        nhgName, RouterID(0), folly::IPAddress::createNetwork(prefix));
  }

  void removeRouteForNhg(
      const std::string& nhgName,
      const std::string& prefix) {
    manager_.removeRouteForNamedNhg(
        nhgName, RouterID(0), folly::IPAddress::createNetwork(prefix));
  }

  // Publishing the state matches what the updater reads in SwSwitch.
  void publish() {
    ClassBasedPolicyUpdater updater(&manager_);
    state_ = updater(state_);
    state_->publish();
  }

  std::shared_ptr<ClassBasedPolicyNode> publishedPolicy(
      const std::string& name) const {
    return state_->getClassBasedPolicies()->getNodeIf(name);
  }

  int64_t defaultId(const std::string& policyName) const {
    auto policy = publishedPolicy(policyName);
    CHECK(policy) << "no published policy " << policyName;
    return *policy->getDefaultNextHopGroup().id();
  }

  int64_t redirectId(const std::string& policyName, ForwardingClass fc) const {
    auto policy = publishedPolicy(policyName);
    CHECK(policy) << "no published policy " << policyName;
    const auto class2Nhg = policy->getClass2NextHopGroup();
    auto it = class2Nhg.find(fc);
    CHECK(it != class2Nhg.end()) << "no redirect nhg for " << policyName;
    return *it->second.id();
  }

  int64_t normalizedId(const std::string& nhgName) const {
    auto id = manager_.getNormalizedNextHopSetIDForPolicyNhg(nhgName);
    CHECK(id.has_value()) << "no normalized id for " << nhgName;
    return static_cast<int64_t>(*id);
  }

  NextHopIDManager manager_;
  std::shared_ptr<SwitchState> state_;
};

// 1. Named NHG referenced only as a policy's default. Its normalized id is the
// ACL match qualifier, so a member change repoints the match.
TEST_F(ClassBasedPolicyUpdaterTest, namedNhgUsedOnlyAsDefault) {
  addOrUpdateNhg(kSharedNhg, "10.0.0.1");
  addPolicy(kPolicyA, kSharedNhg);
  publish();

  auto policy = publishedPolicy(kPolicyA);
  ASSERT_NE(policy, nullptr);
  EXPECT_EQ(*policy->getDefaultNextHopGroup().name(), kSharedNhg);
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kSharedNhg));
  EXPECT_TRUE(policy->getClass2NextHopGroup().empty());

  const auto idBefore = defaultId(kPolicyA);

  // Members change: a new member set is a new normalized set id.
  addOrUpdateNhg(kSharedNhg, "10.0.0.2");
  publish();

  EXPECT_NE(defaultId(kPolicyA), idBefore);
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kSharedNhg));
  EXPECT_EQ(
      *publishedPolicy(kPolicyA)->getDefaultNextHopGroup().name(), kSharedNhg);
}

// 2. Named NHG referenced only as a redirect target. Its normalized id is the
// ACL redirect action, so a member change leaves the match qualifier alone.
TEST_F(ClassBasedPolicyUpdaterTest, namedNhgUsedOnlyAsRedirect) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kSharedNhg, "10.0.0.2");
  addPolicy(kPolicyA, kDefaultNhgA, {{ForwardingClass::CLASS_1, kSharedNhg}});
  publish();

  ASSERT_NE(publishedPolicy(kPolicyA), nullptr);
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kDefaultNhgA));
  EXPECT_EQ(
      *publishedPolicy(kPolicyA)
           ->getClass2NextHopGroup()
           .at(ForwardingClass::CLASS_1)
           .name(),
      kSharedNhg);
  EXPECT_EQ(
      redirectId(kPolicyA, ForwardingClass::CLASS_1), normalizedId(kSharedNhg));

  const auto matchIdBefore = defaultId(kPolicyA);
  const auto redirectIdBefore = redirectId(kPolicyA, ForwardingClass::CLASS_1);

  addOrUpdateNhg(kSharedNhg, "10.0.0.3");
  publish();

  EXPECT_EQ(defaultId(kPolicyA), matchIdBefore);
  EXPECT_NE(redirectId(kPolicyA, ForwardingClass::CLASS_1), redirectIdBefore);
  EXPECT_EQ(
      redirectId(kPolicyA, ForwardingClass::CLASS_1), normalizedId(kSharedNhg));
}

// 3. Named NHG that is polA's redirect target and polB's default. Both resolve
// the one name to the same normalized id, so polA redirects into the very
// next-hop group polB's routes match on.
TEST_F(
    ClassBasedPolicyUpdaterTest,
    namedNhgDefaultForOnePolicyRedirectForAnother) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kSharedNhg, "10.0.0.2");
  addPolicy(kPolicyA, kDefaultNhgA, {{ForwardingClass::CLASS_1, kSharedNhg}});
  addPolicy(kPolicyB, kSharedNhg);
  publish();

  ASSERT_NE(publishedPolicy(kPolicyA), nullptr);
  ASSERT_NE(publishedPolicy(kPolicyB), nullptr);
  const auto sharedIdBefore = normalizedId(kSharedNhg);
  EXPECT_EQ(redirectId(kPolicyA, ForwardingClass::CLASS_1), sharedIdBefore);
  EXPECT_EQ(defaultId(kPolicyB), sharedIdBefore);
  // polA keeps its own match qualifier.
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kDefaultNhgA));
  EXPECT_NE(defaultId(kPolicyA), sharedIdBefore);

  addOrUpdateNhg(kSharedNhg, "10.0.0.3");
  publish();

  // One member change repoints both policies in a single publish pass.
  const auto sharedIdAfter = normalizedId(kSharedNhg);
  EXPECT_NE(sharedIdAfter, sharedIdBefore);
  EXPECT_EQ(redirectId(kPolicyA, ForwardingClass::CLASS_1), sharedIdAfter);
  EXPECT_EQ(defaultId(kPolicyB), sharedIdAfter);
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kDefaultNhgA));
}

// 4. Named NHG used as a redirect target by two policies, under different
// forwarding classes and behind different defaults.
TEST_F(ClassBasedPolicyUpdaterTest, namedNhgRedirectForTwoPolicies) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kDefaultNhgB, "10.0.0.2");
  addOrUpdateNhg(kSharedNhg, "10.0.0.3");
  addPolicy(kPolicyA, kDefaultNhgA, {{ForwardingClass::CLASS_1, kSharedNhg}});
  addPolicy(kPolicyB, kDefaultNhgB, {{ForwardingClass::CLASS_2, kSharedNhg}});
  publish();

  ASSERT_NE(publishedPolicy(kPolicyA), nullptr);
  ASSERT_NE(publishedPolicy(kPolicyB), nullptr);
  const auto sharedIdBefore = normalizedId(kSharedNhg);
  EXPECT_EQ(redirectId(kPolicyA, ForwardingClass::CLASS_1), sharedIdBefore);
  EXPECT_EQ(redirectId(kPolicyB, ForwardingClass::CLASS_2), sharedIdBefore);
  EXPECT_NE(normalizedId(kDefaultNhgA), normalizedId(kDefaultNhgB));

  addOrUpdateNhg(kSharedNhg, "10.0.0.4");
  publish();

  const auto sharedIdAfter = normalizedId(kSharedNhg);
  EXPECT_NE(sharedIdAfter, sharedIdBefore);
  EXPECT_EQ(redirectId(kPolicyA, ForwardingClass::CLASS_1), sharedIdAfter);
  EXPECT_EQ(redirectId(kPolicyB, ForwardingClass::CLASS_2), sharedIdAfter);
  // Each policy keeps its own match qualifier.
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kDefaultNhgA));
  EXPECT_EQ(defaultId(kPolicyB), normalizedId(kDefaultNhgB));
}

TEST_F(ClassBasedPolicyUpdaterTest, unchangedStoreReturnsSameState) {
  addOrUpdateNhg(kSharedNhg, "10.0.0.1");
  addPolicy(kPolicyA, kSharedNhg);
  publish();

  auto stateBefore = state_;
  publish();
  EXPECT_EQ(state_, stateBefore);
}

TEST_F(ClassBasedPolicyUpdaterTest, untouchedPolicyKeepsNodeIdentity) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kDefaultNhgB, "10.0.0.2");
  addPolicy(kPolicyA, kDefaultNhgA);
  addPolicy(kPolicyB, kDefaultNhgB);
  publish();

  auto policyABefore = publishedPolicy(kPolicyA);
  auto policyBBefore = publishedPolicy(kPolicyB);
  ASSERT_NE(policyABefore, nullptr);
  ASSERT_NE(policyBBefore, nullptr);

  // Only polA's default next-hop group changes members.
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.3");
  publish();

  EXPECT_NE(publishedPolicy(kPolicyA), policyABefore);
  EXPECT_EQ(publishedPolicy(kPolicyB), policyBBefore);
}

TEST_F(ClassBasedPolicyUpdaterTest, removedPolicyIsDropped) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kDefaultNhgB, "10.0.0.2");
  addPolicy(kPolicyA, kDefaultNhgA);
  addPolicy(kPolicyB, kDefaultNhgB);
  publish();
  auto policyBBefore = publishedPolicy(kPolicyB);

  manager_.removePolicy(kPolicyA);
  publish();

  EXPECT_EQ(publishedPolicy(kPolicyA), nullptr);
  EXPECT_EQ(publishedPolicy(kPolicyB), policyBBefore);
}

TEST_F(ClassBasedPolicyUpdaterTest, unallocatedNextHopGroupThrows) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");

  addPolicy(kPolicyA, "noSuchNhg");
  EXPECT_THROW(publish(), FbossError);

  manager_.removePolicy(kPolicyA);
  addPolicy(kPolicyA, kDefaultNhgA, {{ForwardingClass::CLASS_1, "noSuchNhg"}});
  EXPECT_THROW(publish(), FbossError);
}

TEST_F(ClassBasedPolicyUpdaterTest, newMatcherPublishesAllPolicies) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addPolicy(kPolicyA, kDefaultNhgA);
  publish();

  state_ = state_->clone();
  auto fibsInfoMap = state_->getFibsInfoMap()->clone();
  fibsInfoMap->addNode(kMatcher2, std::make_shared<FibInfo>());
  state_->resetFibsInfoMap(fibsInfoMap);
  publish();

  auto onMatcher2 =
      state_->getClassBasedPolicies()->getMapNodeIf(HwSwitchMatcher(kMatcher2));
  ASSERT_NE(onMatcher2, nullptr);
  EXPECT_NE(onMatcher2->getPolicyIf(kPolicyA), nullptr);
}

TEST_F(ClassBasedPolicyUpdaterTest, failedPublishIsRetried) {
  addPolicy(kPolicyA, kDefaultNhgA);
  EXPECT_THROW(publish(), FbossError);
  EXPECT_EQ(publishedPolicy(kPolicyA), nullptr);

  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  publish();

  ASSERT_NE(publishedPolicy(kPolicyA), nullptr);
  EXPECT_EQ(defaultId(kPolicyA), normalizedId(kDefaultNhgA));
}

TEST_F(ClassBasedPolicyUpdaterTest, policyIsUnreferencedUntilARouteBinds) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addPolicy(kPolicyA, kDefaultNhgA);
  publish();
  ASSERT_NE(publishedPolicy(kPolicyA), nullptr);
  EXPECT_FALSE(publishedPolicy(kPolicyA)->isReferenced());

  // 1. A route over the default NHG flips the flag.
  addRouteForNhg(kDefaultNhgA, "2001::/64");
  publish();
  EXPECT_TRUE(publishedPolicy(kPolicyA)->isReferenced());

  // 2. Withdrawing the last route flips it back.
  removeRouteForNhg(kDefaultNhgA, "2001::/64");
  publish();
  EXPECT_FALSE(publishedPolicy(kPolicyA)->isReferenced());
}

TEST_F(ClassBasedPolicyUpdaterTest, onlyTheFirstAndLastRouteChangeThePolicy) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addPolicy(kPolicyA, kDefaultNhgA);
  addRouteForNhg(kDefaultNhgA, "2001::/64");
  publish();
  auto before = publishedPolicy(kPolicyA);
  ASSERT_TRUE(before->isReferenced());

  // 1. A second route over the same group leaves the policy untouched.
  addRouteForNhg(kDefaultNhgA, "2002::/64");
  publish();
  EXPECT_EQ(publishedPolicy(kPolicyA), before);

  // 2. So does dropping one of the two.
  removeRouteForNhg(kDefaultNhgA, "2002::/64");
  publish();
  EXPECT_EQ(publishedPolicy(kPolicyA), before);

  // 3. Dropping the last one unreferences it.
  removeRouteForNhg(kDefaultNhgA, "2001::/64");
  publish();
  EXPECT_FALSE(publishedPolicy(kPolicyA)->isReferenced());
}

TEST_F(ClassBasedPolicyUpdaterTest, redirectOnlyRouteDoesNotReferencePolicy) {
  addOrUpdateNhg(kDefaultNhgA, "10.0.0.1");
  addOrUpdateNhg(kSharedNhg, "10.0.0.2");
  addPolicy(kPolicyA, kDefaultNhgA, {{ForwardingClass::CLASS_1, kSharedNhg}});
  addRouteForNhg(kSharedNhg, "2001::/64");
  publish();

  // The ACL matches on the default group, so a route over a redirect target
  // cannot carry traffic into this policy.
  EXPECT_FALSE(publishedPolicy(kPolicyA)->isReferenced());
}

TEST_F(ClassBasedPolicyUpdaterTest, emptyStoreLeavesStateUnchanged) {
  auto stateBefore = state_;
  publish();

  EXPECT_EQ(state_, stateBefore);
  EXPECT_EQ(state_->getClassBasedPolicies()->numNodes(), 0);
}

} // namespace facebook::fboss
