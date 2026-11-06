/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/as-path-list/CmdDeleteProtocolBgpPolicyAsPathList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/BgpAsPathListCliUtils.h"
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
constexpr std::string_view kRegexKeyword = "regex";
} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
BgpAsPathListRef::BgpAsPathListRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: delete protocol bgp policy as-path-list requires <name>, "
        "optionally followed by `regex <regex>`");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: as-path-list name must not be empty");
  }
  listName_ = v[0];
  if (v.size() == 1) {
    return;
  }
  if (v[1] != kRegexKeyword) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unexpected token '{}'. Usage: delete protocol bgp policy "
            "as-path-list <name> [regex <regex>]",
            v[1]));
  }
  if (v.size() < 3 || v[2].empty()) {
    throw std::invalid_argument("Error: `regex` requires a <regex>");
  }
  if (v.size() > 3) {
    throw std::invalid_argument(
        fmt::format("Error: unexpected token '{}' after regex <regex>", v[3]));
  }
  hasRegex_ = true;
  regex_ = v[2];
}

CmdDeleteProtocolBgpPolicyAsPathListTraits::RetType
CmdDeleteProtocolBgpPolicyAsPathList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Delete mirrors add: an absent target is already the requested end state,
  // so this is a success with a warning. Nothing is saved, so a typo'd delete
  // can't stage an unrelated session change.
  auto absent = fmt::format(
      "Warning: BGP as-path-list {} does not exist; nothing to delete",
      args.listName());
  if (!cfg.policies().has_value()) {
    return absent;
  }
  auto& lists = *cfg.policies()->aspath_lists();
  auto it = std::find_if(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == args.listName();
  });
  if (it == lists.end()) {
    return absent;
  }
  if (args.hasRegex()) {
    // Removing one pattern from a referenced list is safe: the name the terms
    // resolve stays defined.
    auto& list = *it;
    if (list.as_paths().has_value()) {
      auto& paths = *list.as_paths();
      auto pathIt = std::find(paths.begin(), paths.end(), args.regex());
      if (pathIt != paths.end()) {
        paths.erase(pathIt);
        if (paths.empty()) {
          list.as_paths().reset();
        }
        session.saveBgpConfig();
        return fmt::format(
            "Successfully deleted BGP as-path-list {} regex {}\n"
            "Config saved to: {}",
            args.listName(),
            args.regex(),
            session.getBgpSessionConfigPath());
      }
    }
    return fmt::format(
        "Warning: BGP as-path-list {} has no regex {}; nothing to delete",
        args.listName(),
        args.regex());
  }
  // A term's as_path_list_names resolves against this list by name at daemon
  // load; erasing the list while a term still names it would commit a
  // dangling reference and fail the load. Refuse and name the terms instead.
  std::vector<std::string> referrers;
  for (const auto& ref :
       bgpcli::findTermsReferencingAsPathList(cfg, args.listName())) {
    referrers.push_back(ref.label);
  }
  if (!referrers.empty()) {
    return fmt::format(
        "Error: BGP as-path-list {} is still referenced by {}; remove those "
        "matches first",
        args.listName(),
        folly::join(", ", referrers));
  }
  lists.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP as-path-list {}\nConfig saved to: {}",
      args.listName(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPolicyAsPathList::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPolicyAsPathList,
    CmdDeleteProtocolBgpPolicyAsPathListTraits>::run();

} // namespace facebook::fboss
