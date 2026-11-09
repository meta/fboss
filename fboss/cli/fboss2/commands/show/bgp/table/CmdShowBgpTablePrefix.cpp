/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTablePrefix.h"

#include <optional>

#include <folly/IPAddress.h>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"

namespace facebook::fboss {
namespace {

TIpPrefix normalizePrefix(const std::string& prefix) {
  const auto network = folly::IPAddress::tryCreateNetwork(
      prefix, -1 /* defaultCidr */, true /* applyMask */);
  if (!network) {
    throw FbossError("Invalid BGP prefix: ", prefix);
  }

  TIpPrefix normalized;
  normalized.afi() = network->first.isV4()
      ? neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV4
      : neteng::fboss::bgp_attr::TBgpAfi::AFI_IPV6;
  normalized.prefix_bin() = std::string(
      reinterpret_cast<const char*>(network->first.bytes()),
      network->first.byteCount());
  normalized.num_bits() = network->second;
  return normalized;
}

/** Replace FIB-out state for every RIB row matching one requested prefix. */
void attachFibOut(
    std::vector<TRibEntry>& ribEntries,
    const TIpPrefix& queriedPrefix,
    TFibOutTable fibOutTable) {
  if (!*fibOutTable.enabled()) {
    return;
  }
  for (auto& ribEntry : ribEntries) {
    if (*ribEntry.prefix() == queriedPrefix) {
      ribEntry.fib_out().reset();
    }
  }
  for (const auto& fibOutEntry : *fibOutTable.entries()) {
    for (auto& ribEntry : ribEntries) {
      if (*ribEntry.prefix() != *fibOutEntry.prefix()) {
        continue;
      }
      if (fibOutEntry.fib_out()) {
        ribEntry.fib_out() = *fibOutEntry.fib_out();
      } else {
        ribEntry.fib_out().reset();
      }
    }
  }
}

} // namespace

CmdShowBgpTablePrefix::RetType CmdShowBgpTablePrefix::queryClient(
    const HostInfo& hostInfo,
    const std::vector<std::string>& prefixes) {
  auto client = utils::createClient<apache::thrift::Client<
      facebook::neteng::fboss::bgp::thrift::TBgpService>>(hostInfo);
  TRibEntryWithHost result;

  if (prefixes.empty()) {
    std::cout
        << "No prefixes entered. Usage: fboss2 show bgp table prefix <prefix>"
        << std::endl;
    return result;
  }

  std::vector<TIpPrefix> normalizedPrefixes;
  normalizedPrefixes.reserve(prefixes.size());
  for (const auto& prefix : prefixes) {
    normalizedPrefixes.push_back(normalizePrefix(prefix));
  }

  auto allEntries = runMethodWithLegacyFallback(
      [&]() {
        std::vector<TRibEntry> entries;
        for (const auto& prefix : prefixes) {
          TCanonicalRibState canonical;
          client->sync_getRibPrefixCanonical(canonical, prefix);
          auto resolved = resolveCanonicalRibState(canonical);
          entries.insert(entries.end(), resolved.begin(), resolved.end());
        }
        return entries;
      },
      [&]() {
        std::vector<TRibEntry> entries;
        for (const auto& prefix : prefixes) {
          std::vector<TRibEntry> newEntry;
          client->sync_getRibPrefix(newEntry, prefix);
          entries.insert(entries.end(), newEntry.begin(), newEntry.end());
        }
        return entries;
      });

  std::optional<bool> trackingEnabled;
  for (size_t i = 0; i < prefixes.size(); ++i) {
    auto fibOut = queryFibOutPrefixIfSupported(*client, prefixes[i]);
    if (!fibOut) {
      break;
    }
    if (trackingEnabled && *trackingEnabled != *fibOut->enabled()) {
      throw FbossError(
          "BGP returned inconsistent FIB-out tracking state across prefix "
          "queries");
    }
    trackingEnabled = *fibOut->enabled();
    if (*trackingEnabled) {
      attachFibOut(allEntries, normalizedPrefixes[i], std::move(*fibOut));
    }
  }

  result.tRibEntries() = std::move(allEntries);
  result.host() = hostInfo.getName();
  result.oobName() = hostInfo.getOobName();
  result.ip() = hostInfo.getIpStr();
  return result;
}

} // namespace facebook::fboss
