/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlanConfig.h"

#include <fmt/format.h>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/vlan/CmdConfigVlan.h"
#include "fboss/cli/fboss2/commands/config/vlan/VlanManager.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

CmdConfigVlanConfigTraits::RetType CmdConfigVlanConfig::queryClient(
    const HostInfo& /* hostInfo */,
    const VlanId& vlanIdArg,
    const ObjectArgType& arg) {
  auto& swConfig = *ConfigSession::getInstance().getAgentConfig().sw();
  VlanID vlanId(vlanIdArg.getVlanId());

  // Ensure VLAN exists, auto-creating it if needed
  auto [created, vlan] = VlanManager::createVlan(swConfig, vlanId);
  if (created) {
    std::cout << fmt::format("Created VLAN {}", static_cast<uint16_t>(vlanId))
              << std::endl;
  }

  const std::string& attr = arg.getAttrName();
  const std::string& value = arg.getValue();

  if (attr == "name") {
    vlan->name() = value;
  } else {
    throw std::invalid_argument(
        fmt::format("Unknown VLAN attribute '{}'. Supported: name", attr));
  }

  ConfigSession::getInstance().saveConfig();

  return fmt::format(
      "Successfully configured VLAN {}: {}=\"{}\"",
      static_cast<uint16_t>(vlanId),
      attr,
      value);
}

void CmdConfigVlanConfig::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigVlanConfig, CmdConfigVlanConfigTraits>::run();

} // namespace facebook::fboss
