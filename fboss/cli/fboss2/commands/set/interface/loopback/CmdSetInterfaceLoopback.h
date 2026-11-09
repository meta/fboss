// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <iostream>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/set/interface/CmdSetInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/LoopbackUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

namespace facebook::fboss {

inline const std::string kSetInterfaceLoopbackYesFlag = "-y,--yes";
inline const std::string kSetInterfaceLoopbackCommandName =
    "set_interface_loopback";

struct CmdSetInterfaceLoopbackTraits : public WriteCommandTraits {
  using ParentCmd = CmdSetInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "loopback_args",
        args,
        "<asic|xphy_system|xphy_line|transceiver_system|transceiver_line> "
        "[input|output] <enable|disable>. input/output selects the CMIS "
        "loopback on that side (transceiver only; default input)");
  }
  using ObjectArgType = loopback_utils::LoopbackComponentAction;
  using RetType = std::string;
  std::vector<utils::LocalOption> LocalOptions = {
      {kSetInterfaceLoopbackYesFlag,
       "Skip the Safer Human Touch confirmation prompt. Use only when you "
       "have read the warning and accept the risk.",
       std::nullopt,
       /*isFlag=*/true},
  };
};

class CmdSetInterfaceLoopback : public CmdHandler<
                                    CmdSetInterfaceLoopback,
                                    CmdSetInterfaceLoopbackTraits> {
 public:
  using ObjectArgType = CmdSetInterfaceLoopbackTraits::ObjectArgType;
  using RetType = CmdSetInterfaceLoopbackTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs,
      const ObjectArgType& action);

  void printOutput(const RetType& model, std::ostream& out = std::cout);
};

} // namespace facebook::fboss
