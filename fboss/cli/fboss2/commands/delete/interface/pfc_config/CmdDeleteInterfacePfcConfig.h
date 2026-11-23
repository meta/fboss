// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

/**
 * Parses the optional positional tokens of
 *   delete interface <name> pfc-config [<attr> ...]
 *
 * With no attribute the whole sw.ports[*].pfc struct is cleared, which
 * disables PFC on the port. With attributes only the named optional fields
 * are reset:
 *   watchdog     - drop the PfcWatchdog sub-struct
 *   rx-duration  - unset rxPfcDurationEnable
 *   tx-duration  - unset txPfcDurationEnable
 *
 * rx, tx and priority-group-policy are not deletable on their own: the first
 * two are plain bools and the third is a required string, so "delete" has no
 * meaning for them. Use `config interface <name> pfc-config` to change them,
 * or delete the whole pfc-config.
 */
class PfcConfigDeleteAttrs : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ PfcConfigDeleteAttrs(std::vector<std::string> v);

  const std::vector<std::string>& getAttributes() const {
    return attributes_;
  }

 private:
  std::vector<std::string> attributes_;
};

struct CmdDeleteInterfacePfcConfigTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "pfc_config_attrs",
        args,
        "[<attr> ...] - omit to remove the whole PFC config, or name the "
        "optional field(s) to reset: watchdog, rx-duration, tx-duration");
  }
  using ObjectArgType = PfcConfigDeleteAttrs;
  using RetType = std::string;
};

class CmdDeleteInterfacePfcConfig : public CmdHandler<
                                        CmdDeleteInterfacePfcConfig,
                                        CmdDeleteInterfacePfcConfigTraits> {
 public:
  using ObjectArgType = CmdDeleteInterfacePfcConfigTraits::ObjectArgType;
  using RetType = CmdDeleteInterfacePfcConfigTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& attrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
