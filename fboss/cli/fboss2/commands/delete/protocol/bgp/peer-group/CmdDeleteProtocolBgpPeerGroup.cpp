/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/peer-group/CmdDeleteProtocolBgpPeerGroup.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
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

// Parse + validate at construction so queryClient stays a thin dispatch.
// Throwing std::invalid_argument is how the framework surfaces arg parse
// errors (same mechanism as BgpPeerGroupConfig).
BgpPeerGroupRef::BgpPeerGroupRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Error: delete protocol bgp peer-group requires exactly one <name>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: peer-group name must not be empty");
  }
  groupName_ = std::move(v[0]);
}

CmdDeleteProtocolBgpPeerGroupTraits::RetType
CmdDeleteProtocolBgpPeerGroup::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Delete mirrors add: an absent group is already the requested end state,
  // so this is a success with a warning. Nothing is saved, so a typo'd delete
  // can't stage an unrelated session change.
  auto absent = fmt::format(
      "Warning: BGP peer-group {} does not exist; nothing to delete",
      args.groupName());
  if (!cfg.peer_groups().has_value()) {
    return absent;
  }
  auto& groups = *cfg.peer_groups();
  auto it = std::find_if(groups.begin(), groups.end(), [&](const auto& group) {
    return *group.name() == args.groupName();
  });
  if (it == groups.end()) {
    return absent;
  }
  // A neighbor's peer_group_name resolves against this group by name at
  // daemon load; erasing the group while a neighbor still points at it would
  // commit a dangling reference. Refuse and name the neighbors instead.
  std::vector<std::string> referencingPeers;
  for (const auto& peer : *cfg.peers()) {
    if (peer.peer_group_name().has_value() &&
        *peer.peer_group_name() == args.groupName()) {
      referencingPeers.push_back(*peer.peer_addr());
    }
  }
  if (!referencingPeers.empty()) {
    return fmt::format(
        "Error: BGP peer-group {} is still referenced by neighbor(s): {}; "
        "delete or re-point those neighbors first",
        args.groupName(),
        folly::join(", ", referencingPeers));
  }
  groups.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP peer-group {}\nConfig saved to: {}",
      args.groupName(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPeerGroup::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPeerGroup,
    CmdDeleteProtocolBgpPeerGroupTraits>::run();

} // namespace facebook::fboss
