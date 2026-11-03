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

#include <fmt/core.h>
#include <algorithm>
#include <string>
#include <vector>

#ifndef IS_OSS
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

/**
 * Lookup/create helpers for the community-list CLI family, shared between the
 * list-level dispatcher (CmdConfigProtocolBgpPolicyCommunityList) and its
 * delete counterpart. A CommunityList is keyed by name. Its values live in the
 * flat `communities` string list, which is the field bgpd matches against; the
 * structured members[] are never read by bgpd and have no CLI.
 */
namespace facebook::fboss::bgpcli {

inline bool communityListExists(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return false;
  }
  const auto& lists = *cfg.policies()->community_lists();
  return std::any_of(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == name;
  });
}

// Find the community-list keyed by name, creating it if absent. Setting an
// attribute on a not-yet-created list implicitly creates it, so command
// ordering stays forgiving; a bare `community-list <name>` creates one
// explicitly. CommunityList's only key field is `name`.
inline bgp::bgp_policy::CommunityList& findOrCreateCommunityList(
    bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  auto& lists = *cfg.policies().ensure().community_lists();
  for (auto& list : lists) {
    if (*list.name() == name) {
      return list;
    }
  }
  lists.emplace_back();
  auto& list = lists.back();
  list.name() = name;
  return list;
}

// A routing-policy term that names a community-list. A match reference
// (`filter` set) is a COMMUNITY match whose
// communities_filter.community_list_names carries the name; an action
// reference (`filter` null, `isAction`) is a community action whose
// community_action_list_names carries it. `label` identifies the term the
// way the CLI addresses it (`policy <P> term <seq>`).
struct CommunityListReference {
  bgp::bgp_policy::CommunityList* filter;
  std::string label;
  bool isAction;
};

// Every reference to `listName`. bgpd resolves both kinds by name at config
// load (CommunityMatch / CommunityAction::PopulateReferences) and fails the
// load on a dangling one, so the delete command refuses while this is
// non-empty. A match's inline copy must also carry the same boolean_operator
// as the list ("Conflicting boolean_operator from the reference") and bgpd
// reads exact_match only from that inline copy, so the config command writes
// both through to every match reference.
inline std::vector<CommunityListReference> findTermsReferencingCommunityList(
    bgp::thrift::BgpConfig& cfg,
    const std::string& listName) {
  std::vector<CommunityListReference> refs;
  if (!cfg.policies().has_value()) {
    return refs;
  }
  auto names = [&](const auto& list) {
    return std::find(list.begin(), list.end(), listName) != list.end();
  };
  for (auto& policy : *cfg.policies()->bgp_policy_statements()) {
    for (auto& term : *policy.policy_entries()) {
      auto label = term.sequence_number().has_value()
          ? fmt::format(
                "policy {} term {}", *policy.name(), *term.sequence_number())
          : fmt::format("policy {} term {}", *policy.name(), *term.name());
      if (term.policy_match_entries().has_value()) {
        for (auto& match : *term.policy_match_entries()->match_entries()) {
          if (!match.communities_filter().has_value() ||
              !match.communities_filter()->community_list_names().has_value()) {
            continue;
          }
          auto& filter = *match.communities_filter();
          if (names(*filter.community_list_names())) {
            refs.push_back({&filter, label, false});
          }
        }
      }
      for (auto& action : *term.policy_action_entries()) {
        if (!action.community_action().has_value() ||
            !action.community_action()
                 ->community_action_list_names()
                 .has_value()) {
          continue;
        }
        if (names(*action.community_action()->community_action_list_names())) {
          refs.push_back({nullptr, label, true});
        }
      }
    }
  }
  return refs;
}

} // namespace facebook::fboss::bgpcli
