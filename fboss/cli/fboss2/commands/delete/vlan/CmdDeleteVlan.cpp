/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/vlan/CmdDeleteVlan.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <cstdint>
#include <iostream>

#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdDeleteVlanTraits::RetType CmdDeleteVlan::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& vlanIdArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  if (VlanManager::findVlan(swConfig, vlanId) == nullptr) {
    return fmt::format(
        "Warning: VLAN {} not found, nothing to delete",
        static_cast<uint16_t>(vlanId));
  }
  // Throws FbossError naming the referrers when the VLAN is still in use.
  VlanManager::deleteVlan(swConfig, vlanId);

  // VLAN membership/creation is applied hitlessly, matching the config vlan
  // subcommands (all save with the default HITLESS action level).
  session.saveConfig();

  return fmt::format("Deleted VLAN {}", static_cast<uint16_t>(vlanId));
}

void CmdDeleteVlan::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdDeleteVlan, CmdDeleteVlanTraits>::run();

} // namespace facebook::fboss
