// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/tunnel/srv6/CmdConfigTunnelSrv6.h"
#include "fboss/cli/fboss2/commands/config/tunnel/srv6/TunnelSrv6ConfigUtils.h"

namespace facebook::fboss {

class TunnelSrv6DecapConfig
    : public srv6_tunnel_utils::TunnelSrv6ConfigArgsBase {
 public:
  /* implicit */ TunnelSrv6DecapConfig(std::vector<std::string> values);
};

struct CmdConfigTunnelSrv6DecapTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigTunnelSrv6;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_config",
        args,
        "<tunnel-id> [ttl-mode uniform|pipe] [dscp-mode uniform|pipe] "
        "[ecn-mode uniform|pipe] "
        "[termination-type p2p|p2mp|mp2p|mp2mp]");
  }
  using ObjectArgType = TunnelSrv6DecapConfig;
  using RetType = std::string;
};

class CmdConfigTunnelSrv6Decap : public CmdHandler<
                                     CmdConfigTunnelSrv6Decap,
                                     CmdConfigTunnelSrv6DecapTraits> {
 public:
  using ObjectArgType = CmdConfigTunnelSrv6DecapTraits::ObjectArgType;
  using RetType = CmdConfigTunnelSrv6DecapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& tunnelConfig);
  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
