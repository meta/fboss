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
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

/**
 * Lookup/create helpers for the as-path-list CLI family, shared between the
 * list-level dispatcher (CmdConfigProtocolBgpPolicyAsPathList) and its delete
 * counterpart. An AsPathList is keyed by name. Its patterns live in the flat
 * as_paths string list, which is the field bgpd matches against; the
 * structured as_path_list[] entries are never read by bgpd and have no CLI.
 */
namespace facebook::fboss::bgpcli {

inline bool asPathListExists(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return false;
  }
  const auto& lists = *cfg.policies()->aspath_lists();
  return std::any_of(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == name;
  });
}

// Find the as-path-list keyed by name, creating it if absent. Setting an
// attribute on a not-yet-created list implicitly creates it, so command
// ordering stays forgiving; a bare `as-path-list <name>` creates one
// explicitly. AsPathList's only key field is `name`.
inline bgp::bgp_policy::AsPathList& findOrCreateAsPathList(
    bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  auto& lists = *cfg.policies().ensure().aspath_lists();
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

inline const bgp::bgp_policy::AsPathList* findAsPathList(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return nullptr;
  }
  const auto& lists = *cfg.policies()->aspath_lists();
  auto it = std::find_if(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == name;
  });
  return it == lists.end() ? nullptr : &*it;
}

// A routing-policy term whose AS_PATH match names an as-path-list. `label`
// identifies the term the way the CLI addresses it (`policy <P> term <seq>`;
// the term name when it has no sequence number) so a rejection can be acted
// on directly.
struct AsPathListReference {
  bgp::bgp_policy::AsPathList* filter;
  std::string label;
};

// Every AS_PATH match whose as_path_filters.as_path_list_names carries
// `listName`. bgpd resolves those names at config load and fails the load on
// a dangling one, so the delete command refuses while this is non-empty, and
// the boolean-operator setter writes through to each match (bgpd requires the
// referencing copy to carry the same operator as the list).
inline std::vector<AsPathListReference> findTermsReferencingAsPathList(
    bgp::thrift::BgpConfig& cfg,
    const std::string& listName) {
  std::vector<AsPathListReference> refs;
  if (!cfg.policies().has_value()) {
    return refs;
  }
  for (auto& policy : *cfg.policies()->bgp_policy_statements()) {
    for (auto& term : *policy.policy_entries()) {
      if (!term.policy_match_entries().has_value()) {
        continue;
      }
      for (auto& match : *term.policy_match_entries()->match_entries()) {
        if (!match.as_path_filters().has_value() ||
            !match.as_path_filters()->as_path_list_names().has_value()) {
          continue;
        }
        auto& filter = *match.as_path_filters();
        const auto& names = *filter.as_path_list_names();
        if (std::find(names.begin(), names.end(), listName) == names.end()) {
          continue;
        }
        refs.push_back(
            {&filter,
             term.sequence_number().has_value()
                 ? fmt::format(
                       "policy {} term {}",
                       *policy.name(),
                       *term.sequence_number())
                 : fmt::format(
                       "policy {} term {}", *policy.name(), *term.name())});
      }
    }
  }
  return refs;
}

} // namespace facebook::fboss::bgpcli
