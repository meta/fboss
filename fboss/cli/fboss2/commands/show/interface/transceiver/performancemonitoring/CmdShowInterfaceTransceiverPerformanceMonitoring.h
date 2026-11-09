// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <string_view>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/CmdShowInterfaceTransceiver.h"
#include "fboss/cli/fboss2/commands/show/interface/transceiver/performancemonitoring/gen-cpp2/model_types.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss {

struct CmdShowInterfaceTransceiverPerformanceMonitoringTraits
    : public ReadCommandTraits {
  using ParentCmd = CmdShowInterfaceTransceiver;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = cli::ShowInterfaceTransceiverPerformanceMonitoringModel;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description();
};

class CmdShowInterfaceTransceiverPerformanceMonitoring
    : public CmdHandler<
          CmdShowInterfaceTransceiverPerformanceMonitoring,
          CmdShowInterfaceTransceiverPerformanceMonitoringTraits> {
 public:
  using ObjectArgType =
      CmdShowInterfaceTransceiverPerformanceMonitoringTraits::ObjectArgType;
  using RetType =
      CmdShowInterfaceTransceiverPerformanceMonitoringTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs);

  void printOutput(const RetType& model, std::ostream& out = std::cout);

  // Canned, synthetic model (no real switch data) used to render a
  // deterministic example for the CLI reference wiki. No live switch.
  static RetType sampleModel();
};

} // namespace facebook::fboss
