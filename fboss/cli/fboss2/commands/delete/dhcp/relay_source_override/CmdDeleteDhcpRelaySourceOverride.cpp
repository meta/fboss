/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/dhcp/relay_source_override/CmdDeleteDhcpRelaySourceOverride.h"
#include "fboss/cli/fboss2/commands/config/dhcp/CmdConfigDhcp.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteDhcpRelaySourceOverrideTraits::RetType
CmdDeleteDhcpRelaySourceOverride::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = removeDhcpSourceOverride(
      *session.getAgentConfig().sw(), "relay", args.getFamily());
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdDeleteDhcpRelaySourceOverride::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteDhcpRelaySourceOverride,
    CmdDeleteDhcpRelaySourceOverrideTraits>::run();

std::vector<std::string> CmdDeleteDhcpRelaySourceOverrideTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(typed, {dhcpFamilies()});
}

} // namespace facebook::fboss
