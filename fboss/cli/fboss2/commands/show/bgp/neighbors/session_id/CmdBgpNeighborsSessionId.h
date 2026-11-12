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

#include <algorithm>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/neighbors/CmdShowBgpNeighbors.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h" // NOLINT(misc-include-cleaner)

namespace facebook::fboss {

struct CmdBgpNeighborsSessionIdTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpNeighbors;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PEERID_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::vector<TBgpSession>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the full per-session detail for one specific BGP session, selected by peer address and session ID rather than by peer address alone. It exists because a peer address is not always unique: when the same neighbor address has more than one session, 'show bgp neighbors <peer>' renders all of them one after another, and the session ID from that output is what narrows the view to the one you care about. The rendered fields are identical to 'show bgp neighbors' - state and uptime, negotiated timers and capabilities, prefix telemetry, TCP endpoints and counters - so only the selection differs. Both the peer address and the session ID are required; without a session ID the command prints its usage line and returns nothing.";
  }
};

class CmdBgpNeighborsSessionId : public CmdHandler<
                                     CmdBgpNeighborsSessionId,
                                     CmdBgpNeighborsSessionIdTraits> {
 public:
  using RetType = CmdBgpNeighborsSessionIdTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedIps,
      const ObjectArgType& queriedSessions) {
    std::vector<TBgpSession> sessions;

    if (queriedSessions.empty()) {
      std::cout
          << "This command must have a session ID as argument: fboss2 show bgp neighbors [ADDR] session_id [Session ID]"
          << std::endl;
      return {};
    }

    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

    client->sync_getBgpNeighborsFromSession(
        sessions, queriedIps[0], queriedSessions[0]);

    return sessions;
  }

  void printOutput(const RetType& neighbors, std::ostream& out = std::cout) {
    printBgpNeighborsOutput(neighbors, out);
  }

  // Canned, synthetic model (no real switch data). A session ID selects
  // exactly one session, so this is the established peer from the shared
  // 'show bgp neighbors' sample on its own - the listen range there has no
  // session to select.
  static RetType sampleModel() {
    // Pick the established session explicitly rather than by position: the
    // parent's sample also holds a listen range, which has no session to
    // select, and relying on ordering would silently document it instead.
    const auto sessions = CmdShowBgpNeighbors::sampleModel();
    const auto established = std::find_if(
        sessions.begin(), sessions.end(), [](const TBgpSession& session) {
          return apache::thrift::
                     is_non_optional_field_set_manually_or_by_serializer(
                         session.peer()) &&
              *session.peer()->peer_state() == TBgpPeerState::ESTABLISHED;
        });
    CHECK(established != sessions.end())
        << "'show bgp neighbors' sample no longer holds an established session";
    return {*established};
  }
};
} // namespace facebook::fboss
