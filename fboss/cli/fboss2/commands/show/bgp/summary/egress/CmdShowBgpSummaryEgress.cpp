/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/summary/egress/CmdShowBgpSummaryEgress.h"

#include <algorithm>
#include <chrono> // NOLINT(misc-include-cleaner)
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "thrift/lib/cpp/util/EnumUtils.h" // NOLINT(misc-include-cleaner)

namespace facebook::fboss {

CmdShowBgpSummaryEgress::RetType CmdShowBgpSummaryEgress::queryClient(
    const HostInfo& hostInfo) {
  std::vector<TPeerEgressStats> peerEgressStats;

  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  client->sync_getPeerEgressStats(peerEgressStats);

  return createModel(peerEgressStats);
}

void CmdShowBgpSummaryEgress::printOutput(
    const RetType& model,
    std::ostream& out) {
  const auto& peerEgressStats = model.peer_egress_stats().value();

  out.imbue(std::locale("C"));

  printGroupSummary(peerEgressStats, out);
  out << std::endl;
  out << std::endl;
  printPeerSummary(peerEgressStats, out);
}

Table CmdShowBgpSummaryEgress::makePeerTable(
    const std::vector<TPeerEgressStats>& peerEgressStats) {
  Table table;
  table.setHeader({
      "Peer",
      "Description",
      "Group Name",
      "State",
      "Uptime",
      "Downtime",
      "PR",
      "PS",
      "UR",
      "US",
      "SU",
      "IQ Blocks",
      "Total IQ Wait (ms)",
      "Last IQ Block",
      "SQ Blocks",
      "Total SQ Wait (ms)",
      "Last SQ Block",
      "Socket Buffered",
      "Last Socket Buffered",
  });

  for (const auto& stats : peerEgressStats) {
    if (!stats.session().has_value()) {
      continue;
    }

    const auto& session = stats.session().value();
    const auto& peer = session.peer().value();

    std::string uptimeDurationString;
    if (*peer.peer_state() == TBgpPeerState::ESTABLISHED) {
      uptimeDurationString = utils::getPrettyElapsedTime(
          utils::getEpochFromDuration(*session.uptime()));
    }

    std::string downtimeDurationString;
    if (((*peer.peer_state() == TBgpPeerState::IDLE ||
          *peer.peer_state() == TBgpPeerState::IDLE_ADMIN)) &&
        (*session.num_resets() > 0)) {
      downtimeDurationString = utils::getPrettyElapsedTime(
          utils::getEpochFromDuration(*session.reset_time()));
    }

    /* PR, PS, UR, US, SU */
    auto rcvd_prefix = *session.prepolicy_rcvd_prefix_count();
    auto sent_prefix = *session.postpolicy_sent_prefix_count();
    auto recv_update_msgs = *session.recv_update_msgs();
    auto sent_update_msgs = *session.sent_update_msgs();
    auto suppressed_updates =
        stats.transient_route_updates_suppressed().value_or(0);

    /* iqueue (boundedAdjRibOutQueue_) related stats */
    auto iq_blocks = stats.adjribout_queue_blocks().value_or(0);
    auto iq_wait = stats.adjribout_queue_total_block_duration().value_or(0);
    std::string last_iq_block;
    if (stats.last_adjribout_queue_block_time() &&
        *stats.last_adjribout_queue_block_time() > 0) {
      last_iq_block =
          utils::parseTimeToTimeStamp(*stats.last_adjribout_queue_block_time());
    }

    /* sendQueue_ related stats */
    auto sq_blocks = stats.send_queue_blocks().value_or(0);
    auto sq_wait = stats.send_queue_total_block_duration().value_or(0);
    std::string last_sq_block;
    if (stats.last_send_queue_block_time() &&
        *stats.last_send_queue_block_time() > 0) {
      last_sq_block =
          utils::parseTimeToTimeStamp(*stats.last_send_queue_block_time());
    }

    /* async socket stats */
    auto sb = stats.total_async_socket_buffered().value_or(0);
    std::string last_sb_time;
    if (stats.last_socket_buffered_time() &&
        *stats.last_socket_buffered_time() > 0) {
      last_sb_time =
          utils::parseTimeToTimeStamp(*stats.last_socket_buffered_time());
    }

    table.addRow({
        session.peer_addr().value(), /* Peer */
        session.description().value(), /* Description */
        stats.group_name().value_or(kNoGroupName), /* Group Name */
        enumNameSafe(folly::copy(peer.peer_state().value()))
            .substr(0, 4), /* State */
        uptimeDurationString, /* Uptime */
        downtimeDurationString, /* Downtime */
        folly::to<std::string>(rcvd_prefix), /* PR */
        folly::to<std::string>(sent_prefix), /* PS */
        folly::to<std::string>(recv_update_msgs), /* UR */
        folly::to<std::string>(sent_update_msgs), /* US */
        folly::to<std::string>(suppressed_updates), /* SU */
        folly::to<std::string>(iq_blocks), /* IQ Blocks */
        folly::to<std::string>(iq_wait), /* Total IQ Wait */
        last_iq_block, /* Last IQ Block */
        folly::to<std::string>(sq_blocks), /* SQ Blocks */
        folly::to<std::string>(sq_wait), /* Total SQ Wait */
        last_sq_block, /* Last SQ Block */
        folly::to<std::string>(sb), /* Socket Buffered */
        last_sb_time, /* Last Socket Buffered */
    });
  }
  return table;
}

void CmdShowBgpSummaryEgress::printPeerSummary(
    const std::vector<TPeerEgressStats>& peerEgressStats,
    std::ostream& out) {
  out << "BGP Peer Egress Summary" << std::endl;
  out << "Acronyms: PR - Prefixes Received, PS - Prefixes Sent, "
      << "UR - Update Messages Received, US - Update Messages Sent, \n"
      << "SU - Suppressed Updates (updates not sent due to packing state compression), "
      << "IQ - Per-peer Input Queue to BGP I/O thread, "
      << "SQ - Per-peer Send Queue to socket \n"
      << std::endl;

  out << makePeerTable(peerEgressStats) << std::endl;
}

Table CmdShowBgpSummaryEgress::makeGroupTable(
    const std::vector<TPeerEgressStats>& peerEgressStats) {
  Table groupSummaryView;
  groupSummaryView.setHeader(
      {"Percentile",
       "Group Name",
       "PR",
       "PS",
       "UR",
       "US",
       "SU",
       "IQ Blocks",
       "Total IQ Wait (ms)",
       "Last IQ Block",
       "SQ Blocks",
       "Total SQ Wait (ms)",
       "Last SQ Block",
       "Socket Buffered",
       "Last Socket Buffered"});

  std::map<std::string, std::vector<std::vector<int64_t>>> groupedValues;

  /* Group all non-string data by @group_name. */
  for (const auto& stats : peerEgressStats) {
    if (!stats.session().has_value()) {
      continue;
    }

    const auto& session = *stats.session();

    /* Skip IDLE and IDLE_ADMIN peers from percentile calculations. */
    const auto& peerState = session.peer()->peer_state();
    if (*peerState == TBgpPeerState::IDLE ||
        *peerState == TBgpPeerState::IDLE_ADMIN) {
      continue;
    }

    std::string group = stats.group_name().value_or(kNoGroupName);

    /* Get group entry if it exists; otherwise create new one in map. */
    if (groupedValues.find(group) == groupedValues.end()) {
      groupedValues[group] =
          std::vector<std::vector<int64_t>>(PeerMetric::MAX_VALUE);
    }

    auto& metricVectors = groupedValues[group];

    /* PR, PS, UR, US, SU */
    metricVectors[PeerMetric::PrefixesRcvd].push_back(
        *session.prepolicy_rcvd_prefix_count());
    metricVectors[PeerMetric::PrefixesSent].push_back(
        *session.postpolicy_sent_prefix_count());
    metricVectors[PeerMetric::UpdatesRcvd].push_back(
        *session.recv_update_msgs());
    metricVectors[PeerMetric::UpdatesSent].push_back(
        *session.sent_update_msgs());
    metricVectors[PeerMetric::SuppressedUpdates].push_back(
        stats.transient_route_updates_suppressed().value_or(0));

    /* iqueue metrics */
    metricVectors[PeerMetric::TotalAdjRibOutQueueBlocks].push_back(
        stats.adjribout_queue_blocks().value_or(0));
    metricVectors[PeerMetric::TotalAdjRibOutQueueWait].push_back(
        stats.adjribout_queue_total_block_duration().value_or(0));
    metricVectors[PeerMetric::LastAdjRibOutQueueBlock].push_back(
        stats.last_adjribout_queue_block_time().value_or(0));

    /* sendQueue metrics */
    metricVectors[PeerMetric::TotalSendQueueBlocks].push_back(
        stats.send_queue_blocks().value_or(0));
    metricVectors[PeerMetric::TotalSendQueueWait].push_back(
        stats.send_queue_total_block_duration().value_or(0));
    metricVectors[PeerMetric::LastSendQueueBlock].push_back(
        stats.last_send_queue_block_time().value_or(0));

    /* async socket metrics */
    metricVectors[PeerMetric::TotalSocketBuffered].push_back(
        stats.total_async_socket_buffered().value_or(0));
    metricVectors[PeerMetric::LastSocketBuffered].push_back(
        stats.last_socket_buffered_time().value_or(0));
  }

  /* Sort data for percentile computation. */
  for (auto& [group, metricVectors] : groupedValues) {
    for (int m = 0; m < metricVectors.size(); ++m) {
      auto& vec = metricVectors[m];
      std::sort(vec.begin(), vec.end());
    }
  }

  std::vector<std::string> percentiles = {"p50", "p95", "p99"};
  std::vector<double> percentileValues = {0.50, 0.95, 0.99};

  for (const auto& [group, metricVectors] : groupedValues) {
    for (size_t p = 0; p < percentiles.size(); ++p) {
      std::vector<std::string> row;
      row.push_back(percentiles[p]);
      row.push_back(group);

      for (size_t i = 0; i < metricVectors.size(); ++i) {
        const auto& vec = metricVectors[i];
        if (vec.empty()) {
          row.emplace_back("0");
        } else {
          size_t index =
              static_cast<size_t>((vec.size() - 1) * percentileValues[p]);
          auto& val = vec[index];
          if (kTimeMetrics.contains(static_cast<PeerMetric>(i))) {
            row.push_back(
                folly::to<std::string>(
                    val > 0 ? utils::parseTimeToTimeStamp(val) : ""));
          } else {
            row.push_back(folly::to<std::string>(val));
          }
        }
      }

      groupSummaryView.addRow({
          row[0], // Percentile
          row[1], // Group Name
          row[2], // PR
          row[3], // PS
          row[4], // UR
          row[5], // US
          row[6], // SU
          row[7], // IQ Blocks
          row[8], // Total IQ Wait
          row[9], // Last IQ Block
          row[10], // SQ Blocks
          row[11], // Total SQ Wait
          row[12], // Last SQ Block
          row[13], // Total Socket Buffered
          row[14], // Last Socket Buffered
      });
    }
  }
  return groupSummaryView;
}

void CmdShowBgpSummaryEgress::printGroupSummary(
    const std::vector<TPeerEgressStats>& peerEgressStats,
    std::ostream& out) {
  out << "BGP Peer Egress Summary by Group Percentiles" << std::endl;
  out << "Acronyms: PR - Prefixes Received, PS - Prefixes Sent, "
      << "UR - Update Messages Received, US - Update Messages Sent, \n"
      << "SU - Suppressed Updates (updates not sent due to packing state compression), "
      << "IQ - Per-peer Input Queue to BGP I/O thread, "
      << "SQ - Per-peer Send Queue to socket \n"
      << std::endl;
  out << makeGroupTable(peerEgressStats) << std::endl;
}

CmdShowBgpSummaryEgress::RetType CmdShowBgpSummaryEgress::createModel(
    std::vector<TPeerEgressStats>& peerEgressStats) {
  std::sort(
      peerEgressStats.begin(),
      peerEgressStats.end(),
      [](const TPeerEgressStats& a, const TPeerEgressStats& b) {
        if (!a.session().has_value() || !b.session().has_value()) {
          return false;
        }
        return a.session()->peer_addr().value() <
            b.session()->peer_addr().value();
      });

  RetType model;
  model.peer_egress_stats() = peerEgressStats;
  return model;
}

std::string_view CmdShowBgpSummaryEgressTraits::description() {
  return "Displays where BGP update traffic is backing up on the way out to each peer. Two tables: first per-group percentiles (p50/p95/p99 across the peers in each AdjRibOut group), then one row per peer. Beyond the prefix and update-message counts the columns track two queues on the egress path - IQ, the per-peer input queue feeding the BGP I/O thread, and SQ, the per-peer send queue feeding the socket - each with a block count, the total time blocked, and when it last blocked, followed by how much the socket itself buffered. A peer that is slow to drain shows rising IQ/SQ blocks and socket buffering while its neighbours do not, which is what separates one sick session from switch-wide congestion; the percentile table is the quick way to see which of the two you have. SU counts updates that were suppressed rather than sent, because packing compressed away a transient change. Peers that belong to no group are listed under the group name NONE. A peer that is not established renders no uptime, but its counters are whatever the daemon still holds - they are not zeroed on session loss, so a down peer commonly still shows the totals from its last session. Peers in IDLE or IDLE_ADMIN are excluded from the percentile table but still appear in the per-peer table below it, so a group whose peers are all idle is missing from the percentiles entirely rather than showing zeros - check the per-peer table before concluding a group has no peers.";
}

namespace {

/*
 * One sample row. A struct with designated initializers rather than a long
 * positional argument list: the render has fifteen columns and a transposed
 * pair (SU vs socket-buffered, IQ wait vs SQ blocks) would silently change the
 * documented example with nothing to catch it.
 */
constexpr auto kSampleV4Group = "RSW-FSW-V4";
constexpr auto kSampleV6Group = "RSW-FSW-V6";

struct SamplePeerEgressSpec {
  std::string peerAddr;
  std::string description;
  // group_name is optional in the thrift struct; leaving it unset is what
  // exercises the value_or(kNoGroupName) fallback in the render.
  std::optional<std::string> groupName;
  bool established = true;
  int64_t prefixesRcvd = 0;
  int64_t prefixesSent = 0;
  int64_t updatesRcvd = 0;
  int64_t updatesSent = 0;
  int64_t suppressed = 0;
  int64_t iqBlocks = 0;
  int64_t iqWaitMs = 0;
  int64_t sqBlocks = 0;
  int64_t sqWaitMs = 0;
  int64_t socketBuffered = 0;
};

TPeerEgressStats sampleEgressStats(const SamplePeerEgressSpec& spec) {
  using facebook::neteng::fboss::bgp::thrift::TBgpPeer;

  // uptime is a duration in milliseconds, rendered as elapsed time.
  static constexpr int64_t kUptimeMs = 39654000;
  /*
   * Epoch ms. printOutput renders these with localtime, so the timestamp
   * columns of the generated example reflect the timezone of whatever machine
   * built the page.
   */
  static constexpr int64_t kLastBlockMs = 1788455780000;

  TBgpPeer bgpPeer;
  bgpPeer.peer_state() =
      spec.established ? TBgpPeerState::ESTABLISHED : TBgpPeerState::IDLE;

  TBgpSession session;
  session.peer() = bgpPeer;
  session.peer_addr() = spec.peerAddr;
  session.description() = spec.description;
  session.uptime() = spec.established ? kUptimeMs : 0;
  session.reset_time() = 0;
  session.num_resets() = 0;
  session.prepolicy_rcvd_prefix_count() = spec.prefixesRcvd;
  session.postpolicy_sent_prefix_count() = spec.prefixesSent;
  session.recv_update_msgs() = spec.updatesRcvd;
  session.sent_update_msgs() = spec.updatesSent;

  TPeerEgressStats egress;
  if (spec.groupName.has_value()) {
    egress.group_name() = *spec.groupName;
  }
  egress.session() = std::move(session);
  egress.transient_route_updates_suppressed() = spec.suppressed;
  egress.adjribout_queue_blocks() = spec.iqBlocks;
  egress.adjribout_queue_total_block_duration() = spec.iqWaitMs;
  egress.send_queue_blocks() = spec.sqBlocks;
  egress.send_queue_total_block_duration() = spec.sqWaitMs;
  egress.total_async_socket_buffered() = spec.socketBuffered;
  if (spec.iqBlocks > 0) {
    egress.last_adjribout_queue_block_time() = kLastBlockMs;
  }
  if (spec.sqBlocks > 0) {
    egress.last_send_queue_block_time() = kLastBlockMs;
  }
  if (spec.socketBuffered > 0) {
    egress.last_socket_buffered_time() = kLastBlockMs;
  }
  return egress;
}

} // namespace

CmdShowBgpSummaryEgress::RetType CmdShowBgpSummaryEgress::sampleModel() {
  /*
   * Five established peers in the v4 group, not two or four. makeGroupTable
   * indexes percentiles as (n - 1) * p truncated, so p95 and p99 select index
   * 3 only once the group has five members - with fewer, every percentile row
   * collapses onto the same peer and the table shows no spread at all.
   *
   * Rows are listed in peer-address order because createModel() sorts by
   * peer_addr before rendering, so this is the order the real command emits.
   */
  const std::vector<SamplePeerEgressSpec> specs = {
      {.peerAddr = "192.0.2.11",
       .description = "fsw001.p001.f01.abc1",
       .groupName = kSampleV4Group,
       .prefixesRcvd = 120,
       .prefixesSent = 8,
       .updatesRcvd = 160,
       .updatesSent = 8},
      {.peerAddr = "192.0.2.12",
       .description = "fsw002.p001.f01.abc1",
       .groupName = kSampleV4Group,
       .prefixesRcvd = 120,
       .prefixesSent = 12,
       .updatesRcvd = 160,
       .updatesSent = 12},
      {.peerAddr = "192.0.2.13",
       .description = "fsw003.p001.f01.abc1",
       .groupName = kSampleV4Group,
       .prefixesRcvd = 120,
       .prefixesSent = 19,
       .updatesRcvd = 160,
       .updatesSent = 19},
      /*
       * The blocked peers. Because the index truncates, p95/p99 select index 3
       * (this peer) and never index 4 - so the last peer's larger numbers show
       * only in the per-peer table, which is itself worth seeing: the
       * percentile row is not the worst peer.
       */
      {.peerAddr = "192.0.2.14",
       .description = "fsw004.p001.f01.abc1",
       .groupName = kSampleV4Group,
       .prefixesRcvd = 120,
       .prefixesSent = 31,
       .updatesRcvd = 160,
       .updatesSent = 31,
       .suppressed = 46,
       .iqBlocks = 5,
       .iqWaitMs = 140,
       .socketBuffered = 4096},
      {.peerAddr = "192.0.2.15",
       .description = "fsw005.p001.f01.abc1",
       .groupName = kSampleV4Group,
       .prefixesRcvd = 120,
       .prefixesSent = 43,
       .updatesRcvd = 160,
       .updatesSent = 43,
       .suppressed = 212,
       .iqBlocks = 17,
       .iqWaitMs = 4820,
       .sqBlocks = 9,
       .sqWaitMs = 2610,
       .socketBuffered = 65536},
      /*
       * A configured listen range nobody has connected on. Its group name is
       * left unset so the render falls back to kNoGroupName, and because it is
       * IDLE it is excluded from the percentile table - only the per-peer
       * table below lists it.
       */
      {.peerAddr = "198.51.100.0/24",
       .description = "",
       .groupName = std::nullopt,
       .established = false},
      {.peerAddr = "2001:db8:e11e:1062::4e",
       .description = "fsw001.p001.f01.abc1",
       .groupName = kSampleV6Group,
       .prefixesRcvd = 1352,
       .prefixesSent = 108,
       .updatesRcvd = 1428,
       .updatesSent = 72,
       .suppressed = 18,
       .iqBlocks = 9,
       .iqWaitMs = 310,
       .sqBlocks = 7,
       .sqWaitMs = 120}};

  RetType model;
  std::vector<TPeerEgressStats> stats;
  stats.reserve(specs.size());
  for (const auto& spec : specs) {
    stats.push_back(sampleEgressStats(spec));
  }
  model.peer_egress_stats() = std::move(stats);
  return model;
}

} // namespace facebook::fboss
