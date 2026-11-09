/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsEntries.h"

#include <iostream>

#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {

CmdShowBgpStatsEntries::RetType CmdShowBgpStatsEntries::queryClient(
    const HostInfo& hostInfo) {
  TEntryStats stats;
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
  client->sync_getEntryStats(stats);
  return stats;
}

void CmdShowBgpStatsEntries::printOutput(
    const RetType& stats,
    std::ostream& out) {
  out << "BGP entry statistics:" << std::endl;
  out << " Total number of unicast routes: "
      << folly::copy(stats.total_ucast_routes().value()) << std::endl;
  out << " Total number of rib paths: "
      << folly::copy(stats.total_rib_paths().value()) << std::endl;
  out << " Total number of adjribs: "
      << folly::copy(stats.total_adj_ribs().value()) << std::endl;
  out << " Total number of originated routes: "
      << folly::copy(stats.total_originated_routes().value()) << std::endl;
  out << " Total number of shadow rib entries: "
      << folly::copy(stats.total_shadow_rib_entries().value()) << std::endl;
  out << " Total number of tracked netlink wrapper interfaces: "
      << folly::copy(stats.total_netlink_wrapper_interfaces().value())
      << std::endl;
  out << " Total number of active netlink wrapper holds: "
      << folly::copy(stats.total_netlink_wrapper_holds_active().value())
      << std::endl;
}

std::string_view CmdShowBgpStatsEntriesTraits::description() {
  return "Displays the size of each table the daemon maintains: unicast routes and RIB paths in the loc-RIB, per-peer AdjRIB count, locally originated routes, shadow RIB entries, and the netlink wrapper's tracked interfaces and active holds. The two counts worth reading together are unicast routes and shadow RIB entries - they should track each other, since the shadow RIB records what was programmed for those routes, and a persistent gap points at a FIB-programming problem that 'show bgp shadowrib' will localise. The AdjRIB count is per direction per peer rather than per session, so it is roughly twice the established session count. These are current sizes, not cumulative counters, so they fall as well as rise.";
}

CmdShowBgpStatsEntries::RetType CmdShowBgpStatsEntries::sampleModel() {
  RetType stats;
  stats.total_ucast_routes() = 1515;
  stats.total_rib_paths() = 11700;
  stats.total_adj_ribs() = 18;
  stats.total_originated_routes() = 4;
  stats.total_shadow_rib_entries() = 1515;
  stats.total_netlink_wrapper_interfaces() = 0;
  stats.total_netlink_wrapper_holds_active() = 0;
  return stats;
}

} // namespace facebook::fboss
