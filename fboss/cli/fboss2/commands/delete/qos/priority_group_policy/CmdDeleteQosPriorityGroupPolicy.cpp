// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/qos/priority_group_policy/CmdDeleteQosPriorityGroupPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteQosPriorityGroupPolicyTraits::RetType
CmdDeleteQosPriorityGroupPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& policyName) {
  auto& session = ConfigSession::getInstance();
  auto& sw = *session.getAgentConfig().sw();
  const std::string& name = policyName.getName();

  if (!sw.portPgConfigs().has_value() ||
      sw.portPgConfigs()->find(name) == sw.portPgConfigs()->end()) {
    throw std::runtime_error(
        fmt::format("Priority group policy '{}' does not exist", name));
  }

  // Refuse rather than cascade: unbinding a port's PFC would change its
  // lossless behaviour without the operator naming that port.
  std::vector<std::string> refs;
  for (const auto& port : *sw.ports()) {
    if (port.pfc().has_value() && *port.pfc()->portPgConfigName() == name) {
      refs.push_back(port.name().value_or(std::to_string(*port.logicalID())));
    }
  }
  if (!refs.empty()) {
    throw std::runtime_error(
        fmt::format(
            "Cannot delete priority group policy '{}': still bound to "
            "interface(s) {}. Run `delete interface <name> pfc-config` on "
            "them first, then retry the delete.",
            name,
            folly::join(", ", refs)));
  }

  sw.portPgConfigs()->erase(name);
  if (sw.portPgConfigs()->empty()) {
    sw.portPgConfigs().reset();
  }
  // Same action level as `config qos priority-group-policy`.
  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format("Successfully deleted priority group policy '{}'", name);
}

void CmdDeleteQosPriorityGroupPolicy::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template void CmdHandler<
    CmdDeleteQosPriorityGroupPolicy,
    CmdDeleteQosPriorityGroupPolicyTraits>::run();

} // namespace facebook::fboss
