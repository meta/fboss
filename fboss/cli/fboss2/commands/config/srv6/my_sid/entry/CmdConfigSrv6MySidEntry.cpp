/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/srv6/my_sid/entry/CmdConfigSrv6MySidEntry.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// Validates prefix, then upserts mySidConfig.entries[function].
CmdConfigSrv6MySidEntryTraits::RetType CmdConfigSrv6MySidEntry::queryClient(
    const HostInfo& /* hostInfo */,
    const LocatorPrefixArg& prefixArg,
    const ObjectArgType& entryArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  auto& mySidConfig = requireMySidConfig(swConfig);
  requireMatchingLocatorPrefix(mySidConfig, prefixArg.getPrefix());

  auto entry = entryArg.buildEntryConfig();
  auto functionValue = entryArg.getFunctionValue();
  auto& entries = *mySidConfig.entries();

  auto it = entries.find(functionValue);
  if (it != entries.end()) {
    if (it->second == entry) {
      return fmt::format(
          "MySID entry {} already set, no change", functionValue);
    }
    it->second = entry;
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
    return fmt::format(
        "Warning: MySID entry {} overwritten with new {} SID",
        functionValue,
        entryArg.getTypeStr());
  }

  entries[functionValue] = entry;
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully added {} SID: entry={}",
      entryArg.getTypeStr(),
      functionValue);
}

void CmdConfigSrv6MySidEntry::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdConfigSrv6MySidEntry, CmdConfigSrv6MySidEntryTraits>::run();

std::vector<std::string> CmdConfigSrv6MySidEntryTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(
      typed, {{}, {"type"}, {"adjacency", "node", "decap"}});
}

} // namespace facebook::fboss
