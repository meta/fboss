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

#include <string>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/delete/interface/CmdDeleteInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

// Parses one or more positional tokens of
//   delete interface <name> sflow <attr> [<attr> ...]
// where <attr> is one of: sample-dest, ingress-rate, egress-rate. All
// attributes present are reset together in a single commit.
class SflowDeleteAttrArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ SflowDeleteAttrArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::vector<std::string>& getAttributes() const {
    return attributes_;
  }

 private:
  std::vector<std::string> attributes_;
};

struct CmdDeleteInterfaceSflowTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdDeleteInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "sflow_attrs",
        args,
        "<attr> [<attr> ...] - which sflow attribute(s) to reset: "
        "sample-dest, ingress-rate, egress-rate");
  }
  using ObjectArgType = SflowDeleteAttrArgs;
  using RetType = std::string;
};

class CmdDeleteInterfaceSflow : public CmdHandler<
                                    CmdDeleteInterfaceSflow,
                                    CmdDeleteInterfaceSflowTraits> {
 public:
  using ObjectArgType = CmdDeleteInterfaceSflowTraits::ObjectArgType;
  using RetType = CmdDeleteInterfaceSflowTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& sflowAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
