/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableDetail.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"

namespace facebook::fboss {

CmdShowBgpTableDetail::RetType CmdShowBgpTableDetail::queryClient(
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

std::string_view CmdShowBgpTableDetailTraits::description() {
  return "Same loc-RIB listing as 'show bgp table', with extra indented lines under each path. Two are always present: the originator (or router) ID with the cluster list, and the full community set - resolved to their configured mnemonic names where the switch knows them. Two more are conditional: an extended-community line for a path that carries any, and, for a path that lost best-path selection, the tie-break step that rejected it. Use it when you need to know why a particular path was not chosen, or which communities a peer attached to a prefix; use the plain 'show bgp table' when you only need the one-line-per-path view.";
}

} // namespace facebook::fboss
