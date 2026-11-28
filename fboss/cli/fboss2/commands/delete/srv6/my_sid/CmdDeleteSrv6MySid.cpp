/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/srv6/my_sid/CmdDeleteSrv6MySid.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// Validates prefix, then clears mySidConfig from the session entirely.
CmdDeleteSrv6MySidTraits::RetType CmdDeleteSrv6MySid::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& prefixArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.config<AgentConfigDomain>().sw();
  const auto& requestedPrefix = prefixArg.getPrefix();

  if (!swConfig.mySidConfig().has_value()) {
    return fmt::format(
        "No MySID config for {} (nothing configured)", requestedPrefix);
  }

  requireMatchingLocatorPrefix(*swConfig.mySidConfig(), requestedPrefix);
  const auto configuredPrefix = *swConfig.mySidConfig()->locatorPrefix();

  swConfig.mySidConfig().reset();
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully deleted SRv6 MySID configuration for {}", configuredPrefix);
}

void CmdDeleteSrv6MySid::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void CmdHandler<CmdDeleteSrv6MySid, CmdDeleteSrv6MySidTraits>::run();

} // namespace facebook::fboss
