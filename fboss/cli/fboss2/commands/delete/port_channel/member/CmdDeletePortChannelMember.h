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
#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/commands/delete/port_channel/CmdDeletePortChannel.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

/*
 * PortChannelMemberDeleteArgs captures the member operation for the
 * `delete port-channel <name> member` command.
 *
 * Usage:
 *   ... member <interface> [<interface> ...]   - remove member(s)
 *   ... member <interface> <attribute>         - reset the attribute to its
 *                                                default
 *
 * where <attribute> is a row of portChannelMemberAttrTable() (priority,
 * lacp rate, lacp mode, lacp hold-timer). Applying the reset is a table
 * dispatch in queryClient.
 */
class PortChannelMemberDeleteArgs
    : public utils::BaseObjectArgType<std::string> {
 public:
  enum class Op {
    REMOVE,
    RESET_ATTR,
  };

  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ PortChannelMemberDeleteArgs(std::vector<std::string> v);

  Op getOp() const {
    return op_;
  }

  /* Member interface names (REMOVE). */
  const std::vector<std::string>& getMemberNames() const {
    return memberNames_;
  }

  /* The single member interface name (RESET_ATTR). */
  const std::string& getMemberName() const {
    return memberNames_.front();
  }

  /* The member attribute table key (RESET_ATTR), e.g. "lacp rate". */
  const std::string& getAttr() const {
    return attr_;
  }

 private:
  Op op_{Op::REMOVE};
  std::vector<std::string> memberNames_;
  std::string attr_;
};

struct CmdDeletePortChannelMemberTraits : public WriteCommandTraits {
  using ParentCmd = CmdDeletePortChannel;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "port_channel_member_delete_config",
        args,
        portChannelMemberDeleteGrammar());
  }
  using ObjectArgType = PortChannelMemberDeleteArgs;
  using RetType = std::string;
};

class CmdDeletePortChannelMember : public CmdHandler<
                                       CmdDeletePortChannelMember,
                                       CmdDeletePortChannelMemberTraits> {
 public:
  using ObjectArgType = CmdDeletePortChannelMemberTraits::ObjectArgType;
  using RetType = CmdDeletePortChannelMemberTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const PortChannelDeleteArgs& deleteConfig,
      const ObjectArgType& memberArgs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
