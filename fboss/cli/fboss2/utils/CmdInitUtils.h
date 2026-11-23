// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <CLI/App.hpp>

#include <vector>

#include "fboss/cli/fboss2/CmdList.h" // @manual=:cmd-list-header

namespace facebook::fboss::utils {
/**
 * This is the main utility library for CLI::App initialization for both
 * the main fboss2 binary and CLI E2E tests. Instead of putting these functions
 * in existing CmdUtils.h or CmdUtilsCommon.h, we put them here so that we can
 * avoid circular dependencies on other libraries like `cmd-subcommands`, which
 * already depends on `cmd-common-utils`.
 */

/**
 * Initialize the CLI app with global options and command tree.
 * This sets up the CLI infrastructure and should be called before parsing.
 *
 * This function is shared between the main CLI binary and CLI E2E tests
 * to ensure consistent CLI initialization.
 */
void initApp(CLI::App& app, bool readOnly = false);

struct RegisteredCommands {
  CommandTree cmdTree;
  CommandTree additionalCmdTree;
  std::vector<Command> specialCmds;
};

RegisteredCommands registeredCommands(bool readOnly);

// Called after CLI11 is initlized but before parsing, for any final
// initialization steps
void postAppInit(int argc, char* argv[], CLI::App& app);
} // namespace facebook::fboss::utils
