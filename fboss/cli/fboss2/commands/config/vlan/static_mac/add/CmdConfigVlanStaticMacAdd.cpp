/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/static_mac/add/CmdConfigVlanStaticMacAdd.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/cli/fboss2/utils/SessionPortMap.h"

namespace facebook::fboss {

CmdConfigVlanStaticMacAddTraits::RetType CmdConfigVlanStaticMacAdd::queryClient(
    const HostInfo& /* hostInfo */,
    const VlanId& vlanIdArg,
    const ObjectArgType& macAndPort) {
  auto& config = ConfigSession::getInstance().getAgentConfig();
  auto& swConfig = *config.sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  // Ensure VLAN exists, creating it if needed
  auto [created, vlan] = VlanManager::createVlan(swConfig, vlanId);
  if (created) {
    std::cout << fmt::format("Created VLAN {}", static_cast<uint16_t>(vlanId))
              << std::endl;
  }

  // Get port logical ID from port name
  const utils::SessionPortMap portMap;
  auto portLogicalId = portMap->getPortLogicalId(macAndPort.getPortName());
  if (!portLogicalId.has_value()) {
    throw std::invalid_argument(
        fmt::format(
            "Port '{}' not found in configuration", macAndPort.getPortName()));
  }

  const std::string& macAddress = macAndPort.getMacAddress();

  // Check if entry already exists - if so, return success (idempotent)
  if (swConfig.staticMacAddrs().has_value()) {
    for (const auto& entry : *swConfig.staticMacAddrs()) {
      if (*entry.vlanID() == static_cast<int32_t>(vlanId) &&
          *entry.macAddress() == macAddress) {
        return fmt::format(
            "Static MAC entry for MAC {} on VLAN {} already exists",
            macAddress,
            static_cast<uint16_t>(vlanId));
      }
    }
  }

  // Create and add the new static MAC entry
  cfg::StaticMacEntry newEntry;
  newEntry.vlanID() = static_cast<int32_t>(vlanId);
  newEntry.macAddress() = macAddress;
  newEntry.egressLogicalPortID() = static_cast<int32_t>(*portLogicalId);

  if (!swConfig.staticMacAddrs().has_value()) {
    swConfig.staticMacAddrs() = std::vector<cfg::StaticMacEntry>();
  }
  swConfig.staticMacAddrs()->push_back(newEntry);

  // Save the updated config
  ConfigSession::getInstance().saveConfig();

  return fmt::format(
      "Successfully added static MAC entry: MAC {} -> VLAN {}, Port {} (ID {})",
      macAddress,
      static_cast<uint16_t>(vlanId),
      macAndPort.getPortName(),
      static_cast<int32_t>(*portLogicalId));
}

void CmdConfigVlanStaticMacAdd::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigVlanStaticMacAdd, CmdConfigVlanStaticMacAddTraits>::run();

} // namespace facebook::fboss
