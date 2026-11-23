/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/port/tagging_mode/CmdConfigVlanPortTaggingMode.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/PortMap.h"

namespace facebook::fboss {

CmdConfigVlanPortTaggingModeTraits::RetType
CmdConfigVlanPortTaggingMode::queryClient(
    const HostInfo& /* hostInfo */,
    const VlanId& vlanIdArg,
    const utils::PortList& portList,
    const ObjectArgType& taggingMode) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.getAgentConfig();
  auto& swConfig = *config.sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  // Ensure VLAN exists, creating it if needed
  auto [created, vlan] = VlanManager::createVlan(swConfig, vlanId);
  if (created) {
    std::cout << fmt::format("Created VLAN {}", static_cast<uint16_t>(vlanId))
              << std::endl;
  }

  const auto& ports = portList.data();
  if (ports.empty()) {
    throw std::invalid_argument("No port name provided");
  }

  // Get port logical IDs from port names
  const auto& portMap = session.getPortMap();
  std::vector<std::pair<std::string, int32_t>> portNamesAndIds;
  for (const auto& portName : ports) {
    auto portLogicalId = portMap.getPortLogicalId(portName);
    if (!portLogicalId.has_value()) {
      throw std::invalid_argument(
          fmt::format("Port '{}' not found in configuration", portName));
    }
    portNamesAndIds.emplace_back(
        portName, static_cast<int32_t>(*portLogicalId));
  }

  bool emitTags = taggingMode.getEmitTags();
  bool emitPriorityTags = taggingMode.getEmitPriorityTags();
  std::vector<std::string> updatedPorts;

  // Update VlanPort entries
  for (const auto& [portName, portLogicalId] : portNamesAndIds) {
    bool found = false;
    for (auto& vlanPort : *swConfig.vlanPorts()) {
      if (*vlanPort.vlanID() == static_cast<int32_t>(vlanId) &&
          *vlanPort.logicalPort() == portLogicalId) {
        vlanPort.emitTags() = emitTags;
        vlanPort.emitPriorityTags() = emitPriorityTags;
        found = true;
        updatedPorts.push_back(portName);
        break;
      }
    }

    if (!found) {
      throw std::invalid_argument(
          fmt::format(
              "Port '{}' (ID {}) is not a member of VLAN {}",
              portName,
              portLogicalId,
              static_cast<uint16_t>(vlanId)));
    }
  }

  // Save the updated config - tagging mode changes are hitless (no agent
  // restart)
  session.saveConfig();

  std::string modeStr;
  if (emitPriorityTags) {
    modeStr = "priority-tagged";
  } else if (emitTags) {
    modeStr = "tagged";
  } else {
    modeStr = "untagged";
  }

  if (updatedPorts.size() == 1) {
    return fmt::format(
        "Successfully set port {} tagging mode to {} on VLAN {}",
        updatedPorts[0],
        modeStr,
        static_cast<uint16_t>(vlanId));
  } else {
    return fmt::format(
        "Successfully set {} ports tagging mode to {} on VLAN {}",
        updatedPorts.size(),
        modeStr,
        static_cast<uint16_t>(vlanId));
  }
}

void CmdConfigVlanPortTaggingMode::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigVlanPortTaggingMode,
    CmdConfigVlanPortTaggingModeTraits>::run();

std::vector<std::string> CmdConfigVlanPortTaggingModeTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(
      typed, {{}, {"tagged", "untagged", "priority-tagged"}});
}

} // namespace facebook::fboss
