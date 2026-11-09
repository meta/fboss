/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/qos/default_policy/CmdDeleteQosDefaultPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteQosDefaultPolicyTraits::RetType CmdDeleteQosDefaultPolicy::queryClient(
    const HostInfo& /* hostInfo */) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  if (!switchConfig.dataPlaneTrafficPolicy().has_value() ||
      !switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().has_value()) {
    return "No default QoS policy is currently configured";
  }

  auto removedName = *switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy();
  switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy().reset();

  // COLDBOOT: a hitless unset of defaultQosPolicy desyncs SaiQosMapManager
  // handle bookkeeping in the hw agent and the next qos policy change aborts
  // it. Relax to HITLESS once the agent handle-ownership bug is fixed.
  session.saveConfig(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  return fmt::format(
      "Successfully removed default QoS policy '{}'", removedName);
}

void CmdDeleteQosDefaultPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteQosDefaultPolicy, CmdDeleteQosDefaultPolicyTraits>::run();

} // namespace facebook::fboss
