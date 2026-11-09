/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTable.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {

CmdShowBgpTable::RetType CmdShowBgpTable::queryClient(
    const HostInfo& hostInfo) {
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  auto entries = queryCanonicalRibWithFallback(
      *client,
      [](auto& c, TCanonicalRibState& state, TBgpAfi afi) {
        c.sync_getRibEntriesCanonical(state, afi);
      },
      [](auto& c, std::vector<TRibEntry>& out, TBgpAfi afi) {
        c.sync_getRibEntries(out, afi);
      });

  TRibEntryWithHost result;
  result.tRibEntries() = std::move(entries);
  result.host() = hostInfo.getName();
  result.oobName() = hostInfo.getOobName();
  result.ip() = hostInfo.getIpStr();
  return result;
}

void CmdShowBgpTable::printOutput(RetType& entries, std::ostream& out) {
  printRIBEntries(out, entries);
}

std::string_view CmdShowBgpTableTraits::description() {
  return "Displays the BGP loc-RIB: every prefix the daemon holds, with all paths received for it. Each prefix header gives the prefix and how many of its paths were selected, active and inactive; each path line then shows the peer it came from and that peer's hostname, the next hop, link bandwidth, origin, local preference, AS path, time since the path last changed, next-hop weight, MED, the received and sent path IDs, weight and IGP cost. Leading markers classify each path: '*' means it is in the best (ECMP) group, '@' marks the single best entry, and '!' a path excluded from selection before comparison. '%' is not one of them - it is printed on the prefix header line immediately after the '>' and means selection is still pending for that whole prefix, so look for it there rather than in the per-path marker column. Paths at a lower local preference - for example from a drained peer - stay in the table but sit outside the best group and carry no marker. Where a rib-policy path-selection statement overrode normal best-path selection for a prefix, the matching criteria are printed under the prefix header. Add 'detail' for per-path communities, originator and cluster list, and the reason each non-best path lost.";
}

} // namespace facebook::fboss
