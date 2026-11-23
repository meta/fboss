/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/static/route/add/CmdConfigProtocolStaticRouteAdd.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

#include <fmt/format.h>
#include <folly/String.h>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/protocol/static/route/StaticRouteUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

template <typename RouteArgType>
std::string addStaticRouteImpl(const RouteArgType& routeArg) {
  auto& config = ConfigSession::getInstance().getAgentConfig();
  auto& swConfig = *config.sw();

  const std::string& prefix = routeArg.getPrefix();
  int32_t vrfId = 0;

  StaticRouteType routeType = routeArg.getRouteType();

  if (routeType == StaticRouteType::NULL_ROUTE) {
    cfg::StaticRouteNoNextHops newRoute;
    newRoute.routerID() = vrfId;
    newRoute.prefix() = prefix;

    auto& nullRoutes = *swConfig.staticRoutesToNull();
    if (findStaticRoute(nullRoutes, vrfId, prefix) != nullRoutes.end()) {
      return fmt::format(
          "Static null0 route {} (VRF {}) already exists", prefix, vrfId);
    }

    eraseStaticRoute(*swConfig.staticRoutesWithNhops(), vrfId, prefix);
    eraseStaticRoute(*swConfig.staticRoutesToCPU(), vrfId, prefix);

    nullRoutes.push_back(newRoute);

    ConfigSession::getInstance().saveConfig();
    return fmt::format(
        "Successfully added static null0 route {} (VRF {})", prefix, vrfId);
  } else if (routeType == StaticRouteType::CPU_ROUTE) {
    cfg::StaticRouteNoNextHops newRoute;
    newRoute.routerID() = vrfId;
    newRoute.prefix() = prefix;

    auto& cpuRoutes = *swConfig.staticRoutesToCPU();
    if (findStaticRoute(cpuRoutes, vrfId, prefix) != cpuRoutes.end()) {
      return fmt::format(
          "Static CPU route {} (VRF {}) already exists", prefix, vrfId);
    }

    eraseStaticRoute(*swConfig.staticRoutesWithNhops(), vrfId, prefix);
    eraseStaticRoute(*swConfig.staticRoutesToNull(), vrfId, prefix);

    cpuRoutes.push_back(newRoute);

    ConfigSession::getInstance().saveConfig();
    return fmt::format(
        "Successfully added static CPU route {} (VRF {})", prefix, vrfId);
  } else {
    cfg::StaticRouteWithNextHops newRoute;
    newRoute.routerID() = vrfId;
    newRoute.prefix() = prefix;
    newRoute.nexthops() = routeArg.getNexthops();

    auto& nhopRoutes = *swConfig.staticRoutesWithNhops();
    auto it = findStaticRoute(nhopRoutes, vrfId, prefix);
    if (it != nhopRoutes.end()) {
      it->nexthops() = routeArg.getNexthops();

      ConfigSession::getInstance().saveConfig();
      std::string nexthopsList = folly::join(", ", routeArg.getNexthops());
      return fmt::format(
          "Successfully updated static route {} (VRF {}) with nexthop{}: {}",
          prefix,
          vrfId,
          routeArg.getNexthops().size() > 1 ? "s" : "",
          nexthopsList);
    }

    eraseStaticRoute(*swConfig.staticRoutesToNull(), vrfId, prefix);
    eraseStaticRoute(*swConfig.staticRoutesToCPU(), vrfId, prefix);

    nhopRoutes.push_back(newRoute);

    ConfigSession::getInstance().saveConfig();
    std::string nexthopsList = folly::join(", ", routeArg.getNexthops());
    return fmt::format(
        "Successfully added static route {} (VRF {}) with nexthop{}: {}",
        prefix,
        vrfId,
        routeArg.getNexthops().size() > 1 ? "s" : "",
        nexthopsList);
  }
}

CmdConfigProtocolStaticIpRouteAddTraits::RetType
CmdConfigProtocolStaticIpRouteAdd::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& routeArg) {
  return addStaticRouteImpl(routeArg);
}

void CmdConfigProtocolStaticIpRouteAdd::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdConfigProtocolStaticIpv6RouteAddTraits::RetType
CmdConfigProtocolStaticIpv6RouteAdd::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& routeArg) {
  return addStaticRouteImpl(routeArg);
}

void CmdConfigProtocolStaticIpv6RouteAdd::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

template std::string addStaticRouteImpl<StaticRouteAddIpArg>(
    const StaticRouteAddIpArg& routeArg);

template std::string addStaticRouteImpl<StaticRouteAddIpv6Arg>(
    const StaticRouteAddIpv6Arg& routeArg);

template void CmdHandler<
    CmdConfigProtocolStaticIpRouteAdd,
    CmdConfigProtocolStaticIpRouteAddTraits>::run();

template void CmdHandler<
    CmdConfigProtocolStaticIpv6RouteAdd,
    CmdConfigProtocolStaticIpv6RouteAddTraits>::run();

namespace {
// <prefix> <nexthop|null0|cpu>: only the keyword next-hops can be offered.
std::vector<std::string> completeStaticRouteArgs(
    const std::vector<std::string>& typed) {
  return utils::completion::completePositions(typed, {{}, {"null0", "cpu"}});
}
} // namespace

std::vector<std::string> CmdConfigProtocolStaticIpRouteAddTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return completeStaticRouteArgs(typed);
}

std::vector<std::string>
CmdConfigProtocolStaticIpv6RouteAddTraits::completeArgs(
    const std::vector<std::string>& typed) {
  return completeStaticRouteArgs(typed);
}

} // namespace facebook::fboss
