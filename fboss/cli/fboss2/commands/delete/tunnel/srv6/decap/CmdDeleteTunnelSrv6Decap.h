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
#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/CmdDeleteTunnelSrv6.h"
#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/TunnelSrv6DeleteUtils.h"

namespace facebook::fboss {

struct CmdDeleteTunnelSrv6DecapTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeleteTunnelSrv6;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "tunnel_delete_args",
        args,
        "<tunnel-id> [ttl-mode|dscp-mode|ecn-mode|termination-type ...]; "
        "with no attributes, deletes the tunnel");
  }
  using ObjectArgType = srv6_tunnel_delete_utils::TunnelSrv6DeleteArgs;
  using RetType = std::string;
};

class CmdDeleteTunnelSrv6Decap : public CmdHandler<
                                     CmdDeleteTunnelSrv6Decap,
                                     CmdDeleteTunnelSrv6DecapTraits> {
 public:
  using ObjectArgType = CmdDeleteTunnelSrv6DecapTraits::ObjectArgType;
  using RetType = CmdDeleteTunnelSrv6DecapTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);
  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
