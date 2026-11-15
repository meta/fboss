// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/AclResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/AclTable.h"
#include "fboss/agent/state/AclTableGroupMap.h"
#include "fboss/agent/state/ClassBasedPolicyMap.h"
#include "fboss/agent/state/ClassBasedPolicyNode.h"
#include "fboss/agent/state/PbrUtils.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

namespace {
constexpr uint32_t kHundredPercentage = 100;

// Unreferenced policies program nothing yet, but counting them rejects the
// addPolicies call itself rather than the later route update that binds one.
size_t numPbrAclRules(const SwitchState& state) {
  size_t rules = 0;
  for (const auto& [_matcher, policyMap] :
       std::as_const(*state.getClassBasedPolicies())) {
    for (const auto& [_name, policy] : std::as_const(*policyMap)) {
      rules += policy->cref<switch_state_tags::class2NextHopGroup>()->size();
    }
  }
  return rules;
}

// ACL table groups are all scoped to the L3 switch matcher, so the state holds
// a single PBR ACL table.
size_t numPbrAclEntries(const SwitchState& state) {
  const auto& groups = state.getAclTableGroups();
  if (!groups) {
    return 0;
  }
  for (const auto& [_matcher, groupMap] : std::as_const(*groups)) {
    if (auto table = getPbrAclTable(*groupMap)) {
      const auto& aclMap = table->getAclMap();
      return aclMap ? aclMap->numEntries() : 0;
    }
  }
  return 0;
}
} // namespace

AclResourceAccountant::AclResourceAccountant(const HwAsicTable* asicTable)
    : asicTable_(asicTable) {}

bool AclResourceAccountant::isValidUpdate(const StateDelta& delta) const {
  const auto& oldState = delta.oldState();
  const auto& newState = delta.newState();
  if (oldState->getAclTableGroups() == newState->getAclTableGroups() &&
      oldState->getClassBasedPolicies() == newState->getClassBasedPolicies()) {
    return true;
  }
  const auto rules = numPbrAclRules(*newState);
  const auto entries = numPbrAclEntries(*newState);
  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    const auto limit = hwAsic->getMaxPbrAclEntries();
    if (!limit.has_value()) {
      continue;
    }
    // 1. Count the rules of every PBR policy, i.e. the ACL entries they would
    //    program if all were referenced, and keep that below
    //    FLAGS_pbr_acl_resource_percentage of the limit.
    // 2. Count the current ACL entries and keep them under 100% of the limit,
    //    to accommodate make-before-break temporaries.
    const auto steadyLimit =
        *limit * FLAGS_pbr_acl_resource_percentage / kHundredPercentage;
    if (rules > steadyLimit || entries > *limit) {
      XLOG(ERR) << "PBR ACL resource limit exceeded. Rules: " << rules
                << " limit: " << steadyLimit << ", entries: " << entries
                << " limit: " << *limit;
      return false;
    }
  }
  return true;
}

} // namespace facebook::fboss
