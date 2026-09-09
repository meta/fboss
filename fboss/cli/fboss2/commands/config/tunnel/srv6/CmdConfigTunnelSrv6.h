/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/tunnel/CmdConfigTunnel.h"

namespace facebook::fboss {

struct CmdConfigTunnelSrv6Traits : public WriteCommandTraits {
  using ParentCmd = CmdConfigTunnel;
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdConfigTunnelSrv6
    : public CmdHandler<CmdConfigTunnelSrv6, CmdConfigTunnelSrv6Traits> {
 public:
  using RetType = CmdConfigTunnelSrv6Traits::RetType;

  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use 'encap' or 'decap' subcommand");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
