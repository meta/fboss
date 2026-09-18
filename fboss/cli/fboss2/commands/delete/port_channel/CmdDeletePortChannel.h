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
 * PortChannelDeleteArgs captures the port-channel name and the attributes to
 * reset for the `delete port-channel` command.
 *
 * Usage: delete port-channel <name> [<attr> ...]
 *
 * With no attributes the whole port-channel is deleted. Valueless attributes
 * (reset to default):
 *   description     - clear the description
 *   minimum-links   - reset minimumCapacity to the default (all links)
 */
class PortChannelDeleteArgs : public utils::MultiArgsConfigType {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ PortChannelDeleteArgs(const std::vector<std::string>& v);

  const std::string& getName() const {
    return name_;
  }

 private:
  std::string name_;
};

struct CmdDeletePortChannelTraits : public WriteCommandTraits {
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    cmd.add_option(
        "port_channel_delete_config",
        args,
        fmt::format("<name> [{}]", portChannelAttrDeleteGrammar()));
  }
  using ObjectArgType = PortChannelDeleteArgs;
  using RetType = std::string;
};

class CmdDeletePortChannel
    : public CmdHandler<CmdDeletePortChannel, CmdDeletePortChannelTraits> {
 public:
  using ObjectArgType = CmdDeletePortChannelTraits::ObjectArgType;
  using RetType = CmdDeletePortChannelTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const ObjectArgType& deleteConfig);

  void printOutput(const RetType& logMsg);
};

} // namespace facebook::fboss
