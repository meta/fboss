/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/qos/queue_config/CmdDeleteQosQueueConfigQueueId.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteQosQueueConfigQueueIdTraits::RetType
CmdDeleteQosQueueConfigQueueId::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::QueueConfigName& name,
    const ObjectArgType& queueId) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  // findQueueConfigList rather than queueConfigListForWrite: a missing name
  // must not silently create an empty entry.
  auto* configList = utils::findQueueConfigList(switchConfig, name);
  if (configList == nullptr) {
    return fmt::format(
        "Warning: queue config '{}' not found, nothing to delete",
        name.getName());
  }

  int16_t queueIdVal = queueId.getQueueId();
  auto it = std::find_if(
      configList->begin(), configList->end(), [queueIdVal](const auto& queue) {
        return *queue.id() == queueIdVal;
      });

  if (it == configList->end()) {
    return fmt::format(
        "Warning: queue-id {} not found in queue config '{}', nothing to delete",
        queueIdVal,
        name.getName());
  }

  // No binding check here, unlike whole-config deletion: dropping one queue
  // leaves Port::portQueueConfigName resolving to the same (smaller) config,
  // so no reference is left dangling. An emptied entry is likewise left in
  // place so queues can be added back to it later.
  configList->erase(it);

  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format(
      "Successfully deleted queue config '{}' queue-id {}",
      name.getName(),
      queueIdVal);
}

void CmdDeleteQosQueueConfigQueueId::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteQosQueueConfigQueueId,
    CmdDeleteQosQueueConfigQueueIdTraits>::run();

} // namespace facebook::fboss
