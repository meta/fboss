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

#include <fmt/core.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using facebook::neteng::fboss::bgp::thrift::TAttributeStats;
using facebook::neteng::fboss::bgp::thrift::TAttributeStatsPayloadKind;
using facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsRequest;
using facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsResponse;

inline TAttributeStats makeBgpStatsAttrsCliModel(
    const TGetDeduplicatorStatsResponse& stats) {
  TAttributeStats model;
  model.dedup_bgp_path() = stats.bgp_path()->entry_count().value();
  model.dedup_bgp_attributes() = stats.bgp_attributes()->entry_count().value();
  model.dedup_as_path() = stats.as_path()->entry_count().value();
  model.dedup_communities() = stats.communities()->entry_count().value();
  model.dedup_cluster_list() = stats.cluster_list()->entry_count().value();
  model.dedup_ext_communities() =
      stats.ext_communities()->entry_count().value();
  model.payload_kind() = TAttributeStatsPayloadKind::DEDUPLICATOR_STATS;
  return model;
}

template <typename Client>
TAttributeStats queryBgpStatsAttrsWithFallback(Client& client) {
  return runMethodWithLegacyFallback(
      [&]() -> TAttributeStats {
        TGetDeduplicatorStatsResponse stats;
        TGetDeduplicatorStatsRequest request;
        client.sync_getDeduplicatorStats(stats, request);
        return makeBgpStatsAttrsCliModel(stats);
      },
      [&]() -> TAttributeStats {
        TAttributeStats stats;
        client.sync_getAttributeStats(stats);
        stats.payload_kind() =
            TAttributeStatsPayloadKind::LEGACY_ATTRIBUTE_STATS;
        return stats;
      });
}

struct CmdShowBgpStatsAttrsTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = TAttributeStats;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays how many DISTINCT path-attribute values the daemon currently stores, one row per deduplicator, laid out by nesting level rather than as a flat list. L1 is bgp_path (a BgpPathC: a pointer to an attribute bundle, plus next hop and topology info). L2 is bgp_attributes, the bundle each L1 path points at, which also carries the scalar attributes. L3 is the four sub-attributes the bundle in turn holds as deduplicated pointers: as_path, communities, cluster_list and ext_communities. The levels are a containment hierarchy, not a partition - L2 is not the sum of the L3 rows, and each count is simply the live size of that deduplicator. Rising L2 or L3 counts against a flat prefix count mean attribute churn (peers re-advertising the same prefixes with varying attributes), which costs memory. Against a bgpd that predates the deduplicator, the command falls back to the older attribute statistics and prints averages instead of this table.";
  }
};

class CmdShowBgpStatsAttrs
    : public CmdHandler<CmdShowBgpStatsAttrs, CmdShowBgpStatsAttrsTraits> {
 public:
  using RetType = CmdShowBgpStatsAttrsTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo) {
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
    return queryBgpStatsAttrsWithFallback(*client);
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Deduplicator counts from
  // a live RSW capture, so the level relationships are realistic.
  static RetType sampleModel() {
    RetType stats;
    stats.payload_kind() = TAttributeStatsPayloadKind::DEDUPLICATOR_STATS;
    stats.dedup_bgp_path() = 12182;
    stats.dedup_bgp_attributes() = 10297;
    stats.dedup_as_path() = 1828;
    stats.dedup_communities() = 58;
    stats.dedup_cluster_list() = 1;
    stats.dedup_ext_communities() = 0;
    return stats;
  }

  void printOutput(const RetType& stats, std::ostream& out = std::cout) {
    switch (stats.payload_kind().value()) {
      case TAttributeStatsPayloadKind::LEGACY_ATTRIBUTE_STATS:
        printLegacyAttributeStats(stats, out);
        return;
      case TAttributeStatsPayloadKind::DEDUPLICATOR_STATS:
        printDeduplicatorSizes(stats, out);
        return;
      case TAttributeStatsPayloadKind::UNKNOWN:
        break;
    }
    throw std::runtime_error(
        "BGP attribute statistics payload kind is unknown or unsupported");
  }

 private:
  static void printLegacyAttributeStats(
      const TAttributeStats& stats,
      std::ostream& out) {
    out << "BGP attribute statistics:" << std::endl;
    out << " Total number of attributes: "
        << folly::copy(stats.total_num_of_attributes().value()) << std::endl;
    out << " Total number of unique attributes: "
        << folly::copy(stats.total_unique_attributes().value()) << std::endl;
    out << fmt::format(
        " Average attribute reference count: {:.2f}\n",
        folly::copy(stats.avg_attribute_refcount().value()));
    out << fmt::format(
        " Average community list length: {:.2f}\n",
        folly::copy(stats.avg_community_list_len().value()));
    out << fmt::format(
        " Average extended community list length: {:.2f}\n",
        folly::copy(stats.avg_extcommunity_list_len().value()));
    out << fmt::format(
        " Average as path length: {:.2f}\n",
        folly::copy(stats.avg_as_path_len().value()));
    out << fmt::format(
        " Average cluster list length: {:.2f}\n",
        folly::copy(stats.avg_cluster_list_len().value()));
    out << fmt::format(
        " Average topology info length: {:.2f}\n",
        folly::copy(stats.avg_topology_info_len().value()));
  }

  /*
   * Live size of each DeDuplicator<T> -- how many DISTINCT values of that type
   * the daemon currently stores.
   *
   * Laid out by NESTING LEVEL, because the flat list invites the reading that
   * L2 is the sum of L3, which it is not:
   *
   *   L1  bgp_path        BgpPathC       = attrs ptr + nexthop + topologyInfo
   *   L2    bgp_attributes  BgpAttributesC = the bundle L1 points at
   *   L3      as_path / communities / cluster_list / ext_communities,
   *           held BY the bundle as deduplicated pointers
   *
   */
  static void printDeduplicatorSizes(const RetType& stats, std::ostream& out) {
    if (!stats.dedup_bgp_path().has_value() ||
        !stats.dedup_bgp_attributes().has_value() ||
        !stats.dedup_as_path().has_value() ||
        !stats.dedup_communities().has_value() ||
        !stats.dedup_cluster_list().has_value() ||
        !stats.dedup_ext_communities().has_value()) {
      throw std::runtime_error(
          "BGP deduplicator statistics payload is incomplete");
    }

    out << "BGP attribute deduplicator statistics "
           "(distinct values stored at each nesting level):"
        << std::endl;

    utils::Table table;
    table.setHeader({"Level", "Collection", "Contents", "Entries"});
    const auto row = [&table](
                         const std::string& level,
                         const std::string& name,
                         const std::string& contents,
                         int64_t entryCount) {
      table.addRow({level, name, contents, folly::to<std::string>(entryCount)});
    };

    /*
     * L1. Ingress pre-policy and post-policy paths reach this deduplicator.
     * AdjRibEntry::setPreOut stores verbatim, but it is handed the RIB
     * best-entry path, which was already interned as a postAttr.
     */
    row("L1",
        "bgp_path",
        "BgpPathC = bgp_attributes + nexthop + topologyInfo",
        stats.dedup_bgp_path().value());

    /*
     * L2. Published to fb303 as `bgpcpp.deduplicated_attributes.total`; that
     * counter name is kept for continuity but is a misnomer, so the row is
     * named after the deduplicator instead.
     *
     * The Contents cell notes that the bundle also holds scalar attributes.
     */
    row("L2",
        "bgp_attributes",
        "BgpAttributesC = the 4 L3 sub-attributes + scalars",
        stats.dedup_bgp_attributes().value());

    static constexpr auto kL3Contents = "sub-attribute held by bgp_attributes";
    row("L3", "as_path", kL3Contents, stats.dedup_as_path().value());
    row("L3", "communities", kL3Contents, stats.dedup_communities().value());
    row("L3", "cluster_list", kL3Contents, stats.dedup_cluster_list().value());
    row("L3",
        "ext_communities",
        kL3Contents,
        stats.dedup_ext_communities().value());

    out << table << std::endl;
  }
};
} // namespace facebook::fboss
