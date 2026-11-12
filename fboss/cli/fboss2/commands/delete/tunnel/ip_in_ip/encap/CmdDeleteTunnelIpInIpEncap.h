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
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/CmdDeleteTunnelIpInIp.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/TunnelIpInIpDeleteUtils.h"

namespace facebook::fboss {

/*
 * TunnelIpInIpEncapDeleteArgs captures the tunnel ID and an optional list of
 * attribute names to reset on an IP-in-IP *encap* tunnel.
 *
 * Usage:
 *   delete tunnel ip-in-ip encap <tunnel-id>                 - delete tunnel
 *   delete tunnel ip-in-ip encap <tunnel-id> <attr> [<attr>] - reset attrs
 *
 * Resettable attributes: ttl-mode, dscp-mode, ecn-mode.
 * (termination-type is decap-only and rejected here; source is required for
 * encap — the agent reads it unconditionally — so it cannot be reset, only
 * reconfigured.)
 */
class TunnelIpInIpEncapDeleteArgs
    : public tunnel_delete_utils::TunnelIpInIpDeleteArgsBase {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ TunnelIpInIpEncapDeleteArgs(std::vector<std::string> v);
};

struct CmdDeleteTunnelIpInIpEncapTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdDeleteTunnelIpInIp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_delete_args",
        args,
        "<tunnel-id> [<attr> ...] — with no attrs, deletes the entire tunnel; "
        "otherwise resets the listed attrs. Resettable attrs: ttl-mode, "
        "dscp-mode, ecn-mode");
  }
  using ObjectArgType = TunnelIpInIpEncapDeleteArgs;
  using RetType = std::string;
};

class CmdDeleteTunnelIpInIpEncap : public CmdHandler<
                                       CmdDeleteTunnelIpInIpEncap,
                                       CmdDeleteTunnelIpInIpEncapTraits> {
 public:
  using ObjectArgType = CmdDeleteTunnelIpInIpEncapTraits::ObjectArgType;
  using RetType = CmdDeleteTunnelIpInIpEncapTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& deleteArgs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
