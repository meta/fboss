/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/holdtimers/CmdShowBgpHoldTimers.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

namespace facebook::fboss {

CmdShowBgpHoldTimers::RetType CmdShowBgpHoldTimers::queryClient(
    const HostInfo& hostInfo,
    const ObjectArgType& queriedPeers) {
  RetType holdTimers;

  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  client->sync_getHoldTimers(holdTimers, queriedPeers);
  return holdTimers;
}

void CmdShowBgpHoldTimers::printOutput(const RetType& data, std::ostream& out) {
  if (data.empty()) {
    out << "No hold timer information available" << std::endl;
    return;
  }

  utils::Table table;
  table.setHeader({"Peer Address", "Hold Time Remaining (ms)"});

  for (const auto& info : data) {
    table.addRow({
        *info.peer_address(),
        folly::to<std::string>(*info.hold_time_remaining_ms()),
    });
  }
  out << table << std::endl;
}

std::string_view CmdShowBgpHoldTimersTraits::description() {
  return "Displays how much of each established session's hold timer is left before the peer would be declared down, in milliseconds. The daemon resets a peer's hold timer every time it receives a keepalive or update from that peer, so on a healthy session the remaining time cycles between the full negotiated hold time and roughly two thirds of it; a value that keeps drifting toward zero for one peer means keepalives from that peer are not arriving. Only sessions with a running hold timer are listed - peers that are not established have no timer and are omitted, which is why this list is usually shorter than 'show bgp summary'. With no argument every peer is shown; pass one or more peer addresses to scope the output. Prints 'No hold timer information available' when no session has a running timer.";
}

CmdShowBgpHoldTimers::RetType CmdShowBgpHoldTimers::sampleModel() {
  auto holdTimer = [](const std::string& peerAddress, int64_t remainingMs) {
    THoldTimerInfo info;
    info.peer_address() = peerAddress;
    info.hold_time_remaining_ms() = remainingMs;
    return info;
  };

  // Values sit between two thirds and the whole of a 30s negotiated hold time,
  // the normal spread when each peer's timer was last reset at a different
  // point in its keepalive cycle.
  return {
      holdTimer("192.0.2.11", 22809),
      holdTimer("192.0.2.12", 29548),
      holdTimer("2001:db8:e11e:1062::4e", 24676),
      holdTimer("2001:db8:e11e:1162::4e", 22513)};
}

template void
CmdHandler<CmdShowBgpHoldTimers, CmdShowBgpHoldTimersTraits>::run();

} // namespace facebook::fboss
