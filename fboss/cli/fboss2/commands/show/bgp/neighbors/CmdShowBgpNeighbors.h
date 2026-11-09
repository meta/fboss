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
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {

using facebook::neteng::fboss::bgp::thrift::TBgpSession;

struct CmdShowBgpNeighborsTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = std::vector<TBgpSession>;

  std::vector<utils::LocalOption> LocalOptions = {
      {kGar, "Show decoded GAR link bandwidth ext community"}};

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays the full per-session detail for BGP neighbors: the remote address and AS, confederation status, peer description, negotiated hold and keepalive timers, session state (with uptime when established, or flap count and last reset reason when not), UCMP link-bandwidth advertise/receive settings, the negotiated capability set (multiprotocol AFIs, route refresh, graceful restart, add-path), and the local/remote TCP endpoint and connection mode. Established sessions additionally render a prefix-telemetry table (current, announced and withdrawn prefixes per direction) and, when non-zero, socket and AdjRib message counters plus the EOR timestamps. With no argument every neighbor is shown, each under a 'neighbor N of M' separator; pass a peer address to scope to one neighbor, and optionally a second address to select a specific session by its BGP session ID. Use this after 'show bgp summary' points at a peer worth investigating.";
  }
};

class CmdShowBgpNeighbors
    : public CmdHandler<CmdShowBgpNeighbors, CmdShowBgpNeighborsTraits> {
 public:
  using RetType = CmdShowBgpNeighborsTraits::RetType;
  // - If no arguments are passed, the command will query all the neighbors
  //
  // - The first argument will always be considered as the neighbor ip
  // - The second argument will always be consdiered as the session id
  // - Every other address after the session id will be ignored.
  //
  // Note: Currently '--session_id' flag is no supported on FBOSS2.
  // In the mean time, the way to query over a neighbor with a session id,
  // the id must be passed right after the peer IP like the following:
  //    fboss2 show bgp neighbors 1.2.3.4 5.6.7.8
  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedIps) {
    std::vector<TBgpSession> sessions;

    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

    if (queriedIps.size() > 1) {
      std::cout << "Displaying information from neighbor: " << queriedIps[0]
                << ", session id: " << queriedIps[1] << std::endl;
      client->sync_getBgpNeighborsFromSession(
          sessions, /*p_peerId=*/queriedIps[0], /*sessionBgpId=*/queriedIps[1]);
    } else {
      client->sync_getBgpNeighbors(sessions, queriedIps);
    }

    return sessions;
  }

  void printOutput(const RetType& neighbors, std::ostream& out = std::cout) {
    printBgpNeighborsOutput(neighbors, out);
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. One established peer and
  // one configured listen range that nobody has connected on, so the example
  // covers both the rich established render and the sparse non-established one.
  static RetType sampleModel() {
    using facebook::neteng::fboss::bgp::thrift::TBgpPeer;
    using facebook::neteng::fboss::bgp::thrift::TBgpSessionDetail;
    using facebook::neteng::fboss::bgp_attr::AdvertiseLinkBandwidth;
    using facebook::neteng::fboss::bgp_attr::ReceiveLinkBandwidth;

    // printOutput passes the ports through ntohs(), so the stored values are
    // in network byte order: 45824 renders as the well-known BGP port 179.
    constexpr int32_t kBgpPortNetworkOrder = 45824;

    TBgpSessionDetail establishedDetails;
    establishedDetails.confed_peer() = false;
    establishedDetails.remote_bgp_id() = 0x0B0200C0; // 192.0.2.11
    establishedDetails.local_router_id() = "192.0.2.1";
    establishedDetails.rr_client() = true;
    establishedDetails.ipv4_unicast() = false;
    establishedDetails.ipv6_unicast() = true;
    establishedDetails.gr_restart_time() = 120;
    establishedDetails.gr_remote_restart_time() = 120;
    establishedDetails.connect_mode() = TBgpSessionConnectMode::PASSIVE_ONLY;
    establishedDetails.local_port() = 0;
    establishedDetails.peer_port() = kBgpPortNetworkOrder;
    establishedDetails.num_of_flaps() = 0;
    establishedDetails.enforce_first_as_rejects() = 0;
    establishedDetails.eor_sent_time() = 1788455780000;
    establishedDetails.eor_received_time() = 1788455782000;
    establishedDetails.sent_update_announcements_ipv6() = 118;
    establishedDetails.sent_update_withdrawals() = 10;
    establishedDetails.recv_update_announcements_ipv6() = 1355;
    establishedDetails.recv_update_withdrawals() = 12;

    TBgpPeer establishedPeer;
    // Sample ASNs (RFC 6996 private-use range); see the note in sampleModel()
    // of CmdShowBgpSummary.h.
    establishedPeer.local_as_4_byte() = 65108;
    establishedPeer.remote_as_4_byte() = 65221;
    establishedPeer.hold_time() = 120;
    establishedPeer.peer_state() = TBgpPeerState::ESTABLISHED;
    establishedPeer.lastResetHoldTimer() = 0;
    establishedPeer.lastSentKeepAlive() = 0;
    establishedPeer.lastRcvdKeepAlive() = 0;

    TBgpSession established;
    established.my_addr() = "2001:db8:e111:f162:27::";
    established.peer_addr() = "2001:db8:e11e:1062::4e";
    established.description() = "fsw001.p001.f01.abc1";
    // uptime is a duration in milliseconds; 39654000ms renders "11h 0m 54s".
    established.uptime() = 39654000;
    established.reset_time() = 0;
    established.num_resets() = 0;
    established.last_reset_reason() = "";
    established.prepolicy_rcvd_prefix_count() = 1343;
    established.postpolicy_rcvd_prefix_count() = 1343;
    established.postpolicy_sent_prefix_count() = 108;
    established.advertise_link_bandwidth() = AdvertiseLinkBandwidth::BEST_PATH;
    established.receive_link_bandwidth() = ReceiveLinkBandwidth::DISABLE;
    established.peer() = establishedPeer;
    established.details() = establishedDetails;

    TBgpSessionDetail listenRangeDetails = establishedDetails;
    listenRangeDetails.remote_bgp_id() = 0;
    listenRangeDetails.ipv6_unicast() = false;
    listenRangeDetails.gr_remote_restart_time() = 0;

    TBgpPeer listenRangePeer = establishedPeer;
    listenRangePeer.remote_as_4_byte() = 65108;
    listenRangePeer.peer_state() = TBgpPeerState::IDLE;

    TBgpSession listenRange;
    listenRange.my_addr() = "2001:db8:e111:f162:27::";
    listenRange.peer_addr() = "2001:db8:1ff:c100::/56";
    listenRange.description() = "";
    listenRange.uptime() = 0;
    listenRange.reset_time() = 0;
    listenRange.num_resets() = 0;
    listenRange.last_reset_reason() = "";
    listenRange.prepolicy_rcvd_prefix_count() = 0;
    listenRange.postpolicy_rcvd_prefix_count() = 0;
    listenRange.postpolicy_sent_prefix_count() = 0;
    listenRange.advertise_link_bandwidth() = AdvertiseLinkBandwidth::BEST_PATH;
    listenRange.receive_link_bandwidth() = ReceiveLinkBandwidth::DISABLE;
    listenRange.peer() = listenRangePeer;
    listenRange.details() = listenRangeDetails;

    return {established, listenRange};
  }
};
} // namespace facebook::fboss
