/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableMoreSpecifics.h"

#include <folly/IPAddress.h>

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"

namespace facebook::fboss {

CmdShowBgpTableMoreSpecifics::RetType CmdShowBgpTableMoreSpecifics::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& prefixes) {
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
  TRibEntryWithHost result;

  if (prefixes.empty()) {
    std::cout
        << "No prefixes entered. Usage: fboss2 show bgp table more-specifics <prefix>"
        << std::endl;
    return result;
  }

  auto entries = runMethodWithLegacyFallback(
      [&]() {
        std::vector<TRibEntry> allEntries;
        for (const auto& prefix : prefixes) {
          TCanonicalRibState canonical;
          client->sync_getRibSubprefixesCanonical(canonical, prefix);
          auto resolved = resolveCanonicalRibState(canonical);
          allEntries.insert(allEntries.end(), resolved.begin(), resolved.end());
        }
        return allEntries;
      },
      [&]() {
        std::vector<TRibEntry> allEntries;
        for (const auto& prefix : prefixes) {
          std::vector<TRibEntry> newEntry;
          client->sync_getRibSubprefixes(newEntry, prefix);
          allEntries.insert(allEntries.end(), newEntry.begin(), newEntry.end());
        }
        return allEntries;
      });

  result.tRibEntries() = std::move(entries);
  result.host() = hostInfo.getName();
  result.oobName() = hostInfo.getOobName();
  result.ip() = hostInfo.getIpStr();
  return result;
}

CmdShowBgpTableMoreSpecifics::RetType
CmdShowBgpTableMoreSpecifics::sampleModel() {
  auto data = sampleRibEntriesWithHost();
  auto& entries = *data.tRibEntries();

  /*
   * Select the covering prefix by matching it, not by position: the shared
   * sample is defined in another file and an index would silently document the
   * wrong prefix (or throw) if its layout changed.
   */
  const auto coveringPrefix = sampleIpPrefix("2001:db8:1c00::/40");
  const auto covering =
      std::find_if(entries.begin(), entries.end(), [&](const TRibEntry& entry) {
        return *entry.prefix() == coveringPrefix;
      });
  CHECK(covering != entries.end())
      << "shared RIB sample no longer holds the v6 aggregate this sample covers";

  // Two /44s under it, one contributed by each uplink, so the example answers
  // the question the command is for - which peer contributes each
  // more-specific. Everything about the paths except their peer is the data
  // 'table detail' already documents.
  const auto coveringEntry = *covering;
  entries = {coveringEntry};
  const std::vector<std::array<std::string, 4>> subs = {
      {"2001:db8:1c00::/44",
       "2001:db8:e11e:1062::4e",
       "fsw001.p001.f01.abc1",
       "192.0.2.101"},
      {"2001:db8:1c10::/44",
       "2001:db8:e11e:1062::5f",
       "fsw002.p001.f01.abc1",
       "192.0.2.102"}};
  for (const auto& [subPrefix, peer, peerDescription, routerAddress] : subs) {
    auto entry = coveringEntry;
    entry.prefix() = sampleIpPrefix(subPrefix);
    entry.best_next_hop() = sampleIpPrefix(peer);
    for (auto& [group, paths] : *entry.paths()) {
      for (auto& path : paths) {
        path.next_hop() = sampleIpPrefix(peer);
        path.peer_id() = sampleIpPrefix(peer);
        path.peer_description() = peerDescription;
        // The originator has to move with the peer, or the second /44 renders
        // as learned from fsw002 but originated by fsw001's router ID.
        path.router_id() = static_cast<int32_t>(
            folly::IPAddress(routerAddress).asV4().toLongHBO());
      }
    }
    entries.push_back(entry);
  }
  return data;
}

} // namespace facebook::fboss
