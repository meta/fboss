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

#include <algorithm>
#include <concepts>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <CLI/App.hpp>

#include "fboss/cli/fboss2/CmdGlobalOptions.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

using ValidFilterMapType = std::unordered_map<
    std::string_view,
    std::shared_ptr<CmdGlobalOptions::BaseTypeVerifier>>;

enum class CliReadWriteMode {
  CLI_MODE_WRITE,
  CLI_MODE_READ,
};

struct CommandRunner {
  void (*run)();
  CliReadWriteMode mode;
  bool documented;
};

class CommandHandlerFn {
 public:
  /* implicit */ CommandHandlerFn(const CommandRunner& runner)
      : fn_{runner.run}, mode_{runner.mode}, documented_{runner.documented} {}

  template <typename F>
    requires(
        !std::is_same_v<std::remove_cvref_t<F>, CommandHandlerFn> &&
        std::is_invocable_v<F&>)
  /* implicit */ CommandHandlerFn(F&& fn) : fn_{std::forward<F>(fn)} {}

  void operator()() const {
    fn_();
  }
  CliReadWriteMode mode() const {
    return mode_;
  }
  bool documented() const {
    return documented_;
  }

 private:
  std::function<void()> fn_;
  CliReadWriteMode mode_{CliReadWriteMode::CLI_MODE_WRITE};
  bool documented_{false};
};

using ValidFilterHandlerFn = std::function<ValidFilterMapType()>;
using ArgTypeHandlerFn = std::function<utils::ObjectArgTypeId()>;
// Returns the tokens that may follow the positional args typed so far after
// a command (see utils/ArgCompletion.h).
using ArgCompleterFn =
    std::function<std::vector<std::string>(const std::vector<std::string>&)>;

// How a command registers its positional argument with CLI11, plus (when the
// command's Traits provide completeArgs) how it completes that argument.
// Built by argRegistrar<Traits> below; constructible from a bare registrar
// for commands without completion.
struct ArgRegistrarFn {
  using RegisterFn = std::function<void(CLI::App&, std::vector<std::string>&)>;

  /* implicit */ ArgRegistrarFn( // NOLINT(google-explicit-constructor)
      RegisterFn registerArgs,
      ArgCompleterFn completeArgs = nullptr)
      : registerArgs(std::move(registerArgs)),
        completeArgs(std::move(completeArgs)) {}

  void operator()(CLI::App& cmd, std::vector<std::string>& args) const {
    registerArgs(cmd, args);
  }

  RegisterFn registerArgs;
  ArgCompleterFn completeArgs; // empty when the command has no completion
};
using LocalOptionsHandlerFn = std::function<std::vector<utils::LocalOption>()>;

using CmdVerb = std::string;
using CmdObject = std::string;
using CmdSubCmd = std::string;
using CmdHelpMsg = std::string;

struct Command {
  // Non-config commands: use ArgTypeHandlerFn (enum-based dispatch via switch)
  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        argTypeHandler{argTypeHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        validFilterHandler{validFilterHandler},
        argTypeHandler{argTypeHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        argTypeHandler{argTypeHandler},
        localOptionsHandler{localOptionsHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        validFilterHandler{validFilterHandler},
        argTypeHandler{argTypeHandler},
        localOptionsHandler{localOptionsHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  // Config commands: use ArgRegistrarFn (direct addCliArg dispatch)
  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgRegistrarFn& argRegistrar,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        argRegistrar{argRegistrar},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgRegistrarFn& argRegistrar,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        validFilterHandler{validFilterHandler},
        argRegistrar{argRegistrar},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgRegistrarFn& argRegistrar,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        argRegistrar{argRegistrar},
        localOptionsHandler{localOptionsHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  Command(
      const std::string& name,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgRegistrarFn& argRegistrar,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : name{name},
        help{help},
        commandHandler{commandHandler},
        validFilterHandler{validFilterHandler},
        argRegistrar{argRegistrar},
        localOptionsHandler{localOptionsHandler},
        subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  // Some commands don't have handlers and only have more subcommands
  Command(
      const std::string& name,
      const std::string& help,
      const std::vector<Command>& subcommands)
      : name{name}, help{help}, subcommands{subcommands} {
    sort(this->subcommands.begin(), this->subcommands.end());
  }

  bool operator<(const Command& other) const {
    return name.compare(other.name) < 0;
  }

  std::string name;
  std::string help;
  std::optional<CommandHandlerFn> commandHandler;
  std::optional<ValidFilterHandlerFn> validFilterHandler;
  std::optional<ArgTypeHandlerFn> argTypeHandler;
  std::optional<ArgRegistrarFn> argRegistrar;
  std::optional<LocalOptionsHandlerFn> localOptionsHandler;
  std::vector<Command> subcommands;
};

struct RootCommand : public Command {
  // Non-config commands: use ArgTypeHandlerFn
  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const std::vector<Command>& subcommands = {})
      : Command(object, help, commandHandler, argTypeHandler, subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            validFilterHandler,
            argTypeHandler,
            subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            argTypeHandler,
            localOptionsHandler,
            subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgTypeHandlerFn& argTypeHandler,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            validFilterHandler,
            argTypeHandler,
            localOptionsHandler,
            subcommands),
        verb{verb} {}

  // Config commands: use ArgRegistrarFn
  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgRegistrarFn& argRegistrar,
      const std::vector<Command>& subcommands = {})
      : Command(object, help, commandHandler, argRegistrar, subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgRegistrarFn& argRegistrar,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            validFilterHandler,
            argRegistrar,
            subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ArgRegistrarFn& argRegistrar,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            argRegistrar,
            localOptionsHandler,
            subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& object,
      const std::string& help,
      const CommandHandlerFn& commandHandler,
      const ValidFilterHandlerFn& validFilterHandler,
      const ArgRegistrarFn& argRegistrar,
      const LocalOptionsHandlerFn& localOptionsHandler,
      const std::vector<Command>& subcommands = {})
      : Command(
            object,
            help,
            commandHandler,
            validFilterHandler,
            argRegistrar,
            localOptionsHandler,
            subcommands),
        verb{verb} {}

  RootCommand(
      const std::string& verb,
      const std::string& name,
      const std::string& help,
      const std::vector<Command>& subcommands)
      : Command(name, help, subcommands), verb{verb} {}

  bool operator<(const RootCommand& other) const {
    return name.compare(other.name) < 0;
  }

  std::string verb;
};

using CommandTree = std::vector<RootCommand>;

struct HelpInfo {
  CmdVerb verb;
  CmdHelpMsg helpMsg;
  ValidFilterMapType validFilterMap;
  std::vector<utils::LocalOption> localOptions;

  HelpInfo(
      const CmdVerb& verb,
      const CmdHelpMsg& helpMsg,
      const ValidFilterMapType& validFilterMap,
      const std::vector<utils::LocalOption>& localOptions)
      : verb(verb),
        helpMsg(helpMsg),
        validFilterMap(validFilterMap),
        localOptions(localOptions) {}

  bool operator==(const HelpInfo& info) const {
    return verb == info.verb && helpMsg == info.helpMsg;
  }
  bool operator<(const HelpInfo& info) const {
    return verb < info.verb;
  }
};

// HelpInfo -> vector of subcommands
using RevHelpForObj = std::map<HelpInfo, std::vector<Command>>;

// obj -> revHelpForObj.
using ReverseHelpTree = std::map<std::string, RevHelpForObj>;

const CommandTree& kCommandTree();
const CommandTree& kBaseAdditionalCommandTree();
// Each binary must provide exactly one definition of kAdditionalCommandTree()
// and kSpecialCommands() in its own CmdListImpl.cpp to satisfy ODR.
const CommandTree& kAdditionalCommandTree();
const std::vector<Command>& kSpecialCommands();

void helpCommandHandler();

inline constexpr CommandRunner kHelpCommandRunner{
    &helpCommandHandler,
    CliReadWriteMode::CLI_MODE_READ,
    false /*documented*/};

template <typename T>
ValidFilterMapType validFilterHandler() {
  return T().getValidFilters();
}

template <typename T>
utils::ObjectArgTypeId argTypeHandler() {
  return T().ObjectArgTypeId;
}

template <typename Traits>
concept HasArgCompleter = requires(const std::vector<std::string>& typed) {
  { Traits::completeArgs(typed) } -> std::same_as<std::vector<std::string>>;
};

template <typename Traits>
ArgCompleterFn argCompleterOf() {
  if constexpr (HasArgCompleter<Traits>) {
    return &Traits::completeArgs;
  } else {
    return nullptr;
  }
}

// Usable wherever a command tree entry expects an ArgRegistrarFn:
// `argRegistrar<CmdConfigFooTraits>` registers the positional via the
// Traits' addCliArg and, if the Traits declare completeArgs, completes it.
template <typename Traits>
inline const ArgRegistrarFn argRegistrar{
    &Traits::addCliArg,
    argCompleterOf<Traits>()};

template <typename T>
std::vector<utils::LocalOption> localOptionsHandler() {
  return T().LocalOptions;
}

} // namespace facebook::fboss
