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
 * TunnelIpInIpEncapConfig captures the tunnel ID and optional (attr, value)
 * pairs for an IP-in-IP *encap* tunnel — the outer header the switch adds when
 * encapsulating matching traffic.
 *
 * Usage: config tunnel ip-in-ip encap <tunnel-id> [<attr> <value> ...]
 *
 * Supported attributes:
 *   destination  <ipv6> [mask <prefix-len>]   outer header destination
 *   source       <ipv6> [mask <prefix-len>]   outer header source
 *   ttl-mode     uniform|pipe
 *   dscp-mode    uniform|pipe
 *   ecn-mode     uniform|pipe
 *   underlay-intf-id  <int>
 *
 * destination, source, and underlay-intf-id are required when creating a new
 * tunnel. termination-type is decap-only and is rejected here. Attribute
 * names and enum values are matched case-insensitively.
 */
class TunnelIpInIpEncapConfig
    : public tunnel_utils::TunnelIpInIpConfigArgsBase {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ TunnelIpInIpEncapConfig(std::vector<std::string> v);
};

struct CmdConfigTunnelIpInIpEncapTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigTunnelIpInIp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_config",
        args,
        "<tunnel-id> [<attr> <value> ...] where <attr> is one of: "
        "destination, source, ttl-mode, dscp-mode, ecn-mode, "
        "underlay-intf-id");
  }
  using ObjectArgType = TunnelIpInIpEncapConfig;
  using RetType = std::string;
};

class CmdConfigTunnelIpInIpEncap : public CmdHandler<
                                       CmdConfigTunnelIpInIpEncap,
                                       CmdConfigTunnelIpInIpEncapTraits> {
 public:
  using ObjectArgType = CmdConfigTunnelIpInIpEncapTraits::ObjectArgType;
  using RetType = CmdConfigTunnelIpInIpEncapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& tunnelConfig);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
