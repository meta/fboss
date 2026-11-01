/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/CmdCompletion.h"

#include <algorithm>
#include <ranges>
#include <set>

#include <CLI/Option.hpp>
#include <folly/CppAttributes.h>

#include "fboss/cli/fboss2/CmdSubcommands.h"

namespace facebook::fboss {

namespace {

bool isHidden(const CLI::App* sub) {
  return sub->get_group().empty();
}

const CLI::App* FOLLY_NULLABLE
findSubcommand(const CLI::App* app, const std::string& word) {
  for (const auto* sub : app->get_subcommands(nullptr)) {
    if (isHidden(sub)) {
      continue;
    }
    if (sub->get_name() == word) {
      return sub;
    }
    const auto& aliases = sub->get_aliases();
    if (std::find(aliases.begin(), aliases.end(), word) != aliases.end()) {
      return sub;
    }
  }
  return nullptr;
}

bool takesPositionals(const CLI::App* app) {
  return !app->get_options(
                 [](const CLI::Option* opt) { return opt->get_positional(); })
              .empty();
}

// Options are looked up on the current command and every ancestor: the
// global options live on the root app and CLI11 accepts them after any
// sub-command.
const CLI::Option* FOLLY_NULLABLE
findOption(const std::vector<const CLI::App*>& chain, const std::string& word) {
  // "--opt=value" carries its value inline.
  auto name = word.substr(0, word.find('='));
  for (const auto* cmd : std::views::reverse(chain)) {
    const auto* opt = cmd->get_option_no_throw(name);
    if (opt != nullptr) {
      return opt;
    }
  }
  return nullptr;
}

} // namespace

std::vector<std::string> completeCommandLine(
    const CLI::App& app,
    const CmdSubcommands& subcommands,
    const std::vector<std::string>& words) {
  if (words.empty()) {
    return {};
  }
  std::vector<const CLI::App*> chain{&app};
  std::vector<std::string> positionals;

  for (size_t i = 0; i + 1 < words.size(); ++i) {
    const auto& word = words[i];
    if (!word.empty() && word[0] == '-') {
      const auto* opt = findOption(chain, word);
      if (opt != nullptr && opt->get_items_expected_min() > 0 &&
          word.find('=') == std::string::npos) {
        ++i; // the option's value
        if (i + 1 == words.size()) {
          // The word under the cursor is the option's value: free-form.
          return {};
        }
      }
      continue;
    }
    if (const auto* sub = findSubcommand(chain.back(), word)) {
      chain.push_back(sub);
      positionals.clear();
      continue;
    }
    if (!takesPositionals(chain.back())) {
      // Neither a sub-command nor an argument: nothing sensible follows.
      return {};
    }
    positionals.push_back(word);
  }

  const auto& current = words.back();
  std::set<std::string> out;
  if (!current.empty() && current[0] == '-') {
    for (const auto* cmd : chain) {
      for (const auto* opt : cmd->get_options()) {
        if (opt->get_group().empty()) {
          continue; // hidden
        }
        for (const auto& name : opt->get_lnames()) {
          out.insert("--" + name);
        }
        for (const auto& name : opt->get_snames()) {
          out.insert("-" + name);
        }
      }
    }
    return {out.begin(), out.end()};
  }

  // Sub-commands may follow positionals (`config interface eth1/1/1
  // pfc-config ...`), except while a value is being completed: when the
  // previous token was itself offered by the completer it is an attribute
  // whose value comes next, not a place a sub-command can go.
  bool offerSubcommands = true;
  const auto* completer = subcommands.getArgCompleter(chain.back());
  if (completer != nullptr && !positionals.empty()) {
    std::vector<std::string> before(positionals.begin(), positionals.end() - 1);
    auto offered = (*completer)(before);
    offerSubcommands =
        std::find(offered.begin(), offered.end(), positionals.back()) ==
        offered.end();
  }
  if (offerSubcommands) {
    for (const auto* sub : chain.back()->get_subcommands(nullptr)) {
      if (!isHidden(sub)) {
        out.insert(sub->get_name());
      }
    }
  }
  if (completer != nullptr) {
    for (auto& token : (*completer)(positionals)) {
      out.insert(std::move(token));
    }
  }
  return {out.begin(), out.end()};
}

} // namespace facebook::fboss
