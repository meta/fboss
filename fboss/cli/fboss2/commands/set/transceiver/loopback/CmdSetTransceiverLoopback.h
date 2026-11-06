// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/set/transceiver/CmdSetTransceiver.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/LoopbackUtils.h"

#include <string>

namespace facebook::fboss {

struct CmdSetTransceiverLoopbackTraits : public WriteCommandTraits {
  using ParentCmd = CmdSetTransceiver;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "loopback_args",
        args,
        "<system|line> [input|output] <enable|disable>, or 'disable' to "
        "disable all modes. system = host side, line = media side; "
        "input/output selects the CMIS loopback on that side (default input)");
  }
  using ObjectArgType = loopback_utils::LoopbackAction;
  using RetType = std::string;
};

class CmdSetTransceiverLoopback : public CmdHandler<
                                      CmdSetTransceiverLoopback,
                                      CmdSetTransceiverLoopbackTraits> {
 public:
  using ObjectArgType = CmdSetTransceiverLoopbackTraits::ObjectArgType;
  using RetType = CmdSetTransceiverLoopbackTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedPorts,
      const ObjectArgType& action);

  void printOutput(const RetType& output, std::ostream& out = std::cout);
};

} // namespace facebook::fboss
