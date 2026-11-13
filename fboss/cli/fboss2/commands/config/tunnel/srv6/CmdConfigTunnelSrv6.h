// Copyright (c) Meta Platforms, Inc. and affiliates.

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
