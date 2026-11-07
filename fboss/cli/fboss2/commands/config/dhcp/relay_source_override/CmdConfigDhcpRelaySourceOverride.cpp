/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/dhcp/relay_source_override/CmdConfigDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigDhcpRelaySourceOverrideTraits::RetType
CmdConfigDhcpRelaySourceOverride::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = applyDhcpSourceOverride(
      *session.getAgentConfig().sw(),
      "relay",
      args.getFamily(),
      args.getIpAddress());
  // Both V4 and V6 relay/reply source overrides are applied hitlessly by
  // ThriftConfigApplier::updateSwitchSettings via plain
  // SwitchSettings::setDhcpV{4,6}{Relay,Reply}Src setters
  // (no SAI ChangeProhibited guard).
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigDhcpRelaySourceOverride::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigDhcpRelaySourceOverride,
    CmdConfigDhcpRelaySourceOverrideTraits>::run();

std::vector<std::string> CmdConfigDhcpRelaySourceOverrideTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(typed, {dhcpFamilies()});
}

} // namespace facebook::fboss
