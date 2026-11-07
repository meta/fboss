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
#include "fboss/cli/fboss2/commands/config/tunnel/srv6/CmdConfigTunnelSrv6.h"
#include "fboss/cli/fboss2/commands/config/tunnel/srv6/TunnelSrv6ConfigUtils.h"

namespace facebook::fboss {

class TunnelSrv6EncapConfig
    : public srv6_tunnel_utils::TunnelSrv6ConfigArgsBase {
 public:
  /* implicit */ TunnelSrv6EncapConfig(std::vector<std::string> values);
};

struct CmdConfigTunnelSrv6EncapTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigTunnelSrv6;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_config",
        args,
        "<tunnel-id> underlay-intf-id <id> source <IPv6> "
        "[ttl-mode uniform|pipe] [dscp-mode uniform|pipe] "
        "[ecn-mode uniform|pipe] "
        "[termination-type p2p|p2mp|mp2p|mp2mp]");
  }
  using ObjectArgType = TunnelSrv6EncapConfig;
  using RetType = std::string;
};

class CmdConfigTunnelSrv6Encap : public CmdHandler<
                                     CmdConfigTunnelSrv6Encap,
                                     CmdConfigTunnelSrv6EncapTraits> {
 public:
  using ObjectArgType = CmdConfigTunnelSrv6EncapTraits::ObjectArgType;
  using RetType = CmdConfigTunnelSrv6EncapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& tunnelConfig);
  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
