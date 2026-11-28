/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlanDefault.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

CmdConfigVlanDefaultTraits::RetType CmdConfigVlanDefault::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& vlanIdArg) {
  auto& session = ConfigSession::getInstance();
  auto& config = session.config<AgentConfigDomain>();
  auto& swConfig = *config.sw();
  int32_t vlanId = vlanIdArg.getVlanId();
  int32_t currentDefault = *swConfig.defaultVlan();

  if (currentDefault == vlanId) {
    return fmt::format("Default VLAN is already set to {}", vlanId);
  }

  // Small helper for ID-based membership checks on different config lists.
  auto hasElemWithId = [](const auto& list, int id, auto accessor) {
    return std::any_of(list.begin(), list.end(), [&](const auto& elem) {
      return accessor(elem) == id;
    });
  };

  // Validate the current default VLAN before changing it.
  //
  // 1) If the old default VLAN is used as ingressVlan for any port and also
  //    has an Interface, we allow the change.
  // 2) If the old default VLAN is used as ingressVlan for any port but has
  //    no Interface, we refuse to proceed and ask the user to fix the config
  //    first (this is the state that causes fboss_sw_agent to crash).
  bool oldVlanHasInterface = hasElemWithId(
      *swConfig.interfaces(), currentDefault, [](const auto& intf) {
        return *intf.vlanID();
      });

  bool oldVlanUsedAsIngress =
      hasElemWithId(*swConfig.ports(), currentDefault, [](const auto& port) {
        return *port.ingressVlan();
      });

  if (oldVlanUsedAsIngress && !oldVlanHasInterface) {
    throw std::invalid_argument(
        fmt::format(
            "Refusing to change default VLAN from {} to {} because VLAN {} is "
            "used as ingressVlan for one or more ports but has no interface. "
            "Move those ports to a different VLAN or attach an interface before "
            "changing the default.",
            currentDefault,
            vlanId,
            currentDefault));
  }

  // New default VLAN handling:
  // 3) If the new VLAN already exists in the VLAN table, do not touch it;
  //    just update sw.defaultVlan.
  // 4) If the new VLAN does not exist, create a non-routable placeholder
  //    entry for it (no interface).
  auto& vlans = *swConfig.vlans();
  auto findById = [](auto& vlanList, int id) {
    return std::find_if(
        vlanList.begin(), vlanList.end(), [id](const auto& vlan) {
          return *vlan.id() == id;
        });
  };

  auto targetVlanIt = findById(vlans, vlanId);
  if (targetVlanIt == vlans.end()) {
    cfg::Vlan newVlan;
    newVlan.id() = vlanId;
    newVlan.name() = fmt::format("default_{}", vlanId);
    newVlan.routable() = false;
    vlans.push_back(newVlan);
  }

  // Remove the older VLAN id from the VLAN object, if it does not have any
  // interface associated with it.
  auto oldVlanIt = findById(vlans, currentDefault);
  if (!oldVlanHasInterface && oldVlanIt != vlans.end()) {
    vlans.erase(oldVlanIt);
  }

  swConfig.defaultVlan() = vlanId;

  session.save<AgentConfigDomain>();

  return fmt::format(
      "Successfully set default VLAN to {} (was {})", vlanId, currentDefault);
}

void CmdConfigVlanDefault::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdConfigVlanDefault, CmdConfigVlanDefaultTraits>::run();

} // namespace facebook::fboss
