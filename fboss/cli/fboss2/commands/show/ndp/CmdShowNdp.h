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

#include <fboss/agent/if/gen-cpp2/ctrl_types.h>
#include <string_view>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/ndp/gen-cpp2/model_types.h"

namespace facebook::fboss {

struct CmdShowNdpTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_IPV6_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = cli::ShowNdpModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowNdp : public CmdHandler<CmdShowNdp, CmdShowNdpTraits> {
 public:
  using ObjectArgType = CmdShowNdpTraits::ObjectArgType;
  using RetType = CmdShowNdpTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedNdpEntries);

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  RetType createModel(
      const std::vector<facebook::fboss::NdpEntryThrift>& ndpEntries,
      const ObjectArgType& queriedNdpEntries,
      std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
      const std::map<int64_t, cfg::DsfNode>& dsfNodes);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
