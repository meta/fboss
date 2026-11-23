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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/arp/gen-cpp2/model_types.h"

namespace facebook::fboss {

struct CmdShowArpTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowArpModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowArp : public CmdHandler<CmdShowArp, CmdShowArpTraits> {
 public:
  using RetType = CmdShowArpTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo);

  std::unordered_map<std::string, std::vector<std::string>>
  getAcceptedFilterValues();

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  RetType createModel(
      std::vector<facebook::fboss::ArpEntryThrift>& arpEntries,
      std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
      const std::map<int64_t, cfg::DsfNode>& dsfNodes);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
