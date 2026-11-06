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

#include <CLI/CLI.hpp>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/CmdList.h" // @manual=:cmd-list-header

namespace facebook::fboss {

enum class VerbRegistration {
  kAll,
  kOnlyUsed,
};

class CmdSubcommands {
 public:
  CmdSubcommands() = default;
  ~CmdSubcommands() = default;
  CmdSubcommands(const CmdSubcommands& other) = delete;
  CmdSubcommands& operator=(const CmdSubcommands& other) = delete;

  // Static function for getting the CmdSubcommands folly::Singleton
  static std::shared_ptr<CmdSubcommands> getInstance();

  void init(
      CLI::App& app,
      const CommandTree& cmdTree,
      const CommandTree& additionalCmdTree,
      const std::vector<Command>& specialCmds,
      VerbRegistration verbRegistration = VerbRegistration::kAll);

 private:
  CLI::App* addCommand(
      CLI::App& app,
      const Command& cmd,
      std::string& fullCmd,
      int depth);
  void addCommandBranch(
      CLI::App& app,
      const Command& cmd,
      std::string& fullCmd,
      int depth = 0);
  void initCommandTree(CLI::App& app, const CommandTree& cmdTree);
};

CommandTree filterReadCommands(CommandTree cmdTree);
std::vector<Command> filterReadCommands(std::vector<Command> cmds);

} // namespace facebook::fboss
