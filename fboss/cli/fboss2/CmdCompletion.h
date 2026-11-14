/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
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

#include <CLI/App.hpp>

namespace facebook::fboss {

class CmdSubcommands;

/*
 * Shell tab-completion for a partially typed fboss2 command line.
 *
 * `words` is every word after the program name, the last one being the
 * (possibly empty) word under the cursor. The initialized CLI11 tree is
 * walked along the leading words: a word naming a sub-command descends, a
 * word starting with '-' is an option (its value is skipped when it takes
 * one), and anything else is a positional argument of the current command.
 *
 * Returns the tokens that may complete the last word:
 * - options (long and short names) when the last word starts with '-';
 * - otherwise the sub-commands of the current command, plus whatever the
 *   command's positional-argument completer (see utils/ArgCompletion.h)
 *   offers given the positionals typed so far.
 *
 * No prefix filtering is done here; the shell does that.
 */
std::vector<std::string> completeCommandLine(
    const CLI::App& app,
    const CmdSubcommands& subcommands,
    const std::vector<std::string>& words);

} // namespace facebook::fboss
