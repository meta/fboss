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
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {
namespace {

state::NamedNextHopGroupAndID makeNhg(const std::string& name, int64_t id) {
  state::NamedNextHopGroupAndID nhg;
  nhg.name() = name;
  nhg.id() = id;
  return nhg;
}

std::shared_ptr<MultiSwitchClassBasedPolicyMap> makePolicies(
    const HwSwitchMatcher& matcher) {
  const std::string kName = "qzk1";
  const auto kDefault = makeNhg("qzk1-default-nhg", 100);
  const std::map<ForwardingClass, state::NamedNextHopGroupAndID>
      class2NextHopGroup{
          {ForwardingClass::CLASS_1, makeNhg("qzk1-gold-nhg", 201)}};
  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  policyMap->addPolicy(
      std::make_shared<ClassBasedPolicyNode>(
          kName, kDefault, class2NextHopGroup, true /*referenced*/));
  auto multi = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  multi->addMapNode(policyMap, matcher);
  return multi;
}

} // namespace

TEST(ClassBasedPolicyMapTest, ResetAndGetThroughSwitchState) {
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();
  auto state = std::make_shared<SwitchState>();
  state->resetClassBasedPolicies(makePolicies(matcher));

  auto policies = state->getClassBasedPolicies();
  ASSERT_NE(policies, nullptr);
  auto policy = policies->getMapNodeIf(matcher)->getPolicyIf("qzk1");
  ASSERT_NE(policy, nullptr);
  EXPECT_EQ(*policy->getDefaultNextHopGroup().id(), 100);
  EXPECT_EQ(
      *policy->getClass2NextHopGroup().at(ForwardingClass::CLASS_1).id(), 201);
}

TEST(ClassBasedPolicyMapTest, ModifyClonesPublishedState) {
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();
  auto state = std::make_shared<SwitchState>();
  state->resetClassBasedPolicies(makePolicies(matcher));
  state->publish();
  EXPECT_TRUE(state->isPublished());

  auto* modified = state->getClassBasedPolicies()->modify(&state);
  EXPECT_NE(modified, nullptr);
  EXPECT_FALSE(state->isPublished());
  EXPECT_NE(modified->getMapNodeIf(matcher)->getPolicyIf("qzk1"), nullptr);
}

TEST(ClassBasedPolicyMapTest, GetClassBasedPoliciesDelta) {
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();
  auto oldState = std::make_shared<SwitchState>();
  oldState->resetClassBasedPolicies(makePolicies(matcher));
  oldState->publish();

  // Same state on both sides -> empty delta.
  EXPECT_TRUE(
      DeltaFunctions::isEmpty(
          StateDelta(oldState, oldState).getClassBasedPoliciesDelta()));

  // Add a second policy -> non-empty delta.
  const std::string kOther = "qzk2";
  auto newState = std::make_shared<SwitchState>();
  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  policyMap->addPolicy(std::make_shared<ClassBasedPolicyNode>(kOther));
  auto multi = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  multi->addMapNode(policyMap, matcher);
  newState->resetClassBasedPolicies(multi);
  newState->publish();

  EXPECT_FALSE(
      DeltaFunctions::isEmpty(
          StateDelta(oldState, newState).getClassBasedPoliciesDelta()));
}

} // namespace facebook::fboss
