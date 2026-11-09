/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/data_plane/traffic_policy/match/action/CmdDeleteDataPlaneTrafficPolicyMatchAction.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include "fboss/cli/fboss2/commands/config/TrafficPolicyUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteDataPlaneTrafficPolicyMatchActionTraits::RetType
CmdDeleteDataPlaneTrafficPolicyMatchAction::queryClient(
    const HostInfo& /* hostInfo */,
    const traffic_policy::MatcherName& matcherName,
    const ObjectArgType& actionType) {
  auto& session = ConfigSession::getInstance();
  auto msg = traffic_policy::deleteAction(
      *session.getAgentConfig().sw(),
      traffic_policy::PolicyKind::DataPlane,
      matcherName.getName(),
      actionType.getActionType());
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdDeleteDataPlaneTrafficPolicyMatchAction::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteDataPlaneTrafficPolicyMatchAction,
    CmdDeleteDataPlaneTrafficPolicyMatchActionTraits>::run();

} // namespace facebook::fboss
