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
#include <optional>
#include <utility>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTable.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;

/**
 * Query FIB-out for one prefix when the target supports the new RPC.
 *
 * An older target is equivalent to tracking being unavailable, so callers
 * retain the ordinary prefix output. Other RPC failures propagate.
 */
template <typename Client>
std::optional<TFibOutTable> queryFibOutPrefixIfSupported(
    Client& client,
    const std::string& prefix) {
  return runMethodWithLegacyFallback(
      [&]() -> std::optional<TFibOutTable> {
        TFibOutTable table;
        TFibOutPrefixRequest request;
        request.prefix() = prefix;
        client.sync_getFibOutPrefix(table, request);
        return table;
      },
      []() -> std::optional<TFibOutTable> { return std::nullopt; });
}

struct CmdShowBgpTablePrefixTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpTable;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays every path the RIB holds for one exact prefix, rendered the same way 'show bgp table detail' renders it: per path the next hop and peer, router or originator ID, local preference, MED, origin, AS path, communities and extended communities, and for a path outside the best group the criterion that lost it best-path selection. The lookup is exact match, not longest match - 'table prefix 10.0.0.0/24' says nothing about a covering 10.0.0.0/8, and 'table more-specifics' is the command that walks downward. Several prefixes may be given at once and their entries are concatenated. A prefix the RIB does not hold produces no output at all, not an empty-result message, so silence here means the prefix is absent rather than the query being malformed. When FIB-out tracking is enabled, each matching entry also shows its submitted normalized nexthop set, pending state, and the number of routes that share that set. This is the detail view by construction; there is no summary form.";
  }
};

class CmdShowBgpTablePrefix
    : public CmdHandler<CmdShowBgpTablePrefix, CmdShowBgpTablePrefixTraits> {
 public:
  using ObjectArgType = CmdShowBgpTablePrefixTraits::RetType;
  using RetType = CmdShowBgpTablePrefixTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& prefixes);

  // Canned, synthetic model (no real switch data): the exact-match lookup
  // yields one entry, so this is the shared 'table detail' sample trimmed to
  // its first prefix.
  static RetType sampleModel() {
    auto data = sampleRibEntriesWithHost();
    auto& entries = *data.tRibEntries();
    // Select the documented prefix by matching it rather than by position, so
    // this cannot silently document a different entry if the shared sample in
    // CmdShowUtils is reordered.
    const auto exactPrefix = sampleIpPrefix("0.0.0.0/0");
    const auto match = std::find_if(
        entries.begin(), entries.end(), [&](const TRibEntry& entry) {
          return *entry.prefix() == exactPrefix;
        });
    CHECK(match != entries.end())
        << "shared RIB sample no longer holds the default route";
    entries = {*match};
    auto fibOut = sampleFibOutTable();
    CHECK_EQ(fibOut.entries()->size(), 2);
    entries.front().fib_out() = std::move(*fibOut.entries()->front().fib_out());
    return data;
  }

  void printOutput(RetType& data, std::ostream& out = std::cout) {
    if (!data.tRibEntries()->empty()) {
      printRIBEntries(out, data, /*detail=*/true);
    }
  }
};
} // namespace facebook::fboss
