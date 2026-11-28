/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/static_mac/delete/CmdConfigVlanStaticMacDelete.h"

#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigVlanStaticMacDeleteTraits::RetType
CmdConfigVlanStaticMacDelete::queryClient(
    const HostInfo& /* hostInfo */,
    const VlanId& vlanIdArg,
    const ObjectArgType& macAddressArg) {
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  const std::string& macAddress = macAddressArg.getMacAddress();

  // Check if VLAN exists — if not, the MAC entry can't exist either
  // (idempotent)
  if (!VlanManager::findVlan(swConfig, vlanId)) {
    return fmt::format(
        "Static MAC entry for MAC {} on VLAN {} does not exist",
        macAddress,
        static_cast<uint16_t>(vlanId));
  }

  // Check if staticMacAddrs exists and find the entry to delete
  // If no entries exist or entry not found, return success (idempotent)
  if (!swConfig.staticMacAddrs().has_value() ||
      swConfig.staticMacAddrs()->empty()) {
    return fmt::format(
        "Static MAC entry for MAC {} on VLAN {} does not exist (no entries configured)",
        macAddress,
        static_cast<uint16_t>(vlanId));
  }

  auto& staticMacs = *swConfig.staticMacAddrs();
  auto it = std::find_if(
      staticMacs.begin(),
      staticMacs.end(),
      [vlanId, &macAddress](const auto& entry) {
        return *entry.vlanID() == static_cast<int32_t>(vlanId) &&
            *entry.macAddress() == macAddress;
      });

  if (it == staticMacs.end()) {
    return fmt::format(
        "Static MAC entry for MAC {} on VLAN {} does not exist",
        macAddress,
        static_cast<uint16_t>(vlanId));
  }

  // Remove the entry
  staticMacs.erase(it);

  // Save the updated config
  ConfigSession::getInstance().save<AgentConfigDomain>();

  return fmt::format(
      "Successfully deleted static MAC entry: MAC {} from VLAN {}",
      macAddress,
      static_cast<uint16_t>(vlanId));
}

void CmdConfigVlanStaticMacDelete::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigVlanStaticMacDelete,
    CmdConfigVlanStaticMacDeleteTraits>::run();

} // namespace facebook::fboss
