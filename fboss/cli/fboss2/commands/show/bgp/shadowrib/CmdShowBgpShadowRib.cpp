/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/shadowrib/CmdShowBgpShadowRib.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"

namespace facebook::fboss {

CmdShowBgpShadowRib::RetType CmdShowBgpShadowRib::queryClient(
    const HostInfo& hostInfo) {
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);

  auto entries = queryCanonicalRibWithFallback(
      *client,
      [](auto& c, TCanonicalRibState& state, TBgpAfi afi) {
        c.sync_getShadowRibEntriesCanonical(state, afi);
      },
      [](auto& c, std::vector<TRibEntry>& out, TBgpAfi afi) {
        c.sync_getShadowRibEntries(out, afi);
      });

  TRibEntryWithHost result;
  result.tRibEntries() = std::move(entries);
  result.host() = hostInfo.getName();
  result.oobName() = hostInfo.getOobName();
  result.ip() = hostInfo.getIpStr();
  return result;
}

void CmdShowBgpShadowRib::printOutput(RetType& entries, std::ostream& out) {
  printRIBEntries(out, entries);
}

std::string_view CmdShowBgpShadowRibTraits::description() {
  return "Displays the shadow RIB - the daemon's own record of the routes it has handed to the FIB - using the same prefix/path listing and markers as 'show bgp table'. Because it is written as routes are programmed rather than as they are selected, comparing it against 'show bgp table' is the way to spot drift between what BGP chose and what actually reached the forwarding plane: a prefix present in one and missing from the other, or a different best path between the two, points at a programming failure rather than a policy or selection problem. The shadow RIB is also what the daemon reconciles against on restart. Path counts here reflect what was programmed, so a prefix that shows several ECMP members in 'show bgp table' can legitimately show fewer selected paths in this view. Entry counts for both tables are in 'show bgp stats entries'.";
}

} // namespace facebook::fboss
