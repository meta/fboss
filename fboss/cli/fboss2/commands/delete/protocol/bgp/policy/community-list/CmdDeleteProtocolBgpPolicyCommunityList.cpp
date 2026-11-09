/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/community-list/CmdDeleteProtocolBgpPolicyCommunityList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/community-list/BgpCommunityListCliUtils.h"
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
constexpr std::string_view kCommunityKeyword = "community";

void appendUnique(std::vector<std::string>& labels, const std::string& label) {
  if (std::find(labels.begin(), labels.end(), label) == labels.end()) {
    labels.push_back(label);
  }
}
} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
BgpCommunityListRef::BgpCommunityListRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: delete protocol bgp policy community-list requires <name>, "
        "optionally followed by `community <community>`");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: community-list name must not be empty");
  }
  listName_ = v[0];
  if (v.size() == 1) {
    return;
  }
  if (v[1] != kCommunityKeyword) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unexpected token '{}'. Usage: delete protocol bgp policy "
            "community-list <name> [community <community>]",
            v[1]));
  }
  if (v.size() < 3 || v[2].empty()) {
    throw std::invalid_argument("Error: `community` requires a <community>");
  }
  if (v.size() > 3) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unexpected token '{}' after community <community>", v[3]));
  }
  hasCommunity_ = true;
  community_ = v[2];
}

CmdDeleteProtocolBgpPolicyCommunityListTraits::RetType
CmdDeleteProtocolBgpPolicyCommunityList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Delete mirrors add: an absent target is already the requested end state,
  // so this is a success with a warning. Nothing is saved, so a typo'd delete
  // can't stage an unrelated session change.
  auto absent = fmt::format(
      "Warning: BGP community-list {} does not exist; nothing to delete",
      args.listName());
  if (!cfg.policies().has_value()) {
    return absent;
  }
  auto& lists = *cfg.policies()->community_lists();
  auto it = std::find_if(lists.begin(), lists.end(), [&](const auto& list) {
    return *list.name() == args.listName();
  });
  if (it == lists.end()) {
    return absent;
  }
  if (args.hasCommunity()) {
    // Remove one value from communities; the list itself stays.
    auto& list = *it;
    if (list.communities().has_value()) {
      auto& communities = *list.communities();
      auto valueIt =
          std::find(communities.begin(), communities.end(), args.community());
      if (valueIt != communities.end()) {
        if (communities.size() == 1) {
          // A community action resolves the list's values at daemon load and
          // fails on an empty one ("Missing communities in CommunityList");
          // a match reference tolerates it.
          const auto refs =
              bgpcli::findTermsReferencingCommunityList(cfg, args.listName());
          std::vector<std::string> actions;
          actions.reserve(refs.size());
          for (const auto& ref : refs) {
            if (ref.isAction) {
              appendUnique(actions, ref.label);
            }
          }
          if (!actions.empty()) {
            return fmt::format(
                "Error: {} is the last community of BGP community-list {}, "
                "which is still used by the community action of {}; remove "
                "those actions first",
                args.community(),
                args.listName(),
                folly::join(", ", actions));
          }
        }
        communities.erase(valueIt);
        if (communities.empty()) {
          // Leave the list looking like one that never had values, so a
          // later `community <community>` add starts from the same shape.
          list.communities().reset();
        }
        session.saveBgpConfig();
        return fmt::format(
            "Successfully deleted BGP community-list {} community {}\n"
            "Config saved to: {}",
            args.listName(),
            args.community(),
            session.getBgpSessionConfigPath());
      }
    }
    return fmt::format(
        "Warning: BGP community-list {} has no community {}; nothing to "
        "delete",
        args.listName(),
        args.community());
  }
  // A term's community_list_names / community_action_list_names resolve
  // against this list by name at daemon load; erasing the list while a term
  // still names it would commit a dangling reference and fail the load.
  // Refuse and name the terms instead.
  const auto refs =
      bgpcli::findTermsReferencingCommunityList(cfg, args.listName());
  if (!refs.empty()) {
    std::vector<std::string> referrers;
    referrers.reserve(refs.size());
    for (const auto& ref : refs) {
      appendUnique(referrers, ref.label);
    }
    return fmt::format(
        "Error: BGP community-list {} is still referenced by {}; remove those "
        "matches and actions first",
        args.listName(),
        folly::join(", ", referrers));
  }
  lists.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP community-list {}\nConfig saved to: {}",
      args.listName(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPolicyCommunityList::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPolicyCommunityList,
    CmdDeleteProtocolBgpPolicyCommunityListTraits>::run();

} // namespace facebook::fboss
