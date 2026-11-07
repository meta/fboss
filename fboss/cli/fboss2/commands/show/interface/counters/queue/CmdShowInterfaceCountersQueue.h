// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string_view>
#include "fboss/agent/hw/gen-cpp2/hardware_stats_types_fwd.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types_fwd.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/CmdShowInterfaceCounters.h"
#include "fboss/cli/fboss2/commands/show/interface/counters/queue/gen-cpp2/model_types.h"

namespace facebook::fboss {

struct CmdShowInterfaceCountersQueueTraits : public ReadCommandTraits {
  using ParentCmd = CmdShowInterfaceCounters;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::InterfaceQueueCountersModel;
  static constexpr bool ALLOW_FILTERING = true;
  // Counters a queue does not report are STAT_UNINITIALIZED (-1), which
  // --aggregate would add into its result.
  static constexpr bool ALLOW_AGGREGATION = false;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowInterfaceCountersQueue : public CmdHandler<
                                          CmdShowInterfaceCountersQueue,
                                          CmdShowInterfaceCountersQueueTraits> {
 public:
  using ObjectArgType = CmdShowInterfaceCountersQueueTraits::ObjectArgType;
  using RetType = CmdShowInterfaceCountersQueueTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& queriedIfs);
  RetType createModel(
      const std::map<int32_t, facebook::fboss::PortInfoThrift>& portEntries,
      const std::map<std::string, facebook::fboss::HwPortStats>& portStats,
      const std::vector<std::string>& queriedIfs);
  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
