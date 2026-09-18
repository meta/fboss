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
#include "fboss/cli/fboss2/commands/config/port_channel/CmdConfigPortChannel.h"
#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

/*
 * PortChannelMemberArgs captures the member operation for the
 * `config port-channel <name> member` command.
 *
 * Usage:
 *   ... member add <interface> [<interface> ...]
 *   ... member remove <interface> [<interface> ...]
 *   ... member <interface> <attribute> <value>
 *
 * where <attribute> is a row of portChannelMemberAttrTable() (priority,
 * lacp rate, lacp mode, lacp hold-timer). The attribute name and value are
 * validated at construction; applying them is a table dispatch in
 * queryClient.
 *
 * `member add` creates the port-channel when it does not exist yet.
 */
class PortChannelMemberArgs : public utils::BaseObjectArgType<std::string> {
 public:
  enum class Op {
    ADD,
    REMOVE,
    SET_ATTR,
  };

  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ PortChannelMemberArgs(std::vector<std::string> v);

  Op getOp() const {
    return op_;
  }

  /* Member interface names (ADD / REMOVE). */
  const std::vector<std::string>& getMemberNames() const {
    return memberNames_;
  }

  /* The single member interface name (SET_ATTR). */
  const std::string& getMemberName() const {
    return memberNames_.front();
  }

  /* The member attribute table key (SET_ATTR), e.g. "lacp rate". */
  const std::string& getAttr() const {
    return attr_;
  }

  /* The raw CLI value for the attribute (SET_ATTR). */
  const std::string& getValue() const {
    return value_;
  }

 private:
  Op op_{Op::ADD};
  std::vector<std::string> memberNames_;
  std::string attr_;
  std::string value_;
};

struct CmdConfigPortChannelMemberTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigPortChannel;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "port_channel_member_config", args, portChannelMemberSetGrammar());
  }
  using ObjectArgType = PortChannelMemberArgs;
  using RetType = std::string;
};

class CmdConfigPortChannelMember : public CmdHandler<
                                       CmdConfigPortChannelMember,
                                       CmdConfigPortChannelMemberTraits> {
 public:
  using ObjectArgType = CmdConfigPortChannelMemberTraits::ObjectArgType;
  using RetType = CmdConfigPortChannelMemberTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const PortChannelConfigArgs& portChannelConfig,
      const ObjectArgType& memberArgs);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
