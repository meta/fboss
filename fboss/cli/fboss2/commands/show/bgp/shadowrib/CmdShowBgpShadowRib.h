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
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h" // NOLINT(misc-include-cleaner)
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

namespace facebook::fboss {
using neteng::fboss::bgp::thrift::TRibEntry;
using neteng::fboss::bgp::thrift::TRibEntryWithHost;
using neteng::fboss::bgp_attr::TBgpAfi;

struct CmdShowBgpShadowRibTraits : public ReadCommandTraits {
  using ParentCmd = void;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = TRibEntryWithHost;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowBgpShadowRib
    : public CmdHandler<CmdShowBgpShadowRib, CmdShowBgpShadowRibTraits> {
 public:
  using RetType = CmdShowBgpShadowRibTraits::RetType;
  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(RetType& entries, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. Shares the RIB listing
  // sample with 'show bgp table', which renders the same way.
  static RetType sampleModel() {
    return sampleRibEntriesWithHost();
  }
};
} // namespace facebook::fboss
