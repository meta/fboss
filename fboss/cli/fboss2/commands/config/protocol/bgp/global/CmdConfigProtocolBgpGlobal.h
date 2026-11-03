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

// Parsed `config protocol bgp global <attribute> [value ...]`, validated at
// construction. values() is greedy to keep network6's multi-token tail.
class BgpGlobalConfig : public utils::BaseObjectArgType<std::string> {
 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  /* implicit */ BgpGlobalConfig(std::vector<std::string> v);
  const std::string& attr() const {
    return attr_;
  }
  const std::vector<std::string>& values() const {
    return values_;
  }
  const static utils::ObjectArgTypeId id =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;

 private:
  std::string attr_;
  std::vector<std::string> values_;
};

// Single handler for the whole `config protocol bgp global` family. The
// attribute name is the first positional token and the remaining tokens are
// its value(s); dispatch and per-attribute parsing live in the .cpp so adding
// a new global tunable is a one-entry change rather than a new command class.
struct CmdConfigProtocolBgpGlobalTraits : public WriteCommandTraits {
  using ParentCmd = CmdConfigProtocolBgp;
  static void addCliArg(CLI::App& cmd, std::vector<std::string>& args) {
    // Stop CLI11's parent-chain subcommand fallthrough from stealing value
    // tokens that happen to match a sibling command name (e.g. a policy
    // named "peer-group"); see CmdConfigProtocolBgpNeighborTraits.
    cmd.positionals_at_end();
    cmd.add_option("args", args, "<attribute> <value> [value ...]");
  }
  using ObjectArgType = BgpGlobalConfig;
  using RetType = std::string;
};

class CmdConfigProtocolBgpGlobal : public CmdHandler<
                                       CmdConfigProtocolBgpGlobal,
                                       CmdConfigProtocolBgpGlobalTraits> {
 public:
  using ObjectArgType = CmdConfigProtocolBgpGlobalTraits::ObjectArgType;
  using RetType = CmdConfigProtocolBgpGlobalTraits::RetType;

  RetType queryClient(const HostInfo& hostInfo, const ObjectArgType& args);

  void printOutput(const RetType& output);
};

} // namespace facebook::fboss
