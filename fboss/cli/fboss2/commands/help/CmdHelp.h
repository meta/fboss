// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/cli/fboss2/CmdArgsLists.h"
#include "fboss/cli/fboss2/CmdList.h"
#include "fboss/cli/fboss2/CmdSubcommands.h"

namespace facebook::fboss {

class CmdHelp {
 private:
  ReverseHelpTree tree_;

  ReverseHelpTree revHelpTree(const std::vector<CommandTree>& cmdTrees) {
    ReverseHelpTree root;
    for (const auto& cmd : mergeCommandTrees(cmdTrees)) {
      auto& objectName = cmd.name;
      auto& verb = cmd.verb;
      auto& verbHelp = cmd.help;
      const auto& subcommands = cmd.subcommands;
      const auto& validFilterOptional = cmd.validFilterHandler;
      ValidFilterMapType validFilterMap = {};
      if (validFilterOptional.has_value()) {
        const auto& validFilterMapFn = *validFilterOptional;
        validFilterMap = validFilterMapFn();
      }
      std::vector<utils::LocalOption> localOptions;
      const auto& localOptionsOptional = cmd.localOptionsHandler;
      if (localOptionsOptional.has_value()) {
        const auto& localOptionsFn = *localOptionsOptional;
        localOptions = localOptionsFn();
      }
      RevHelpForObj& revHelp = root[objectName];
      HelpInfo helpInfo{verb, verbHelp, validFilterMap, localOptions};
      // even if there are no subcommands,
      // the entry for (verb, verbHelp) must exist!
      if (subcommands.size() == 0) {
        revHelp[helpInfo] = std::vector<Command>();
      }
      for (const Command& c : subcommands) {
        revHelp[helpInfo].push_back(c);
      }
    }
    return root;
  }

 public:
  explicit CmdHelp(const std::vector<CommandTree>& cmdTrees) {
    tree_ = revHelpTree(cmdTrees);
  }

  void run() {
    auto helpArgsTuple = CmdArgsLists::getInstance()->at(0);
    if (helpArgsTuple.size() != 1) {
      std::cerr << "Help takes exactly one argument" << std::endl;
      exit(EINVAL);
    }
    auto helpArg = helpArgsTuple[0];
    printHelpInfo(helpArg);
    std::cout << "Filterable fields for command show " << helpArg << " are [ ";
    printFilterInfo(helpArg);
  }

  void printSubCommandTree(
      const std::vector<Command>& subCmds,
      size_t level,
      std::ostream& out) {
    for (const auto& cmd : subCmds) {
      std::vector<utils::LocalOption> localOptions;
      const auto& localOptionsOptional = cmd.localOptionsHandler;
      if (localOptionsOptional.has_value()) {
        const auto& localOptionsFn = *localOptionsOptional;
        localOptions = localOptionsFn();
      }
      out << std::string(level - 1, '\t') << "    |---" << cmd.name << ":\t"
          << cmd.help << std::endl;
      out << getLocalOptionInfo(localOptions, level + 1);
      printSubCommandTree(cmd.subcommands, level + 1, out);
    }
  }

  void printHelpInfo(std::string helpArg, std::ostream& out = std::cout) {
    const auto& helpTreeMapForObj = tree_[helpArg];
    for (const auto& [helpInfo, subCmds] : helpTreeMapForObj) {
      out << helpInfo.verb << " " << helpArg << ":\t" << helpInfo.helpMsg
          << std::endl;
      out << getLocalOptionInfo(helpInfo.localOptions, 1);
      printSubCommandTree(subCmds, 1, out);
      std::cout << std::endl;
    }
  }

  void printFilterInfoHelper(const ValidFilterMapType& validFilterMap) {
    for (const auto& [key, baseTypeVerifier] : validFilterMap) {
      std::cout << key;
      const auto& acceptedValues = baseTypeVerifier->getAcceptableValues();
      if (!acceptedValues.empty()) {
        std::cout << " (Acceptable values { ";
        for (const auto& value : acceptedValues) {
          std::cout << value << " ";
        }
        std::cout << "}), ";
      } else {
        std::cout << ", ";
      }
    }
  }

  void printFilterInfo(const std::string& helpArg) {
    const auto& helpTreeMapForObj = tree_[helpArg];
    for (const auto& [helpInfo, subCmds] : helpTreeMapForObj) {
      if (helpInfo.validFilterMap.size() != 0) {
        printFilterInfoHelper(helpInfo.validFilterMap);
      }
    }
    std::cout << "]" << std::endl;
  }

  std::string getLocalOptionInfo(
      const std::vector<utils::LocalOption>& localOptions,
      size_t level) {
    if (localOptions.size() == 0) {
      return "";
    }
    std::string localOptionInfo;
    for (const auto& localOption : localOptions) {
      std::string currentLocalOptionInfo = fmt::format(
          "{}    {}:\t{}\n",
          std::string(level - 1, '\t'),
          localOption.name,
          localOption.helpMsg);
      localOptionInfo += currentLocalOptionInfo;
    }
    return localOptionInfo;
  }
};

inline utils::ObjectArgTypeId helpArgTypeHandler() {
  return utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
}

} // namespace facebook::fboss
