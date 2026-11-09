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

struct CmdShowBgpTableMoreSpecificsTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowBgpTable;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IP_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Displays every RIB entry covered by a prefix - the prefix itself plus everything more specific beneath it - rendered the same way 'show bgp table detail' renders it. The covering prefix is included in the result, not excluded, so a single-entry result means nothing more specific than the queried prefix exists. This is the command for the aggregation questions 'show bgp table prefix' cannot answer: whether a supernet is being punched through by longer prefixes, and which peers are contributing them. Several prefixes may be given at once and their entries are concatenated. A prefix that covers nothing in the RIB, including itself, produces no output at all rather than an empty-result message.";
  }
};

class CmdShowBgpTableMoreSpecifics : public CmdHandler<
                                         CmdShowBgpTableMoreSpecifics,
                                         CmdShowBgpTableMoreSpecificsTraits> {
 public:
  using ObjectArgType = CmdShowBgpTableMoreSpecificsTraits::RetType;
  using RetType = CmdShowBgpTableMoreSpecificsTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& prefixes);

  // Canned, synthetic model (no real switch data): the covering prefix from
  // the shared 'table detail' sample plus two entries beneath it, so the
  // example shows the parent being included alongside the more-specifics.
  static RetType sampleModel();

  void printOutput(RetType& data, std::ostream& out = std::cout) {
    if (!data.tRibEntries()->empty()) {
      printRIBEntries(out, data, /*detail=*/true);
    }
  }
};
} // namespace facebook::fboss
