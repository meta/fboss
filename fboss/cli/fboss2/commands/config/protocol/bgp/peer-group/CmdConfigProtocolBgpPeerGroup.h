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
#include "CLI/App.hpp"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/CmdConfigProtocolBgp.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

// Parsed `peer-group <name> [<attribute> <value> ...]`, validated at
// construction. Attributes may span two tokens (e.g. `timers hold-time`),
// matched longest-prefix-first. Bare `peer-group <name>` creates the group.
// Mirrors BgpNeighborConfig.
class BgpPeerGroupConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpPeerGroupConfig(std::vector<std::string> v);
  const std::string& groupName() const {
    return groupName_;
  }
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string groupName_;
  std::string attr_; // matched dispatch key ("" = bare create)
  std::vector<std::string> values_;
};

// Single handler for the whole `config protocol bgp peer-group` family;
// dispatch and per-attribute parsing live in the .cpp so a new tunable is a
// one-entry change. Mirrors CmdConfigProtocolBgpNeighbor.
struct CmdConfigProtocolBgpPeerGroupTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stops CLI11 from classifying attribute tokens as subcommands once the
    // group name is consumed. See CmdConfigProtocolBgpNeighborTraits.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<name> [<attribute> <value> ...]");
  }
  using ObjectArgType = BgpPeerGroupConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpPeerGroup : public CmdHandler<
                                          CmdConfigProtocolBgpPeerGroup,
                                          CmdConfigProtocolBgpPeerGroupTraits> {
 public:
  using ObjectArgType = CmdConfigProtocolBgpPeerGroupTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpPeerGroupTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
