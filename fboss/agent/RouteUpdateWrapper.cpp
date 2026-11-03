/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/RouteUpdateWrapper.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"

#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

void RouteUpdateWrapper::addRoute(
    RouterID vrf,
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientId,
    const RouteNextHopEntry& nhop,
    std::optional<RouteNextHopSet> nhops) {
  UnicastRoute tempRoute;
  tempRoute.dest()->ip() = network::toBinaryAddress(network);
  tempRoute.dest()->prefixLength() = mask;
  if (nhop.getAction() == RouteForwardAction::NEXTHOPS) {
    tempRoute.nextHops() = nhops.has_value()
        ? util::fromRouteNextHopSet(*nhops)
        : util::fromRouteNextHopSet(nhop.getNextHopSet());
    tempRoute.action() = RouteForwardAction::NEXTHOPS;
  } else {
    tempRoute.action() = nhop.getAction() == RouteForwardAction::DROP
        ? RouteForwardAction::DROP
        : RouteForwardAction::TO_CPU;
  }
  if (nhop.getCounterID().has_value()) {
    tempRoute.counterID() = *nhop.getCounterID();
  }
  if (nhop.getClassID().has_value()) {
    tempRoute.classID() = *nhop.getClassID();
  }
  addRoute(vrf, clientId, std::move(tempRoute));
}

void RouteUpdateWrapper::addRoute(
    RouterID vrf,
    ClientID clientId,
    const UnicastRoute& route) {
  ribRoutesToAddDel_[std::make_pair(vrf, clientId)].toAdd.emplace_back(route);
}

void RouteUpdateWrapper::delRoute(
    RouterID vrf,
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientId) {
  IpPrefix pfx;
  pfx.ip() = network::toBinaryAddress(network);
  pfx.prefixLength() = mask;
  delRoute(vrf, pfx, clientId);
}

void RouteUpdateWrapper::delRoute(
    RouterID vrf,
    const IpPrefix& pfx,
    ClientID clientId) {
  ribRoutesToAddDel_[std::make_pair(vrf, clientId)].toDel.emplace_back(
      std::move(pfx));
}

void RouteUpdateWrapper::addRoute(ClientID clientId, const MplsRoute& route) {
  ribMplsRoutesToAddDel_[{RouterID(0), clientId}].toAdd.emplace_back(route);
}

void RouteUpdateWrapper::addRoute(
    ClientID clientId,
    MplsLabel label,
    const RouteNextHopSet& nhops) {
  MplsRoute tempRoute;
  tempRoute.topLabel() = label;
  tempRoute.nextHops() = util::fromRouteNextHopSet(nhops);
  addRoute(clientId, std::move(tempRoute));
}

void RouteUpdateWrapper::delRoute(MplsLabel label, ClientID clientId) {
  ribMplsRoutesToAddDel_[std::make_pair(RouterID(0), clientId)]
      .toDel.emplace_back(label);
}

void RouteUpdateWrapper::program(const SyncFibInfo& syncFibInfo) {
  if (syncFibInfo.isSyncFibIP()) {
    for (const auto& ridAndClient : syncFibInfo.ridAndClients) {
      if (ribRoutesToAddDel_.find(ridAndClient) == ribRoutesToAddDel_.end()) {
        ribRoutesToAddDel_[ridAndClient] = AddDelRoutes{};
      }
    }
  }
  if (syncFibInfo.isSyncFibMpls()) {
    for (const auto& ridAndClient : syncFibInfo.ridAndClients) {
      if (ribMplsRoutesToAddDel_.find(ridAndClient) ==
          ribMplsRoutesToAddDel_.end()) {
        ribMplsRoutesToAddDel_[ridAndClient] = AddDelMplsRoutes{};
      }
    }
  }
  programStandAloneRib(syncFibInfo.ridAndClients);
  ribRoutesToAddDel_.clear();
  ribMplsRoutesToAddDel_.clear();
  configRoutes_.reset();
}

void RouteUpdateWrapper::programMinAlpmState() {
  getRib()->ensureVrf(RouterID(0));
  addRoute(
      RouterID(0),
      folly::IPAddressV4("0.0.0.0"),
      0,
      ClientID::STATIC_INTERNAL,
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));
  addRoute(
      RouterID(0),
      folly::IPAddressV6("::"),
      0,
      ClientID::STATIC_INTERNAL,
      RouteNextHopEntry(
          RouteForwardAction::DROP, AdminDistance::MAX_ADMIN_DISTANCE));
  program();
}

void RouteUpdateWrapper::printStats(const UpdateStatistics& stats) const {
  XLOG(DBG0) << " Routes added: " << stats.v4RoutesAdded + stats.v6RoutesAdded
             << " Routes deleted: "
             << stats.v4RoutesDeleted + stats.v6RoutesDeleted << " Duration "
             << stats.duration.count() << " us ";
}

void RouteUpdateWrapper::printMplsStats(const UpdateStatistics& stats) const {
  XLOG(DBG0) << " Mpls Routes added: " << stats.mplsRoutesAdded
             << " Mpls Routes deleted: " << stats.mplsRoutesDeleted
             << " Duration " << stats.duration.count() << " us ";
}

void RouteUpdateWrapper::programStandAloneRib(const SyncFibFor& syncFibFor) {
  if (configRoutes_) {
    getRib()->reconfigure(
        resolver_,
        configRoutes_->configRouterIDToInterfaceRoutes,
        configRoutes_->staticRoutesWithNextHops,
        configRoutes_->staticRoutesToNull,
        configRoutes_->staticRoutesToCpu,
        configRoutes_->staticIp2MplsRoutes,
        configRoutes_->staticMplsRoutesWithNextHops,
        configRoutes_->staticMplsRoutesToNull,
        configRoutes_->staticMplsRoutesToCpu,
        configRoutes_->staticMySids,
        *ribToSwitchStateFunc_,
        ribToSwitchStateCookie_,
        configRoutes_->ecmpOverrideModeChanged);
  }
  if (!remoteLoopbackIntfRouteToAdd_.empty() ||
      !remoteLoopbackIntfRouteToDel_.empty()) {
    getRib()->updateRemoteInterfaceRoutes(
        resolver_,
        remoteLoopbackIntfRouteToAdd_,
        remoteLoopbackIntfRouteToDel_,
        *ribToSwitchStateFunc_,
        ribToSwitchStateCookie_);
  }
  for (const auto& [ridClientId, addDelRoutes] : ribRoutesToAddDel_) {
    auto stats = getRib()->update(
        resolver_,
        ridClientId.first,
        ridClientId.second,
        clientIdToAdminDistance(ridClientId.second),
        addDelRoutes.toAdd,
        addDelRoutes.toDel,
        syncFibFor.find(ridClientId) != syncFibFor.end(),
        "RIB update",
        *ribToSwitchStateFunc_,
        ribToSwitchStateCookie_);
    printStats(stats);
    updateStats(stats);
  }
  // update MPLS routes
  for (const auto& [ridClientId, addDelRoutes] : ribMplsRoutesToAddDel_) {
    auto stats = getRib()->update(
        resolver_,
        ridClientId.first,
        ridClientId.second,
        clientIdToAdminDistance(ridClientId.second),
        addDelRoutes.toAdd,
        addDelRoutes.toDel,
        syncFibFor.find(ridClientId) != syncFibFor.end(),
        "MPLS RIB update",
        *ribToSwitchStateFunc_,
        ribToSwitchStateCookie_);
    printMplsStats(stats);
  }
}

void RouteUpdateWrapper::programClassID(
    RouterID rid,
    const std::vector<folly::CIDRNetwork>& prefixes,
    std::optional<cfg::AclLookupClass> classId,
    bool async) {
  if (async) {
    getRib()->setClassIDAsync(
        resolver_,
        rid,
        prefixes,
        *ribToSwitchStateFunc_,
        classId,
        ribToSwitchStateCookie_);
  } else {
    getRib()->setClassID(
        resolver_,
        rid,
        prefixes,
        *ribToSwitchStateFunc_,
        classId,
        ribToSwitchStateCookie_);
  }
}

void RouteUpdateWrapper::setRoutesToConfig(
    const RouterIDAndNetworkToInterfaceRoutes& _configRouterIDToInterfaceRoutes,
    const std::vector<cfg::StaticRouteWithNextHops>& _staticRoutesWithNextHops,
    const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToNull,
    const std::vector<cfg::StaticRouteNoNextHops>& _staticRoutesToCpu,
    const std::vector<cfg::StaticIp2MplsRoute>& _staticIp2MplsRoutes,
    const std::vector<cfg::StaticMplsRouteWithNextHops>&
        _staticMplsRoutesWithNextHops,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& _staticMplsRoutesToNull,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& _staticMplsRoutesToCpu,
    std::vector<MySidWithNextHops> _staticMySids,
    bool _ecmpOverrideModeChanged) {
  configRoutes_ = std::make_unique<ConfigRoutes>(ConfigRoutes{
      _configRouterIDToInterfaceRoutes,
      _staticRoutesWithNextHops,
      _staticRoutesToNull,
      _staticRoutesToCpu,
      _staticIp2MplsRoutes,
      _staticMplsRoutesWithNextHops,
      _staticMplsRoutesToNull,
      _staticMplsRoutesToCpu,
      std::move(_staticMySids),
      _ecmpOverrideModeChanged});
}

void RouteUpdateWrapper::setRemoteLoopbackInterfaceRoutesToConfig(
    const RouterIDAndNetworkToInterfaceRoutes& toAdd,
    const RouterIDToPrefixes& toDel) {
  remoteLoopbackIntfRouteToAdd_ = toAdd;
  remoteLoopbackIntfRouteToDel_ = toDel;
}
} // namespace facebook::fboss
