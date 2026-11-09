/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <folly/ScopeGuard.h>
#include <gtest/gtest.h>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"
#include "fboss/agent/rib/FibUpdateHelpers.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

DECLARE_bool(enable_nexthop_id_manager);

namespace facebook::fboss {

namespace {
const RouterID kRid(0);
const std::string kMatcher{"id=0"};
// One name used for both a named NHG and a class-based policy.
const std::string kSharedName{"shared"};
const std::string kPolicyDefaultNhg{"policyDefaultNhg"};
const std::string kRedirectNhg{"redirectNhg"};

std::function<void(const NextHopIDManager*)> nextHopIdStateUpdate(
    std::shared_ptr<SwitchState>* switchState) {
  return [switchState](const NextHopIDManager* nextHopIDManager) {
    SwitchStateNextHopIdUpdater nhopUpdater(nextHopIDManager);
    ClassBasedPolicyUpdater policyUpdater(nextHopIDManager);
    *switchState = policyUpdater(nhopUpdater(*switchState));
    (*switchState)->publish();
  };
}

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

HwSwitchMatcher scope() {
  return HwSwitchMatcher{std::unordered_set<SwitchID>{SwitchID(0)}};
}
} // namespace

class PolicyRouteBindingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    rib_.ensureVrf(kRid);
    switchState_ = makeSwitchState();
    switchState_->publish();

    addNamedNhg(kSharedName, "fe80::10");
    addNamedNhg(kPolicyDefaultNhg, "fe80::20");

    ClassBasedPolicy policy;
    policy.name() = kSharedName;
    policy.defaultNexthopGroup() = kPolicyDefaultNhg;
    rib_.addOrUpdatePolicies({policy}, nextHopIdStateUpdate(&switchState_));
  }

  std::shared_ptr<SwitchState> makeSwitchState() const {
    auto switchState = std::make_shared<SwitchState>();
    auto intf = std::make_shared<Interface>(
        InterfaceID(1),
        kRid,
        std::optional<VlanID>(VlanID(1)),
        folly::StringPiece("intf1"),
        folly::MacAddress("00:00:00:00:00:01"),
        9000,
        false,
        false);
    auto intfs = std::make_shared<MultiSwitchInterfaceMap>();
    intfs->addNode(intf, scope());
    switchState->resetIntfs(intfs);
    auto fibsInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    fibsInfoMap->addNode(kMatcher, std::make_shared<FibInfo>());
    switchState->resetFibsInfoMap(fibsInfoMap);
    return switchState;
  }

  void addNamedNhg(const std::string& name, const std::string& nhip) {
    RouteNextHopSet nhops{
        makeResolvedNextHop(InterfaceID(1), nhip, UCMP_DEFAULT_WEIGHT)};
    rib_.addOrUpdateNamedNextHopGroups(
        scopeResolver(),
        {{name, nhops}},
        ribToSwitchStateUpdate,
        &switchState_);
  }

  void addRoute(
      const std::string& prefix,
      const NamedRouteDestination& namedDest) {
    auto network = folly::IPAddress::createNetwork(prefix);
    UnicastRoute route;
    route.dest()->ip() = facebook::network::toBinaryAddress(network.first);
    route.dest()->prefixLength() = network.second;
    route.namedRouteDestination() = namedDest;
    rib_.update(
        scopeResolver(),
        kRid,
        ClientID::BGPD,
        AdminDistance::EBGP,
        {route},
        {},
        false,
        "add named route",
        ribToSwitchStateUpdate,
        &switchState_);
  }

  int64_t publishedRedirectId(ForwardingClass fc) const {
    auto policy = switchState_->getClassBasedPolicies()->getNodeIf(kSharedName);
    CHECK(policy) << "policy not published";
    return *policy->getClass2NextHopGroup().at(fc).id();
  }

  int64_t publishedDefaultId() const {
    auto policy = switchState_->getClassBasedPolicies()->getNodeIf(kSharedName);
    CHECK(policy) << "policy not published";
    return *policy->getDefaultNextHopGroup().id();
  }

  int64_t normalizedId(const std::string& nhgName) const {
    auto id =
        rib_.getNextHopIDManagerCopy()->getNormalizedNextHopSetIDForPolicyNhg(
            nhgName);
    CHECK(id.has_value()) << "no normalized id for " << nhgName;
    return static_cast<int64_t>(*id);
  }

  bool routeBoundTo(const std::string& nhgName, const std::string& prefix) {
    auto manager = rib_.getNextHopIDManagerCopy();
    const auto& routes = manager->getRoutesForNamedNhg(nhgName);
    return routes.count({kRid, folly::IPAddress::createNetwork(prefix)}) == 1;
  }

  RoutingInformationBase rib_;
  std::shared_ptr<SwitchState> switchState_;
};

TEST_F(PolicyRouteBindingTest, nextHopGroupVariantBindsToNamedNhg) {
  NamedRouteDestination namedDest;
  namedDest.nextHopGroup() = kSharedName;
  addRoute("2001:db8:1::/64", namedDest);

  EXPECT_TRUE(routeBoundTo(kSharedName, "2001:db8:1::/64"));
  EXPECT_FALSE(routeBoundTo(kPolicyDefaultNhg, "2001:db8:1::/64"));
}

TEST_F(PolicyRouteBindingTest, policyNameVariantBindsToPolicyDefault) {
  NamedRouteDestination namedDest;
  namedDest.policyName() = kSharedName;
  addRoute("2001:db8:2::/64", namedDest);

  EXPECT_TRUE(routeBoundTo(kPolicyDefaultNhg, "2001:db8:2::/64"));
  EXPECT_FALSE(routeBoundTo(kSharedName, "2001:db8:2::/64"));
}

TEST_F(PolicyRouteBindingTest, unknownPolicyNameThrows) {
  NamedRouteDestination namedDest;
  namedDest.policyName() = "noSuchPolicy";
  EXPECT_THROW(addRoute("2001:db8:3::/64", namedDest), FbossError);
}

// addOrUpdatePolicies runs on the RIB event base thread; a throw there must
// reach the caller instead of escaping the loop and terminating the process.
TEST(PolicyRouteBindingNoIdManagerTest, addPolicyWithoutIdManagerThrows) {
  auto prevEnableNextHopIdManager = FLAGS_enable_nexthop_id_manager;
  FLAGS_enable_nexthop_id_manager = false;
  SCOPE_EXIT {
    FLAGS_enable_nexthop_id_manager = prevEnableNextHopIdManager;
  };
  RoutingInformationBase rib;
  rib.ensureVrf(kRid);
  auto switchState = std::make_shared<SwitchState>();

  ClassBasedPolicy policy;
  policy.name() = kSharedName;
  policy.defaultNexthopGroup() = kPolicyDefaultNhg;
  EXPECT_THROW(
      rib.addOrUpdatePolicies({policy}, nextHopIdStateUpdate(&switchState)),
      FbossError);
}

TEST_F(PolicyRouteBindingTest, redirectNhgMemberChangeRepublishesPolicy) {
  addNamedNhg(kRedirectNhg, "fe80::30");

  ClassBasedPolicy policy;
  policy.name() = kSharedName;
  policy.defaultNexthopGroup() = kPolicyDefaultNhg;
  NextHopGroup redirect;
  redirect.name() = kRedirectNhg;
  policy.class2NextHopGroup()[ForwardingClass::CLASS_1] = redirect;
  rib_.addOrUpdatePolicies({policy}, nextHopIdStateUpdate(&switchState_));

  const auto idBefore = publishedRedirectId(ForwardingClass::CLASS_1);
  EXPECT_EQ(idBefore, normalizedId(kRedirectNhg));

  // Members change: a new member set is a new normalized id.
  addNamedNhg(kRedirectNhg, "fe80::31");

  const auto idAfter = publishedRedirectId(ForwardingClass::CLASS_1);
  EXPECT_NE(idAfter, idBefore);
  EXPECT_EQ(idAfter, normalizedId(kRedirectNhg));
  // The match qualifier is untouched.
  EXPECT_EQ(publishedDefaultId(), normalizedId(kPolicyDefaultNhg));
}

TEST_F(PolicyRouteBindingTest, removePoliciesValidatesNames) {
  EXPECT_THROW(
      rib_.removePolicies({""}, nextHopIdStateUpdate(&switchState_)),
      FbossError);
  EXPECT_NE(rib_.getNextHopIDManagerCopy()->getPolicy(kSharedName), nullptr);

  auto stateBefore = switchState_;
  rib_.removePolicies({}, nextHopIdStateUpdate(&switchState_));
  EXPECT_EQ(switchState_, stateBefore);
  EXPECT_NE(rib_.getNextHopIDManagerCopy()->getPolicy(kSharedName), nullptr);
}

TEST_F(PolicyRouteBindingTest, addPoliciesRejectsBatchBeforeStoringAny) {
  ClassBasedPolicy good;
  good.name() = "goodPolicy";
  good.defaultNexthopGroup() = kPolicyDefaultNhg;

  // Second policy names no NHG for its traffic class, which addOrUpdatePolicy
  // throws on. Pre-validation must reject the batch before the first is stored.
  ClassBasedPolicy bad;
  bad.name() = "badPolicy";
  bad.defaultNexthopGroup() = kPolicyDefaultNhg;
  bad.class2NextHopGroup()[ForwardingClass::CLASS_1] = NextHopGroup{};

  EXPECT_THROW(
      rib_.addOrUpdatePolicies(
          {good, bad}, nextHopIdStateUpdate(&switchState_)),
      FbossError);

  auto manager = rib_.getNextHopIDManagerCopy();
  EXPECT_EQ(manager->getPolicy("goodPolicy"), nullptr);
  EXPECT_EQ(manager->getPolicy("badPolicy"), nullptr);
}

} // namespace facebook::fboss
