/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/qos/queue_config/CmdConfigQosQueueConfigQueueId.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/QueueConfigUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigQosQueueConfigQueueIdTraits::RetType
CmdConfigQosQueueConfigQueueId::queryClient(
    const HostInfo& /* hostInfo */,
    const utils::QueueConfigName& name,
    const ObjectArgType& config) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  auto& configList = utils::queueConfigListForWrite(switchConfig, name);
  int16_t queueIdVal = config.getQueueId();

  // Edit a local copy; splice back only after all args validate, so a mid-parse
  // throw leaves the existing queue config untouched.
  cfg::PortQueue* existing = nullptr;
  for (auto& queue : configList) {
    if (*queue.id() == queueIdVal) {
      existing = &queue;
      break;
    }
  }

  cfg::PortQueue work;
  if (existing != nullptr) {
    work = *existing;
  } else {
    work.id() = queueIdVal;
    work.scheduling() = cfg::QueueScheduling::WEIGHTED_ROUND_ROBIN;
  }

  utils::applyPortQueueConfig(
      work, config.getAttributes(), config.getAqmAttributes());

  if (existing != nullptr) {
    *existing = work;
  } else {
    configList.push_back(work);
  }

  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format(
      "Successfully configured queue-config '{}' queue-id {}",
      name.getName(),
      queueIdVal);
}

void CmdConfigQosQueueConfigQueueId::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigQosQueueConfigQueueId,
    CmdConfigQosQueueConfigQueueIdTraits>::run();

} // namespace facebook::fboss
