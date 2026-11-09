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

#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;
using facebook::fboss::utils::Table;

struct CmdShowBgpStreamSummaryTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::vector<TBgpStreamSession>;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the clients subscribed to the BGP route stream - the pub/sub feed the daemon serves to external consumers such as mp-bgp monitors - one row per subscriber: its numeric peer ID, the subscriber name it registered with, how many prefixes have been sent to it, and how long the subscription has been up. Rows are ordered by peer ID. A subscriber whose uptime keeps resetting is reconnecting, and one whose route count is far below the others is likely subscribed to a narrower feed or is not keeping up. These are stream subscribers, not BGP peers: they do not appear in 'show bgp summary' and they hold no BGP session. Prints nothing at all when no client is subscribed. Use 'show bgp stream subscriber <id> pre-policy|post-policy' with a peer ID from this table to see what a specific subscriber is actually being sent.";
  }
};

class CmdShowBgpStreamSummary : public CmdHandler<
                                    CmdShowBgpStreamSummary,
                                    CmdShowBgpStreamSummaryTraits> {
 public:
  using RetType = CmdShowBgpStreamSummaryTraits::RetType;
  using ObjectArgType = CmdShowBgpStreamSummary::ObjectArgType;

  RetType queryClient(const HostInfo& hostInfo) {
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    std::vector<TBgpStreamSession> sessions;
    client->sync_getBgpStreamSessions(sessions);

    return sessions;
  }

  /*
   * Canned, synthetic model (no real switch data) used to render an example
   * for the CLI reference wiki. Two subscriptions from the same route monitor,
   * as a live RSW capture showed. Everything renders identically run to run
   * except the Uptime column: printOutput turns the sampled duration into an
   * epoch and back into an elapsed time, reading the wall clock once for each,
   * so the rendered seconds can differ by one between runs.
   */
  static RetType sampleModel() {
    auto session = [](int32_t peerId,
                      const std::string& name,
                      int64_t sentPrefixCount,
                      int64_t uptimeMs) {
      TBgpStreamSession stream;
      stream.peer_id() = peerId;
      stream.subscriber_name() = name;
      stream.sent_prefix_count() = sentPrefixCount;
      stream.uptime() = uptimeMs;
      return stream;
    };

    // uptime is a duration in milliseconds, rendered as elapsed time.
    return {
        session(1, "tsp_cco/netsystems/BgpMonitor_abc1/1", 10214, 40315000),
        session(2, "tsp_cco/netsystems/BgpMonitor_abc1/0", 10214, 40301000)};
  }

  void printOutput(RetType& sessions, std::ostream& out = std::cout) {
    if (sessions.empty()) {
      return;
    }

    std::sort(
        sessions.begin(),
        sessions.end(),
        [](const TBgpStreamSession& a, const TBgpStreamSession& b) -> bool {
          return folly::copy(a.peer_id().value()) <
              folly::copy(b.peer_id().value());
        });

    out << "BGP stream summary information for subscribers\n" << std::endl;
    Table table;
    table.setHeader({
        "Peer ID",
        "Name",
        "NumRoutes",
        "Uptime",
    });

    for (const auto& session : sessions) {
      table.addRow(
          {folly::to<std::string>(folly::copy(session.peer_id().value())),
           session.subscriber_name().value(),
           folly::to<std::string>(*session.sent_prefix_count()),
           utils::getPrettyElapsedTime(
               utils::getEpochFromDuration(
                   folly::copy(session.uptime().value())))});
    }
    out << table << std::endl;
  }
};

} // namespace facebook::fboss
