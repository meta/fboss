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

#include "fboss/agent/state/ClassBasedPolicyNode.h"

namespace facebook::fboss {
namespace {
state::NamedNextHopGroupAndID makeNhg(const std::string& name, int64_t id) {
  state::NamedNextHopGroupAndID nhg;
  nhg.name() = name;
  nhg.id() = id;
  return nhg;
}
} // namespace

TEST(ClassBasedPolicyNodeTest, GettersAndSetters) {
  const std::string kName = "qzk1-traffic-policy";
  ClassBasedPolicyNode policy(kName);
  EXPECT_EQ(policy.getID(), kName);

  const auto defaultNhg = makeNhg("qzk1-default-nhg", 100);
  policy.setDefaultNextHopGroup(defaultNhg);
  EXPECT_EQ(policy.getDefaultNextHopGroup(), defaultNhg);

  const std::map<ForwardingClass, state::NamedNextHopGroupAndID>
      class2NextHopGroup{
          {ForwardingClass::CLASS_1, makeNhg("qzk1-gold-nhg", 201)},
          {ForwardingClass::CLASS_2, makeNhg("qzk1-silver-nhg", 202)}};
  policy.setClass2NextHopGroup(class2NextHopGroup);
  EXPECT_EQ(policy.getClass2NextHopGroup(), class2NextHopGroup);

  EXPECT_FALSE(policy.isReferenced());
  policy.setReferenced(true);
  EXPECT_TRUE(policy.isReferenced());
  EXPECT_TRUE(*policy.toThrift().referenced());
  policy.setReferenced(false);
  EXPECT_FALSE(policy.isReferenced());
  EXPECT_FALSE(*policy.toThrift().referenced());
}

TEST(ClassBasedPolicyNodeTest, ConstructWithAllFields) {
  const std::string kName = "qzk1";
  const auto kDefault = makeNhg("qzk1-default-nhg", 100);
  const std::map<ForwardingClass, state::NamedNextHopGroupAndID>
      class2NextHopGroup{
          {ForwardingClass::CLASS_1, makeNhg("qzk1-gold-nhg", 201)}};
  ClassBasedPolicyNode policy(
      kName, kDefault, class2NextHopGroup, true /*referenced*/);

  EXPECT_EQ(policy.getID(), kName);
  EXPECT_EQ(policy.getDefaultNextHopGroup(), kDefault);
  EXPECT_EQ(policy.getClass2NextHopGroup(), class2NextHopGroup);
  EXPECT_TRUE(policy.isReferenced());

  auto thrift = policy.toThrift();
  EXPECT_EQ(*thrift.name(), kName);
  EXPECT_EQ(*thrift.defaultNextHopGroup()->id(), 100);
  EXPECT_EQ(*thrift.defaultNextHopGroup()->name(), "qzk1-default-nhg");
  EXPECT_EQ(
      *thrift.class2NextHopGroup()->at(ForwardingClass::CLASS_1).id(), 201);
  EXPECT_EQ(
      *thrift.class2NextHopGroup()->at(ForwardingClass::CLASS_1).name(),
      "qzk1-gold-nhg");
  EXPECT_TRUE(*thrift.referenced());
}

} // namespace facebook::fboss
