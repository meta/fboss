// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/CmdList.h"

#include "fboss/cli/fboss2/commands/help/CmdHelp.h"

namespace facebook::fboss {

const CommandTree& kAdditionalCommandTree() {
  return kBaseAdditionalCommandTree();
}

// Defined per-binary because it calls kAdditionalCommandTree(), which resolves
// to a different implementation in each binary. Cannot live in a shared
// library.
void helpCommandHandler() {
  const auto& cmdTree = kCommandTree();
  const auto& addCmdTree = kAdditionalCommandTree();
  std::vector<CommandTree> cmdTrees = {cmdTree, addCmdTree};
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
