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
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/interface/CmdConfigInterface.h"
#include "fboss/cli/fboss2/utils/InterfaceList.h"

namespace facebook::fboss {

struct CmdConfigInterfaceDhcpTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigInterface;
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_NONE;
  using ObjectArgType = std::monostate;
  using RetType = std::string;
};

class CmdConfigInterfaceDhcp
    : public CmdHandler<CmdConfigInterfaceDhcp, CmdConfigInterfaceDhcpTraits> {
 public:
  RetType queryClient(
      const HostInfo& /* hostInfo */,
      const utils::InterfaceList& interfaces) {
    if (interfaces.empty()) {
      throw std::invalid_argument("No interface name provided");
    }
    throw std::runtime_error(
        "Incomplete command, please use one of the subcommands (e.g. relay)");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
