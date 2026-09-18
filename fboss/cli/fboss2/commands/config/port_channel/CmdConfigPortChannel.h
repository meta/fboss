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

#include <fmt/format.h>

#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/port_channel/PortChannelUtils.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

/*
 * PortChannelConfigArgs captures the port-channel name and optional
 * attribute-value pairs from the CLI.
 *
 * Usage: config port-channel <name> [<attr> <value> ...]
 *
 * where <attr> is a row of portChannelAttrTable() (description,
 * minimum-links). Attribute names and values are validated at construction;
 * applying them is a table dispatch in queryClient.
 *
 * A port-channel is created by adding its first member (see the `member`
 * subcommand); an aggregate port without members cannot be applied by the
 * agent.
 */
class PortChannelConfigArgs : public utils::MultiArgsConfigType {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ PortChannelConfigArgs(const std::vector<std::string>& v);

  const std::string& getName() const {
    return name_;
  }

 private:
  std::string name_;
};

struct CmdConfigPortChannelTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "port_channel_config",
        args,
        fmt::format(
            "<name> [<attr> <value> ...] where <attr> <value> is one of: {}",
            portChannelAttrSetGrammar()));
  }
  using ObjectArgType = PortChannelConfigArgs;
  using RetType = std::string;
};

class CmdConfigPortChannel
    : public CmdHandler<CmdConfigPortChannel, CmdConfigPortChannelTraits> {
 public:
  using ObjectArgType = CmdConfigPortChannelTraits::ObjectArgType;
  using RetType = CmdConfigPortChannelTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& portChannelConfig);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
