// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/commands/help/CmdHelp.h"
#include "fboss/cli/fboss2/utils/CmdInitUtils.h"

namespace facebook::fboss {

const CommandTree& kAdditionalCommandTree() {
  return kBaseAdditionalCommandTree();
}

void helpCommandHandler() {
  const auto cmds = utils::registeredCommands(true /*readOnly*/);
  std::vector<CommandTree> cmdTrees = {cmds.cmdTree, cmds.additionalCmdTree};
  CmdHelp(cmdTrees).run();
}

const std::vector<Command>& kSpecialCommands() {
  static const std::vector<Command> cmds = {Command(
      "help",
      "Help command showing info on all verbs applicable for the requested object",
      kHelpCommandRunner,
      helpArgTypeHandler)};
  return cmds;
}

} // namespace facebook::fboss
