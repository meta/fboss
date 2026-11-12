/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/prefix-list/CmdDeleteProtocolBgpPolicyPrefixList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/BgpPrefixListCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {
// CLI keyword selecting the nested entry by its base prefix, matching the
// config command's grammar.
constexpr std::string_view kObjectName = "prefix-list";
constexpr std::string_view kPrefixKeyword = "prefix";

// Every routing-policy term whose PREFIX_LIST match names `listName` in
// prefix_filters.prefix_list_names (the field bgpd resolves at config load),
// labelled the way the CLI addresses the term.
std::vector<std::string> findTermsReferencingPrefixList(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& listName) {
  std::vector<std::string> referrers;
  if (!cfg.policies().has_value()) {
    return referrers;
  }
  for (const auto& policy : *cfg.policies()->bgp_policy_statements()) {
    for (const auto& term : *policy.policy_entries()) {
      if (!term.policy_match_entries().has_value()) {
        continue;
      }
      for (const auto& match : *term.policy_match_entries()->match_entries()) {
        if (!match.prefix_filters().has_value()) {
          continue;
        }
        const auto& names = *match.prefix_filters()->prefix_list_names();
        if (std::find(names.begin(), names.end(), listName) == names.end()) {
          continue;
        }
        referrers.push_back(
            term.sequence_number().has_value()
                ? fmt::format(
                      "policy {} term {}",
                      *policy.name(),
                      *term.sequence_number())
                : fmt::format(
                      "policy {} term {}", *policy.name(), *term.name()));
        break;
      }
    }
  }
  return referrers;
}
} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
BgpPrefixListRef::BgpPrefixListRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  // Pre-empt the generic member-selector message for a missing prefix: the
  // shared parser would call the member a <name>, but here it is a prefix.
  if (v.size() >= 2 && v[1] == kPrefixKeyword &&
      (v.size() < 3 || v[2].empty())) {
    throw std::invalid_argument("Error: `prefix` requires a <prefix/len>");
  }
  auto selector = bgpcli::parseListMemberSelector(
      v,
      kObjectName,
      kPrefixKeyword,
      "Error: delete protocol bgp policy prefix-list requires <name>, "
      "optionally followed by `prefix <prefix/len>`");
  // Unlike the config grammar, nothing may follow the parsed prefix: there
  // are no attributes to delete through this command.
  if (selector.restStart < v.size()) {
    throw std::invalid_argument(
        selector.memberName
            ? fmt::format(
                  "Error: unexpected token '{}' after prefix <prefix/len>",
                  v.at(selector.restStart))
            : fmt::format(
                  "Error: unexpected token '{}'. Usage: delete protocol bgp "
                  "policy prefix-list <name> [prefix <prefix/len>]",
                  v.at(selector.restStart)));
  }
  listName_ = std::move(selector.listName);
  if (selector.memberName) {
    const auto normalizedPrefix =
        bgpcli::normalizePrefixWithLength(*selector.memberName);
    if (!normalizedPrefix) {
      throw std::invalid_argument(
          fmt::format(
              "Error: Invalid prefix '{}'; expected <prefix/len>",
              *selector.memberName));
    }
    basePrefix_ = *normalizedPrefix;
  }
}

CmdDeleteProtocolBgpPolicyPrefixListTraits::RetType
CmdDeleteProtocolBgpPolicyPrefixList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Delete mirrors add: an absent target is already the requested end state,
  // so this is a success with a warning. Nothing is saved, so a typo'd delete
  // can't stage an unrelated session change.
  const auto absent = [&] {
    return fmt::format(
        "Warning: BGP prefix-list {} does not exist; nothing to delete",
        args.listName());
  };
  if (!cfg.policies().has_value()) {
    return absent();
  }
  auto& lists = *cfg.policies()->prefix_lists();
  auto it = std::find_if(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == args.listName();
  });
  if (it == lists.end()) {
    return absent();
  }
  if (args.hasPrefix()) {
    // Delete a single entry; the list itself stays.
    auto& entries = *it->prefixes();
    auto entryIt =
        std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
          return bgpcli::prefixListEntryMatches(entry, args.basePrefix());
        });
    if (entryIt == entries.end()) {
      return fmt::format(
          "Warning: BGP prefix-list {} has no prefix {}; nothing to delete",
          args.listName(),
          args.basePrefix());
    }
    entries.erase(entryIt);
    session.saveBgpConfig();
    return fmt::format(
        "Successfully deleted BGP prefix-list {} prefix {}\n"
        "Config saved to: {}",
        args.listName(),
        args.basePrefix(),
        session.getBgpSessionConfigPath());
  }
  // A term's prefix_list_names resolves against this list by name at daemon
  // load; erasing the list while a term still names it would commit a
  // dangling reference and fail the load. Refuse and name the terms instead.
  // Deleting a single entry above is safe: the name stays defined.
  auto referrers = findTermsReferencingPrefixList(cfg, args.listName());
  if (!referrers.empty()) {
    return fmt::format(
        "Error: BGP prefix-list {} is still referenced by {}; remove those "
        "matches first",
        args.listName(),
        folly::join(", ", referrers));
  }
  lists.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP prefix-list {}\nConfig saved to: {}",
      args.listName(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPolicyPrefixList::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPolicyPrefixList,
    CmdDeleteProtocolBgpPolicyPrefixListTraits>::run();

} // namespace facebook::fboss
