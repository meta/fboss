// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

namespace {
struct PolicyDelta {
  std::vector<std::shared_ptr<ClassBasedPolicyNode>> toUpdate;
  std::vector<std::string> toDel;
};

int64_t resolveNhgOrThrow(
    const ClassBasedPolicyNhgs& policy,
    const std::string& name,
    const std::string& nhgName) {
  if (auto it = policy.normalizedIds.find(nhgName);
      it != policy.normalizedIds.end()) {
    return static_cast<int64_t>(it->second);
  }
  XLOG(ERR) << "Unallocated next-hop group '" << nhgName << "' in policy '"
            << name << "'";
  throw FbossError("Unallocated next-hop group ", nhgName, " in ", name);
}

state::ClassBasedPolicyFields resolvePolicy(
    const NextHopIDManager& manager,
    const std::string& name,
    const ClassBasedPolicyNhgs& policy) {
  state::ClassBasedPolicyFields fields;
  fields.name() = name;
  fields.defaultNextHopGroup()->name() = policy.defaultNexthopGroup;
  fields.defaultNextHopGroup()->id() =
      resolveNhgOrThrow(policy, name, policy.defaultNexthopGroup);
  for (const auto& [fc, nhgName] : policy.class2NextHopGroup) {
    auto& entry = fields.class2NextHopGroup()[fc];
    entry.name() = nhgName;
    entry.id() = resolveNhgOrThrow(policy, name, nhgName);
  }
  fields.referenced() =
      manager.hasRoutesForNamedNhg(policy.defaultNexthopGroup);
  return fields;
}

bool samePolicy(
    const ClassBasedPolicyNode& node,
    const state::ClassBasedPolicyFields& fields) {
  return node.getDefaultNextHopGroup() == *fields.defaultNextHopGroup() &&
      node.getClass2NextHopGroup() == *fields.class2NextHopGroup() &&
      node.isReferenced() == *fields.referenced();
}

std::map<std::string, PolicyDelta> computePolicyDeltas(
    const NextHopIDManager& manager,
    const std::shared_ptr<SwitchState>& state) {
  const auto& store = manager.getPbrPolicyToNamedNhg();
  const auto& published = state->getClassBasedPolicies();
  std::map<std::string, state::ClassBasedPolicyFields> resolved;
  std::map<std::string, std::shared_ptr<ClassBasedPolicyNode>> newPolicies;
  auto getPolicyFields = [&](const std::string& name,
                             const ClassBasedPolicyNhgs& policy)
      -> const state::ClassBasedPolicyFields& {
    auto itr = resolved.find(name);
    if (itr == resolved.end()) {
      itr = resolved.emplace(name, resolvePolicy(manager, name, policy)).first;
    }
    return itr->second;
  };
  auto addToDelta = [&](PolicyDelta& delta,
                        const std::shared_ptr<ClassBasedPolicyMap>& existing,
                        const std::string& name,
                        const ClassBasedPolicyNhgs& policy) {
    auto node = existing ? existing->getPolicyIf(name) : nullptr;
    const auto& fields = getPolicyFields(name, policy);
    if (node && samePolicy(*node, fields)) {
      return;
    }
    auto& newPolicy = newPolicies[name];
    if (!newPolicy) {
      newPolicy = std::make_shared<ClassBasedPolicyNode>(fields);
    }
    delta.toUpdate.push_back(newPolicy);
  };

  std::map<std::string, PolicyDelta> deltas;
  for (const auto& [matcher, _] : std::as_const(*state->getFibsInfoMap())) {
    auto existing = published->getMapNodeIf(HwSwitchMatcher(matcher));
    PolicyDelta delta;
    for (const auto& [name, policy] : store) {
      addToDelta(delta, existing, name, policy);
    }
    if (existing) {
      for (const auto& [name, _policy] : std::as_const(*existing)) {
        if (!store.contains(name)) {
          delta.toDel.push_back(name);
        }
      }
    }
    if (!delta.toUpdate.empty() || !delta.toDel.empty()) {
      deltas.emplace(matcher, std::move(delta));
    }
  }
  for (const auto& [matcher, policyMap] : std::as_const(*published)) {
    if (state->getFibsInfoMap()->getNodeIf(matcher) || policyMap->empty()) {
      continue;
    }
    PolicyDelta delta;
    for (const auto& [name, _policy] : std::as_const(*policyMap)) {
      delta.toDel.push_back(name);
    }
    deltas.emplace(matcher, std::move(delta));
  }
  return deltas;
}

std::shared_ptr<SwitchState> applyPolicyDeltas(
    const std::shared_ptr<SwitchState>& state,
    const std::map<std::string, PolicyDelta>& deltas) {
  auto newMap = state->getClassBasedPolicies()->clone();
  for (const auto& [matcher, delta] : deltas) {
    HwSwitchMatcher hwMatcher(matcher);
    auto existing = newMap->getMapNodeIf(hwMatcher);
    auto policyMap =
        existing ? existing->clone() : std::make_shared<ClassBasedPolicyMap>();
    for (const auto& policy : delta.toUpdate) {
      if (policyMap->getPolicyIf(policy->getID())) {
        policyMap->updateNode(policy);
      } else {
        policyMap->addPolicy(policy);
      }
    }
    for (const auto& name : delta.toDel) {
      policyMap->removePolicy(name);
    }
    if (existing) {
      newMap->updateMapNode(policyMap, hwMatcher);
    } else {
      newMap->addMapNode(policyMap, hwMatcher);
    }
  }
  auto nextState = state;
  SwitchState::modify(&nextState);
  nextState->resetClassBasedPolicies(newMap);
  return nextState;
}
} // namespace

ClassBasedPolicyUpdater::ClassBasedPolicyUpdater(
    const NextHopIDManager* nextHopIDManager)
    : nextHopIDManager_(nextHopIDManager) {}

std::shared_ptr<SwitchState> ClassBasedPolicyUpdater::operator()(
    const std::shared_ptr<SwitchState>& state) {
  if (!nextHopIDManager_) {
    return state;
  }
  auto deltas = computePolicyDeltas(*nextHopIDManager_, state);
  if (deltas.empty()) {
    return state;
  }
  return applyPolicyDeltas(state, deltas);
}

} // namespace facebook::fboss
