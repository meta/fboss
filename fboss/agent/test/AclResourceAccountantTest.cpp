// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/Conv.h>
#include <gtest/gtest.h>

#include "fboss/agent/AclResourceAccountant.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {
namespace {

std::shared_ptr<ClassBasedPolicyNode>
makePolicy(const std::string& name, int numTrafficClasses, bool referenced) {
  std::map<ForwardingClass, state::NamedNextHopGroupAndID> class2Nhg;
  for (int tc = 1; tc <= numTrafficClasses; ++tc) {
    state::NamedNextHopGroupAndID nhg;
    nhg.name() = folly::to<std::string>(name, "_nhg", tc);
    nhg.id() = tc;
    class2Nhg[static_cast<ForwardingClass>(tc)] = nhg;
  }
  state::NamedNextHopGroupAndID defaultNhg;
  defaultNhg.name() = name + "_default";
  defaultNhg.id() = 100;
  return std::make_shared<ClassBasedPolicyNode>(
      name, std::as_const(defaultNhg), std::as_const(class2Nhg), referenced);
}

std::shared_ptr<SwitchState> makeState(
    const std::vector<std::shared_ptr<ClassBasedPolicyNode>>& policies,
    int numEntries) {
  auto state = std::make_shared<SwitchState>();
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();

  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  for (const auto& policy : policies) {
    policyMap->addPolicy(policy);
  }
  auto multiPolicy = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  multiPolicy->addMapNode(policyMap, matcher);
  state->resetClassBasedPolicies(multiPolicy);

  auto aclMap = std::make_shared<AclMap>();
  for (int i = 0; i < numEntries; ++i) {
    aclMap->addEntry(
        std::make_shared<AclEntry>(0, folly::to<std::string>("entry", i)));
  }
  const std::string tableName =
      cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
  auto table = std::make_shared<AclTable>(0, tableName);
  table->setAclMap(aclMap);
  auto tableMap = std::make_shared<AclTableMap>();
  tableMap->addTable(table);
  auto group = std::make_shared<AclTableGroup>(cfg::AclStage::INGRESS);
  group->setAclTableMap(tableMap);
  auto groupMap = std::make_shared<AclTableGroupMap>();
  groupMap->addAclTableGroup(group);
  auto multiGroup = std::make_shared<MultiSwitchAclTableGroupMap>();
  multiGroup->addMapNode(groupMap, matcher);
  state->resetAclTableGroups(multiGroup);

  state->publish();
  return state;
}

std::unique_ptr<HwAsicTable> makeAsicTable(cfg::AsicType asicType) {
  std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo{
      {0, createSwitchInfo(cfg::SwitchType::NPU, asicType)}};
  return std::make_unique<HwAsicTable>(
      switchIdToSwitchInfo, std::nullopt, std::map<int64_t, cfg::DsfNode>{});
}

} // namespace

class AclResourceAccountantTest : public ::testing::Test {
 public:
  void SetUp() override {
    // MockAsic allows 8 PBR ACL entries; the default 75% leaves 6 rules.
    asicTable_ = makeAsicTable(cfg::AsicType::ASIC_TYPE_MOCK);
    accountant_ = std::make_unique<AclResourceAccountant>(asicTable_.get());
    emptyState_ = std::make_shared<SwitchState>();
    emptyState_->publish();
  }

  bool isValid(const std::shared_ptr<SwitchState>& newState) const {
    return accountant_->isValidUpdate(StateDelta(emptyState_, newState));
  }

 protected:
  std::unique_ptr<HwAsicTable> asicTable_;
  std::unique_ptr<AclResourceAccountant> accountant_;
  std::shared_ptr<SwitchState> emptyState_;
};

TEST_F(AclResourceAccountantTest, SteadyRuleLimit) {
  EXPECT_TRUE(isValid(makeState(
      {makePolicy("p1", 3, true /*referenced*/),
       makePolicy("p2", 3, true /*referenced*/)},
      6)));
  EXPECT_FALSE(isValid(makeState(
      {makePolicy("p1", 3, true /*referenced*/),
       makePolicy("p2", 4, true /*referenced*/)},
      7)));
}

TEST_F(AclResourceAccountantTest, UnreferencedPoliciesCountTowardRules) {
  EXPECT_FALSE(isValid(makeState(
      {makePolicy("p1", 3, false /*referenced*/),
       makePolicy("p2", 4, false /*referenced*/)},
      0)));
}

TEST_F(AclResourceAccountantTest, SwapMayUseWholeTable) {
  // 6 steady rules, with a swap holding temporary copies of 2 or 3 of them.
  const std::vector<std::shared_ptr<ClassBasedPolicyNode>> policies{
      makePolicy("p1", 3, true /*referenced*/),
      makePolicy("p2", 3, true /*referenced*/)};
  EXPECT_TRUE(isValid(makeState(policies, 8)));
  EXPECT_FALSE(isValid(makeState(policies, 9)));
}

TEST_F(AclResourceAccountantTest, UnchangedAclsAndPoliciesNotChecked) {
  auto overLimit = makeState({makePolicy("p1", 7, true /*referenced*/)}, 9);
  auto next = overLimit->clone();
  next->publish();
  EXPECT_TRUE(accountant_->isValidUpdate(StateDelta(overLimit, next)));
}

TEST_F(AclResourceAccountantTest, NoAsicLimitNeverRejects) {
  auto asicTable = makeAsicTable(cfg::AsicType::ASIC_TYPE_FAKE);
  AclResourceAccountant accountant(asicTable.get());
  EXPECT_TRUE(accountant.isValidUpdate(StateDelta(
      emptyState_, makeState({makePolicy("p1", 7, true /*referenced*/)}, 9))));
}

} // namespace facebook::fboss
