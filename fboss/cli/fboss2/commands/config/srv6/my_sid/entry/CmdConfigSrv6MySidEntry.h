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
#include "fboss/cli/fboss2/commands/config/srv6/my_sid/CmdConfigSrv6MySid.h"
#include "fboss/cli/fboss2/commands/config/srv6/utils/Srv6MySidCliUtils.h"

namespace facebook::fboss {

// CLI: `config srv6 my-sid <prefix> entry <fn> type adjacency|node|decap ...`
// Upserts one MySID function entry under the configured locator prefix.
struct CmdConfigSrv6MySidEntryTraits : public WriteCommandTraits {
  static std::vector<std::string> completeArgs(
      const std::vector<std::string>& typed);
  using ParentCmd = CmdConfigSrv6MySid;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "entry_args",
        args,
        "<function> type <adjacency|node|decap> [type-specific args]");
  }
  using ObjectArgType = MySidEntryArg;
  using RetType = std::string;
};

class CmdConfigSrv6MySidEntry : public CmdHandler<
                                    CmdConfigSrv6MySidEntry,
                                    CmdConfigSrv6MySidEntryTraits> {
 public:
  using ObjectArgType = CmdConfigSrv6MySidEntryTraits::ObjectArgType;
  using RetType = CmdConfigSrv6MySidEntryTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const LocatorPrefixArg& prefix,
      const ObjectArgType& entryArg);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
