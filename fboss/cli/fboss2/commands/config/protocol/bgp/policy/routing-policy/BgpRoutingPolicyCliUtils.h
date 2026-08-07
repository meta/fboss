/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"

/**
 * Lookup/create helpers for the routing-policy CLI family, shared between the
 * policy-level dispatcher (CmdConfigProtocolBgpPolicyRoutingPolicy), the term
 * subcommand (CmdConfigProtocolBgpPolicyRoutingPolicyTerm), and their delete
 * counterparts. A routing-policy (BgpPolicyStatement) is keyed by name; a
 * term (BgpPolicyTerm in policy_entries[]) is keyed by sequence_number.
 */
namespace facebook::fboss::bgpcli {

inline bool routingPolicyExists(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return false;
  }
  const auto& policies = *cfg.policies()->bgp_policy_statements();
  return std::any_of(policies.begin(), policies.end(), [&](const auto& p) {
    return *p.name() == name;
  });
}

// The routing-policy statement keyed by name, or nullptr. Non-creating;
// used by the delete commands so a typo'd delete can't stage a change.
inline bgp::bgp_policy::BgpPolicyStatement* findRoutingPolicy(
    bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return nullptr;
  }
  auto& policies = *cfg.policies()->bgp_policy_statements();
  auto it = std::find_if(policies.begin(), policies.end(), [&](const auto& p) {
    return *p.name() == name;
  });
  return it == policies.end() ? nullptr : &*it;
}

// Find the routing-policy statement keyed by name, creating it if absent.
// Setting an attribute on a not-yet-created policy implicitly creates it, so
// command ordering stays forgiving; a bare `routing-policy <name>` creates one
// explicitly. BgpPolicyStatement's only key field is `name`.
inline bgp::bgp_policy::BgpPolicyStatement& findOrCreateRoutingPolicy(
    bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  auto& policies = *cfg.policies().ensure().bgp_policy_statements();
  for (auto& policy : policies) {
    if (*policy.name() == name) {
      return policy;
    }
  }
  policies.emplace_back();
  auto& policy = policies.back();
  policy.name() = name;
  return policy;
}

inline bool routingPolicyTermExists(
    const bgp::bgp_policy::BgpPolicyStatement& policy,
    int64_t seqNum) {
  const auto& terms = *policy.policy_entries();
  return std::any_of(terms.begin(), terms.end(), [&](const auto& t) {
    return t.sequence_number().has_value() && *t.sequence_number() == seqNum;
  });
}

// Find the term keyed by sequence_number, creating it if absent. The name is
// seeded from the seq-num so every term has a stable, unique identity —
// BgpPolicyAction.next_term_id references terms by name.
inline bgp::bgp_policy::BgpPolicyTerm& findOrCreateRoutingPolicyTerm(
    bgp::bgp_policy::BgpPolicyStatement& policy,
    int64_t seqNum) {
  auto& terms = *policy.policy_entries();
  for (auto& term : terms) {
    if (term.sequence_number().has_value() &&
        *term.sequence_number() == seqNum) {
      return term;
    }
  }
  terms.emplace_back();
  auto& term = terms.back();
  term.sequence_number() = seqNum;
  term.name() = std::to_string(seqNum);
  return term;
}

} // namespace facebook::fboss::bgpcli
