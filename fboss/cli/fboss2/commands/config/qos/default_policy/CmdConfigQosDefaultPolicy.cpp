/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/qos/default_policy/CmdConfigQosDefaultPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <iostream>
#include <stdexcept>

#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

DefaultPolicyName::DefaultPolicyName(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("QoS policy name is required");
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format(
            "Expected single QoS policy name, got: {}", folly::join(", ", v)));
  }
  const auto& name = v[0];
  static const re2::RE2 kValidPolicyNamePattern(
      "^[a-zA-Z][a-zA-Z0-9_-]{0,63}$");
  if (!re2::RE2::FullMatch(name, kValidPolicyNamePattern)) {
    throw std::invalid_argument(
        "Invalid QoS policy name: '" + name +
        "'. Name must start with a letter, contain only alphanumeric "
        "characters, underscores, or hyphens, and be 1-64 characters long.");
  }
  data_.push_back(name);
}

CmdConfigQosDefaultPolicyTraits::RetType CmdConfigQosDefaultPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& policyName) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.config<AgentConfigDomain>();
  auto& switchConfig = *agentConfig.sw();

  const auto& name = policyName.getName();
  bool found = false;
  for (const auto& policy : *switchConfig.qosPolicies()) {
    if (*policy.name() == name) {
      found = true;
      break;
    }
  }
  if (!found) {
    std::vector<std::string> available;
    for (const auto& policy : *switchConfig.qosPolicies()) {
      available.push_back(*policy.name());
    }
    throw std::invalid_argument(
        fmt::format(
            "QoS policy '{}' not found in config. Available policies: {}",
            name,
            available.empty() ? "<none>" : folly::join(", ", available)));
  }

  if (!switchConfig.dataPlaneTrafficPolicy().has_value()) {
    switchConfig.dataPlaneTrafficPolicy() = cfg::TrafficPolicyConfig{};
  }
  switchConfig.dataPlaneTrafficPolicy()->defaultQosPolicy() = name;

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format("Successfully set default QoS policy to '{}'", name);
}

void CmdConfigQosDefaultPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigQosDefaultPolicy, CmdConfigQosDefaultPolicyTraits>::run();

} // namespace facebook::fboss
