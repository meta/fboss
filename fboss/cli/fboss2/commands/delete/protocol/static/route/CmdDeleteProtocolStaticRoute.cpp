/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/static/route/CmdDeleteProtocolStaticRoute.h"

#include <fmt/format.h>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/protocol/static/route/StaticRouteUtils.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

template <typename PrefixArgType>
std::string deleteStaticRouteImpl(const PrefixArgType& prefixArg) {
  auto& config = ConfigSession::getInstance().config<AgentConfigDomain>();
  auto& swConfig = *config.sw();

  const std::string& prefix = prefixArg.getPrefix();
  int32_t vrfId = 0;

  bool found = false;
  found |= eraseStaticRoute(*swConfig.staticRoutesWithNhops(), vrfId, prefix);
  found |= eraseStaticRoute(*swConfig.staticRoutesToNull(), vrfId, prefix);
  found |= eraseStaticRoute(*swConfig.staticRoutesToCPU(), vrfId, prefix);

  if (!found) {
    return fmt::format("Static route {} (VRF {}) not found", prefix, vrfId);
  }

  ConfigSession::getInstance().save<AgentConfigDomain>();
  return fmt::format(
      "Successfully deleted static route {} (VRF {})", prefix, vrfId);
}

CmdDeleteProtocolStaticIpRouteTraits::RetType
CmdDeleteProtocolStaticIpRoute::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& prefixArg) {
  return deleteStaticRouteImpl(prefixArg);
}

void CmdDeleteProtocolStaticIpRoute::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdDeleteProtocolStaticIpv6RouteTraits::RetType
CmdDeleteProtocolStaticIpv6Route::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& prefixArg) {
  return deleteStaticRouteImpl(prefixArg);
}

void CmdDeleteProtocolStaticIpv6Route::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template std::string deleteStaticRouteImpl<StaticRouteDeleteIpPrefixArg>(
    const StaticRouteDeleteIpPrefixArg& prefixArg);

template std::string deleteStaticRouteImpl<StaticRouteDeleteIpv6PrefixArg>(
    const StaticRouteDeleteIpv6PrefixArg& prefixArg);

template void CmdHandler<
    CmdDeleteProtocolStaticIpRoute,
    CmdDeleteProtocolStaticIpRouteTraits>::run();

template void CmdHandler<
    CmdDeleteProtocolStaticIpv6Route,
    CmdDeleteProtocolStaticIpv6RouteTraits>::run();

} // namespace facebook::fboss
