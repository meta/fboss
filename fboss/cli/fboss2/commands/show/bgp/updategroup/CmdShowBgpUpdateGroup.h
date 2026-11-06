/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <fmt/format.h>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/updategroup/gen-cpp2/bgp_update_group_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TUpdateGroupInfo;
using facebook::neteng::fboss::bgp::thrift::TUpdateGroupKey;
using facebook::neteng::fboss::bgp::thrift::TUpdateGroupPeerInfo;
using facebook::neteng::fboss::bgp::thrift::TUpdateGroupStats;
using facebook::neteng::fboss::bgp::thrift::TUpdateGroupSummary;

struct CmdShowBgpUpdateGroupTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
  using ObjectArgType = std::vector<std::string>;
  using RetType = cli::ShowBgpUpdateGroupModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays BGP update groups - the sets of peers that negotiated identical outbound parameters and therefore share one encoded copy of every update, which is what keeps advertisement cost proportional to distinct peer configurations rather than to peer count. With no argument it lists every group: ID, egress policy, state, how many members it has and how many of those are in sync or detached, the post-policy prefix count split v4/v6, and the RIB version the group last encoded. Pass a group ID for the detail view, which adds the full group key (the negotiated parameters that define the group - session type, peer group, egress policy and route filter, AFIs, add-path, RR-client and confederation status, out-delay, link bandwidth, private-ASN removal, 4-byte-AS and extended-next-hop capability, local AS), the cumulative message and queueing counters, and a per-peer table. Read the member counts first: members far above in-sync, or a non-zero detached count, means one slow peer is holding the group's encoding back, and the per-peer table's OutQ and DRV identify which. Per-peer table abbreviations (also printed as a legend above the table): LDR = last detach reason and LDT = last detach time (both '--' if the peer was never detached by the group, which excludes peers that came up detached), LRV = last RIB version the peer has seen, DRV = RIB version at which the peer detached, OutQ = depth of the peer's outbound (AdjRibOut) queue, SOE = self-owned entries (Adj-RIB-Out entries the peer holds under its own key while detached; 0 for in-sync peers, which share the group's entries), LSC = last update-group state change. Two results are not errors - 'Update group: DISABLED' means the feature is off on this switch, and 'No active update groups.' means it is on but nothing has grouped yet. An unknown group ID reports 'Update group not found.'; a non-numeric one is rejected outright.";
  }
};

class CmdShowBgpUpdateGroup
    : public CmdHandler<CmdShowBgpUpdateGroup, CmdShowBgpUpdateGroupTraits> {
 public:
  using RetType = CmdShowBgpUpdateGroupTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& args) {
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

    RetType model;
    if (args.empty()) {
      facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupSummariesResponse
          response;
      client->sync_getUpdateGroupSummaries(response);
      model.enable_update_group() = response.enable_update_group().value();
      model.update_group_summaries() = std::move(*response.update_groups());
      model.detail_mode() = false;
      return model;
    }

    int64_t targetId;
    try {
      size_t pos = 0;
      targetId = std::stoll(args[0], &pos);
      if (pos != args[0].size()) {
        throw std::invalid_argument("trailing characters");
      }
    } catch (const std::exception& e) {
      throw std::invalid_argument(
          fmt::format("Invalid update-group ID '{}': {}", args[0], e.what()));
    }

    facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupInfoRequest request;
    request.group_id() = targetId;
    facebook::neteng::fboss::bgp::thrift::TGetUpdateGroupInfoResponse response;
    client->sync_getUpdateGroupInfo(response, request);

    model.enable_update_group() = response.enable_update_group().value();
    model.update_groups() = std::move(*response.update_groups());
    model.detail_mode() = true;

    return model;
  }

  /*
   * Canned, synthetic model (no real switch data) used to render a
   * deterministic example for the CLI reference wiki. This is the no-argument
   * summary view, which is what the bare command does; the detail view is
   * reached by passing a group ID and is described in the prose.
   */
  static RetType sampleModel() {
    auto summary = [](int64_t groupId,
                      const std::string& policyName,
                      const std::string& state,
                      int64_t members,
                      int64_t inSync,
                      int64_t detached,
                      int64_t prefixesV4,
                      int64_t prefixesV6,
                      int64_t ribVersion) {
      TUpdateGroupSummary group;
      group.group_id() = groupId;
      group.egress_policy_name() = policyName;
      group.group_state() = state;
      group.member_count() = members;
      group.in_sync_peer_count() = inSync;
      group.detached_peer_count() = detached;
      group.post_out_prefix_count() = prefixesV4 + prefixesV6;
      group.post_out_prefix_count_ipv4() = prefixesV4;
      group.post_out_prefix_count_ipv6() = prefixesV6;
      group.last_seen_rib_version() = ribVersion;
      return group;
    };

    RetType model;
    model.enable_update_group() = true;
    model.detail_mode() = false;
    /*
     * Two groups: a healthy one whose members are all in sync, and one where
     * in-sync trails the member count and a peer has detached - the shape the
     * prose says to read the member counts for. The second also has a RIB
     * version of 0, which renders "N/A" rather than 0.
     */
    model.update_group_summaries() = {
        summary(1, "SAMPLE_UPLINK_OUT", "READY", 8, 8, 0, 5000, 3000, 123456),
        summary(2, "SAMPLE_SPINE_OUT", "WAITING", 11, 7, 4, 5000, 3000, 0)};
    return model;
  }

  void printOutput(const RetType& model, std::ostream& out = std::cout) {
    out.imbue(std::locale("C"));

    if (!model.enable_update_group().value()) {
      out << "Update group: DISABLED" << std::endl;
      return;
    }

    if (model.detail_mode().value()) {
      const auto& updateGroups = model.update_groups().value();
      if (updateGroups.empty()) {
        out << "Update group not found." << std::endl;
        return;
      }
      printDetailView(updateGroups[0], out);
      return;
    }

    const auto& summaries = model.update_group_summaries().value();
    if (summaries.empty()) {
      out << "No active update groups." << std::endl;
      return;
    }
    printSummaryView(summaries, out);
  }

 private:
  static std::string boolStr(bool v) {
    return v ? "Yes" : "No";
  }

  static std::string epochMsToString(int64_t epochMs) {
    auto seconds = epochMs / 1000;
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buf);
  }

  void printSummaryView(
      const std::vector<TUpdateGroupSummary>& groups,
      std::ostream& out) {
    out << "Update group: ENABLED" << std::endl;
    out << "Update Groups: " << groups.size() << std::endl;
    out << std::endl;

    Table table;
    table.setHeader(
        {"ID",
         "Policy Name",
         "State",
         "Members",
         "InSync",
         "Detached",
         "Prefixes(v4/v6)",
         "RIB Ver"});

    for (const auto& group : groups) {
      auto prefixes = fmt::format(
          "{}({}/{})",
          group.post_out_prefix_count().value(),
          group.post_out_prefix_count_ipv4().value(),
          group.post_out_prefix_count_ipv6().value());

      auto ribVer = group.last_seen_rib_version().value();
      table.addRow(
          {std::to_string(group.group_id().value()),
           group.egress_policy_name().value(),
           group.group_state().value(),
           std::to_string(group.member_count().value()),
           std::to_string(group.in_sync_peer_count().value()),
           std::to_string(group.detached_peer_count().value()),
           prefixes,
           ribVer > 0 ? std::to_string(ribVer) : "N/A"});
    }

    out << table << std::endl;
  }

  void printDetailView(const TUpdateGroupInfo& group, std::ostream& out) {
    const auto& key = group.group_key().value();

    out << "=== Update Group " << group.group_id().value()
        << " ===" << std::endl;

    out << std::endl;
    out << "Group Key:" << std::endl;
    out << "  Session Type:            " << key.session_type().value()
        << std::endl;
    out << "  Peer Group:              " << key.peer_group_name().value()
        << std::endl;
    out << "  Egress Policy:           " << key.egress_policy_name().value()
        << std::endl;
    out << "  Route Filter:            " << key.route_filter_stmt_name().value()
        << std::endl;

    out << "  AFI:                     "
        << (key.afi_ipv4_negotiated().value() ? "IPv4" : "")
        << (key.afi_ipv4_negotiated().value() &&
                    key.afi_ipv6_negotiated().value()
                ? "+"
                : "")
        << (key.afi_ipv6_negotiated().value() ? "IPv6" : "") << std::endl;

    out << "  Add-Path Send:           " << boolStr(key.send_add_path().value())
        << std::endl;
    out << "  RR Client:               " << boolStr(key.is_rr_client().value())
        << std::endl;
    out << "  Confed Peer:             "
        << boolStr(key.is_confed_peer().value()) << std::endl;
    out << "  Out Delay:               " << key.out_delay_seconds().value()
        << "s" << std::endl;

    out << "  Link Bandwidth:          advertise="
        << (key.advertise_link_bandwidth().has_value()
                ? apache::thrift::util::enumNameSafe(
                      key.advertise_link_bandwidth().value())
                : "-")
        << ", receive="
        << (key.receive_link_bandwidth().has_value()
                ? apache::thrift::util::enumNameSafe(
                      key.receive_link_bandwidth().value())
                : "-")
        << ", bps="
        << (key.link_bandwidth_bps().has_value()
                ? std::to_string(key.link_bandwidth_bps().value())
                : "-")
        << std::endl;

    out << "  Remove Private ASN:      "
        << boolStr(key.remove_private_asn().value()) << std::endl;
    out << "  4-Byte AS Capable:       "
        << boolStr(key.as4_byte_capable().value()) << std::endl;
    out << "  Ext NH Encoding:         "
        << boolStr(key.ext_nh_encoding_capable().value()) << std::endl;
    out << "  Peer Override:           " << boolStr(key.peer_override().value())
        << std::endl;
    out << "  Local AS:                " << key.local_as().value() << std::endl;
    out << "  AS Confederation ID:     "
        << (key.as_confed_id().has_value()
                ? std::to_string(key.as_confed_id().value())
                : "-")
        << std::endl;

    out << std::endl;
    out << "Runtime State:" << std::endl;
    out << "  Group State:               " << group.group_state().value()
        << std::endl;
    out << "  Member Count:              " << group.member_count().value()
        << std::endl;
    out << "  Number of In-Sync Members: " << group.in_sync_peer_count().value()
        << std::endl;
    out << "  Number of Detached Members: "
        << group.detached_peer_count().value() << std::endl;
    out << "  Last RIB Version:          "
        << group.last_seen_rib_version().value() << std::endl;

    const auto& stateCounts = group.peer_state_counts().value();
    if (!stateCounts.empty()) {
      out << std::endl;
      out << "Sub-State Breakdown:" << std::endl;
      for (const auto& [state, count] : stateCounts) {
        out << fmt::format("  {:<26} {}", state + ":", count) << std::endl;
      }
    }

    const auto& stats = group.stats().value();

    // Advertisement telemetry mirrors the per-peer `show bgp neighbors` view:
    // the current advertised-prefix gauge AND the cumulative message/PDU
    // counts, shown as two separate sections. There is no socket-layer
    // counterpart at the group level (socket tx/rx is inherently per-peer) --
    // see `show bgp neighbors` for a member's socket counts.
    out << std::endl;
    out << "Prefixes advertised (post-policy RIB-OUT):" << std::endl;
    out << "  Total:                   "
        << stats.post_out_prefix_count().value() << std::endl;
    out << "  IPv4:                    "
        << stats.post_out_prefix_count_ipv4().value() << std::endl;
    out << "  IPv6:                    "
        << stats.post_out_prefix_count_ipv6().value() << std::endl;

    out << std::endl;
    out << "Messages sent (PDU count):" << std::endl;
    out << "  Announcements (v4/v6):   "
        << stats.total_sent_announcement_msgs_ipv4().value() << "/"
        << stats.total_sent_announcement_msgs_ipv6().value() << std::endl;
    out << "  Withdrawals:             "
        << stats.total_sent_withdrawal_msgs().value() << std::endl;

    out << std::endl;
    out << "Stats:" << std::endl;
    out << "  Queue Blocks:            "
        << stats.group_total_queue_blocks().value()
        << " (total wait: " << stats.group_total_queue_wait_ms().value()
        << "ms)" << std::endl;
    out << "  Slow Peer Detachments:   "
        << stats.slow_peer_detachments().value() << std::endl;
    out << "  DFP Rejoins:             " << stats.dfp_rejoin_events().value()
        << std::endl;
    out << "  DSP Rejoins:             " << stats.dsp_rejoin_events().value()
        << std::endl;
    out << "  Lazy Clones:             " << stats.lazy_clone_events().value()
        << std::endl;
    out << "  Collapse Corrections:    "
        << stats.collapse_entries_corrected().value() << std::endl;

    out << std::endl;
    out << "Diagnostics:" << std::endl;

    if (group.initial_dump_completion_time_ms().has_value()) {
      out << "  Initial Dump Completed:  "
          << epochMsToString(group.initial_dump_completion_time_ms().value())
          << std::endl;
    } else {
      out << "  Initial Dump Completed:  N/A" << std::endl;
    }

    out << "  Total Discrepancies:     " << group.total_discrepancies().value()
        << std::endl;

    const auto& peers = group.peers().value();
    if (!peers.empty()) {
      out << std::endl;
      out << "Peers:" << std::endl;
      out << "Legend:" << std::endl;
      out << "  LDR  Last detach reason ('--' if never detached by group, "
             "excludes peers that came up detached)"
          << std::endl;
      out << "  LDT  Last detach time ('--' if never detached by group, "
             "excludes peers that came up detached)"
          << std::endl;
      out << "  LRV  Last RIB version the peer has seen" << std::endl;
      out << "  DRV  RIB version at which the peer detached" << std::endl;
      out << "  OutQ Depth of the peer's outbound (AdjRibOut) queue"
          << std::endl;
      out << "  SOE  Self-owned entries: Adj-RIB-Out entries the peer holds "
             "under its own key while detached (0 for in-sync peers)"
          << std::endl;
      out << "  LSC  Last update-group state change" << std::endl;
      out << std::endl;

      Table table;
      table.setHeader(
          {"Peer",
           "State",
           "Sync",
           "Blocked",
           "Detached",
           "LDR",
           "LDT",
           "Type",
           "LRV",
           "DRV",
           "OutQ",
           "SOE",
           "LSC"});

      for (const auto& peer : peers) {
        table.addRow(
            {peer.peer_addr().value(),
             peer.peer_state().value(),
             boolStr(peer.is_in_sync().value()),
             boolStr(peer.is_blocked().value()),
             boolStr(peer.is_detached().value()),
             peer.last_detach_reason().has_value()
                 ? peer.last_detach_reason().value()
                 : std::string("--"),
             peer.last_detach_time_ms().has_value()
                 ? epochMsToString(peer.last_detach_time_ms().value())
                 : std::string("--"),
             peer.detach_type().has_value() ? peer.detach_type().value()
                                            : std::string("-"),
             std::to_string(peer.last_seen_rib_version().value()),
             peer.detached_rib_version().has_value()
                 ? std::to_string(peer.detached_rib_version().value())
                 : std::string("-"),
             std::to_string(peer.queue_size().value()),
             std::to_string(peer.entry_count().value()),
             peer.last_modified_peer_update_state_time_ms().has_value()
                 ? epochMsToString(
                       peer.last_modified_peer_update_state_time_ms().value())
                 : std::string("-")});
      }
      out << table << std::endl;
    }
  }
};

} // namespace facebook::fboss
