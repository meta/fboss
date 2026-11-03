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

struct CmdShowBgpTableCommunityTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpTable;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_COMMUNITY_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays every RIB entry carrying a given community, rendered the same way 'show bgp table detail' renders it. The argument may be a raw 'asn:value' pair or a community name, in which case it is resolved against the switch's own running config. Name matching accepts the name exactly as the config registers it, and also accepts a bare name whose registered form carries the 'COMM_' prefix - so if the config defines 'COMM_ADMIT_CONTROLLER' then both that and 'ADMIT_CONTROLLER' resolve, but a config that defines the name without the prefix does not accept the prefixed spelling. A name the running config does not define is reported as 'Unknown Community Name' rather than returning an empty table, which distinguishes a typo from a community nothing currently carries. When a configured name maps to several communities, the first drives the server-side lookup and the second is applied as an additional filter; a third and any beyond it are ignored, so a name mapping to more than two communities is not narrowed to the whole set. That additional filter works at the granularity of a path group, not a path: a group keeps all of its paths if any one of them carries the community, and only groups where none do are dropped, along with any entry left with no groups. That last behaviour looks unintended rather than designed - the filter computes the matching subset of a group's paths and then discards it - so treat a multi-path group in the result as 'this group contains a match', not 'every path here carries the community'. Both address families are searched. Use this to answer \'which prefixes are tagged with this policy marker\' - the inverse of reading the community column of 'show bgp table detail' one prefix at a time.";
  }
};

class CmdShowBgpTableCommunity : public CmdHandler<
                                     CmdShowBgpTableCommunity,
                                     CmdShowBgpTableCommunityTraits> {
 public:
  using ObjectArgType = CmdShowBgpTableCommunityTraits::RetType;
  using RetType = CmdShowBgpTableCommunityTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedCommunities);

  // Canned, synthetic model (no real switch data). Every path in the shared
  // 'table detail' sample carries AS65108.DEFAULT (65108:337), so that
  // sample is exactly what a query for it returns.
  static RetType sampleModel() {
    return sampleRibEntriesWithHost();
  }

  void printOutput(RetType& data, std::ostream& out = std::cout) {
    if (!data.tRibEntries()->empty()) {
      printRIBEntries(out, data, /*detail=*/true);
    }
  }

  std::vector<TRibEntry> filterEntriesByCommunities(
      std::vector<TRibEntry>& entries,
      const std::string& remainingCommunity) {
    for (auto& entry : entries) {
      auto& pathsMap = *entry.paths();
      for (auto it = pathsMap.begin(); it != pathsMap.end();) {
        const auto& [name, paths] = *it;
        std::vector<TBgpPath> filteredPaths;
        for (const auto& path : paths) {
          for (const auto& comm :
               *apache::thrift::get_pointer(path.communities())) {
            const std::string communityString = communityToString(comm);
            if (remainingCommunity == communityString) {
              filteredPaths.emplace_back(path);
            }
          }
        }
        if (filteredPaths.empty()) {
          it = pathsMap.erase(it); // erase returns iterator to next element
        } else {
          ++it;
        }
      }
    }
    std::vector<TRibEntry> result;
    for (const auto& entry : entries) {
      if (!entry.paths().value().empty()) {
        result.emplace_back(entry);
      }
    }
    return result;
  }
};
} // namespace facebook::fboss
