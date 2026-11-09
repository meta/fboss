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
#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

namespace facebook::fboss {
using facebook::neteng::routing::policy::thrift::TPolicyStats;

struct CmdShowBgpStatsPolicyTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = TPolicyStats;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowBgpStatsPolicy
    : public CmdHandler<CmdShowBgpStatsPolicy, CmdShowBgpStatsPolicyTraits> {
 public:
  using RetType = CmdShowBgpStatsPolicyTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);
  void printOutput(const RetType& policy_stats, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};
} // namespace facebook::fboss
