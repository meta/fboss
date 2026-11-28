/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/entry/CmdDeleteSrv6MySidEntry.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// Validates prefix, then erases one function entry (idempotent if missing).
CmdDeleteSrv6MySidEntryTraits::RetType CmdDeleteSrv6MySidEntry::queryClient(
    const HostInfo& /* hostInfo */,
    const LocatorPrefixArg& prefixArg,
    const ObjectArgType& deleteArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.config<AgentConfigDomain>().sw();

  if (!swConfig.mySidConfig().has_value()) {
    return fmt::format(
        "MySID entry {} does not exist (no mySidConfig configured)",
        deleteArg.getFunctionValue());
  }

  auto& mySidConfig = *swConfig.mySidConfig();
  requireMatchingLocatorPrefix(mySidConfig, prefixArg.getPrefix());

  auto& entries = *mySidConfig.entries();
  auto functionValue = deleteArg.getFunctionValue();
  auto it = entries.find(functionValue);
  if (it == entries.end()) {
    return fmt::format("MySID entry {} does not exist", functionValue);
  }

  entries.erase(it);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format("Successfully deleted MySID entry {}", functionValue);
}

void CmdDeleteSrv6MySidEntry::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteSrv6MySidEntry, CmdDeleteSrv6MySidEntryTraits>::run();

} // namespace facebook::fboss
