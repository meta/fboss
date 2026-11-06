/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/IPAddress.h>
#include <algorithm>
#include <optional>
#include <string>

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

/**
 * Lookup/create helpers for the prefix-list CLI family, shared between the
 * list-level dispatcher (CmdConfigProtocolBgpPolicyPrefixList), the prefix
 * subcommand (CmdConfigProtocolBgpPolicyPrefixListEntry), and the delete
 * counterparts. A PrefixList is keyed by name; a PrefixListEntry (in
 * prefixes[]) is keyed by base_prefix, the only identity bgpd keeps (it
 * merges entries by prefix and rejects seq_num).
 */
namespace facebook::fboss::bgpcli {

inline bool prefixListExists(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  if (!cfg.policies().has_value()) {
    return false;
  }
  const auto& lists = *cfg.policies()->prefix_lists();
  return std::any_of(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == name;
  });
}

// Find the prefix-list keyed by name, creating it if absent. Setting an
// attribute on a not-yet-created list implicitly creates it, so command
// ordering stays forgiving; a bare `prefix-list <name>` creates one
// explicitly. PrefixList's only key field is `name`.
inline bgp::routing_policy::PrefixList& findOrCreatePrefixList(
    bgp::thrift::BgpConfig& cfg,
    const std::string& name) {
  auto& lists = *cfg.policies().ensure().prefix_lists();
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

// A prefix with an explicit /len, normalized with host bits cleared. folly
// fills in a default mask for a bare address, so the slash is checked
// separately.
inline std::optional<std::string> normalizePrefixWithLength(
    const std::string& s) {
  if (s.find('/') == std::string::npos) {
    return std::nullopt;
  }
  if (auto network = folly::IPAddress::tryCreateNetwork(
          s, /* defaultCidr */ -1, /* applyMask */ true)) {
    return folly::IPAddress::networkToString(*network);
  }
  return std::nullopt;
}

inline bool prefixListEntryMatches(
    const bgp::routing_policy::PrefixListEntry& entry,
    const std::string& normalizedBasePrefix) {
  const auto storedPrefix = normalizePrefixWithLength(*entry.base_prefix());
  return storedPrefix && *storedPrefix == normalizedBasePrefix;
}

inline bool prefixListEntryExists(
    const bgp::routing_policy::PrefixList& list,
    const std::string& basePrefix) {
  const auto& entries = *list.prefixes();
  return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
    return prefixListEntryMatches(entry, basePrefix);
  });
}

// Find the entry keyed by base_prefix within a list's prefixes[], creating it
// if absent. bgpd merges entries that share a base_prefix, so one entry per
// prefix is the only shape the CLI needs to address.
inline bgp::routing_policy::PrefixListEntry& findOrCreatePrefixListEntry(
    bgp::routing_policy::PrefixList& list,
    const std::string& basePrefix) {
  auto& entries = *list.prefixes();
  for (auto& entry : entries) {
    if (prefixListEntryMatches(entry, basePrefix)) {
      entry.base_prefix() = basePrefix;
      return entry;
    }
  }
  entries.emplace_back();
  auto& entry = entries.back();
  entry.base_prefix() = basePrefix;
  return entry;
}

} // namespace facebook::fboss::bgpcli
