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
#include "fboss/cli/fboss2/commands/delete/tunnel/CmdDeleteTunnel.h"

namespace facebook::fboss {

struct CmdDeleteTunnelSrv6Traits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteTunnel;
  using ObjectArgType = utils::NoneArgType;
  using RetType = std::string;
};

class CmdDeleteTunnelSrv6
    : public CmdHandler<CmdDeleteTunnelSrv6, CmdDeleteTunnelSrv6Traits> {
 public:
  using RetType = CmdDeleteTunnelSrv6Traits::RetType;

  RetType queryClient(const HostInfo& /* hostInfo */) {
    throw std::runtime_error(
        "Incomplete command, please use 'encap' or 'decap' subcommand");
  }

  void printOutput(const RetType& /* model */) {}
};

} // namespace facebook::fboss
