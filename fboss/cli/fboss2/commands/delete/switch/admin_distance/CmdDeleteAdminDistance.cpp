/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/switch/admin_distance/CmdDeleteAdminDistance.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <iostream>
#include "fboss/cli/fboss2/commands/config/switch/admin_distance/CmdConfigAdminDistance.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

AdminDistanceDeleteArg::AdminDistanceDeleteArg(std::vector<std::string> v) {
  if (v.size() != 1) {
    throw std::invalid_argument(
        fmt::format(
            "Expected exactly one argument: <client-id>, got {}", v.size()));
  }

  // Shared with `config switch admin-distance`: an entry for the forbidden
  // clients is never consulted by the agent, so neither setting nor removing
  // one is meaningful.
  clientId_ =
      parseAdminDistanceClientId(v[0], "removing the admin distance entry");

  data_ = std::move(v);
}

CmdDeleteAdminDistanceTraits::RetType CmdDeleteAdminDistance::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& arg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  int32_t clientId = arg.getClientId();

  // Once the entry is gone, getAdminDistanceForClientId() (Utils.cpp, reached
  // via SwSwitch::clientIdToAdminDistance) finds no mapping and returns
  // AdminDistance::MAX_ADMIN_DISTANCE (255) for this client -- it does not fall
  // back to the per-client default in switch_config.thrift, since that default
  // only applies when the whole clientIdToAdminDistance field is absent from
  // the config.
  auto& adminDistanceMap = *swConfig.clientIdToAdminDistance();
  auto it = adminDistanceMap.find(clientId);
  if (it == adminDistanceMap.end()) {
    return fmt::format(
        "Warning: no admin distance configured for client-id {}, nothing to "
        "delete",
        clientId);
  }
  adminDistanceMap.erase(it);

  // clientIdToAdminDistance is only consulted at route-program time; existing
  // routes are not re-stamped when the map changes. A coldboot is required to
  // flush and re-program all routes with the restored default distance.
  session.saveConfig(
      cli::ServiceType::AGENT,
      cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART);

  return fmt::format(
      "Successfully removed admin distance entry for client-id {}. Routes from "
      "this client will use MAX_ADMIN_DISTANCE (255). A coldboot is required to "
      "apply the change to existing routes.",
      clientId);
}

void CmdDeleteAdminDistance::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteAdminDistance, CmdDeleteAdminDistanceTraits>::run();

} // namespace facebook::fboss
