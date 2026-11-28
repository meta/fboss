/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

CmdConfigVlanTraits::RetType CmdConfigVlan::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& vlanIdArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.config<AgentConfigDomain>().sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  bool created = VlanManager::createVlan(swConfig, vlanId).first;
  if (!created) {
    return fmt::format("VLAN {} already exists", static_cast<uint16_t>(vlanId));
  }

  session.save<AgentConfigDomain>();

  return fmt::format(
      "Successfully created VLAN {}", static_cast<uint16_t>(vlanId));
}

void CmdConfigVlan::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigVlan, CmdConfigVlanTraits>::run();

} // namespace facebook::fboss
