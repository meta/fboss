/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfig.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <iostream>
#include <stdexcept>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteQosQueueConfigTraits::RetType CmdDeleteQosQueueConfig::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& name) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  if (name.isDefault()) {
    // Clearing defaultPortQueues reverts every port without an explicit
    // portQueueConfigName to the ASIC defaults. There is no binding to check
    // first: no port names the default explicitly, so it can never be left
    // dangling the way a named config can.
    auto& defaultPortQueues = *switchConfig.defaultPortQueues();
    if (defaultPortQueues.empty()) {
      return "Warning: default queue config is empty, nothing to delete";
    }
    defaultPortQueues.clear();

    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);
    return "Successfully deleted the default queue config";
  }

  auto& portQueueConfigs = *switchConfig.portQueueConfigs();
  auto it = portQueueConfigs.find(name.getName());
  if (it == portQueueConfigs.end()) {
    return fmt::format(
        "Warning: queue config '{}' not found, nothing to delete",
        name.getName());
  }

  // Port::portQueueConfigName is a plain string with no referential integrity
  // of its own, so removing a bound config would leave those ports naming an
  // entry that no longer exists. Refuse rather than guess: reverting them to
  // the default is a different intent than switching them to another config,
  // and only the operator knows which was meant.
  auto boundPorts = utils::portsUsingQueueConfig(switchConfig, name);
  if (!boundPorts.empty()) {
    throw std::runtime_error(
        fmt::format(
            "Queue config '{}' is still used by interface(s) {}. Point them "
            "elsewhere first -- 'config interface <intf> queue-config "
            "<other>' to switch, or 'config interface <intf> queue-config "
            "{}' to fall back to the switch-wide default.",
            name.getName(),
            folly::join(", ", boundPorts),
            utils::kDefaultQueueConfigName));
  }

  portQueueConfigs.erase(it);

  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format("Successfully deleted queue config '{}'", name.getName());
}

void CmdDeleteQosQueueConfig::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteQosQueueConfig, CmdDeleteQosQueueConfigTraits>::run();

} // namespace facebook::fboss
