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
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

// Parses one or more <attr> <value> pairs of
//   config interface <name> sflow <attr> <value> [<attr> <value> ...]
// where <attr> is one of: sample-dest, ingress-rate, egress-rate. All
// attributes present are applied together in a single commit (e.g.
// "sample-dest cpu ingress-rate 100 egress-rate 50" in one call).
class SflowAttrArgs : public utils::BaseObjectArgType<std::string> {
 public:
  /* implicit */ SflowAttrArgs( // NOLINT(google-explicit-constructor)
      std::vector<std::string> v);

  const std::vector<std::pair<std::string, std::string>>& getAttributes()
      const {
    return attributes_;
  }

 private:
  std::vector<std::pair<std::string, std::string>> attributes_;
};

struct CmdConfigInterfaceSflowTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigInterface;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "sflow_attrs",
        args,
        "<attr> <value> [<attr> <value> ...] where <attr> is one of: "
        "sample-dest, ingress-rate, egress-rate");
  }
  using ObjectArgType = SflowAttrArgs;
  using RetType = std::string;
};

class CmdConfigInterfaceSflow : public CmdHandler<
                                    CmdConfigInterfaceSflow,
                                    CmdConfigInterfaceSflowTraits> {
 public:
  using ObjectArgType = CmdConfigInterfaceSflowTraits::ObjectArgType;
  using RetType = CmdConfigInterfaceSflowTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const utils::InterfaceList& interfaces,
      const ObjectArgType& sflowAttrs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
