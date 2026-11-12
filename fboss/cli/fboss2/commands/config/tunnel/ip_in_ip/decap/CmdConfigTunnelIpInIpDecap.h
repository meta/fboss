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
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/CmdConfigTunnelIpInIp.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/TunnelIpInIpConfigUtils.h"

namespace facebook::fboss {

/*
 * TunnelIpInIpDecapConfig captures the tunnel ID and optional (attr, value)
 * pairs for an IP-in-IP *decap* tunnel — the local termination that strips the
 * outer header off matching traffic.
 *
 * Usage: config tunnel ip-in-ip decap <tunnel-id> [<attr> <value> ...]
 *
 * Supported attributes:
 *   destination  <ipv6> [mask <prefix-len>]   tunnel termination match address
 *   source       <ipv6> [mask <prefix-len>]   optional outer-source filter
 *   ttl-mode     uniform|pipe
 *   dscp-mode    uniform|pipe
 *   ecn-mode     uniform|pipe
 *   termination-type  p2p|p2mp (p2p requires 'source'; mp2p/mp2mp are not
 *                     supported by the agent)
 *   underlay-intf-id  <int>
 *
 * destination and underlay-intf-id are required when creating a new tunnel.
 * Attribute names and enum values are matched case-insensitively.
 */
class TunnelIpInIpDecapConfig
    : public tunnel_utils::TunnelIpInIpConfigArgsBase {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ TunnelIpInIpDecapConfig(std::vector<std::string> v);
};

struct CmdConfigTunnelIpInIpDecapTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigTunnelIpInIp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_config",
        args,
        "<tunnel-id> [<attr> <value> ...] where <attr> is one of: "
        "destination, source, ttl-mode, dscp-mode, ecn-mode, "
        "termination-type, underlay-intf-id");
  }
  using ObjectArgType = TunnelIpInIpDecapConfig;
  using RetType = std::string;
};

class CmdConfigTunnelIpInIpDecap : public CmdHandler<
                                       CmdConfigTunnelIpInIpDecap,
                                       CmdConfigTunnelIpInIpDecapTraits> {
 public:
  using ObjectArgType = CmdConfigTunnelIpInIpDecapTraits::ObjectArgType;
  using RetType = CmdConfigTunnelIpInIpDecapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& tunnelConfig);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
