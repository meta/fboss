// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/show/interface/CmdShowInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"

namespace facebook::fboss {

struct CmdShowInterfaceTransceiverTraits : public ReadCommandTraits,
                                           public CliDocsExempt {
  using ParentCmd = CmdShowInterface;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdShowInterfaceTransceiver : public CmdHandler<
                                        CmdShowInterfaceTransceiver,
                                        CmdShowInterfaceTransceiverTraits> {
 public:
  using ObjectArgType = CmdShowInterfaceTransceiverTraits::ObjectArgType;
  using RetType = CmdShowInterfaceTransceiverTraits::RetType;

  [[noreturn]] RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs);

  void printOutput(const RetType& model);
};

} // namespace facebook::fboss
