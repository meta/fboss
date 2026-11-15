// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/PbrAclManager.h"
#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroup.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/AclTableMap.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/MatchAction.h"
#include "fboss/agent/state/PbrUtils.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {
namespace {

std::string kPbrTable() {
  return cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE();
}

std::shared_ptr<ClassBasedPolicyNode> makePolicy(
    const std::string& name,
    int64_t defaultId,
    const std::map<ForwardingClass, int64_t>& class2Nhg,
    const std::map<ForwardingClass, std::string>& class2Name = {},
    const std::string& defaultName = "",
    bool referenced = true) {
  std::map<ForwardingClass, state::NamedNextHopGroupAndID> class2;
  for (const auto& [fc, id] : class2Nhg) {
    state::NamedNextHopGroupAndID nhg;
    auto it = class2Name.find(fc);
    nhg.name() = it != class2Name.end() ? it->second : "";
    nhg.id() = id;
    class2[fc] = nhg;
  }
  state::NamedNextHopGroupAndID defaultNhg;
  defaultNhg.name() = defaultName;
  defaultNhg.id() = defaultId;
  return std::make_shared<ClassBasedPolicyNode>(
      name, std::as_const(defaultNhg), std::as_const(class2), referenced);
}

std::shared_ptr<ClassBasedPolicyNode> makeUnreferencedPolicy(
    const std::string& name,
    int64_t defaultId,
    const std::map<ForwardingClass, int64_t>& class2Nhg) {
  return makePolicy(name, defaultId, class2Nhg, {}, "", false /*referenced*/);
}

std::shared_ptr<SwitchState> makeState(
    const std::vector<std::shared_ptr<ClassBasedPolicyNode>>& policies,
    bool withPbrTable,
    const std::vector<std::shared_ptr<AclEntry>>& seedEntries = {}) {
  auto state = std::make_shared<SwitchState>();
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();

  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  for (const auto& policy : policies) {
    policyMap->addPolicy(policy);
  }
  auto multiPolicy = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  multiPolicy->addMapNode(policyMap, matcher);
  state->resetClassBasedPolicies(multiPolicy);

  if (withPbrTable) {
    const std::string tableName = kPbrTable();
    auto table = std::make_shared<AclTable>(0, tableName);
    auto aclMap = std::make_shared<AclMap>();
    for (const auto& entry : seedEntries) {
      aclMap->addEntry(entry);
    }
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
  }

  state->publish();
  return state;
}

std::shared_ptr<SwitchState> runManager(
    PbrAclManager& mgr,
    const std::shared_ptr<SwitchState>& newState) {
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, newState);
  return mgr.modifyState(deltas).back().newState();
}

std::shared_ptr<const AclMap> pbrAcls(const std::shared_ptr<SwitchState>& s) {
  return s->getAclsForTable(cfg::AclStage::INGRESS, kPbrTable());
}

// Build the next desired state from a prior manager result: keep the same ACL
// table (so it carries the already-programmed PBR entries) but swap in a new
// set of policies. This models the real update flow where the desired state is
// derived from the previously applied (PBR-programmed) state.
std::shared_ptr<SwitchState> withPolicies(
    const std::shared_ptr<SwitchState>& base,
    const std::vector<std::shared_ptr<ClassBasedPolicyNode>>& policies) {
  auto state = base->clone();
  auto matcher = HwSwitchMatcher::defaultHwSwitchMatcher();
  auto policyMap = std::make_shared<ClassBasedPolicyMap>();
  for (const auto& policy : policies) {
    policyMap->addPolicy(policy);
  }
  auto multiPolicy = std::make_shared<MultiSwitchClassBasedPolicyMap>();
  multiPolicy->addMapNode(policyMap, matcher);
  state->resetClassBasedPolicies(multiPolicy);
  state->publish();
  return state;
}

std::shared_ptr<SwitchState> runManagerDelta(
    PbrAclManager& mgr,
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState) {
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, newState);
  return mgr.modifyState(deltas).back().newState();
}

int64_t redirectOf(const std::shared_ptr<AclEntry>& entry) {
  auto action = MatchAction::fromThrift(entry->getAclAction()->toThrift());
  return *action.getRedirectNextHopGroupId();
}

std::string counterNameOf(const std::shared_ptr<AclEntry>& entry) {
  auto action = MatchAction::fromThrift(entry->getAclAction()->toThrift());
  return *action.getTrafficCounter()->name();
}

} // namespace

TEST(PbrUtils, EntryAndCounterNames) {
  EXPECT_EQ(
      makePbrAclEntryName(
          "qzk1", ForwardingClass::CLASS_2, false /*temporary*/),
      "qzk1_tc2");
  EXPECT_EQ(
      makePbrCounterName(ForwardingClass::CLASS_1, "qzk1-gold-nhg"),
      "CLASS_1_qzk1-gold-nhg");
}

TEST(PbrUtils, TemporaryEntryName) {
  EXPECT_EQ(
      makePbrAclEntryName("qzk1", ForwardingClass::CLASS_2, true /*temporary*/),
      "#qzk1_tc2");
  EXPECT_FALSE(isPbrTempAclEntry("qzk1_tc2"));
  EXPECT_TRUE(isPbrTempAclEntry("#qzk1_tc2"));
}

// A rule and its temporary copy sit at the one PBR priority; only the name
// differs.
TEST(PbrUtils, TemporaryEntryKeepsPriority) {
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto original = createAclEntriesFromPolicy(policy);
  auto temporary = createAclEntriesFromPolicy(policy, true /*temporary*/);
  ASSERT_EQ(original.size(), 1);
  ASSERT_EQ(temporary.size(), 1);
  EXPECT_EQ(original[0]->getID(), "qzk1_tc1");
  EXPECT_EQ(temporary[0]->getID(), "#qzk1_tc1");
  EXPECT_EQ(original[0]->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(temporary[0]->getPriority(), FLAGS_pbr_acl_priority);
}

TEST(PbrUtils, CreateAclEntriesFromPolicy) {
  auto policy = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}},
      {{ForwardingClass::CLASS_1, "qzk1-gold-nhg"},
       {ForwardingClass::CLASS_2, "qzk1-silver-nhg"}});
  auto entries = createAclEntriesFromPolicy(policy);
  ASSERT_EQ(entries.size(), 2);

  const auto& tc1 = entries[0];
  EXPECT_EQ(tc1->getID(), "qzk1_tc1");
  EXPECT_EQ(tc1->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(tc1->getNextHopGroupId(), 100);
  EXPECT_EQ(tc1->getTrafficClass(), 1);
  auto action = MatchAction::fromThrift(tc1->getAclAction()->toThrift());
  EXPECT_EQ(action.getRedirectNextHopGroupId(), 201);
  ASSERT_TRUE(action.getTrafficCounter().has_value());
  EXPECT_EQ(*action.getTrafficCounter()->name(), "CLASS_1_qzk1-gold-nhg");

  const auto& tc2 = entries[1];
  EXPECT_EQ(tc2->getID(), "qzk1_tc2");
  EXPECT_EQ(tc2->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(tc2->getNextHopGroupId(), 100);
  EXPECT_EQ(tc2->getTrafficClass(), 2);
  auto action2 = MatchAction::fromThrift(tc2->getAclAction()->toThrift());
  EXPECT_EQ(action2.getRedirectNextHopGroupId(), 202);
  ASSERT_TRUE(action2.getTrafficCounter().has_value());
  EXPECT_EQ(*action2.getTrafficCounter()->name(), "CLASS_2_qzk1-silver-nhg");
}

TEST(PbrAclManagerTest, SynthesizesEntries) {
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto result = runManager(mgr, makeState({policy}, true /*withPbrTable*/));
  auto acls = pbrAcls(result);
  ASSERT_NE(acls, nullptr);
  auto entry = acls->getEntryIf("qzk1_tc1");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getNextHopGroupId(), 100);
  EXPECT_EQ(entry->getTrafficClass(), 1);
  auto action = MatchAction::fromThrift(entry->getAclAction()->toThrift());
  EXPECT_EQ(action.getRedirectNextHopGroupId(), 201);
}

TEST(PbrAclManagerTest, EmptyPoliciesIsNoOp) {
  PbrAclManager mgr;
  auto state = makeState({}, true /*withPbrTable*/);
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, state);
  auto result = mgr.modifyState(deltas).back().newState();
  // No PBR entries synthesized and no state churn.
  EXPECT_EQ(pbrAcls(result)->numEntries(), 0);
  EXPECT_EQ(result, state);
}

TEST(PbrAclManagerTest, MissingTableThrows) {
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto state = makeState({policy}, false /*withPbrTable*/);
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, state);
  EXPECT_THROW(mgr.modifyState(deltas), FbossError);
}

TEST(PbrAclManagerTest, ReconstructIsDeterministic) {
  auto p1 = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto p2 = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  auto state = makeState({p1, p2}, true /*withPbrTable*/);
  PbrAclManager mgr;
  auto first = mgr.reconstructFromSwitchState(state).back().newState();
  auto second = mgr.reconstructFromSwitchState(state).back().newState();
  EXPECT_EQ(*pbrAcls(first), *pbrAcls(second));
  EXPECT_EQ(pbrAcls(first)->numEntries(), 2);
}

TEST(PbrAclManagerTest, PreservesNonPbrEntries) {
  auto nonPbr = std::make_shared<AclEntry>(1, std::string("ip-count"));
  nonPbr->setDscp(10);
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto result = runManager(mgr, makeState({policy}, true, {nonPbr}));
  auto acls = pbrAcls(result);
  ASSERT_NE(acls, nullptr);
  auto preserved = acls->getEntryIf("ip-count");
  ASSERT_NE(preserved, nullptr);
  EXPECT_EQ(preserved->getDscp(), 10);
  EXPECT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
}

TEST(PbrAclManagerTest, RejectsPolicyEntryNameTakenByConfigEntry) {
  auto configEntry = std::make_shared<AclEntry>(
      AclTable::kDataplaneAclMaxPriority, std::string("qzk1_tc1"));
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  EXPECT_THROW(
      runManager(mgr, makeState({policy}, true, {configEntry})), FbossError);
}

TEST(PbrAclManagerTest, IncrementalAddPolicy) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto aEntry = pbrAcls(r1)->getEntryIf("qzk1_tc1");
  ASSERT_NE(aEntry, nullptr);

  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a, b}));
  auto acls = pbrAcls(r2);
  // qzk1 is untouched (same node object, not rebuilt); qzk2 is added.
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1"), aEntry);
  auto bEntry = acls->getEntryIf("qzk2_tc2");
  ASSERT_NE(bEntry, nullptr);
  EXPECT_EQ(redirectOf(bEntry), 202);
  EXPECT_EQ(acls->numEntries(), 2);
}

TEST(PbrAclManagerTest, IncrementalRemovePolicy) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b}, true));
  auto aEntry = pbrAcls(r1)->getEntryIf("qzk1_tc1");
  ASSERT_NE(pbrAcls(r1)->getEntryIf("qzk2_tc2"), nullptr);

  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a}));
  auto acls = pbrAcls(r2);
  EXPECT_EQ(acls->getEntryIf("qzk2_tc2"), nullptr); // removed
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1"), aEntry); // untouched
  EXPECT_EQ(acls->numEntries(), 1);
}

TEST(PbrAclManagerTest, IncrementalChangeRedirect) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto prio = pbrAcls(r1)->getEntryIf("qzk1_tc1")->getPriority();

  auto a2 = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 999}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a2}));
  auto entry = pbrAcls(r2)->getEntryIf("qzk1_tc1");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(redirectOf(entry), 999); // redirect updated
  EXPECT_EQ(entry->getPriority(), prio); // same slot
  EXPECT_EQ(pbrAcls(r2)->numEntries(), 1);
}

TEST(PbrAclManagerTest, IncrementalChangeTrafficClasses) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto prio1 = pbrAcls(r1)->getEntryIf("qzk1_tc1")->getPriority();

  // Add CLASS_2: the existing CLASS_1 entry keeps its slot.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a2}));
  ASSERT_NE(pbrAcls(r2)->getEntryIf("qzk1_tc2"), nullptr);
  EXPECT_EQ(pbrAcls(r2)->getEntryIf("qzk1_tc1")->getPriority(), prio1);
  EXPECT_EQ(pbrAcls(r2)->numEntries(), 2);

  // Drop CLASS_1: only its entry is removed.
  auto a3 = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_2, 202}});
  auto r3 = runManagerDelta(mgr, r2, withPolicies(r2, {a3}));
  EXPECT_EQ(pbrAcls(r3)->getEntryIf("qzk1_tc1"), nullptr);
  ASSERT_NE(pbrAcls(r3)->getEntryIf("qzk1_tc2"), nullptr);
  EXPECT_EQ(pbrAcls(r3)->numEntries(), 1);
}

TEST(PbrAclManagerTest, IncrementalChangeDefaultNhg) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));

  auto a2 = makePolicy("qzk1", 500, {{ForwardingClass::CLASS_1, 201}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a2}));
  auto entry = pbrAcls(r2)->getEntryIf("qzk1_tc1");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getNextHopGroupId(), 500); // match NHG updated
}

TEST(PbrAclManagerTest, IncrementalChangeMixed) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  // One delta that drops CLASS_1 (3d), changes CLASS_2's redirect (3a), and
  // adds CLASS_3 (3c).
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_2, 999}, {ForwardingClass::CLASS_3, 203}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a2}));
  auto acls = pbrAcls(r2);
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1"), nullptr); // 3d dropped
  auto tc2 = acls->getEntryIf("qzk1_tc2");
  ASSERT_NE(tc2, nullptr);
  EXPECT_EQ(redirectOf(tc2), 999); // 3a redirect changed
  EXPECT_EQ(tc2->getPriority(), FLAGS_pbr_acl_priority);
  auto tc3 = acls->getEntryIf("qzk1_tc3");
  ASSERT_NE(tc3, nullptr);
  EXPECT_EQ(redirectOf(tc3), 203); // 3c added
  EXPECT_EQ(tc3->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(acls->numEntries(), 2);
}

TEST(PbrAclManagerTest, AllTrafficClassesShareThePbrPriority) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  auto c = makePolicy("qzk3", 400, {{ForwardingClass::CLASS_3, 203}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b, c}, true));
  auto prioA = pbrAcls(r1)->getEntryIf("qzk1_tc1")->getPriority();
  auto prioC = pbrAcls(r1)->getEntryIf("qzk3_tc3")->getPriority();

  // Change only qzk2's redirect; qzk1 and qzk3 keep their slots.
  auto b2 = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 999}});
  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {a, b2, c}));
  auto acls = pbrAcls(r2);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk2_tc2")), 999);
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1")->getPriority(), prioA);
  EXPECT_EQ(acls->getEntryIf("qzk3_tc3")->getPriority(), prioC);
  EXPECT_EQ(acls->numEntries(), 3);
}

TEST(PbrAclManagerTest, IncrementalNoOpDeltaReturnsInput) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  // Re-publish an identical (but distinct-object) policy set: no ACL churn.
  auto s2 = withPolicies(
      r1, {makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}})});
  auto r2 = runManagerDelta(mgr, r1, s2);
  EXPECT_EQ(r2, s2);
}

// --- Add scenarios ---

TEST(PbrAclManagerTest, AddPolicyWithMultipleTrafficClasses) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201},
       {ForwardingClass::CLASS_2, 202},
       {ForwardingClass::CLASS_3, 203}});
  PbrAclManager mgr;
  auto acls = pbrAcls(runManager(mgr, makeState({a}, true)));
  ASSERT_EQ(acls->numEntries(), 3);
  // Every entry sits at the one PBR priority; the name tells them apart.
  EXPECT_EQ(
      acls->getEntryIf("qzk1_tc1")->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(
      acls->getEntryIf("qzk1_tc2")->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(
      acls->getEntryIf("qzk1_tc3")->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc1")), 201);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc3")), 203);
}

TEST(PbrAclManagerTest, AddTwoNewPoliciesInOneDelta) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto acls = pbrAcls(runManager(mgr, makeState({a, b}, true)));
  ASSERT_EQ(acls->numEntries(), 2);
  ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
  ASSERT_NE(acls->getEntryIf("qzk2_tc2"), nullptr);
  // Distinct policies coexist at the one PBR priority.
  EXPECT_EQ(
      acls->getEntryIf("qzk1_tc1")->getPriority(), FLAGS_pbr_acl_priority);
  EXPECT_EQ(
      acls->getEntryIf("qzk2_tc2")->getPriority(), FLAGS_pbr_acl_priority);
}

// --- Remove scenarios ---

TEST(PbrAclManagerTest, RemovePolicyWithMultipleTrafficClasses) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_3, 203}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 3);

  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {b}));
  auto acls = pbrAcls(r2);
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(acls->getEntryIf("qzk1_tc2"), nullptr);
  ASSERT_NE(acls->getEntryIf("qzk2_tc3"), nullptr); // untouched
  EXPECT_EQ(acls->numEntries(), 1);
}

TEST(PbrAclManagerTest, RemoveAllPoliciesKeepsNonPbrEntries) {
  auto nonPbr = std::make_shared<AclEntry>(1, std::string("ip-count"));
  nonPbr->setDscp(10);
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b}, true, {nonPbr}));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 3);

  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {}));
  auto acls = pbrAcls(r2);
  EXPECT_EQ(acls->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(acls->getEntryIf("qzk2_tc2"), nullptr);
  ASSERT_NE(acls->getEntryIf("ip-count"), nullptr); // non-PBR entry survives
  EXPECT_EQ(acls->numEntries(), 1);
}

TEST(PbrAclManagerTest, RemoveThenReAddReusesSlot) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto prio = pbrAcls(r1)->getEntryIf("qzk1_tc1")->getPriority();

  auto r2 = runManagerDelta(mgr, r1, withPolicies(r1, {}));
  EXPECT_EQ(pbrAcls(r2)->numEntries(), 0);

  // Removal freed the slot, so re-adding the same policy reclaims it (would
  // have probed to a different slot had removePolicyEntries not freed it).
  auto r3 = runManagerDelta(mgr, r2, withPolicies(r2, {a}));
  ASSERT_NE(pbrAcls(r3)->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(pbrAcls(r3)->getEntryIf("qzk1_tc1")->getPriority(), prio);
}

// --- Change scenarios ---

TEST(PbrAclManagerTest, ChangeDefaultNhgUpdatesAllEntries) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto prio1 = pbrAcls(r1)->getEntryIf("qzk1_tc1")->getPriority();
  auto prio2 = pbrAcls(r1)->getEntryIf("qzk1_tc2")->getPriority();

  // 3b: default/match NHG change rewrites the match field of every entry.
  auto a2 = makePolicy(
      "qzk1",
      500,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto acls = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {a2})));
  auto tc1 = acls->getEntryIf("qzk1_tc1");
  auto tc2 = acls->getEntryIf("qzk1_tc2");
  ASSERT_NE(tc1, nullptr);
  ASSERT_NE(tc2, nullptr);
  EXPECT_EQ(tc1->getNextHopGroupId(), 500);
  EXPECT_EQ(tc2->getNextHopGroupId(), 500);
  EXPECT_EQ(redirectOf(tc1), 201); // redirects unchanged
  EXPECT_EQ(redirectOf(tc2), 202);
  EXPECT_EQ(tc1->getPriority(), prio1); // slots unchanged
  EXPECT_EQ(tc2->getPriority(), prio2);
}

TEST(PbrAclManagerTest, ChangeRedirectLeavesOtherTrafficClassUntouched) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  auto tc2Before = pbrAcls(r1)->getEntryIf("qzk1_tc2");

  // Change only CLASS_1's redirect.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 999}, {ForwardingClass::CLASS_2, 202}});
  auto acls = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {a2})));
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc1")), 999);
  // CLASS_2 entry is not rebuilt -- same COW node object.
  EXPECT_EQ(acls->getEntryIf("qzk1_tc2"), tc2Before);
}

TEST(PbrAclManagerTest, ChangeRedirectNhgNameUpdatesCounter) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}},
      {{ForwardingClass::CLASS_1, "gold"}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));
  EXPECT_EQ(counterNameOf(pbrAcls(r1)->getEntryIf("qzk1_tc1")), "CLASS_1_gold");

  // Same redirect id, renamed NHG: the entry is rewritten so its counter name
  // tracks the new NHG name.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}},
      {{ForwardingClass::CLASS_1, "platinum"}});
  auto entry = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {a2})))
                   ->getEntryIf("qzk1_tc1");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(counterNameOf(entry), "CLASS_1_platinum");
  EXPECT_EQ(redirectOf(entry), 201); // redirect id unchanged
}

TEST(PbrAclManagerTest, ChangedPolicyWithMissingEntriesThrows) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));

  // Desired state changes the policy but its original entries are absent from
  // the (fresh, empty) PBR table, so beginPolicySwap throws.
  auto a2 = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 999}});
  auto bad = makeState({a2}, true /*withPbrTable*/); // empty table
  EXPECT_THROW(runManagerDelta(mgr, r1, bad), FbossError);
}

// --- Make-before-break scenarios ---

TEST(PbrAclManagerTest, RedirectChangeIsMakeBeforeBreak) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}},
      {{ForwardingClass::CLASS_1, "gold"}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));

  // 1. Same named group, new members: its normalized id changes.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 999}},
      {{ForwardingClass::CLASS_1, "gold"}});
  std::vector<StateDelta> deltas;
  deltas.emplace_back(r1, withPolicies(r1, {a2}));
  auto out = mgr.modifyState(deltas);

  // 2. The swap expands into ordered make, update and drop deltas.
  ASSERT_EQ(out.size(), 3);
  for (size_t i = 1; i < out.size(); ++i) {
    EXPECT_EQ(out[i - 1].newState(), out[i].oldState());
  }

  // 3. The make delta carries the rule and its temporary copy, so the rule is
  // never absent from hardware.
  auto made = pbrAcls(out[0].newState());
  EXPECT_EQ(made->numEntries(), 2);
  ASSERT_NE(made->getEntryIf("qzk1_tc1"), nullptr);
  ASSERT_NE(made->getEntryIf("#qzk1_tc1"), nullptr);
  EXPECT_EQ(redirectOf(made->getEntryIf("#qzk1_tc1")), 999);
  EXPECT_EQ(redirectOf(made->getEntryIf("qzk1_tc1")), 201);

  // 4. The update delta rewrites the original in place to match its copy.
  auto updated = pbrAcls(out[1].newState());
  EXPECT_EQ(updated->numEntries(), 2);
  auto original = updated->getEntryIf("qzk1_tc1");
  auto temporary = updated->getEntryIf("#qzk1_tc1");
  ASSERT_NE(original, nullptr);
  ASSERT_NE(temporary, nullptr);
  EXPECT_EQ(original->getPriority(), temporary->getPriority());
  EXPECT_EQ(original->getNextHopGroupId(), temporary->getNextHopGroupId());
  EXPECT_EQ(original->getTrafficClass(), temporary->getTrafficClass());
  EXPECT_EQ(redirectOf(original), redirectOf(temporary));

  // 5. The drop delta leaves only the original name.
  auto after = pbrAcls(out[2].newState());
  EXPECT_EQ(after->numEntries(), 1);
  EXPECT_EQ(after->getEntryIf("#qzk1_tc1"), nullptr);
  ASSERT_NE(after->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(redirectOf(after->getEntryIf("qzk1_tc1")), 999);

  // 6. Every entry at every step names one counter, so its refcount never
  // falls to zero and the count carries over instead of resetting.
  const auto counter = counterNameOf(after->getEntryIf("qzk1_tc1"));
  for (const auto& d : out) {
    for (const auto* name : {"qzk1_tc1", "#qzk1_tc1"}) {
      if (auto entry = pbrAcls(d.newState())->getEntryIf(name)) {
        EXPECT_EQ(counterNameOf(entry), counter) << name;
      }
    }
  }
}

TEST(PbrAclManagerTest, SwapEndsUnderTheOriginalName) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto state = runManager(mgr, makeState({a}, true));

  // The second swap reuses the temporary name the first one freed.
  for (int64_t redirect : {999, 777}) {
    auto next = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, redirect}});
    state = runManagerDelta(mgr, state, withPolicies(state, {next}));
    auto acls = pbrAcls(state);
    EXPECT_EQ(acls->numEntries(), 1);
    EXPECT_EQ(acls->getEntryIf("#qzk1_tc1"), nullptr);
    ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
    EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc1")), redirect);
  }
}

TEST(PbrAclManagerTest, AddingATrafficClassNeedsNoSwap) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));

  // A pure add has no rule to keep live, so it needs no temporary copy.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  std::vector<StateDelta> deltas;
  deltas.emplace_back(r1, withPolicies(r1, {a2}));
  auto out = mgr.modifyState(deltas);
  EXPECT_EQ(out.size(), 1);
  EXPECT_NE(pbrAcls(out[0].newState())->getEntryIf("qzk1_tc2"), nullptr);
}

TEST(PbrAclManagerTest, SwapTransientStaysWithinOnePolicy) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto b = makePolicy(
      "qzk2",
      300,
      {{ForwardingClass::CLASS_3, 203}, {ForwardingClass::CLASS_4, 204}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b}, true));
  const auto steadyState = pbrAcls(r1)->numEntries();
  ASSERT_EQ(steadyState, 4);

  // 1. Both policies swap every one of their redirects in one update.
  auto a2 = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 901}, {ForwardingClass::CLASS_2, 902}});
  auto b2 = makePolicy(
      "qzk2",
      300,
      {{ForwardingClass::CLASS_3, 903}, {ForwardingClass::CLASS_4, 904}});
  std::vector<StateDelta> deltas;
  deltas.emplace_back(r1, withPolicies(r1, {a2, b2}));
  auto out = mgr.modifyState(deltas);

  // 2. Make, update and drop per policy, not one window covering both.
  ASSERT_EQ(out.size(), 6);

  // 3. No intermediate carries more than one policy's worth of duplication.
  // Batched, both policies would be mid-swap at once and this would reach 8.
  const size_t kTcsPerPolicy = 2;
  for (const auto& d : out) {
    EXPECT_LE(pbrAcls(d.newState())->numEntries(), steadyState + kTcsPerPolicy);
  }

  // 4. Both policies still land on their new redirects.
  auto after = pbrAcls(out.back().newState());
  EXPECT_EQ(after->numEntries(), steadyState);
  EXPECT_EQ(redirectOf(after->getEntryIf("qzk1_tc1")), 901);
  EXPECT_EQ(redirectOf(after->getEntryIf("qzk1_tc2")), 902);
  EXPECT_EQ(redirectOf(after->getEntryIf("qzk2_tc3")), 903);
  EXPECT_EQ(redirectOf(after->getEntryIf("qzk2_tc4")), 904);
}

TEST(PbrAclManagerTest, AddsAndRemovesShareOneDelta) {
  auto keep = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto drop = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({keep, drop}, true));

  // One policy added and one removed, with nothing changed in place.
  auto add = makePolicy("qzk3", 400, {{ForwardingClass::CLASS_3, 203}});
  std::vector<StateDelta> deltas;
  deltas.emplace_back(r1, withPolicies(r1, {keep, add}));
  auto out = mgr.modifyState(deltas);

  ASSERT_EQ(out.size(), 1);
  auto after = pbrAcls(out[0].newState());
  EXPECT_EQ(after->numEntries(), 2);
  EXPECT_NE(after->getEntryIf("qzk3_tc3"), nullptr);
  EXPECT_EQ(after->getEntryIf("qzk2_tc2"), nullptr);
}

// --- Warm-boot reconstruct / rollback scenarios ---

TEST(PbrAclManagerTest, ReconstructReusesProgrammedEntry) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}},
      {{ForwardingClass::CLASS_1, "gold"}});
  // Seed the table with the policy's entry, as a warm boot would present it.
  auto seed = createAclEntriesFromPolicy(a);
  auto state = makeState({a}, true, seed);

  PbrAclManager mgr;
  auto acls = pbrAcls(mgr.reconstructFromSwitchState(state).back().newState());
  auto entry = acls->getEntryIf("qzk1_tc1");
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->getPriority(), FLAGS_pbr_acl_priority);
}

TEST(PbrAclManagerTest, ReconstructIsIdempotent) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_3, 203}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a, b}, true));
  // Reconstructing from an already-programmed state reproduces it
  // byte-for-byte -- identical entries are skipped, so there is no churn.
  auto r2 = mgr.reconstructFromSwitchState(r1).back().newState();
  EXPECT_EQ(*pbrAcls(r2), *pbrAcls(r1));
}

TEST(PbrAclManagerTest, ModifyStateEmitsOneDeltaPerInputDelta) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto oldState = std::make_shared<SwitchState>();
  oldState->publish();

  // 1. A delta that programs entries.
  std::vector<StateDelta> deltas;
  deltas.emplace_back(oldState, makeState({a}, true /*withPbrTable*/));
  auto out = mgr.modifyState(deltas);
  EXPECT_EQ(out.size(), 1);

  // 2. A delta the manager leaves alone still yields exactly one.
  std::vector<StateDelta> noPolicyDeltas;
  noPolicyDeltas.emplace_back(oldState, makeState({}, true /*withPbrTable*/));
  EXPECT_EQ(mgr.modifyState(noPolicyDeltas).size(), 1);

  // 3. Two input deltas yield two output deltas, chained oldState to newState.
  std::vector<StateDelta> twoDeltas;
  auto first = makeState({a}, true /*withPbrTable*/);
  twoDeltas.emplace_back(oldState, first);
  twoDeltas.emplace_back(first, withPolicies(first, {a, b}));
  auto twoOut = mgr.modifyState(twoDeltas);
  EXPECT_EQ(twoOut.size(), 2);
  EXPECT_EQ(twoOut[0].newState(), twoOut[1].oldState());
}

TEST(PbrAclManagerTest, ReconstructReturnsOneDelta) {
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_3, 203}});
  PbrAclManager mgr;
  auto state = makeState({a, b}, true /*withPbrTable*/);
  auto deltas = mgr.reconstructFromSwitchState(state);
  EXPECT_EQ(deltas.size(), 1);
  EXPECT_EQ(pbrAcls(deltas.back().newState())->numEntries(), 3);
}

TEST(PbrAclManagerTest, RemovingAPolicyWithoutItsOriginalEntryThrows) {
  auto policy = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto seeded = createAclEntriesFromPolicy(policy, true /*temporary*/);
  auto state = makeState({policy}, true /*withPbrTable*/, seeded);

  PbrAclManager mgr;
  EXPECT_THROW(
      runManagerDelta(mgr, state, withPolicies(state, {})), FbossError);
}

TEST(PbrAclManagerTest, DroppingATrafficClassWithoutItsOriginalEntryThrows) {
  auto policy = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto tc1Only = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto state = makeState(
      {policy}, true /*withPbrTable*/, createAclEntriesFromPolicy(tc1Only));

  PbrAclManager mgr;
  EXPECT_THROW(
      runManagerDelta(mgr, state, withPolicies(state, {tc1Only})), FbossError);
}

TEST(PbrAclManagerTest, TwoPoliciesShareThePbrPriorityAcrossGetAclsDelta) {
  // 1. Two policies, four rules, all synthesized at the one PBR priority.
  auto a = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  auto b = makePolicy(
      "qzk2",
      300,
      {{ForwardingClass::CLASS_1, 203}, {ForwardingClass::CLASS_3, 204}});
  PbrAclManager mgr;
  auto programmed = runManager(mgr, makeState({a, b}, true /*withPbrTable*/));

  auto acls = pbrAcls(programmed);
  ASSERT_EQ(acls->numEntries(), 4);
  for (const auto& [_, entry] : std::as_const(*acls)) {
    EXPECT_EQ(entry->getPriority(), FLAGS_pbr_acl_priority);
  }

  // 2. getAclsDelta builds a PrioAclMap over the table on every state update,
  // so all four must key apart. On a priority-only key this threw duplicate
  // node ID, which SwSwitch turns into XLOG(FATAL).
  auto empty = std::make_shared<SwitchState>();
  empty->publish();
  StateDelta delta(empty, programmed);
  int added = 0;
  for (const auto& entry :
       delta.getAclsDelta(cfg::AclStage::INGRESS, kPbrTable())) {
    EXPECT_EQ(entry.getOld(), nullptr);
    ASSERT_NE(entry.getNew(), nullptr);
    EXPECT_EQ(entry.getNew()->getPriority(), FLAGS_pbr_acl_priority);
    ++added;
  }
  EXPECT_EQ(added, 4);
}

TEST(PbrAclManagerTest, UpdateFailedThenReapplySucceeds) {
  auto a = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto b = makePolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a}, true));

  // A speculative update adds qzk2, then "fails" to program in HW.
  runManagerDelta(mgr, r1, withPolicies(r1, {a, b}));
  // updateFailed resyncs to the known-good (qzk1-only) state.
  mgr.updateFailed(r1);

  // Re-applying the same update now succeeds cleanly with no double-count.
  auto acls = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {a, b})));
  ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
  ASSERT_NE(acls->getEntryIf("qzk2_tc2"), nullptr);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk2_tc2")), 202);
  EXPECT_EQ(acls->numEntries(), 2);
}

// --- Route-reference gating ---

TEST(PbrAclManagerTest, UnreferencedPolicyProgramsNoEntries) {
  auto a =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  EXPECT_EQ(pbrAcls(runManager(mgr, makeState({a}, true)))->numEntries(), 0);
}

TEST(PbrAclManagerTest, OnlyReferencedPoliciesProgram) {
  auto ref = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto unref =
      makeUnreferencedPolicy("qzk2", 300, {{ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto acls = pbrAcls(runManager(mgr, makeState({ref, unref}, true)));
  ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(acls->getEntryIf("qzk2_tc2"), nullptr);
  EXPECT_EQ(acls->numEntries(), 1);
}

TEST(PbrAclManagerTest, UnreferencedPolicyRemovalIsNoOp) {
  auto a =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  // 1. Nothing is programmed for it.
  auto r1 = runManager(mgr, makeState({a}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 0);
  // 2. Removing it must not trip removePolicyEntries' missing-entry throw.
  EXPECT_EQ(
      pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {})))->numEntries(), 0);
}

TEST(PbrAclManagerTest, PolicyBecomingReferencedProgramsEntries) {
  auto unref =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto ref = makePolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({unref}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 0);

  auto acls = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {ref})));
  ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc1")), 201);
}

TEST(PbrAclManagerTest, PolicyLosingItsLastRouteDropsEntries) {
  std::map<ForwardingClass, int64_t> class2Nhg{
      {ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}};
  auto ref = makePolicy("qzk1", 100, class2Nhg);
  auto unref = makeUnreferencedPolicy("qzk1", 100, class2Nhg);
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({ref}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 2);

  EXPECT_EQ(
      pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {unref})))
          ->numEntries(),
      0);
}

TEST(PbrAclManagerTest, UnreferencedPolicyChangeStaysUnprogrammed) {
  auto a1 =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  auto a2 =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 999}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({a1}, true));
  EXPECT_EQ(
      pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {a2})))->numEntries(),
      0);
}

TEST(PbrAclManagerTest, BecomingReferencedWhileTrafficClassesChange) {
  auto unref =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  // The same delta both binds the first route and adds a traffic class.
  auto ref = makePolicy(
      "qzk1",
      100,
      {{ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({unref}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 0);

  auto acls = pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {ref})));
  ASSERT_NE(acls->getEntryIf("qzk1_tc1"), nullptr);
  ASSERT_NE(acls->getEntryIf("qzk1_tc2"), nullptr);
  EXPECT_EQ(redirectOf(acls->getEntryIf("qzk1_tc2")), 202);
  EXPECT_EQ(acls->numEntries(), 2);
}

TEST(PbrAclManagerTest, LosingTheLastRouteWhileTrafficClassesChange) {
  std::map<ForwardingClass, int64_t> class2Nhg{
      {ForwardingClass::CLASS_1, 201}, {ForwardingClass::CLASS_2, 202}};
  auto ref = makePolicy("qzk1", 100, class2Nhg);
  // Drops CLASS_2 in the same delta that withdraws the last route: the removal
  // must follow the old policy's traffic classes, not the new one's.
  auto unref =
      makeUnreferencedPolicy("qzk1", 100, {{ForwardingClass::CLASS_1, 201}});
  PbrAclManager mgr;
  auto r1 = runManager(mgr, makeState({ref}, true));
  ASSERT_EQ(pbrAcls(r1)->numEntries(), 2);

  EXPECT_EQ(
      pbrAcls(runManagerDelta(mgr, r1, withPolicies(r1, {unref})))
          ->numEntries(),
      0);
}

} // namespace facebook::fboss
