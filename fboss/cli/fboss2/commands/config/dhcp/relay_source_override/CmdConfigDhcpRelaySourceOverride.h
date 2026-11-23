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

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

struct CmdConfigDhcpRelaySourceOverrideTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigDhcp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // `expected(2)` prevents CLI11 from treating the second token as a
    // subcommand name rather than an argument to this positional.
    cmd.add_option(
           "family_ip",
           args,
           "<family> <ip> where <family> is one of: ipv4, ipv6")
        ->required()
        ->expected(2);
  }
  using ObjectArgType = DhcpSourceOverrideArgs;
  using RetType = std::string;
};

class CmdConfigDhcpRelaySourceOverride
    : public CmdHandler<
          CmdConfigDhcpRelaySourceOverride,
          CmdConfigDhcpRelaySourceOverrideTraits> {
 public:
  using ObjectArgType = CmdConfigDhcpRelaySourceOverrideTraits::ObjectArgType;
  using RetType = CmdConfigDhcpRelaySourceOverrideTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
