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

#include <netinet/in.h>
#include <sys/socket.h>
#include <ratio> // NOLINT(misc-include-cleaner)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception> // NOLINT(misc-include-cleaner)
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/show/bgp/summary/gen-cpp2/bgp_summary_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
using apache::thrift::util::enumNameSafe;
using facebook::fboss::utils::Table;
using facebook::neteng::fboss::bgp::thrift::TBgpLocalConfig;
using facebook::neteng::fboss::bgp::thrift::TBgpSession;
using neteng::fboss::bgp::thrift::TBgpDrainState;
using neteng::fboss::bgp::thrift::TBgpPeerState;
using std::chrono::duration_cast;
using std::chrono::system_clock;

// Local option name, and the command's key in the CmdLocalOptions registry
// (the subcommand path joined with underscores; see
// CmdSubcommands::addCommand).
inline constexpr auto kBgpSummarySortBy = "--sort-by";
inline constexpr auto kBgpSummaryCmdName = "show_bgp_summary";

struct CmdShowBgpSummaryTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowBgpSummaryModel;

  std::vector<utils::LocalOption> LocalOptions = {
      {kBgpSummarySortBy,
       "Comma-separated column keys to sort peers by, applied in the order "
       "given: peer, as, state, gr, pr, pa, ps, prd, ug, ugps, uptime, "
       "downtime, description, session-id, flaps. Append "
       "':desc' to a key to reverse it, e.g. --sort-by pr:desc,peer"}};

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays a one-line-per-peer overview of every BGP session, under a global header carrying the daemon uptime, router ID, local and confederation ASNs, switch drain state, UCMP and update-group settings, aggregate path counts, loc-RIB prefix count and RIB version. EOR sent and EORs received report completed directions out of established peers, or N/A against a bgpd too old to report EoR status. For per-peer timestamps and pending status use 'show bgp neighbors <peer>'. Dynamic listen ranges render as IDLE rows but are not peers: the Peers total excludes them and a Peer Groups Configured line counts them separately. The 'BGP is up for' and 'Loc-RIB Prefix Count' header lines are omitted entirely against a bgpd too old to report them. Rows show peer address, remote AS, session state, graceful-restart support, prefixes received/accepted/sent, uptime, description, peer BGP identifier and flap count. PRD appears only when prefixes were dropped, UG/UGPS only when update-group is enabled, and Downtime only for a peer that is IDLE or admin-down and has reset at least once. Listen-range entries with no active session render as IDLE with zero counters. Rows are ordered by peer address by default; '--sort-by' accepts comma-separated keys (peer, as, state, gr, pr, pa, ps, prd, ug, ugps, uptime, downtime, description, session-id, flaps), each optionally suffixed with ':desc'. Numeric columns sort numerically, and peers that tie on every requested key stay in peer-address order.";
  }
};

class CmdShowBgpSummary
    : public CmdHandler<CmdShowBgpSummary, CmdShowBgpSummaryTraits> {
 public:
  using RetType = CmdShowBgpSummaryTraits::RetType;

  // Columns the table can be sorted on, named after the header they render
  // under. Includes the conditional columns (PRD/UG/UGPS/Downtime): sorting on
  // one that is currently hidden is harmless.
  enum class SortColumn {
    PEER,
    AS,
    STATE,
    GR,
    PR,
    PA,
    PS,
    PRD,
    UG,
    UGPS,
    UPTIME,
    DOWNTIME,
    DESCRIPTION,
    SESSION_ID,
    FLAPS,
  };

  struct SortKey {
    SortColumn column;
    bool descending{false};
  };

  /*
   * Parse the --sort-by value: a comma-separated list of column keys in
   * priority order, each optionally suffixed with ':asc' or ':desc'. Direction
   * is a suffix rather than a '-' prefix because CLI11 would parse a leading
   * dash as the start of another option. Throws std::invalid_argument on an
   * unknown key or direction, which CmdHandler surfaces as "Invalid argument".
   */
  static std::vector<SortKey> parseSortKeys(const std::string& spec) {
    static const std::map<std::string, SortColumn> kColumnsByKey = {
        {"as", SortColumn::AS},
        {"description", SortColumn::DESCRIPTION},
        {"downtime", SortColumn::DOWNTIME},
        {"flaps", SortColumn::FLAPS},
        {"gr", SortColumn::GR},
        {"pa", SortColumn::PA},
        {"peer", SortColumn::PEER},
        {"pr", SortColumn::PR},
        {"prd", SortColumn::PRD},
        {"ps", SortColumn::PS},
        {"session-id", SortColumn::SESSION_ID},
        {"state", SortColumn::STATE},
        {"ug", SortColumn::UG},
        {"ugps", SortColumn::UGPS},
        {"uptime", SortColumn::UPTIME},
    };

    std::vector<SortKey> keys;
    std::vector<std::string> tokens;
    folly::split(',', spec, tokens);
    for (const auto& token : tokens) {
      std::string key = folly::trimWhitespace(token).str();
      if (key.empty()) {
        continue;
      }
      bool descending = false;
      if (const auto colon = key.find(':'); colon != std::string::npos) {
        const std::string direction = key.substr(colon + 1);
        if (direction == "desc") {
          descending = true;
        } else if (direction != "asc") {
          throw std::invalid_argument(
              fmt::format(
                  "{}: unknown sort direction '{}', expected 'asc' or 'desc'",
                  kBgpSummarySortBy,
                  direction));
        }
        key = key.substr(0, colon);
      }
      std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      const auto column = kColumnsByKey.find(key);
      if (column == kColumnsByKey.end()) {
        std::vector<std::string> validKeys;
        validKeys.reserve(kColumnsByKey.size());
        for (const auto& [name, _] : kColumnsByKey) {
          validKeys.emplace_back(name);
        }
        throw std::invalid_argument(
            fmt::format(
                "{}: unknown column '{}', expected one of: {}",
                kBgpSummarySortBy,
                key,
                folly::join(", ", validKeys)));
      }
      keys.push_back({column->second, descending});
    }
    return keys;
  }

  RetType queryClient(const HostInfo& hostInfo) {
    // Parsed before any thrift call so a bad --sort-by fails immediately
    // rather than after querying the daemon.
    const auto sortKeys = parseSortKeys(
        CmdLocalOptions::getInstance()->getLocalOption(
            kBgpSummaryCmdName, kBgpSummarySortBy));

    std::vector<TBgpSession> sessions;
    TBgpLocalConfig config;
    TBgpDrainState drain_state;

    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

    client->sync_getBgpSessions(sessions);
    client->sync_getBgpLocalConfig(config);
    client->sync_getDrainState(drain_state);

    const int64_t ribVersion = client->sync_getRibVersion();

    /*
     * getProcessUptimeSeconds() and getNumPrefixes() are newer thrift methods
     * that may not exist on the bgpd binary deployed to prod. Against an older
     * daemon the calls throw (TApplicationException, UNKNOWN_METHOD), so guard
     * them and leave the values unset when unavailable -- printOutput omits the
     * corresponding lines. This self-heals once the new binary is deployed; no
     * code change is needed to re-enable them. getRibVersion() already exists
     * on prod, so it stays outside the guard.
     */
    std::optional<int64_t> processUptimeSeconds;
    std::optional<int64_t> totalPrefixCount;
    try {
      processUptimeSeconds = client->sync_getProcessUptimeSeconds();
      totalPrefixCount = client->sync_getNumPrefixes();
    } catch (const std::exception& ex) {
      XLOG(DBG2) << "process uptime / prefix count unavailable on this bgpd: "
                 << ex.what();
    }

    return createModel(
        sessions,
        config,
        drain_state,
        processUptimeSeconds,
        totalPrefixCount,
        ribVersion,
        sortKeys);
  }

  // Format process uptime, omitting leading zero units so a short uptime reads
  // naturally: 45 -> "45s", 330 -> "5m 30s", 3661 -> "1h 1m 1s",
  // 90061 -> "1d 1h 1m 1s". Units below the largest non-zero one are kept
  // (e.g. "3h 0m 5s") so the value stays unambiguous.
  static std::string formatUptime(int64_t totalSeconds) {
    if (totalSeconds < 0) {
      totalSeconds = 0;
    }
    const int64_t days = totalSeconds / 86400;
    const int64_t hours = (totalSeconds % 86400) / 3600;
    const int64_t minutes = (totalSeconds % 3600) / 60;
    const int64_t seconds = totalSeconds % 60;
    if (days > 0) {
      return fmt::format("{}d {}h {}m {}s", days, hours, minutes, seconds);
    }
    if (hours > 0) {
      return fmt::format("{}h {}m {}s", hours, minutes, seconds);
    }
    if (minutes > 0) {
      return fmt::format("{}m {}s", minutes, seconds);
    }
    return fmt::format("{}s", seconds);
  }

  void printOutput(const RetType& bgpSummary, std::ostream& out = std::cout) {
    const auto& config = bgpSummary.config().value();
    const auto& sessions = bgpSummary.sessions().value();
    const auto& drain_state = bgpSummary.drain_state().value();

    // prevent printing number with comma, e.g: 65,000
    out.imbue(std::locale("C"));

    out << "BGP summary information for VRF default" << std::endl;

    /*
     * Process uptime one-liner, e.g. "BGP is up for 1d 1h 1m 1s". Omitted when
     * unset (older bgpd without getProcessUptimeSeconds(); see queryClient()).
     */
    if (bgpSummary.process_uptime_seconds().has_value()) {
      out << "BGP is up for "
          << formatUptime(
                 folly::copy(bgpSummary.process_uptime_seconds().value()))
          << std::endl;
    }

    // Retrive the router's Ip Address from its ID.
    char ip_buff[INET_ADDRSTRLEN];
    auto id = folly::copy(config.my_router_id().value());
    inet_ntop(
        AF_INET, &((struct in_addr*)&id)->s_addr, ip_buff, INET_ADDRSTRLEN);

    // TODO(michaeluw): T113736668 deprecate i32 asns field
    const uint32_t local_as = folly::copy(config.local_as_4_byte().value())
        ? static_cast<uint32_t>(folly::copy(config.local_as_4_byte().value()))
        : folly::copy(config.local_as().value());

    out << "Router ID - " << ip_buff << ", Local ASN - " << local_as;

    // TODO(michaeluw): T113736668 deprecate i32 asns field
    if (const auto& confed_asn =
            folly::copy(config.local_confed_as_4_byte().value())) {
      out << ", Confed ASN - " << confed_asn << std::endl;
    } else if (
        const auto& confed_asn_old =
            apache::thrift::get_pointer(config.local_confed_as())) {
      out << ", Confed ASN - " << *confed_asn_old << std::endl;
    } else {
      out << std::endl;
    }
    // Add BGP drain status
    if (drain_state.drain_state().has_value()) {
      out << fmt::format(
                 "BGP Switch Drain State: {}",
                 enumNameSafe(*drain_state.drain_state()))
          << std::endl;
    } else {
      out << "BGP Switch Drain State: N/A" << std::endl;
    }

    // Add UCMP config header
    out << "UCMP weight programming is ";
    if (config.program_ucmp_weights().value()) {
      out << "ENABLED, with normalization width of "
          << folly::copy(config.ucmp_width().value()) << std::endl;
    } else {
      out << "DISABLED" << std::endl;
    }

    // Show if update-group is enabled
    out << "Update group is ";
    if (config.enable_update_group().value()) {
      out << "ENABLED" << std::endl;
    } else {
      out << "DISABLED" << std::endl;
    }

    // Add Summary
    const uint64_t total_neighbors = sessions.size();
    uint64_t up_neighbors = 0, paths_rcvd = 0, paths_accepted = 0,
             paths_sent = 0;
    // Dynamic listen ranges ("peer groups", e.g. 10.127.240.0/23) render as
    // IDLE rows but are not peers: they never establish and never send EoR.
    // The server renders their address as a prefix, which is how they are
    // told apart here.
    uint64_t peer_group_count = 0;

    // First pass: collect row data and detect whether any peer has downtime, so
    // the Downtime column can be omitted entirely when no peer is down.
    struct PeerRowData {
      std::string peerAddr;
      std::string remoteAs;
      std::string state;
      std::string gr;
      std::string pr;
      std::string pa;
      std::string ps;
      std::string prd;
      std::string ug;
      std::string ugps;
      std::string uptime;
      std::string downtime;
      std::string description;
      std::string sessionId;
      std::string flaps;
    };
    std::vector<PeerRowData> rows;
    bool hasAnyDowntime = false;
    bool hasAnyPreDrops = false;
    // Optional status is present even when no EoR has completed. Older
    // daemons do not populate it at all.
    bool hasEoRReceivedStatus = false;
    bool hasEoRSentStatus = false;
    size_t eorReceivedCount = 0;
    size_t eorSentCount = 0;

    for (const auto& bgpSession : sessions) {
      const auto& peer = bgpSession.peer().value();
      up_neighbors +=
          (folly::copy(peer.peer_state().value()) ==
           TBgpPeerState::ESTABLISHED);
      if (bgpSession.peer_addr().value().find('/') != std::string::npos) {
        ++peer_group_count;
      }

      auto rcvd_prefix = *bgpSession.prepolicy_rcvd_prefix_count();
      auto accepted_prefix = *bgpSession.postpolicy_rcvd_prefix_count();
      auto sent_prefix = *bgpSession.postpolicy_sent_prefix_count();

      /*
       * Per-peer routes dropped because the peer reached its configured
       * pre-filter prefix limit (server reports this count). Surfaced as the
       * PRD column, shown only when some peer actually dropped routes. A
       * non-zero count is reliable even when the live prefix count has churned
       * back below the limit, so it avoids digging through logs.
       */
      auto pre_dropped =
          bgpSession.prepolicy_rcvd_dropped_prefix_count().value_or(0);
      if (pre_dropped > 0) {
        hasAnyPreDrops = true;
      }

      paths_rcvd += rcvd_prefix;
      paths_accepted += accepted_prefix;
      paths_sent += sent_prefix;

      const uint32_t peer_remote_as =
          folly::copy(peer.remote_as_4_byte().value())
          ? folly::copy(peer.remote_as_4_byte().value())
          : folly::copy(peer.remote_as().value());

      std::string uptimeDurationString;
      if (*peer.peer_state() == TBgpPeerState::ESTABLISHED) {
        uptimeDurationString = utils::getPrettyElapsedTime(
            utils::getEpochFromDuration(*bgpSession.uptime()));
      }

      std::string downtimeDurationString;
      if (((*peer.peer_state() == TBgpPeerState::IDLE ||
            *peer.peer_state() == TBgpPeerState::IDLE_ADMIN)) &&
          (*bgpSession.num_resets() > 0)) {
        // If the peer is IDLE but num_resets() == 0, then the peer has never
        // been in ESTABLISHED state and hence does not have downtime yet.
        downtimeDurationString = utils::getPrettyElapsedTime(
            utils::getEpochFromDuration(*bgpSession.reset_time()));
      }
      if (!downtimeDurationString.empty()) {
        hasAnyDowntime = true;
      }

      if (bgpSession.eor_received().has_value()) {
        hasEoRReceivedStatus = true;
        if (*peer.peer_state() == TBgpPeerState::ESTABLISHED &&
            *bgpSession.eor_received()) {
          ++eorReceivedCount;
        }
      }
      if (bgpSession.eor_sent().has_value()) {
        hasEoRSentStatus = true;
        if (*peer.peer_state() == TBgpPeerState::ESTABLISHED &&
            *bgpSession.eor_sent()) {
          ++eorSentCount;
        }
      }

      // Update-group ID: show value or "-" if not set.
      std::string ugStr = "-";
      if (auto ugId =
              apache::thrift::get_pointer(bgpSession.update_group_id())) {
        ugStr = folly::to<std::string>(*ugId);
      }

      // Peer update-group state: show value or "-" if not set.
      std::string peerStateStr = "-";
      if (auto ps = apache::thrift::get_pointer(
              bgpSession.peer_state_update_group())) {
        peerStateStr = *ps;
      }

      rows.push_back(
          {bgpSession.peer_addr().value(),
           folly::to<std::string>(peer_remote_as),
           enumNameSafe(folly::copy(peer.peer_state().value())).substr(0, 4),
           folly::copy(peer.graceful().value()) ? "Y" : "N",
           folly::to<std::string>(rcvd_prefix),
           folly::to<std::string>(accepted_prefix),
           folly::to<std::string>(sent_prefix),
           folly::to<std::string>(pre_dropped),
           ugStr,
           peerStateStr,
           uptimeDurationString,
           downtimeDurationString,
           bgpSession.description().value(),
           bgpSession.peer_bgp_id().value(),
           folly::to<std::string>(
               folly::copy(bgpSession.num_resets().value()))});
    }

    // Build the table with conditional columns:
    //  - UG/UGPS: only when update-group is enabled
    //  - Downtime: only when at least one peer has downtime
    const bool enableUpdateGroup = config.enable_update_group().value();

    auto buildRow = [&](const PeerRowData& row) {
      std::vector<Table::RowData> fields = {
          row.peerAddr,
          row.remoteAs,
          row.state,
          row.gr,
          row.pr,
          row.pa,
          row.ps};
      // PRD: per-peer dropped count, shown only when some peer actually dropped
      // routes.
      if (hasAnyPreDrops) {
        fields.emplace_back(row.prd);
      }
      if (enableUpdateGroup) {
        fields.emplace_back(row.ug);
        fields.emplace_back(row.ugps);
      }
      fields.emplace_back(row.uptime);
      if (hasAnyDowntime) {
        fields.emplace_back(row.downtime);
      }
      fields.emplace_back(row.description);
      fields.emplace_back(row.sessionId);
      fields.emplace_back(row.flaps);
      return fields;
    };

    std::vector<std::string> header = {
        "Peer", "AS", "State", "GR", "PR", "PA", "PS"};
    if (hasAnyPreDrops) {
      header.emplace_back("PRD");
    }
    if (enableUpdateGroup) {
      header.emplace_back("UG");
      header.emplace_back("UGPS");
    }
    header.emplace_back("Uptime");
    if (hasAnyDowntime) {
      header.emplace_back("Downtime");
    }
    header.emplace_back("Description");
    header.emplace_back("Session ID");
    header.emplace_back("Flaps");

    Table table;
    table.setHeader({header.begin(), header.end()});
    for (const auto& row : rows) {
      auto fields = buildRow(row);
      table.addRow({fields.begin(), fields.end()});
    }

    // Peer prefix groups (dynamic listen ranges) are listed but are not peers.
    const uint64_t peer_total = total_neighbors - peer_group_count;
    out << "Peers: UP - " << up_neighbors << ", TOTAL - " << peer_total
        << std::endl;
    out << "Peer Groups Configured - " << peer_group_count << std::endl;
    // Count only established peers; pending EoRs are false, not absent.
    // Absence signals an old daemon without EoR status support.
    out << "EOR sent (UP peers) - ";
    if (hasEoRSentStatus) {
      out << eorSentCount << "/" << up_neighbors;
    } else {
      out << "N/A";
    }
    out << std::endl;
    out << "EORs received (UP peers) - ";
    if (hasEoRReceivedStatus) {
      out << eorReceivedCount << "/" << up_neighbors;
    } else {
      out << "N/A";
    }
    out << std::endl;
    out << "Paths: Received - " << paths_rcvd << ", Accepted - "
        << paths_accepted << ", Sent - " << paths_sent << std::endl;
    /*
     * Loc-RIB prefix count is omitted when unset (older bgpd without
     * getNumPrefixes(); see queryClient()). RIB Version comes from
     * getRibVersion(), which is already deployed to prod, so it is always
     * shown.
     */
    if (bgpSummary.total_prefix_count().has_value()) {
      out << "Loc-RIB Prefix Count: "
          << folly::copy(bgpSummary.total_prefix_count().value()) << std::endl;
    }
    out << "RIB Version: " << folly::copy(bgpSummary.rib_version().value())
        << std::endl;
    std::string acronyms =
        "Acronyms: PR - Prefixes Received, PA - Prefixes Accepted, PS - Prefixes Sent";
    if (hasAnyPreDrops) {
      acronyms += ", PRD - Prefixes Received Dropped";
    }
    if (enableUpdateGroup) {
      acronyms += ", UG - Update Group, UGPS - Update Group Peer State";
    }
    out << acronyms << "\n" << std::endl;
    out << table << std::endl;
  }

  // Three-way compare of two sessions on a single column. Numeric columns are
  // compared as numbers rather than as their rendered strings, so e.g. sorting
  // on PR does not order 100 before 9.
  static int compareOnColumn(
      const TBgpSession& a,
      const TBgpSession& b,
      SortColumn column) {
    auto cmp = [](const auto& lhs, const auto& rhs) {
      if (lhs < rhs) {
        return -1;
      }
      return rhs < lhs ? 1 : 0;
    };
    // TODO(michaeluw): T113736668 deprecate i32 asns field
    auto remoteAs = [](const TBgpSession& session) -> uint32_t {
      const auto& peer = session.peer().value();
      return folly::copy(peer.remote_as_4_byte().value())
          ? folly::copy(peer.remote_as_4_byte().value())
          : folly::copy(peer.remote_as().value());
    };
    /*
     * uptime and reset_time are durations, and are only rendered for the peer
     * states printOutput renders them for. Sessions with a blank cell compare
     * as -1 so they group together at one end rather than interleaving with
     * real durations.
     */
    auto uptime = [](const TBgpSession& session) -> int64_t {
      const auto& peer = session.peer().value();
      return folly::copy(peer.peer_state().value()) ==
              TBgpPeerState::ESTABLISHED
          ? folly::copy(session.uptime().value())
          : -1;
    };
    auto downtime = [](const TBgpSession& session) -> int64_t {
      const auto& peer = session.peer().value();
      const auto state = folly::copy(peer.peer_state().value());
      const bool down = (state == TBgpPeerState::IDLE ||
                         state == TBgpPeerState::IDLE_ADMIN) &&
          folly::copy(session.num_resets().value()) > 0;
      return down ? folly::copy(session.reset_time().value()) : -1;
    };

    switch (column) {
      case SortColumn::PEER:
        return cmp(a.peer_addr().value(), b.peer_addr().value());
      case SortColumn::AS:
        return cmp(remoteAs(a), remoteAs(b));
      case SortColumn::STATE:
        // Compared on the rendered abbreviation so the column reads as sorted.
        return cmp(
            enumNameSafe(folly::copy(a.peer().value().peer_state().value()))
                .substr(0, 4),
            enumNameSafe(folly::copy(b.peer().value().peer_state().value()))
                .substr(0, 4));
      case SortColumn::GR:
        return cmp(
            folly::copy(a.peer().value().graceful().value()),
            folly::copy(b.peer().value().graceful().value()));
      case SortColumn::PR:
        return cmp(
            folly::copy(a.prepolicy_rcvd_prefix_count().value()),
            folly::copy(b.prepolicy_rcvd_prefix_count().value()));
      case SortColumn::PA:
        return cmp(
            folly::copy(a.postpolicy_rcvd_prefix_count().value()),
            folly::copy(b.postpolicy_rcvd_prefix_count().value()));
      case SortColumn::PS:
        return cmp(
            folly::copy(a.postpolicy_sent_prefix_count().value()),
            folly::copy(b.postpolicy_sent_prefix_count().value()));
      case SortColumn::PRD:
        return cmp(
            a.prepolicy_rcvd_dropped_prefix_count().value_or(0),
            b.prepolicy_rcvd_dropped_prefix_count().value_or(0));
      case SortColumn::UG:
        // Unset renders as "-"; -1 keeps those rows together.
        return cmp(
            a.update_group_id().value_or(-1), b.update_group_id().value_or(-1));
      case SortColumn::UGPS:
        return cmp(
            a.peer_state_update_group().value_or("-"),
            b.peer_state_update_group().value_or("-"));
      case SortColumn::UPTIME:
        return cmp(uptime(a), uptime(b));
      case SortColumn::DOWNTIME:
        return cmp(downtime(a), downtime(b));
      case SortColumn::DESCRIPTION:
        return cmp(a.description().value(), b.description().value());
      case SortColumn::SESSION_ID:
        return cmp(a.peer_bgp_id().value(), b.peer_bgp_id().value());
      case SortColumn::FLAPS:
        return cmp(
            folly::copy(a.num_resets().value()),
            folly::copy(b.num_resets().value()));
    }
    return 0;
  }

  // Move the data from bgp_thrift:TBgpSession to
  // bgp_summary_thrift:TBgpSummary
  RetType createModel(
      std::vector<TBgpSession>& sessions,
      TBgpLocalConfig& config,
      TBgpDrainState& drain_state,
      std::optional<int64_t> processUptimeSeconds,
      std::optional<int64_t> totalPrefixCount,
      int64_t ribVersion,
      const std::vector<SortKey>& sortKeys = {}) {
    std::sort(
        sessions.begin(),
        sessions.end(),
        [](const TBgpSession& a, const TBgpSession& b) {
          return a.peer_addr().value() < b.peer_addr().value();
        });

    /*
     * Layered on top of the peer-address sort above rather than replacing it,
     * so the stable sort leaves peers that tie on every requested column in
     * peer-address order instead of in whatever order the daemon returned.
     */
    if (!sortKeys.empty()) {
      std::stable_sort(
          sessions.begin(),
          sessions.end(),
          [&sortKeys](const TBgpSession& a, const TBgpSession& b) {
            for (const auto& key : sortKeys) {
              const int order = compareOnColumn(a, b, key.column);
              if (order != 0) {
                return key.descending ? order > 0 : order < 0;
              }
            }
            return false;
          });
    }

    RetType model;
    model.sessions() = sessions;
    model.config() = config;
    model.drain_state() = drain_state;
    // Left unset when the daemon does not implement the getter, so printOutput
    // omits the corresponding line (see queryClient()).
    if (processUptimeSeconds.has_value()) {
      model.process_uptime_seconds() = *processUptimeSeconds;
    }
    if (totalPrefixCount.has_value()) {
      model.total_prefix_count() = *totalPrefixCount;
    }
    model.rib_version() = ribVersion;
    return model;
  }

  // Canned, synthetic model (no real switch data) used to render an example
  // for the CLI reference wiki. Shaped after an RSW with two upstream FSWs
  // peered over both address families plus one idle listen-range, trimmed down
  // from a real fleet capture. Everything renders identically run to run except
  // the Uptime column, which is derived from the wall clock (see below).
  static RetType sampleModel() {
    // printOutput hands my_router_id's raw in-memory bytes to inet_ntop, so the
    // value has to be in network byte order. Derive it rather than hardcoding
    // the byte-swapped literal, which would render differently on a big-endian
    // host.
    TBgpLocalConfig config;
    config.my_router_id() =
        static_cast<int32_t>(folly::IPAddressV4("192.0.2.1").toLong());
    // Sample ASNs (RFC 6996 private-use range): this sample renders onto the
    // CLI reference wiki and ships in the open-source tree.
    config.local_as_4_byte() = 65108;
    config.local_confed_as_4_byte() = 64873;
    config.program_ucmp_weights() = false;
    config.enable_update_group() = false;

    TBgpDrainState drainState;
    drainState.drain_state() = facebook::bgp::bgp_policy::DrainState::UNDRAINED;

    auto establishedSession = [](const std::string& peerAddr,
                                 uint32_t remoteAs,
                                 int64_t rcvd,
                                 int64_t sent,
                                 const std::string& description,
                                 const std::string& sessionId) {
      /*
       * uptime is a duration in milliseconds. printOutput converts it to an
       * epoch and then back to an elapsed time, reading the wall clock once
       * for each, so 39654000ms renders as "11h 0m 54s" give or take a second
       * between runs rather than as a fixed string.
       */
      static constexpr int64_t kSessionUptimeMs = 39654000;
      TBgpSession session;
      session.peer()->peer_state() = TBgpPeerState::ESTABLISHED;
      session.peer()->remote_as_4_byte() = remoteAs;
      session.peer()->graceful() = true;
      session.peer_addr() = peerAddr;
      session.prepolicy_rcvd_prefix_count() = rcvd;
      session.postpolicy_rcvd_prefix_count() = rcvd;
      session.postpolicy_sent_prefix_count() = sent;
      session.uptime() = kSessionUptimeMs;
      session.eor_received() = true;
      session.eor_sent() = true;
      session.reset_time() = 0;
      session.num_resets() = 0;
      session.description() = description;
      session.peer_bgp_id() = sessionId;
      return session;
    };

    // A configured listen range that no peer has connected on yet: IDLE with
    // no resets, so printOutput leaves its uptime and downtime blank. Kept
    // disjoint from the established peers above, so the example does not read
    // as a range that already has sessions inside it.
    TBgpSession listenRange;
    listenRange.peer()->peer_state() = TBgpPeerState::IDLE;
    listenRange.peer()->remote_as_4_byte() = 65390;
    listenRange.peer()->graceful() = false;
    listenRange.peer_addr() = "198.51.100.0/24";
    listenRange.prepolicy_rcvd_prefix_count() = 0;
    listenRange.postpolicy_rcvd_prefix_count() = 0;
    listenRange.postpolicy_sent_prefix_count() = 0;
    listenRange.uptime() = 0;
    listenRange.reset_time() = 0;
    listenRange.num_resets() = 0;
    listenRange.description() = "";
    listenRange.peer_bgp_id() = "0.0.0.0";
    listenRange.eor_received() = false;
    listenRange.eor_sent() = false;

    RetType model;
    // Ordered the way createModel() sorts live sessions: by peer address
    // string.
    model.sessions() = {
        establishedSession(
            "192.0.2.11",
            65221,
            121,
            43,
            "fsw001.p001.f01.abc1",
            "192.0.2.101"),
        establishedSession(
            "192.0.2.12",
            64650,
            121,
            43,
            "fsw002.p001.f01.abc1",
            "192.0.2.102"),
        listenRange,
        establishedSession(
            "2001:db8:e11e:1062::4e",
            65221,
            1343,
            108,
            "fsw001.p001.f01.abc1",
            "192.0.2.101"),
        establishedSession(
            "2001:db8:e11e:1162::4e",
            64650,
            1343,
            108,
            "fsw002.p001.f01.abc1",
            "192.0.2.102")};
    model.config() = config;
    model.drain_state() = drainState;
    model.process_uptime_seconds() = 39655;
    model.total_prefix_count() = 1464;
    model.rib_version() = 2013;
    return model;
  }
};

} // namespace facebook::fboss
