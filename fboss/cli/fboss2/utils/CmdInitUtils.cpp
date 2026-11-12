// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/cli/fboss2/utils/CmdInitUtils.h"

#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"

namespace facebook::fboss::utils {

RegisteredCommands registeredCommands(bool readOnly) {
  if (readOnly) {
    return {
        filterReadCommands(kCommandTree()),
        filterReadCommands(kAdditionalCommandTree()),
        filterReadCommands(kSpecialCommands())};
  }
  return {kCommandTree(), kAdditionalCommandTree(), kSpecialCommands()};
}

void initApp(CLI::App& app, bool readOnly) {
  app.require_subcommand();

  /*
   * initialize available global options for CLI
   */
  CmdGlobalOptions::getInstance()->init(app);

  /*
   * initialize/build CLI command token trees
   *
   * NOTE: kCommandTree/kAdditionalCommandTree/kSpecialCommands will be linked
   * from elsewhere to make `CmdSubcommands` an independent lib.
   */
  if (readOnly) {
    const auto cmds = registeredCommands(readOnly);
    CmdSubcommands::getInstance()->init(
        app,
        cmds.cmdTree,
        cmds.additionalCmdTree,
        cmds.specialCmds,
        VerbRegistration::kOnlyUsed);
    return;
  }
  CmdSubcommands::getInstance()->init(
      app, kCommandTree(), kAdditionalCommandTree(), kSpecialCommands());
}
} // namespace facebook::fboss::utils
