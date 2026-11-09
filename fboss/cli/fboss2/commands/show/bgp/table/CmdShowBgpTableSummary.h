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
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/gen-cpp2/bgp_table_summary_types.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/Table.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#include "thrift/lib/cpp/util/EnumUtils.h"

namespace facebook::fboss {
using apache::thrift::util::enumNameSafe;
using facebook::fboss::utils::Table;
using neteng::fboss::bgp::thrift::TRibSummary;
using neteng::fboss::bgp_attr::TBgpAfi;

struct CmdShowBgpTableSummaryTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowBgpTableSummaryModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays per-address-family totals for the BGP loc-RIB, one block for IPv4 and one for IPv6: how many prefixes and paths the table holds and how those paths split into active and inactive, how the prefixes break down by where they were learned (eBGP, iBGP, confederation eBGP, locally originated), how many carry a next hop the daemon could not resolve, and a histogram of prefix counts by mask length. A single RIB-wide unresolvable-next-hop count is printed once at the end rather than per family. The active/inactive split is omitted entirely against an older bgpd that does not report inactive paths, rather than being guessed. Use the mask-length histogram to spot an unexpected flood of more-specifics, and the eBGP/iBGP/confed split to confirm a switch is learning routes from the sessions you expect.";
  }
};

class CmdShowBgpTableSummary
    : public CmdHandler<CmdShowBgpTableSummary, CmdShowBgpTableSummaryTraits> {
 public:
  using RetType = CmdShowBgpTableSummaryTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo) {
    auto client = utils::createClient<apache::thrift::Client<
        facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

    TRibSummary v4;
    TRibSummary v6;
    client->sync_getRibSummary(v4, TBgpAfi::AFI_IPV4);
    client->sync_getRibSummary(v6, TBgpAfi::AFI_IPV6);

    cli::ShowBgpTableSummaryModel model;
    model.summaries() = {std::move(v4), std::move(v6)};
    return model;
  }

  void printOutput(const RetType& model, std::ostream& out = std::cout) {
    for (const auto& summary : model.summaries().value()) {
      out << "Address Family: " << enumNameSafe(summary.afi().value())
          << std::endl;
      const auto displayedTotalPaths =
          std::max<int64_t>(summary.total_paths().value(), 0);
      out << "Total Prefixes: " << summary.total_prefixes().value()
          << "  Total Paths: " << displayedTotalPaths;
      /*
       * Omit the split entirely when the server did not report it (an older
       * bgpd predating inactive_paths), rather than deriving one from a default
       * and reporting a fully active RIB that was never measured.
       */
      if (const auto inactivePaths = summary.inactive_paths().to_optional()) {
        /*
         * total_paths moves synchronously on announce/withdraw while
         * inactive_paths is reconciled at the next selection pass. Bound both
         * server-supplied values to the subset invariant so that transient
         * skew or corrupt negative input cannot print an impossible split (for
         * example, 8 total and 10 inactive). The raw Thrift values remain
         * available to callers.
         */
        const auto displayedInactivePaths =
            std::clamp<int64_t>(*inactivePaths, 0, displayedTotalPaths);
        const auto activePaths = displayedTotalPaths - displayedInactivePaths;
        out << " (Active: " << activePaths
            << ", Inactive: " << displayedInactivePaths << ")";
      }
      out << std::endl;
      out << "  External (eBGP): " << summary.ebgp_prefixes().value()
          << "  Internal (iBGP): " << summary.ibgp_prefixes().value()
          << "  Confed-eBGP: " << summary.confed_ebgp_prefixes().value()
          << "  Local: " << summary.local_prefixes().value() << std::endl;
      out << "  Routes with unresolved next-hops: "
          << summary.routes_with_unresolved_nexthops().value() << std::endl;

      Table table;
      table.setHeader({"Mask Length", "Number Of Prefixes"});
      // prefix_length_counts is an ordered map, so lengths print ascending.
      for (const auto& [maskLen, count] :
           summary.prefix_length_counts().value()) {
        table.addRow({"/" + std::to_string(maskLen), std::to_string(count)});
      }
      out << table << std::endl << std::endl;
    }

    // RIB-wide (not per-AFI): identical across summaries, so render it once.
    if (!model.summaries()->empty()) {
      out << "Unresolvable next-hops: "
          << model.summaries()->front().unresolvable_nexthops_count().value()
          << std::endl;
    }
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Totals are an RSW-shaped
  // capture: everything learned over confederation eBGP from the upstream
  // FSWs, plus the switch's own originated prefixes.
  static RetType sampleModel() {
    TRibSummary v4;
    v4.afi() = TBgpAfi::AFI_IPV4;
    v4.total_prefixes() = 171;
    v4.total_paths() = 969;
    v4.inactive_paths() = 0;
    v4.ebgp_prefixes() = 0;
    v4.ibgp_prefixes() = 0;
    v4.confed_ebgp_prefixes() = 170;
    v4.local_prefixes() = 1;
    v4.routes_with_unresolved_nexthops() = 0;
    v4.prefix_length_counts() = {{0, 1}, {19, 48}, {24, 8}, {32, 114}};
    v4.unresolvable_nexthops_count() = 0;

    TRibSummary v6;
    v6.afi() = TBgpAfi::AFI_IPV6;
    v6.total_prefixes() = 1344;
    v6.total_paths() = 10731;
    v6.inactive_paths() = 0;
    v6.ebgp_prefixes() = 0;
    v6.ibgp_prefixes() = 0;
    v6.confed_ebgp_prefixes() = 1341;
    v6.local_prefixes() = 3;
    v6.routes_with_unresolved_nexthops() = 0;
    v6.prefix_length_counts() = {
        {0, 1}, {46, 93}, {54, 36}, {56, 93}, {64, 867}, {68, 190}, {128, 64}};
    v6.unresolvable_nexthops_count() = 0;

    RetType model;
    model.summaries() = {v4, v6};
    return model;
  }
};
} // namespace facebook::fboss
