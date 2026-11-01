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
#include "fboss/cli/fboss2/commands/delete/dhcp/CmdDeleteDhcp.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

struct CmdDeleteDhcpReplySourceOverrideTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdDeleteDhcp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option("family", args, "<family> is one of: ipv4, ipv6")
        ->required()
        ->expected(1);
  }
  using ObjectArgType = DhcpSourceOverrideDeleteArgs;
  using RetType = std::string;
};

class CmdDeleteDhcpReplySourceOverride
    : public CmdHandler<
          CmdDeleteDhcpReplySourceOverride,
          CmdDeleteDhcpReplySourceOverrideTraits> {
 public:
  using ObjectArgType = CmdDeleteDhcpReplySourceOverrideTraits::ObjectArgType;
  using RetType = CmdDeleteDhcpReplySourceOverrideTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
