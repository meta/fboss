// Copyright (c) Meta Platforms, Inc. and affiliates.

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
