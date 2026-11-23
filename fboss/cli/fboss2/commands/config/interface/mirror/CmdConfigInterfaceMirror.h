// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

// Parses one or more <direction> <mirror-name> pairs of
//   config interface <name> mirror <direction> <mirror-name> [...]
// where <direction> is ingress or egress. Both directions can be bound in
// one call (e.g. "ingress span0 egress span1").
class MirrorAttrArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ MirrorAttrArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> attributes_;
};

struct CmdConfigInterfaceMirrorTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Leaf: keep a mirror name that matches a sibling command (e.g. `sflow`)
    // from being taken as that subcommand.
    cmd.positionals_at_end();
    cmd.add_option(
        "mirror_attrs",
        args,
        "<direction> <mirror-name> [<direction> <mirror-name>] where "
        "<direction> is one of: ingress, egress");
  }
  using ObjectArgType = MirrorAttrArgs;
  using RetType = std::string;
};

class CmdConfigInterfaceMirror : public CmdHandler<
                                     CmdConfigInterfaceMirror,
                                     CmdConfigInterfaceMirrorTraits> {
 public:
  using ObjectArgType = CmdConfigInterfaceMirrorTraits::ObjectArgType;
  using RetType = CmdConfigInterfaceMirrorTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& mirrorAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
