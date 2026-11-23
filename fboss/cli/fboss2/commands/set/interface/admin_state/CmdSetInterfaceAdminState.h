// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <iostream>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/CmdLocalOptions.h"
#include "fboss/cli/fboss2/commands/set/interface/CmdSetInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/lib/phy/gen-cpp2/phy_types.h"

namespace facebook::fboss {

inline const std::string kSetInterfaceAdminStateYesFlag = "-y,--yes";
// Must match the CLI path key fboss2 stores local options under.
inline const std::string kSetInterfaceAdminStateCommandName =
    "set_interface_admin-state";

// `<xphy_system|xphy_line> <enable|disable>`. Component names are parsed by
// the same prbsComponents() helper as `loopback` and `prbs`.
class AdminStateArg : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ AdminStateArg( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  phy::PortComponent component() const {
    return component_;
  }
  const std::string& componentName() const {
    return componentName_;
  }
  bool adminUp() const {
    return adminUp_;
  }

 private:
  phy::PortComponent component_{phy::PortComponent::GB_LINE};
  std::string componentName_;
  bool adminUp_{true};
};

struct CmdSetInterfaceAdminStateTraits : public WriteCommandTraits {
  using ParentCmd = CmdSetInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "admin_state_args", args, "<xphy_system|xphy_line> <enable|disable>");
  }
  using ObjectArgType = AdminStateArg;
  using RetType = std::string;
  std::vector<utils::LocalOption> LocalOptions = {
      {kSetInterfaceAdminStateYesFlag,
       "Skip the Safer Human Touch confirmation prompt. Use only when you "
       "have read the warning and accept the risk.",
       std::nullopt,
       /*isFlag=*/true},
  };
};

class CmdSetInterfaceAdminState : public CmdHandler<
                                      CmdSetInterfaceAdminState,
                                      CmdSetInterfaceAdminStateTraits> {
 public:
  using ObjectArgType = CmdSetInterfaceAdminStateTraits::ObjectArgType;
  using RetType = CmdSetInterfaceAdminStateTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::PortList& queriedIfs,
      const ObjectArgType& adminState);

  void printOutput(const RetType& model, std::ostream& out = std::cout);
};

} // namespace facebook::fboss
