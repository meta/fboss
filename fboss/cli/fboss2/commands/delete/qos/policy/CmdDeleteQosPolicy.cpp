/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/qos/policy/CmdDeleteQosPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/QosPolicyUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Collect every place a TrafficPolicyConfig can name a QoS policy, so the
// delete refuses instead of leaving a config that points at a policy which no
// longer exists.
void collectReferences(
    const cfg::TrafficPolicyConfig& policyConfig,
    const std::string& name,
    const std::string& location,
    std::vector<std::string>& refs) {
  if (policyConfig.defaultQosPolicy().has_value() &&
      *policyConfig.defaultQosPolicy() == name) {
    refs.push_back(fmt::format("{}.defaultQosPolicy", location));
  }
  if (policyConfig.portIdToQosPolicy().has_value()) {
    for (const auto& [portId, policyName] : *policyConfig.portIdToQosPolicy()) {
      if (policyName == name) {
        refs.push_back(
            fmt::format("{}.portIdToQosPolicy[{}]", location, portId));
      }
    }
  }
}

std::vector<std::string> findReferences(
    const cfg::SwitchConfig& switchConfig,
    const std::string& name) {
  std::vector<std::string> refs;
  if (switchConfig.dataPlaneTrafficPolicy().has_value()) {
    collectReferences(
        *switchConfig.dataPlaneTrafficPolicy(),
        name,
        "dataPlaneTrafficPolicy",
        refs);
  }
  if (switchConfig.cpuTrafficPolicy().has_value() &&
      switchConfig.cpuTrafficPolicy()->trafficPolicy().has_value()) {
    collectReferences(
        *switchConfig.cpuTrafficPolicy()->trafficPolicy(),
        name,
        "cpuTrafficPolicy.trafficPolicy",
        refs);
  }
  return refs;
}

} // namespace

CmdDeleteQosPolicyTraits::RetType CmdDeleteQosPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& policyName) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  const std::string name = policyName.getName();
  if (name.empty()) {
    throw std::runtime_error("qos policy name is required");
  }
  auto& qosPolicies = *switchConfig.qosPolicies();

  auto it = utils::findQosPolicy(qosPolicies, name);
  if (it == qosPolicies.end()) {
    return fmt::format(
        "Warning: QoS policy '{}' not found, nothing to delete", name);
  }

  // Refuse rather than cascade: clearing the referring field would silently
  // change forwarding behaviour on ports the user did not name. Point at the
  // exact fields so the operator knows what to unset first — there is no CLI
  // today for defaultQosPolicy / portIdToQosPolicy (unlike
  // `delete interface … queuing-policy`).
  auto refs = findReferences(switchConfig, name);
  if (!refs.empty()) {
    throw std::runtime_error(
        fmt::format(
            "Cannot delete QoS policy '{}': still referenced by {}. "
            "Unset those fields first, then retry the delete.",
            name,
            folly::join(", ", refs)));
  }

  qosPolicies.erase(it);
  session.saveConfig();

  return fmt::format("Successfully deleted QoS policy '{}'", name);
}

void CmdDeleteQosPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template void CmdHandler<CmdDeleteQosPolicy, CmdDeleteQosPolicyTraits>::run();

} // namespace facebook::fboss
