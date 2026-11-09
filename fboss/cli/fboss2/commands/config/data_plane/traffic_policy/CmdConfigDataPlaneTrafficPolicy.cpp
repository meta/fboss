/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/data_plane/traffic_policy/CmdConfigDataPlaneTrafficPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <iostream>
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigDataPlaneTrafficPolicyTraits::RetType
CmdConfigDataPlaneTrafficPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto msg = traffic_policy::applyAction(
      *session.getAgentConfig().sw(),
      traffic_policy::PolicyKind::DataPlane,
      args.getMatcherName(),
      args.getActionTokens());

  // MatchAction edits ride the same delta path as any other ACL change:
  // processAclTableGroupDelta applies them at runtime.
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return msg;
}

void CmdConfigDataPlaneTrafficPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template void CmdHandler<
    CmdConfigDataPlaneTrafficPolicy,
    CmdConfigDataPlaneTrafficPolicyTraits>::run();

} // namespace facebook::fboss
