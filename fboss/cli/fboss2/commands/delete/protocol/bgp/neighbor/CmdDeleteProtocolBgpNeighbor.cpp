/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/neighbor/CmdDeleteProtocolBgpNeighbor.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpPeerAddr.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

// Parse + validate at construction so queryClient stays a thin dispatch.
// Throwing std::invalid_argument is how the framework surfaces arg parse
// errors (same mechanism as BgpNeighborConfig).
BgpNeighborRef::BgpNeighborRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Error: delete protocol bgp neighbor requires exactly one "
        "<ip-address>");
  }
  auto normalized = bgpcli::normalizeBgpPeerAddr(v[0]);
  if (!normalized) {
    throw std::invalid_argument(
        fmt::format("Error: Invalid neighbor address '{}'", v[0]));
  }
  peerAddr_ = std::move(*normalized);
}

CmdDeleteProtocolBgpNeighborTraits::RetType
CmdDeleteProtocolBgpNeighbor::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  auto it = bgpcli::findBgpPeer(cfg, args.peerAddr());
  if (it == cfg.peers()->end()) {
    // Delete mirrors add: an absent neighbor is already the requested end
    // state, so this is a success with a warning. Nothing is saved, so a
    // typo'd delete can't stage an unrelated session change.
    return fmt::format(
        "Warning: BGP neighbor {} does not exist; nothing to delete",
        args.peerAddr());
  }
  cfg.peers()->erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP neighbor {}\nConfig saved to: {}",
      args.peerAddr(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpNeighbor::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpNeighbor,
    CmdDeleteProtocolBgpNeighborTraits>::run();

} // namespace facebook::fboss
