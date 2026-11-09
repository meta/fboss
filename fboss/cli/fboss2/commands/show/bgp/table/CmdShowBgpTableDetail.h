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

#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "neteng/fboss/bgp/if/gen-cpp2/TBgpService.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using namespace neteng::fboss::bgp::thrift;
using namespace neteng::fboss::bgp_attr;

struct CmdShowBgpTableDetailTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowBgpTableDetail
    : public CmdHandler<CmdShowBgpTableDetail, CmdShowBgpTableDetailTraits> {
 public:
  using RetType = CmdShowBgpTableDetailTraits::RetType;
  RetType queryClient(const HostInfo& hostInfo);

  void printOutput(RetType& entries, std::ostream& out = std::cout) {
    printRIBEntries(out, entries, /*detail=*/true);
  }

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Same rows as
  // 'show bgp table'; detail only adds lines to each path.
  static RetType sampleModel() {
    return sampleRibEntriesWithHost();
  }
};
} // namespace facebook::fboss
