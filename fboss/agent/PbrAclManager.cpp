// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/PbrAclManager.h"
#include "fboss/agent/FbossError.h"
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
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/PbrUtils.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

#include <memory>
#include <string>
#include <vector>

namespace facebook::fboss {

namespace {

[[noreturn]] void throwPbrTableMissing(
    const std::string& pbrTable,
    size_t numPolicies) {
  throw FbossError(
      "PBR table '",
      pbrTable,
      "' missing from SwitchState but ",
      numPolicies,
      " PBR policies are configured");
}

struct TemporaryRule {
  std::shared_ptr<ClassBasedPolicyNode> policy;
  ForwardingClass trafficClass;
};

struct PolicyAclUpdate {
  bool changed{false};
  std::vector<TemporaryRule> temporaries;
};

struct ChangedPolicy {
  std::shared_ptr<ClassBasedPolicyNode> old;
  std::shared_ptr<ClassBasedPolicyNode> updated;
};

enum class PolicyAction { Add, Remove, Change, None };

PolicyAction classifyPolicy(
    const std::shared_ptr<ClassBasedPolicyNode>& old,
    const std::shared_ptr<ClassBasedPolicyNode>& newPolicy) {
  if (!old->isReferenced()) {
    return newPolicy->isReferenced() ? PolicyAction::Add : PolicyAction::None;
  }
  return newPolicy->isReferenced() ? PolicyAction::Change
                                   : PolicyAction::Remove;
}

std::shared_ptr<AclTable> pbrTableIn(const AclTableGroupMap& groupMap) {
  auto group = groupMap.getAclTableGroupIf(cfg::AclStage::INGRESS);
  auto tableMap = group ? group->getAclTableMap() : nullptr;
  return tableMap ? tableMap->getTableIf(
                        cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE())
                  : nullptr;
}

void removeRuleEntry(
    AclMap& aclMap,
    const std::string& policyName,
    ForwardingClass trafficClass,
    bool temporary) {
  auto name = makePbrAclEntryName(policyName, trafficClass, temporary);
  if (!aclMap.removeNodeIf(name)) {
    throw FbossError(
        "PBR policy '",
        policyName,
        "' has no programmed ACL entry '",
        name,
        "' to remove");
  }
}

void removePolicyEntries(
    AclMap& aclMap,
    const std::shared_ptr<ClassBasedPolicyNode>& policy,
    bool temporary) {
  for (const auto& [tc, _] : policy->getClass2NextHopGroup()) {
    removeRuleEntry(aclMap, policy->getID(), tc, temporary);
  }
}

bool addPolicyEntries(
    AclMap& aclMap,
    const std::shared_ptr<ClassBasedPolicyNode>& policy) {
  bool changed = false;
  for (const auto& entry : createAclEntriesFromPolicy(policy)) {
    // reconstructFromSwitchState replays every policy as added against a state
    // whose ACL map is already populated, so an add can meet its own entry.
    if (auto cur = aclMap.getEntryIf(entry->getID()); cur && *cur == *entry) {
      continue;
    }
    aclMap.removeNodeIf(entry->getID());
    aclMap.addNode(entry);
    changed = true;
  }
  return changed;
}

PolicyAclUpdate applyChangedPolicy(
    AclMap& aclMap,
    const std::shared_ptr<ClassBasedPolicyNode>& old,
    const std::shared_ptr<ClassBasedPolicyNode>& newPolicy) {
  const bool defaultChanged = *old->getDefaultNextHopGroup().id() !=
      *newPolicy->getDefaultNextHopGroup().id();
  const auto& oldTc2Nhg = old->getClass2NextHopGroup();
  const auto& newTc2Nhg = newPolicy->getClass2NextHopGroup();
  const auto& policyName = newPolicy->getID();
  PolicyAclUpdate update;
  for (const auto& [tc, newNhg] : newTc2Nhg) {
    auto oldIt = oldTc2Nhg.find(tc);
    if (oldIt == oldTc2Nhg.end()) {
      aclMap.addNode(makePbrAclEntry(newPolicy, tc));
      update.changed = true;
      continue;
    }
    if (!defaultChanged && oldIt->second == newNhg) {
      continue;
    }
    auto original = makePbrAclEntryName(policyName, tc, false /*temporary*/);
    if (!aclMap.getEntryIf(original)) {
      throw FbossError(
          "changed PBR policy '",
          policyName,
          "' has no programmed ACL entry '",
          original,
          "' to update");
    }
    // TODO(zecheng): implement hitless (make-before-break) update here.
    aclMap.updateNode(makePbrAclEntry(newPolicy, tc));
    update.changed = true;
  }
  for (const auto& [tc, _] : oldTc2Nhg) {
    if (newTc2Nhg.count(tc)) {
      continue;
    }
    removeRuleEntry(aclMap, policyName, tc, false /*temporary*/);
    update.changed = true;
  }
  return update;
}

bool applyAddsAndRemoves(AclMap& newAclMap, const StateDelta& delta) {
  bool changed = false;
  DeltaFunctions::forEachChanged(
      delta.getClassBasedPoliciesDelta(),
      [&](const std::shared_ptr<ClassBasedPolicyNode>& old,
          const std::shared_ptr<ClassBasedPolicyNode>& newPolicy) {
        switch (classifyPolicy(old, newPolicy)) {
          case PolicyAction::Add:
            changed |= addPolicyEntries(newAclMap, newPolicy);
            break;
          case PolicyAction::Remove:
            removePolicyEntries(newAclMap, old, false /*temporary*/);
            changed = true;
            break;
          case PolicyAction::Change:
          case PolicyAction::None:
            break;
        }
      },
      [&](const std::shared_ptr<ClassBasedPolicyNode>& newPolicy) {
        if (!newPolicy->isReferenced()) {
          return;
        }
        changed |= addPolicyEntries(newAclMap, newPolicy);
      },
      [&](const std::shared_ptr<ClassBasedPolicyNode>& old) {
        if (!old->isReferenced()) {
          return;
        }
        removePolicyEntries(newAclMap, old, false /*temporary*/);
        changed = true;
      });
  return changed;
}

std::vector<ChangedPolicy> changedPolicies(const StateDelta& delta) {
  std::vector<ChangedPolicy> changed;
  DeltaFunctions::forEachChanged(
      delta.getClassBasedPoliciesDelta(),
      [&](const std::shared_ptr<ClassBasedPolicyNode>& old,
          const std::shared_ptr<ClassBasedPolicyNode>& newPolicy) {
        if (classifyPolicy(old, newPolicy) == PolicyAction::Change) {
          changed.push_back(ChangedPolicy{old, newPolicy});
        }
      },
      [](const std::shared_ptr<ClassBasedPolicyNode>&) {},
      [](const std::shared_ptr<ClassBasedPolicyNode>&) {});
  return changed;
}

// Returns nullptr, not the input state, when no matcher's map changed.
template <typename Mutate>
std::shared_ptr<SwitchState> updatePbrTables(
    const std::shared_ptr<SwitchState>& state,
    Mutate&& mutate) {
  auto newState = state;
  bool changed = false;
  for (const auto& [matcherStr, groupMap] :
       std::as_const(*state->getAclTableGroups())) {
    auto table = pbrTableIn(*groupMap);
    if (!table) {
      continue;
    }
    auto oldAclMap = table->getAclMap();
    auto newAclMap =
        oldAclMap ? oldAclMap->clone() : std::make_shared<AclMap>();
    if (!mutate(*newAclMap)) {
      continue;
    }
    table
        ->modify(&newState, HwSwitchMatcher(matcherStr), cfg::AclStage::INGRESS)
        ->setAclMap(std::move(newAclMap));
    changed = true;
  }
  return changed ? newState : nullptr;
}

bool hasPbrTable(const std::shared_ptr<SwitchState>& state) {
  auto groups = state->getAclTableGroups();
  if (!groups) {
    return false;
  }
  for (const auto& [_, groupMap] : std::as_const(*groups)) {
    if (pbrTableIn(*groupMap)) {
      return true;
    }
  }
  return false;
}

} // namespace

PbrAclManager::InputOutputState::InputOutputState(const StateDelta& in) {
  in.oldState()->publish();
  in.newState()->publish();
  out_.emplace_back(in.oldState(), in.newState());
}

void PbrAclManager::InputOutputState::publishLastDelta() {
  if (!out_.empty()) {
    DCHECK(out_.back().oldState()->isPublished());
    out_.back().newState()->publish();
  }
}

void PbrAclManager::InputOutputState::appendDelta(const StateDelta& delta) {
  publishLastDelta();
  DCHECK(out_.empty() || out_.back().newState() == delta.oldState());
  out_.emplace_back(delta.oldState(), delta.newState());
}

void PbrAclManager::InputOutputState::replaceLastDelta(
    const StateDelta& delta) {
  CHECK(!out_.empty());
  DCHECK(out_.back().oldState()->isPublished());
  DCHECK_EQ(out_.back().oldState(), delta.oldState());
  out_.pop_back();
  out_.emplace_back(delta.oldState(), delta.newState());
}

template <typename AddrT>
void PbrAclManager::InputOutputState::addOrUpdateRoute(
    RouterID rid,
    const std::shared_ptr<Route<AddrT>>& route,
    bool withRouteDelta) {
  DCHECK(getCurrentStateDelta().oldState()->isPublished());
  if (withRouteDelta) {
    publishLastDelta();
  }
  auto oldState = getCurrentStateDelta().newState();
  auto newState = oldState->clone();
  auto fib =
      newState->getFibsInfoMap()->getFibContainer(rid)->getFib<AddrT>()->modify(
          rid, &newState);
  if (fib->getRouteIf(route->prefix())) {
    fib->updateNode(route);
  } else {
    fib->addNode(route);
  }
  if (withRouteDelta) {
    appendDelta(StateDelta(oldState, newState));
  } else {
    replaceLastDelta(StateDelta(getCurrentStateDelta().oldState(), newState));
  }
}

template void PbrAclManager::InputOutputState::addOrUpdateRoute(
    RouterID rid,
    const std::shared_ptr<Route<folly::IPAddressV4>>& route,
    bool withRouteDelta);
template void PbrAclManager::InputOutputState::addOrUpdateRoute(
    RouterID rid,
    const std::shared_ptr<Route<folly::IPAddressV6>>& route,
    bool withRouteDelta);

StateDelta PbrAclManager::InputOutputState::getCurrentStateDelta() const {
  CHECK(!out_.empty());
  return StateDelta(out_.back().oldState(), out_.back().newState());
}

std::vector<StateDelta> PbrAclManager::modifyState(
    const std::vector<StateDelta>& deltas) {
  return modifyStateImpl(deltas);
}

std::vector<StateDelta> PbrAclManager::reconstructFromSwitchState(
    const std::shared_ptr<SwitchState>& curState) {
  XLOG(DBG2) << "PbrAclManager reconstructing from switch state";
  std::vector<StateDelta> deltas;
  deltas.emplace_back(std::make_shared<SwitchState>(), curState);
  deltas = modifyStateImpl(deltas);
  CHECK(!deltas.empty());
  StateDelta combined(deltas.front().oldState(), deltas.back().newState());
  deltas.clear();
  deltas.emplace_back(std::move(combined));
  return deltas;
}

void PbrAclManager::updateDone() {
  XLOG(DBG2) << "PbrAclManager update done";
}

void PbrAclManager::updateFailed(const std::shared_ptr<SwitchState>& curState) {
  XLOG(DBG2) << "PbrAclManager update failed";
  reconstructFromSwitchState(curState);
}

void PbrAclManager::processDelta(
    const StateDelta& delta,
    InputOutputState* inOutState) {
  auto newState = delta.newState();

  // TODO(zecheng): also react to namedNextHopGroup member changes (FibInfo
  // normalized-id deltas), which shift a policy's match/redirect ids without a
  // policy delta.
  if (DeltaFunctions::isEmpty(delta.getClassBasedPoliciesDelta())) {
    return;
  }
  auto policies = newState->getClassBasedPolicies();
  size_t numPolicies = policies ? policies->numNodes() : 0;

  if (!hasPbrTable(newState)) {
    if (numPolicies > 0) {
      throwPbrTableMissing(
          cfg::switch_config_constants::DEFAULT_PBR_ACL_TABLE(), numPolicies);
    }
    return;
  }

  bool first = true;
  auto emit = [&](const std::shared_ptr<SwitchState>& state) {
    state->publish();
    if (first) {
      inOutState->replaceLastDelta(StateDelta(delta.oldState(), state));
      first = false;
    } else {
      inOutState->appendDelta(
          StateDelta(inOutState->getCurrentStateDelta().newState(), state));
    }
    inOutState->updated = true;
  };

  if (auto addedAndRemoved = updatePbrTables(newState, [&](AclMap& aclMap) {
        return applyAddsAndRemoves(aclMap, delta);
      })) {
    newState = addedAndRemoved;
    emit(newState);
  }

  for (const auto& policy : changedPolicies(delta)) {
    auto made = updatePbrTables(newState, [&](AclMap& aclMap) {
      return applyChangedPolicy(aclMap, policy.old, policy.updated).changed;
    });
    if (!made) {
      continue;
    }
    newState = made;
    emit(newState);
  }
}

std::vector<StateDelta> PbrAclManager::modifyStateImpl(
    const std::vector<StateDelta>& deltas) {
  if (deltas.empty()) {
    return {};
  }
  std::vector<StateDelta> retDeltas;
  retDeltas.reserve(deltas.size());
  auto oldState = deltas.begin()->oldState();
  for (const auto& delta : deltas) {
    StateDelta curDelta(oldState, delta.newState());
    InputOutputState inOutState{curDelta};
    processDelta(curDelta, &inOutState);
    inOutState.publishLastDelta();
    for (auto& out : inOutState.moveDeltas()) {
      oldState = out.newState();
      retDeltas.emplace_back(out.oldState(), out.newState());
    }
  }
  return retDeltas;
}

} // namespace facebook::fboss
