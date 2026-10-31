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

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/aggregateport/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss {

struct CmdShowAggregatePortTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_PORT_LIST;
  using ObjectArgType = std::vector<std::string>;
  using RetType = cli::ShowAggregatePortModel;
  static constexpr bool ALLOW_FILTERING = true;
  static constexpr bool ALLOW_AGGREGATION = true;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowAggregatePort
    : public CmdHandler<CmdShowAggregatePort, CmdShowAggregatePortTraits> {
 public:
  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& queriedPorts);

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  RetType createModel(
      const std::vector<facebook::fboss::AggregatePortThrift>&
          aggregatePortEntries,
      std::map<int32_t, facebook::fboss::PortInfoThrift> portInfo,
      const std::map<int32_t, facebook::fboss::InterfaceDetail>& interfaces,
      const ObjectArgType& queriedPorts,
      const std::optional<std::vector<facebook::fboss::LacpPartnerPair>>&
          lacpPartnerPairs = std::nullopt);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
