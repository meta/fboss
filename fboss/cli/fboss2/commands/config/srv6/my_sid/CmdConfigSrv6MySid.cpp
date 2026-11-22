/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/srv6/my_sid/CmdConfigSrv6MySid.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

// Creates mySidConfig with the given locator when the block does not yet exist.
CmdConfigSrv6MySidTraits::RetType CmdConfigSrv6MySid::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& prefixArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.config<AgentConfigDomain>().sw();
  const auto& prefix = prefixArg.getPrefix();

  if (swConfig.mySidConfig().has_value()) {
    const auto& configured = *swConfig.mySidConfig()->locatorPrefix();
    throw std::runtime_error(
        fmt::format(
            "SRv6 MySID already configured with {}. "
            "Run: delete srv6 my-sid {} first.",
            configured,
            configured));
  }

  swConfig.mySidConfig() = cfg::MySidConfig{};
  swConfig.mySidConfig()->locatorPrefix() = prefix;
  swConfig.mySidConfig()->entries() = {};

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully initialized SRv6 MySID with locator {}", prefix);
}

void CmdConfigSrv6MySid::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void CmdHandler<CmdConfigSrv6MySid, CmdConfigSrv6MySidTraits>::run();

} // namespace facebook::fboss
