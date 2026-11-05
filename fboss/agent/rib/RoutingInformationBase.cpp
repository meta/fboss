/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/rib/RoutingInformationBase.h"

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/rib/ConfigApplier.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/state/FibDeltaHelpers.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NodeMap-defs.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/rib/RibMySidUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"

#include <exception>
#include <memory>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

namespace facebook::fboss {

namespace {

template <typename RouterIDToRouteTableT>
RibMySidUpdater::VrfRouteTables makeVrfRouteTables(
    RouterIDToRouteTableT& routerIDToRouteTable) {
  RibMySidUpdater::VrfRouteTables routeTables;
  for (auto& [_rid, rt] : routerIDToRouteTable) {
    routeTables.emplace_back(&rt.v4NetworkToRoute, &rt.v6NetworkToRoute);
  }
  return routeTables;
}

class RibIpRouteUpdate {
 public:
  using ThriftRoute = UnicastRoute;
  using ThriftRouteId = IpPrefix;
  using RibRoute = RibRouteUpdater::RouteEntry;
  using RibRouteId = folly::CIDRNetwork;
  static RibRoute ToAddFn(
      const ThriftRoute& route,
      const AdminDistance distance,
      RoutingInformationBase::UpdateStatistics& stats,
      const RibRouteTables& ribTables) {
    auto network = facebook::network::toIPAddress(*route.dest()->ip());
    auto mask = static_cast<uint8_t>(*route.dest()->prefixLength());
    std::optional<RouteCounterID> counterID;
    std::optional<cfg::AclLookupClass> classID;
    if (route.counterID().has_value()) {
      counterID = route.counterID().value();
    }
    if (route.classID().has_value()) {
      classID = route.classID().value();
    }
    if (network.isV4()) {
      ++stats.v4RoutesAdded;
    } else {
      ++stats.v6RoutesAdded;
    }

    if (apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            route.namedRouteDestination())) {
      const auto& namedDest = *route.namedRouteDestination();
      const auto destType = namedDest.getType();
      if (destType == NamedRouteDestination::Type::nextHopGroup ||
          destType == NamedRouteDestination::Type::policyName) {
        if (!route.nextHops()->empty()) {
          throw FbossError(
              "Route cannot specify both nextHops and namedRouteDestination");
        }
        auto namedNhg = destType == NamedRouteDestination::Type::nextHopGroup
            ? *namedDest.nextHopGroup()
            : ribTables.getPolicyDefaultNextHopGroup(*namedDest.policyName());
        auto adminDistance = route.adminDistance().value_or(distance);
        RouteNextHopEntry entry(
            RouteForwardAction::NEXTHOPS, adminDistance, counterID, classID);
        entry.setNamedNextHopGroup(namedNhg);
        return RibRoute{{network, mask}, entry};
      }
    }

    return RibRoute{
        {network, mask},
        RouteNextHopEntry::from(route, distance, counterID, classID)};
  }
  static RibRouteId ToDelFn(
      const ThriftRouteId& prefix,
      RoutingInformationBase::UpdateStatistics& stats) {
    auto network = facebook::network::toIPAddress(*prefix.ip());
    auto mask = static_cast<uint8_t>(*prefix.prefixLength());
    if (network.isV4()) {
      ++stats.v4RoutesDeleted;
    } else {
      ++stats.v6RoutesDeleted;
    }
    return {network, mask};
  }
};

class RibMplsRouteUpdate {
 public:
  using ThriftRoute = MplsRoute;
  using ThriftRouteId = MplsLabel;
  using RibRoute = RibRouteUpdater::MplsRouteEntry;
  using RibRouteId = LabelID;
  static RibRoute ToAddFn(
      const ThriftRoute& route,
      const AdminDistance distance,
      RoutingInformationBase::UpdateStatistics& stats,
      const RibRouteTables& /*ribTables*/) {
    ++stats.mplsRoutesAdded;
    return RibRoute{
        LabelID(folly::copy(route.topLabel().value())),
        RouteNextHopEntry::from(route, distance, std::nullopt, std::nullopt)};
  }

  static RibRouteId ToDelFn(
      const ThriftRouteId& topLabel,
      RoutingInformationBase::UpdateStatistics& stats) {
    ++stats.mplsRoutesDeleted;
    return RibRouteId(topLabel);
  }
};

class Timer {
 public:
  explicit Timer(std::chrono::microseconds* duration)
      : duration_(duration), start_(std::chrono::steady_clock::now()) {}

  ~Timer() {
    auto end = std::chrono::steady_clock::now();
    *duration_ =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
  }

  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;
  Timer(Timer&&) = delete;
  Timer& operator=(Timer&&) = delete;

 private:
  std::chrono::microseconds* duration_;
  std::chrono::time_point<std::chrono::steady_clock> start_;
};

std::shared_ptr<MySid> mySidFromEntry(const MySidEntry& entry) {
  auto type = *entry.type();
  if (entry.namedNextHops().has_value() && !entry.nextHops()->empty()) {
    throw FbossError(
        "Only one of nextHops or namedNextHops must be set in MySidEntry");
  }
  if (entry.namedNextHops().has_value() &&
      entry.namedNextHops()->getType() !=
          NamedRouteDestination::Type::nextHopGroup) {
    throw FbossError(
        "Only nextHopGroup is supported in namedNextHops for MySidEntry");
  }
  bool handled = false;
  switch (type) {
    case MySidType::ADJACENCY_MICRO_SID:
      // Adjacency SIDs can have at most one nexthop. Named next hop groups
      // are not supported.
      if (entry.namedNextHops().has_value()) {
        throw FbossError(
            "NamedNextHops are not supported for ADJACENCY_MICRO_SID MySid type");
      }
      if (entry.nextHops()->size() > 1) {
        throw FbossError(
            "At most one NextHop can be specified for ADJACENCY_MICRO_SID MySid type");
      }
      handled = true;
      break;
    case MySidType::NODE_MICRO_SID:
    case MySidType::BINDING_MICRO_SID:
      if (entry.nextHops()->empty() && !entry.namedNextHops().has_value()) {
        throw FbossError(
            "One of named NHG or NextHops must be specified for (BINDING|NODE)_MICRO_SID MySid type");
      }
      handled = true;
      break;
    case MySidType::DECAPSULATE_AND_LOOKUP:
      if (!entry.nextHops()->empty()) {
        throw FbossError(
            "NextHops are not supported for DECAPSULATE_AND_LOOKUP MySid type");
      }
      if (entry.namedNextHops().has_value()) {
        throw FbossError(
            "NamedNextHops are not supported for DECAPSULATE_AND_LOOKUP MySid type");
      }
      handled = true;
      break;
  }
  if (!handled) {
    throw FbossError("Unsupported MySid type: ", static_cast<int>(type));
  }
  state::MySidFields fields;
  fields.type() = *entry.type();
  fields.mySid() = *entry.mySid();
  auto mySid = std::make_shared<MySid>(fields);
  mySid->setUnresolveNextHopsId(std::nullopt);
  mySid->setResolvedNextHopsId(std::nullopt);
  return mySid;
}

void validateMySidNextHops(
    MySidType type,
    const RouteNextHopEntry::NextHopSet& nextHops) {
  if (type == MySidType::BINDING_MICRO_SID) {
    for (const auto& nhop : nextHops) {
      if (nhop.srv6SegmentList().empty()) {
        throw FbossError(
            "All nexthops must have a SID list for BINDING_MICRO_SID MySid type");
      }
      if (!nhop.tunnelId().has_value() || nhop.tunnelId()->empty()) {
        throw FbossError(
            "All nexthops must have a tunnelId for BINDING_MICRO_SID MySid type");
      }
      if (!nhop.tunnelType().has_value() ||
          *nhop.tunnelType() != TunnelType::SRV6_ENCAP) {
        throw FbossError(
            "All nexthops must have tunnelType SRV6_ENCAP for BINDING_MICRO_SID MySid type");
      }
    }
  }
}

// Rebuild the unresolved-routes index from the given RIB.
template <typename AddressT>
void refreshUnresolvedIndex(
    const NetworkToRouteMap<AddressT>& ribMap,
    std::unordered_map<
        std::pair<AddressT, uint8_t>,
        std::shared_ptr<Route<AddressT>>>* index) {
  std::unordered_map<
      std::pair<AddressT, uint8_t>,
      std::shared_ptr<Route<AddressT>>>
      next;
  for (const auto& node : ribMap) {
    const auto& route = node.value();
    if (route->isResolved()) {
      continue;
    }
    auto key =
        std::make_pair(route->prefix().network(), route->prefix().mask());
    auto it = index->find(key);
    if (it != index->end() && it->second.get() == route.get()) {
      next.emplace(std::move(key), it->second);
    } else {
      route->publish();
      next.emplace(std::move(key), route);
    }
  }
  *index = std::move(next);
}

void refreshUnresolvedMplsIndex(
    const LabelToRouteMap& ribMap,
    std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>* index) {
  std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>> next;
  for (const auto& [labelId, route] : ribMap) {
    if (route->isResolved()) {
      continue;
    }
    auto it = index->find(labelId);
    if (it != index->end() && it->second.get() == route.get()) {
      next.emplace(labelId, it->second);
    } else {
      route->publish();
      next.emplace(labelId, route);
    }
  }
  *index = std::move(next);
}

// Rewrites one optional NextHopSetID through `remap`; returns true if changed.
bool remapNextHopSetId(
    std::optional<NextHopSetID>& id,
    const std::unordered_map<NextHopSetID, NextHopSetID>& remap) {
  if (!id) {
    return false;
  }
  auto it = remap.find(*id);
  if (it == remap.end()) {
    return false;
  }
  id = it->second;
  return true;
}

// Repoints every NextHopSetID a route carries (resolved fwd + per-client
// entries) onto its primary id. Returns the rewritten route, or the original
// shared_ptr if nothing changed. Refcounts are untouched: reconstruct already
// counted each reference on the primary set.
template <typename AddressT>
std::shared_ptr<Route<AddressT>> remapRouteNextHopSetIds(
    const std::shared_ptr<Route<AddressT>>& route,
    const std::unordered_map<NextHopSetID, NextHopSetID>& remap) {
  auto resolvedId = route->getForwardInfo().getResolvedNextHopSetID();
  auto normalizedId =
      route->getForwardInfo().getNormalizedResolvedNextHopSetID();
  bool fwdChanged = remapNextHopSetId(resolvedId, remap);
  fwdChanged |= remapNextHopSetId(normalizedId, remap);

  std::vector<std::pair<ClientID, RouteNextHopEntry>> rewrittenClients;
  for (const auto& [clientId, entry] :
       std::as_const(route->getEntryForClients())) {
    auto clientSetId = entry->getClientNextHopSetID();
    if (remapNextHopSetId(clientSetId, remap)) {
      RouteNextHopEntry newEntry(entry->toThrift());
      newEntry.setClientNextHopSetID(clientSetId);
      rewrittenClients.emplace_back(clientId, std::move(newEntry));
    }
  }

  if (!fwdChanged && rewrittenClients.empty()) {
    return route;
  }

  auto writable = route->isPublished() ? route->clone() : route;
  if (fwdChanged) {
    RouteNextHopEntry fwd(route->getForwardInfo().toThrift());
    fwd.setResolvedNextHopSetID(resolvedId);
    fwd.setNormalizedResolvedNextHopSetID(normalizedId);
    writable->setResolved(fwd);
  }
  for (auto& [clientId, newEntry] : rewrittenClients) {
    writable->update(clientId, newEntry);
  }
  writable->publish();
  return writable;
}

RouteNextHopSet resolveNhopFromRib(
    const VrfRouteTables& routeTables,
    const NextHopIDManager* manager,
    const NextHop& nh,
    uint32_t ecmpWidth) {
  if (nh.intfID().has_value()) {
    return {nh};
  }

  const auto& addr = nh.addr();
  RouteNextHopSet resolved;

  auto collectResolved = [&](auto* routeMap, const auto& nhAddr) {
    if (!routeMap) {
      return false;
    }
    auto it = routeMap->longestMatch(nhAddr, nhAddr.bitCount());
    if (it == routeMap->end()) {
      return false;
    }
    const auto& route = it->value();
    if (!route || !route->isResolved()) {
      return false;
    }

    const auto fwdNhops = getNormalizedNextHopsFromRib(
        manager, route->getForwardInfo(), ecmpWidth);
    // Only MySid and policy next hop group next hops resolve here, and they
    // need an egress interface in this level. Next hops resolved in the next
    // level have none; unicast routes resolve through them in RibRouteUpdater.
    if (fwdNhops.empty() ||
        std::any_of(fwdNhops.begin(), fwdNhops.end(), [](const auto& fwdNh) {
          return fwdNh.resolvedInNextLevel();
        })) {
      return false;
    }
    if (route->isConnected()) {
      resolved.insert(ResolvedNextHop(
          nh.addr(),
          fwdNhops.begin()->intf(),
          fwdNhops.begin()->weight(),
          nh.labelForwardingAction(),
          nh.disableTTLDecrement(),
          nh.topologyInfo(),
          nh.adjustedWeight(),
          nh.srv6SegmentList(),
          nh.tunnelType(),
          nh.tunnelId(),
          nh.cost(),
          nh.role(),
          std::nullopt, /* vrf */
          false, /* resolvedInNextLevel */
          nh.overlayDstMac()));
    } else {
      std::vector<ResolvedNextHop> nhops;
      nhops.reserve(fwdNhops.size());
      for (const auto& fwdNh : fwdNhops) {
        nhops.emplace_back(
            fwdNh.addr(),
            fwdNh.intf(),
            fwdNh.weight(),
            nh.labelForwardingAction(),
            nh.disableTTLDecrement(),
            nh.topologyInfo(),
            nh.adjustedWeight(),
            nh.srv6SegmentList(),
            nh.tunnelType(),
            nh.tunnelId(),
            nh.cost(),
            nh.role(),
            std::nullopt, /* vrf */
            false, /* resolvedInNextLevel */
            nh.overlayDstMac());
      }
      resolved.insert(nhops.begin(), nhops.end());
    }
    return true;
  };

  for (const auto& [v4Routes, v6Routes] : routeTables) {
    bool found = false;
    if (addr.isV4()) {
      found = collectResolved(v4Routes, addr.asV4());
    } else {
      found = collectResolved(v6Routes, addr.asV6());
    }
    if (found) {
      break;
    }
  }
  return resolved;
}

RibRouteTables::OverrideNhops overrideNhopsOf(const RouteNextHopEntry& entry) {
  return {entry.getOverrideNextHops(), entry.getOverrideNextHopSetID()};
}

} // namespace

template <typename AddressT, typename FibType, typename IndexT>
void reconstructRib(
    const std::shared_ptr<FibType>& fib,
    NetworkToRouteMap<AddressT>* addrToRoute,
    const IndexT& unresolvedIndex) {
  addrToRoute->clear();
  if constexpr (!std::is_same_v<FibType, MultiLabelForwardingInformationBase>) {
    for (const auto& iter : std::as_const(*fib)) {
      const auto& route = iter.second;
      addrToRoute->insert(route->prefix(), route);
    }
  } else {
    for (const auto& miter : std::as_const(*fib)) {
      for (const auto& iter : std::as_const(*miter.second)) {
        const auto& route = iter.second;
        addrToRoute->insert(route->prefix(), route);
      }
    }
  }
  for (const auto& [_, route] : unresolvedIndex) {
    addrToRoute->insert(route->prefix(), route);
  }
}

void reconstructMySidTableFromSwitchState(
    const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
    MySidTable* mySidTable) {
  mySidTable->clear();
  for (const auto& miter : std::as_const(*mySidMap)) {
    for (const auto& [key, mySid] : std::as_const(*miter.second)) {
      auto cidr = mySid->getMySid();
      folly::CIDRNetworkV6 cidrV6(cidr.first.asV6(), cidr.second);
      mySidTable->emplace(cidrV6, mySid);
    }
  }
}

std::string RibRouteTables::getPolicyDefaultNextHopGroup(
    const std::string& policyName) const {
  return synchronizedRouteTables_.withRLock(
      [&](const auto& routeTables) -> std::string {
        if (!routeTables.nextHopIDManager) {
          throw FbossError("NextHopIDManager not initialized");
        }
        const auto* policy =
            routeTables.nextHopIDManager->getPolicy(policyName);
        if (!policy) {
          throw FbossError(
              "Class-based policy '", policyName, "' does not exist");
        }
        return policy->defaultNexthopGroup;
      });
}

template <typename RibUpdateFn>
void RibRouteTables::updateRib(RouterID vrf, const RibUpdateFn& updateRibFn) {
  auto lockedRouteTables = synchronizedRouteTables_.wlock();
  auto it = lockedRouteTables->routerIDToRouteTable.find(vrf);
  if (it == lockedRouteTables->routerIDToRouteTable.end()) {
    throw FbossError("VRF ", vrf, " not configured");
  }
  auto& routeTable = it->second;
  updateRibFn(
      routeTable,
      &lockedRouteTables->mySidTable,
      lockedRouteTables->nextHopIDManager.get(),
      lockedRouteTables->ecmpWidth,
      lockedRouteTables->routerIDToRouteTable);
  if (lockedRouteTables->nextHopIDManager) {
    auto* nhIdManager = lockedRouteTables->nextHopIDManager.get();
    const bool hasMySids = !lockedRouteTables->mySidTable.empty();
    const bool hasPolicies = !nhIdManager->getPbrPolicyToNamedNhg().empty();
    if (hasMySids || hasPolicies) {
      auto routeTables =
          makeVrfRouteTables(lockedRouteTables->routerIDToRouteTable);
      if (hasMySids) {
        RibMySidUpdater mySidUpdater(
            routeTables,
            nhIdManager,
            &lockedRouteTables->mySidTable,
            lockedRouteTables->ecmpWidth);
        mySidUpdater.resolve();
      }
      if (hasPolicies) {
        resolveNamedNhgs(
            routeTables, nhIdManager, lockedRouteTables->ecmpWidth);
      }
    }
  }
}

void RibRouteTables::resolveDependentVrfs(
    RouterID changedVrf,
    RouterIDToRouteTable& routeTables,
    NextHopIDManager* nextHopIDManager,
    MySidTable* mySidTable,
    uint32_t ecmpWidth,
    std::size_t* cyclesDetectedOut,
    std::vector<RouterID>& vrfsToProgram) {
  // TODO: next hops are expected to point only into the default VRF; enforce
  // it, or re-resolve the VRFs depending on any changed VRF
  if (changedVrf != RouterID(0)) {
    return;
  }
  for (auto& [vrf, routeTable] : routeTables) {
    if (vrf == RouterID(0)) {
      continue;
    }
    RibRouteUpdater updater(
        &routeTable.v4NetworkToRoute,
        &routeTable.v6NetworkToRoute,
        &routeTable.labelToRoute,
        nextHopIDManager,
        mySidTable,
        ecmpWidth,
        vrf,
        routeTable.restrictedInterfaces(),
        &routeTables);
    updater.update({}, {}, std::set<ClientID>{});
    if (cyclesDetectedOut) {
      *cyclesDetectedOut += updater.cyclesDetected();
    }
    vrfsToProgram.push_back(vrf);
  }
}

template <typename RibUpdateFn>
void RibRouteTables::updateRibMySids(const RibUpdateFn& updateRibFn) {
  auto lockedRouteTables = synchronizedRouteTables_.wlock();
  auto routeTables =
      makeVrfRouteTables(lockedRouteTables->routerIDToRouteTable);
  updateRibFn(
      routeTables,
      &lockedRouteTables->mySidTable,
      lockedRouteTables->nextHopIDManager.get(),
      lockedRouteTables->ecmpWidth);
}

void RibRouteTables::reconfigure(
    const SwitchIdScopeResolver* resolver,
    const RouterIDAndNetworkToInterfaceRoutes& configRouterIDToInterfaceRoutes,
    const std::vector<cfg::StaticRouteWithNextHops>& staticRoutesWithNextHops,
    const std::vector<cfg::StaticRouteNoNextHops>& staticRoutesToNull,
    const std::vector<cfg::StaticRouteNoNextHops>& staticRoutesToCpu,
    const std::vector<cfg::StaticIp2MplsRoute>& staticIp2MplsRoutes,
    const std::vector<cfg::StaticMplsRouteWithNextHops>&
        staticMplsRoutesWithNextHops,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToNull,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToCpu,
    const std::vector<MySidWithNextHops>& staticMySids,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    void* cookie,
    bool ecmpModeChanged,
    const std::optional<InterfaceToVrf>& interfaceToVrf) {
  // Config application is accomplished in the following sequence of steps:
  // 1. Update the VRFs held in RoutingInformationBase's
  // SynchronizedRouteTables data-structure
  //
  // For each VRF specified in config:
  //
  // 2. Update all of RIB's static routes to be only those specified in
  // config
  //
  // 3. Update all of RIB's interface routes to be only those specified in
  // config
  //
  // 4. Re-resolve routes
  //
  // 5. Update FIB
  //
  // Steps 2-5 take place in ConfigApplier.

  std::vector<RouterID> existingVrfs = getVrfList();

  auto setVrfInterfaces = [&interfaceToVrf](auto& routerIDToRouteTable) {
    for (auto& [vrf, routeTable] : routerIDToRouteTable) {
      if (!interfaceToVrf) {
        routeTable.interfaces.reset();
        continue;
      }
      routeTable.interfaces.emplace();
      for (const auto& [intf, intfVrf] : *interfaceToVrf) {
        if (intfVrf == vrf) {
          routeTable.interfaces->insert(intf);
        }
      }
    }
  };
  setVrfInterfaces(synchronizedRouteTables_.wlock()->routerIDToRouteTable);

  auto configureRoutesForVrf =
      [&](RouterID vrf, const PrefixToInterfaceIDAndIP& interfaceRoutes) {
        // A ConfigApplier object should be independent of the VRF whose
        // routes it is processing. However, because interface and static
        // routes for _all_ VRFs are passed to ConfigApplier, the vrf
        // argument is needed to identify the subset of those routes which
        // should be processed.

        // ConfigApplier can be made independent of the VRF whose routes it
        // is processing by the use of boost::filter_iterator.
        updateRib(
            vrf,
            [&](auto& routeTable,
                auto* mySidTable,
                auto* nextHopIDManager,
                uint32_t ecmpWidth,
                RouterIDToRouteTable& routeTables) {
              ConfigApplier configApplier(
                  vrf,
                  &(routeTable.v4NetworkToRoute),
                  &(routeTable.v6NetworkToRoute),
                  &(routeTable.labelToRoute),
                  folly::range(
                      interfaceRoutes.cbegin(), interfaceRoutes.cend()),
                  folly::range(
                      staticRoutesToCpu.cbegin(), staticRoutesToCpu.cend()),
                  folly::range(
                      staticRoutesToNull.cbegin(), staticRoutesToNull.cend()),
                  folly::range(
                      staticRoutesWithNextHops.cbegin(),
                      staticRoutesWithNextHops.cend()),
                  folly::range(
                      staticIp2MplsRoutes.cbegin(), staticIp2MplsRoutes.cend()),
                  folly::range(
                      staticMplsRoutesWithNextHops.cbegin(),
                      staticMplsRoutesWithNextHops.cend()),
                  folly::range(
                      staticMplsRoutesToNull.cbegin(),
                      staticMplsRoutesToNull.cend()),
                  folly::range(
                      staticMplsRoutesToCpu.cbegin(),
                      staticMplsRoutesToCpu.cend()),
                  folly::range(staticMySids.cbegin(), staticMySids.cend()),
                  nextHopIDManager,
                  mySidTable,
                  ecmpWidth,
                  routeTable.restrictedInterfaces(),
                  &routeTables);
              // Apply config
              configApplier.apply();
            });
        updateFib(resolver, vrf, ribToSwitchStateFunc, cookie, ecmpModeChanged);
      };
  // Because of this sequential loop over each VRF, config application scales
  // linearly with the number of VRFs. If FBOSS is run in a multi-VRF routing
  // architecture in the future, this slow-down can be avoided by
  // parallelizing this loop. Converting this loop to use task-level
  // parallelism should be straightfoward because it has been written to avoid
  // dependencies across different iterations of the loop.
  for (const auto& vrf : existingVrfs) {
    // VRFs removed from config: drop routes from every client and remove the
    // VRF from the FIB
    if (configRouterIDToInterfaceRoutes.find(vrf) !=
        configRouterIDToInterfaceRoutes.end()) {
      continue;
    }
    if (vrf == RouterID(0)) {
      // The default VRF always exists, even without interface routes
      configureRoutesForVrf(vrf, {} /* No interface routes*/);
      continue;
    }
    updateRib(
        vrf,
        [vrf](
            auto& routeTable,
            auto* mySidTable,
            auto* nextHopIDManager,
            uint32_t ecmpWidth,
            auto& /*routeTables*/) {
          RibRouteUpdater updater(
              &routeTable.v4NetworkToRoute,
              &routeTable.v6NetworkToRoute,
              &routeTable.labelToRoute,
              nextHopIDManager,
              mySidTable,
              ecmpWidth,
              vrf);
          const auto& clients = apache::thrift::TEnumTraits<ClientID>::values;
          updater.update(
              {}, {}, std::set<ClientID>(clients.begin(), clients.end()));
        });
    updateFib(
        resolver,
        vrf,
        ribToSwitchStateFunc,
        cookie,
        ecmpModeChanged,
        true /* vrfDeleted */);
  }
  {
    auto lockedRouteTables = synchronizedRouteTables_.wlock();
    lockedRouteTables->routerIDToRouteTable = constructRouteTables(
        lockedRouteTables, configRouterIDToInterfaceRoutes);
    setVrfInterfaces(lockedRouteTables->routerIDToRouteTable);
  }
  for (auto& vrf : getVrfList()) {
    const auto& interfaceRoutes = configRouterIDToInterfaceRoutes.at(vrf);
    configureRoutesForVrf(vrf, interfaceRoutes);
  }
}

void RibRouteTables::updateRemoteInterfaceRoutes(
    const SwitchIdScopeResolver* resolver,
    const RouterIDAndNetworkToInterfaceRoutes& toAdd,
    const boost::container::flat_map<
        facebook::fboss::RouterID,
        std::vector<
            std::pair<folly::CIDRNetwork, facebook::fboss::InterfaceID>>>&
        toDel,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) {
  auto makeNhop = [](const auto& interfaceIDAndAddr) {
    auto interfaceID = interfaceIDAndAddr.first;
    auto address = interfaceIDAndAddr.second;
    ResolvedNextHop resolvedNextHop(address, interfaceID, UCMP_DEFAULT_WEIGHT);
    RouteNextHopEntry nextHop(
        static_cast<NextHop>(resolvedNextHop),
        AdminDistance::DIRECTLY_CONNECTED);
    return nextHop;
  };

  auto routeIntfMatches =
      [](const auto& routeTable,
         const folly::CIDRNetwork& network,
         InterfaceID intfID,
         const NextHopIDManager* nextHopIDManager) -> std::optional<bool> {
    auto check = [&intfID, nextHopIDManager](
                     const auto& rib, const auto& addr, uint8_t mask) {
      auto it = rib.exactMatch(addr, mask);
      if (it == rib.end()) {
        return std::optional<bool>{};
      }
      auto entry =
          it->value()->getEntryForClient(ClientID::REMOTE_INTERFACE_ROUTE);
      if (!entry) {
        return std::optional<bool>{};
      }
      // Per-client entry: resolve via clientNextHopSetID, not inline.
      const auto nhops = getClientNextHopsFromRib(nextHopIDManager, *entry);
      if (nhops.empty()) {
        return std::optional<bool>{};
      }
      return std::optional<bool>{nhops.begin()->intfID() == intfID};
    };
    return network.first.isV4() ? check(
                                      routeTable.v4NetworkToRoute,
                                      network.first.asV4().mask(network.second),
                                      network.second)
                                : check(
                                      routeTable.v6NetworkToRoute,
                                      network.first.asV6().mask(network.second),
                                      network.second);
  };

  for (auto& vrf : getVrfList()) {
    std::vector<RibRouteUpdater::RouteEntry> toAddRoutes;
    std::vector<folly::CIDRNetwork> toDelRoutes;
    const auto& toAddIter = toAdd.find(vrf);
    if (toAddIter != toAdd.end()) {
      for (const auto& [network, interfaceIDAndAddr] : toAddIter->second) {
        toAddRoutes.emplace_back(network, makeNhop(interfaceIDAndAddr));
      }
    }
    const auto& toDelIter = toDel.find(vrf);
    if (!toAddRoutes.empty() || toDelIter != toDel.end()) {
      std::vector<RouterID> vrfsToProgram{vrf};
      updateRib(
          vrf,
          [&](auto& routeTable,
              auto* mySidTable,
              auto* nextHopIDManager,
              uint32_t ecmpWidth,
              RouterIDToRouteTable& routeTables) {
            if (toDelIter != toDel.end()) {
              for (const auto& [network, intfID] : toDelIter->second) {
                // Remote interface route deletion is guarded by the
                // originating interface to avoid removing a prefix that was
                // already replaced by another remote interface update.
                auto intfMatches = routeIntfMatches(
                    routeTable, network, intfID, nextHopIDManager);
                if (!intfMatches.has_value()) {
                  continue;
                }
                if (*intfMatches) {
                  toDelRoutes.push_back(network);
                } else {
                  XLOG(ERR) << "Skipping remote interface route delete for "
                            << "interface " << intfID
                            << " because the existing route does not belong "
                               "to that interface";
                }
              }
            }
            if (toAddRoutes.empty() && toDelRoutes.empty()) {
              return;
            }
            RibRouteUpdater updater(
                &(routeTable.v4NetworkToRoute),
                &(routeTable.v6NetworkToRoute),
                &(routeTable.labelToRoute),
                nextHopIDManager,
                mySidTable,
                ecmpWidth,
                vrf,
                routeTable.restrictedInterfaces(),
                &routeTables);
            updater.update(
                {{ClientID::REMOTE_INTERFACE_ROUTE, toAddRoutes}},
                {{ClientID::REMOTE_INTERFACE_ROUTE, toDelRoutes}},
                {});
            resolveDependentVrfs(
                vrf,
                routeTables,
                nextHopIDManager,
                mySidTable,
                ecmpWidth,
                nullptr /* cyclesDetectedOut */,
                vrfsToProgram);
          });
      updateFib(resolver, vrfsToProgram, ribToSwitchStateFunc, cookie);
    }
  }
}

template <typename RouteType, typename RouteIdType>
void RibRouteTables::update(
    const SwitchIdScopeResolver* resolver,
    RouterID routerID,
    ClientID clientID,
    AdminDistance adminDistanceFromClientID,
    const std::vector<RouteType>& toAddRoutes,
    const std::vector<RouteIdType>& toDelPrefixes,
    bool resetClientsRoutes,
    folly::StringPiece updateType,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie,
    std::size_t* cyclesDetectedOut) {
  std::vector<RouterID> vrfsToProgram{routerID};
  updateRib(
      routerID,
      [&](auto& routeTable,
          auto* mySidTable,
          auto* nextHopIDManager,
          uint32_t ecmpWidth,
          RouterIDToRouteTable& routeTables) {
        auto resolvedRoutes = toAddRoutes;
        if constexpr (std::is_same_v<RouteType, RibRouteUpdater::RouteEntry>) {
          if (nextHopIDManager) {
            for (auto& routeEntry : resolvedRoutes) {
              auto nhgName = routeEntry.nhopEntry.getNamedNextHopGroup();
              if (!nhgName.has_value()) {
                continue;
              }
              auto nhopsOpt = nextHopIDManager->getNextHopsForName(*nhgName);
              if (!nhopsOpt.has_value()) {
                throw FbossError(
                    "Named next-hop group '", *nhgName, "' does not exist");
              }
              auto cleanupOldNhg = [&](const auto& existingRoute) {
                auto oldEntry = existingRoute->getEntryForClient(clientID);
                if (!oldEntry) {
                  return;
                }
                auto oldNhg = oldEntry->getNamedNextHopGroup();
                if (!oldNhg.has_value() || *oldNhg == *nhgName) {
                  return;
                }
                if (existingRoute->numClientsForNamedNhg(*oldNhg) <= 1) {
                  nextHopIDManager->removeRouteForNamedNhg(
                      *oldNhg, routerID, routeEntry.prefix);
                }
              };
              if (routeEntry.prefix.first.isV4()) {
                auto it = routeTable.v4NetworkToRoute.exactMatch(
                    routeEntry.prefix.first.asV4(), routeEntry.prefix.second);
                if (it != routeTable.v4NetworkToRoute.end()) {
                  cleanupOldNhg(it->value());
                }
              } else {
                auto it = routeTable.v6NetworkToRoute.exactMatch(
                    routeEntry.prefix.first.asV6(), routeEntry.prefix.second);
                if (it != routeTable.v6NetworkToRoute.end()) {
                  cleanupOldNhg(it->value());
                }
              }

              routeEntry.nhopEntry.setNextHops(*nhopsOpt);
              nextHopIDManager->addRouteForNamedNhg(
                  *nhgName, routerID, routeEntry.prefix);
            }
          }
        }
        RibRouteUpdater updater(
            &(routeTable.v4NetworkToRoute),
            &(routeTable.v6NetworkToRoute),
            &(routeTable.labelToRoute),
            nextHopIDManager,
            mySidTable,
            ecmpWidth,
            routerID,
            routeTable.restrictedInterfaces(),
            &routeTables);
        updater.update(
            clientID, resolvedRoutes, toDelPrefixes, resetClientsRoutes);
        if (cyclesDetectedOut) {
          *cyclesDetectedOut += updater.cyclesDetected();
        }
        resolveDependentVrfs(
            routerID,
            routeTables,
            nextHopIDManager,
            mySidTable,
            ecmpWidth,
            cyclesDetectedOut,
            vrfsToProgram);
      });
  updateFib(resolver, vrfsToProgram, ribToSwitchStateFunc, cookie);
}

void RibRouteTables::updateFib(
    const SwitchIdScopeResolver* resolver,
    RouterID vrf,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie,
    bool ecmpModeChanged,
    bool vrfDeleted) {
  updateFib(
      resolver,
      std::vector<VrfFibUpdate>{{vrf, vrfDeleted}},
      ribToSwitchStateFunc,
      cookie,
      ecmpModeChanged);
}

void RibRouteTables::updateFib(
    const SwitchIdScopeResolver* resolver,
    const std::vector<RouterID>& vrfs,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie,
    bool ecmpModeChanged) {
  std::vector<VrfFibUpdate> vrfUpdates;
  vrfUpdates.reserve(vrfs.size());
  for (const auto& vrf : vrfs) {
    vrfUpdates.push_back({vrf});
  }
  updateFib(
      resolver, vrfUpdates, ribToSwitchStateFunc, cookie, ecmpModeChanged);
}

void RibRouteTables::updateFib(
    const SwitchIdScopeResolver* resolver,
    const std::vector<VrfFibUpdate>& vrfUpdates,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie,
    bool ecmpModeChanged) {
  std::vector<RouterID> vrfs;
  vrfs.reserve(vrfUpdates.size());
  for (const auto& vrfUpdate : vrfUpdates) {
    vrfs.push_back(vrfUpdate.vrf);
  }
  std::optional<StateDelta> fibDelta;
  try {
    auto lockedRouteTables = synchronizedRouteTables_.rlock();
    std::vector<RibVrfFib> vrfFibs;
    vrfFibs.reserve(vrfUpdates.size());
    for (const auto& vrfUpdate : vrfUpdates) {
      const auto& routeTable =
          lockedRouteTables->routerIDToRouteTable.find(vrfUpdate.vrf)->second;
      vrfFibs.push_back(
          RibVrfFib{
              vrfUpdate.vrf,
              &routeTable.v4NetworkToRoute,
              &routeTable.v6NetworkToRoute,
              &routeTable.labelToRoute,
              vrfUpdate.deleted});
    }
    auto gotDelta = ribToSwitchStateFunc(
        resolver,
        vrfFibs,
        lockedRouteTables->nextHopIDManager.get(),
        lockedRouteTables->mySidTable,
        cookie);
    std::optional<StateDelta> tmp(
        StateDelta(gotDelta.oldState(), gotDelta.newState()));
    fibDelta.swap(tmp);
  } catch (const FbossHwUpdateError& hwUpdateError) {
    {
      SCOPE_FAIL {
        XLOG(FATAL) << " RIB Rollback failed, aborting program";
      };
      auto lockedRouteTables = synchronizedRouteTables_.wlock();
      for (const auto& vrf : vrfs) {
        auto fib =
            hwUpdateError.appliedState->getFibsInfoMap()->getFibContainer(vrf);
        auto& routeTable =
            lockedRouteTables->routerIDToRouteTable.find(vrf)->second;
        reconstructRib(
            fib->getFibV4(),
            &routeTable.v4NetworkToRoute,
            routeTable.unresolvedV4Routes);
        reconstructRib(
            fib->getFibV6(),
            &routeTable.v6NetworkToRoute,
            routeTable.unresolvedV6Routes);
        // The label FIB is global and belongs to the default VRF
        if (FLAGS_mpls_rib && vrf == RouterID(0)) {
          auto labelFib =
              hwUpdateError.appliedState->getLabelForwardingInformationBase();
          reconstructRib(
              labelFib,
              &routeTable.labelToRoute,
              routeTable.unresolvedMplsRoutes);
        }
      }

      // Reconstruct NextHopIDManager from the applied state's FIB and MySid
      // table
      if (lockedRouteTables->nextHopIDManager) {
        lockedRouteTables->nextHopIDManager->reconstructFromSwitchStateMaps(
            hwUpdateError.appliedState->getFibsInfoMap(),
            hwUpdateError.appliedState->getMySids(),
            hwUpdateError.appliedState->getLabelForwardingInformationBase(),
            this,
            hwUpdateError.appliedState->getClassBasedPolicies());
      }

      // Reconstruct MySidTable from the applied state
      reconstructMySidTableFromSwitchState(
          hwUpdateError.appliedState->getMySids(),
          &lockedRouteTables->mySidTable);
    }
    throw;
  }
  // Refresh unresolved-routes index from the post-update RIB.
  // Consumed by the rollback path (wired in the next diff).
  {
    auto lockedRouteTables = synchronizedRouteTables_.wlock();
    for (const auto& vrf : vrfs) {
      auto& routeTable =
          lockedRouteTables->routerIDToRouteTable.find(vrf)->second;
      refreshUnresolvedIndex(
          routeTable.v4NetworkToRoute, &routeTable.unresolvedV4Routes);
      refreshUnresolvedIndex(
          routeTable.v6NetworkToRoute, &routeTable.unresolvedV6Routes);
      if (FLAGS_mpls_rib) {
        refreshUnresolvedMplsIndex(
            routeTable.labelToRoute, &routeTable.unresolvedMplsRoutes);
      }
    }
  }
  CHECK(fibDelta.has_value());
  if (ecmpModeChanged) {
    // The config's own state update re-moded overrides before this fib update,
    // so fibDelta does not carry that change. Sync from all routes instead.
    updateEcmpOverrides(
        vrfs,
        StateDelta(std::make_shared<SwitchState>(), fibDelta->newState()));
  } else {
    updateEcmpOverrides(vrfs, *fibDelta);
  }
}

void RibRouteTables::updateFib(
    const SwitchIdScopeResolver* resolver,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  try {
    auto lockedRouteTables = synchronizedRouteTables_.rlock();
    ribMySidToSwitchStateFunc(
        resolver,
        lockedRouteTables->nextHopIDManager.get(),
        lockedRouteTables->mySidTable,
        cookie);
  } catch (const FbossHwUpdateError& hwUpdateError) {
    {
      SCOPE_FAIL {
        XLOG(FATAL) << " RIB Rollback failed, aborting program";
      };
      auto lockedRouteTables = synchronizedRouteTables_.wlock();
      // Reconstruct MySidTable from the applied state
      reconstructMySidTableFromSwitchState(
          hwUpdateError.appliedState->getMySids(),
          &lockedRouteTables->mySidTable);
      // Reconstruct NextHopIDManager to match the rolled-back MySid table
      if (lockedRouteTables->nextHopIDManager) {
        lockedRouteTables->nextHopIDManager->reconstructFromSwitchStateMaps(
            hwUpdateError.appliedState->getFibsInfoMap(),
            hwUpdateError.appliedState->getMySids(),
            hwUpdateError.appliedState->getLabelForwardingInformationBase(),
            this,
            hwUpdateError.appliedState->getClassBasedPolicies());
      }
    }
    throw;
  }
}

void RibRouteTables::updateEcmpOverrides(
    const std::vector<RouterID>& vrfs,
    const StateDelta& delta) {
  if (!FLAGS_enable_ecmp_resource_manager) {
    return;
  }
  std::unordered_map<
      RouterID,
      std::unordered_map<folly::CIDRNetwork, std::optional<cfg::SwitchingMode>>>
      rid2prefix2SwitchingMode;
  std::unordered_map<
      RouterID,
      std::unordered_map<folly::CIDRNetwork, OverrideNhops>>
      rid2prefix2Nhops;

  forEachChangedRoute(
      delta,
      [&rid2prefix2SwitchingMode, &rid2prefix2Nhops](
          RouterID rid, const auto& oldRoute, const auto& newRoute) {
        if (!newRoute->isResolved()) {
          return;
        }
        if (!oldRoute->isResolved()) {
          if (newRoute->getForwardInfo()
                  .getOverrideEcmpSwitchingMode()
                  .has_value()) {
            rid2prefix2SwitchingMode[rid][newRoute->prefix().toCidrNetwork()] =
                newRoute->getForwardInfo().getOverrideEcmpSwitchingMode();
          }
          if (newRoute->getForwardInfo().getOverrideNextHops().has_value() ||
              newRoute->getForwardInfo()
                  .getOverrideNextHopSetID()
                  .has_value()) {
            rid2prefix2Nhops[rid][newRoute->prefix().toCidrNetwork()] =
                overrideNhopsOf(newRoute->getForwardInfo());
          }
        } else {
          // both are resolved
          if (oldRoute->getForwardInfo().getOverrideEcmpSwitchingMode() !=
              newRoute->getForwardInfo().getOverrideEcmpSwitchingMode()) {
            rid2prefix2SwitchingMode[rid][newRoute->prefix().toCidrNetwork()] =
                newRoute->getForwardInfo().getOverrideEcmpSwitchingMode();
          }
          if (oldRoute->getForwardInfo().getOverrideNextHops() !=
                  newRoute->getForwardInfo().getOverrideNextHops() ||
              oldRoute->getForwardInfo().getOverrideNextHopSetID() !=
                  newRoute->getForwardInfo().getOverrideNextHopSetID()) {
            rid2prefix2Nhops[rid][newRoute->prefix().toCidrNetwork()] =
                overrideNhopsOf(newRoute->getForwardInfo());
          }
        }
      },
      [&rid2prefix2SwitchingMode, &rid2prefix2Nhops](
          RouterID rid, const auto& newRoute) {
        // For new routes, just copy the override info into RIB. Alternatively
        // we could could check the overrides being set and only set for
        // routes which have overrides set. We take a more blanket approach
        // as there are call sites  (e.g. config reload, cancelled update) where
        // we create a delta of (emptyState, newState). While none of these are
        // expected to clear overrides and just looking for overrides being
        // set would be sufficient. We take a more defensive approach of just
        // mimicking overrides (set or unset) of new routes.
        if (newRoute->isResolved()) {
          rid2prefix2SwitchingMode[rid][newRoute->prefix().toCidrNetwork()] =
              newRoute->getForwardInfo().getOverrideEcmpSwitchingMode();
          rid2prefix2Nhops[rid][newRoute->prefix().toCidrNetwork()] =
              overrideNhopsOf(newRoute->getForwardInfo());
        }
      },
      [&rid2prefix2SwitchingMode](RouterID /*rid*/, const auto& /*oldRoute*/) {
      });

  for (const auto& vrf : vrfs) {
    auto rmitr = rid2prefix2SwitchingMode.find(vrf);
    if (rmitr != rid2prefix2SwitchingMode.end()) {
      setOverrideEcmpMode(vrf, rmitr->second);
    }
    auto rnitr = rid2prefix2Nhops.find(vrf);
    if (rnitr != rid2prefix2Nhops.end()) {
      setOverrideEcmpNhops(vrf, rnitr->second);
    }
  }
}

void RibRouteTables::ensureVrf(RouterID rid) {
  auto lockedRouteTables = synchronizedRouteTables_.wlock();
  if (lockedRouteTables->routerIDToRouteTable.find(rid) ==
      lockedRouteTables->routerIDToRouteTable.end()) {
    lockedRouteTables->routerIDToRouteTable.insert(
        std::make_pair(rid, VrfRouteTable()));
  }
}

std::vector<RouterID> RibRouteTables::getVrfList() const {
  auto lockedRouteTables = synchronizedRouteTables_.rlock();
  std::vector<RouterID> res;
  for (const auto& entry : lockedRouteTables->routerIDToRouteTable) {
    res.push_back(entry.first);
  }
  return res;
}

void RibRouteTables::setClassID(
    const SwitchIdScopeResolver* resolver,
    RouterID rid,
    const std::vector<folly::CIDRNetwork>& prefixes,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    std::optional<cfg::AclLookupClass> classId,
    void* cookie) {
  updateRib(
      rid,
      [&](auto& routeTable,
          auto* /*mySidTable*/,
          auto* /*nextHopIDManager*/,
          uint32_t /*ecmpWidth*/,
          auto& /*routeTables*/) {
        // Update rib
        auto updateRoute = [&classId](auto& rib, auto ip, uint8_t mask) {
          auto ritr = rib.exactMatch(ip, mask);
          if (ritr == rib.end() || ritr->value()->getClassID() == classId) {
            return;
          }
          ritr->value() = ritr->value()->clone();
          ritr->value()->updateClassID(classId);
          ritr->value()->publish();
        };
        auto& v4Rib = routeTable.v4NetworkToRoute;
        auto& v6Rib = routeTable.v6NetworkToRoute;
        for (auto& prefix : prefixes) {
          if (prefix.first.isV4()) {
            updateRoute(v4Rib, prefix.first.asV4(), prefix.second);
          } else {
            updateRoute(v6Rib, prefix.first.asV6(), prefix.second);
          }
        }
      });
  updateFib(resolver, rid, ribToSwitchStateFunc, cookie);
}

void RibRouteTables::setOverrideEcmpMode(
    RouterID rid,
    const std::unordered_map<
        folly::CIDRNetwork,
        std::optional<cfg::SwitchingMode>>& prefix2EcmpMode) {
  updateRib(
      rid,
      [&](auto& routeTable,
          auto* /*mySidTable*/,
          auto* /*nextHopIDManager*/,
          uint32_t /*ecmpWidth*/,
          auto& /*routeTables*/) {
        // Update rib
        auto updateRoute =
            [](auto& rib,
               auto ip,
               uint8_t mask,
               std::optional<cfg::SwitchingMode> overrideEcmpMode) {
              auto ritr = rib.exactMatch(ip, mask);
              if (ritr == rib.end()) {
                return;
              }
              auto& ribRoute = ritr->value();
              if (!ribRoute->isResolved() ||
                  ribRoute->getForwardInfo().getOverrideEcmpSwitchingMode() ==
                      overrideEcmpMode) {
                return;
              }
              ritr->value() = ribRoute->clone();
              const auto& curForwardInfo = ritr->value()->getForwardInfo();
              // Copy and adjust rather than rebuild from the nexthop set:
              // under drop_inline_nexthops_with_id that set is empty and the
              // ctor rejects it.
              RouteNextHopEntry newForwardInfo(curForwardInfo.toThrift());
              newForwardInfo.setOverrideEcmpSwitchingMode(overrideEcmpMode);
              ritr->value()->setResolved(newForwardInfo);
              ritr->value()->publish();
            };
        auto& v4Rib = routeTable.v4NetworkToRoute;
        auto& v6Rib = routeTable.v6NetworkToRoute;
        for (const auto& [prefix, ecmpMode] : prefix2EcmpMode) {
          if (prefix.first.isV4()) {
            updateRoute(v4Rib, prefix.first.asV4(), prefix.second, ecmpMode);
          } else {
            updateRoute(v6Rib, prefix.first.asV6(), prefix.second, ecmpMode);
          }
        }
      });
}

void RibRouteTables::setOverrideEcmpNhops(
    RouterID rid,
    const std::unordered_map<folly::CIDRNetwork, OverrideNhops>& prefix2Nhops) {
  updateRib(
      rid,
      [&](auto& routeTable,
          auto* /*mySidTable*/,
          auto* /*nextHopIDManager*/,
          uint32_t /*ecmpWidth*/,
          auto& /*routeTables*/) {
        // Update rib
        auto updateRoute =
            [](auto& rib,
               auto ip,
               uint8_t mask,
               const RibRouteTables::OverrideNhops& overrideNhops) {
              auto ritr = rib.exactMatch(ip, mask);
              if (ritr == rib.end()) {
                return;
              }
              auto& ribRoute = ritr->value();
              if (!ribRoute->isResolved() ||
                  (ribRoute->getForwardInfo().getOverrideNextHops() ==
                       overrideNhops.nhops &&
                   ribRoute->getForwardInfo().getOverrideNextHopSetID() ==
                       overrideNhops.setId)) {
                return;
              }
              ritr->value() = ribRoute->clone();
              const auto& curForwardInfo = ritr->value()->getForwardInfo();
              // See setOverrideEcmpSwitchingMode: copy and adjust.
              RouteNextHopEntry newForwardInfo(curForwardInfo.toThrift());
              newForwardInfo.setOverrideNextHops(overrideNhops.nhops);
              newForwardInfo.setOverrideNextHopSetID(overrideNhops.setId);
              ritr->value()->setResolved(newForwardInfo);
              ritr->value()->publish();
            };
        auto& v4Rib = routeTable.v4NetworkToRoute;
        auto& v6Rib = routeTable.v6NetworkToRoute;
        for (const auto& [prefix, nhops] : prefix2Nhops) {
          if (prefix.first.isV4()) {
            updateRoute(v4Rib, prefix.first.asV4(), prefix.second, nhops);
          } else {
            updateRoute(v6Rib, prefix.first.asV6(), prefix.second, nhops);
          }
        }
      });
}

template <typename AddressT>
std::shared_ptr<Route<AddressT>> RibRouteTables::longestMatch(
    const AddressT& address,
    RouterID vrf) const {
  StopWatch lookupTimer(std::nullopt, false);
  auto ribTables = synchronizedRouteTables_.rlock();
  auto vrfIt = ribTables->routerIDToRouteTable.find(vrf);
  auto rt = vrfIt == ribTables->routerIDToRouteTable.end()
      ? nullptr
      : vrfIt->second.longestMatch(address);
  if (lookupTimer.msecsElapsed().count() > 1000) {
    XLOG(WARNING) << " Lookup for : " << address
                  << " took: " << lookupTimer.msecsElapsed().count() << " ms ";
  }
  return rt;
}

RibRouteTables::RouterIDToRouteTable RibRouteTables::constructRouteTables(
    const SynchronizedRouteTables::WLockedPtr& lockedRouteTables,
    const RouterIDAndNetworkToInterfaceRoutes& configRouterIDToInterfaceRoutes)
    const {
  RouterIDToRouteTable newRouteTables;

  RouterIDToRouteTable::iterator newRouteTablesIter;
  for (const auto& routerIDAndInterfaceRoutes :
       configRouterIDToInterfaceRoutes) {
    const RouterID configVrf = routerIDAndInterfaceRoutes.first;

    newRouteTablesIter = newRouteTables.emplace_hint(
        newRouteTables.cend(), configVrf, VrfRouteTable());

    auto oldRouteTablesIter =
        lockedRouteTables->routerIDToRouteTable.find(configVrf);
    if (oldRouteTablesIter == lockedRouteTables->routerIDToRouteTable.end()) {
      // configVrf did not exist in the RIB, so it has been added to
      // newRouteTables with an empty set of routes
      continue;
    }

    // configVrf exists in the RIB, so it will be shallow copied into
    // newRouteTables.
    newRouteTablesIter->second = std::move(oldRouteTablesIter->second);
  }

  return newRouteTables;
}

RoutingInformationBase::RoutingInformationBase() {
  CHECK(
      !FLAGS_drop_inline_nexthops_with_id ||
      (FLAGS_enable_nexthop_id_manager && FLAGS_resolve_nexthops_from_id))
      << "drop_inline_nexthops_with_id requires enable_nexthop_id_manager and "
         "resolve_nexthops_from_id";
  XLOG(INFO) << "[NextHop ID Manager] flags:"
             << " enable_nexthop_id_manager="
             << (FLAGS_enable_nexthop_id_manager ? "enabled" : "disabled")
             << " resolve_nexthops_from_id="
             << (FLAGS_resolve_nexthops_from_id ? "enabled" : "disabled")
             << " verify_fib_nexthop_id_consistency="
             << (FLAGS_verify_fib_nexthop_id_consistency ? "enabled"
                                                         : "disabled")
             << " drop_inline_nexthops_with_id="
             << (FLAGS_drop_inline_nexthops_with_id ? "enabled" : "disabled");
  ribUpdateThread_ = std::make_unique<std::thread>([this] {
    initThread("ribUpdateThread");
    ribUpdateEventBase_.loopForever();
  });
}

RoutingInformationBase::~RoutingInformationBase() {
  stop();
}

void RoutingInformationBase::stop() {
  if (ribUpdateThread_) {
    ribUpdateEventBase_.runInFbossEventBaseThread(
        [this] { ribUpdateEventBase_.terminateLoopSoon(); });
    ribUpdateThread_->join();
    ribUpdateThread_.reset();
  }
}

void RoutingInformationBase::ensureRunning() const {
  if (!ribUpdateThread_) {
    throw FbossError(
        "RIB thread is not yet running or is in the process of exiting");
  }
}

void RoutingInformationBase::reconfigure(
    const SwitchIdScopeResolver* resolver,
    const RouterIDAndNetworkToInterfaceRoutes& configRouterIDToInterfaceRoutes,
    const std::vector<cfg::StaticRouteWithNextHops>& staticRoutesWithNextHops,
    const std::vector<cfg::StaticRouteNoNextHops>& staticRoutesToNull,
    const std::vector<cfg::StaticRouteNoNextHops>& staticRoutesToCpu,
    const std::vector<cfg::StaticIp2MplsRoute>& staticIp2MplsRoutes,
    const std::vector<cfg::StaticMplsRouteWithNextHops>&
        staticMplsRoutesWithNextHops,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToNull,
    const std::vector<cfg::StaticMplsRouteNoNextHops>& staticMplsRoutesToCpu,
    const std::vector<MySidWithNextHops>& staticMySids,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    void* cookie,
    bool ecmpModeChanged,
    const std::optional<InterfaceToVrf>& interfaceToVrf) {
  ensureRunning();
  auto updateFn = [&] {
    ribTables_.reconfigure(
        resolver,
        configRouterIDToInterfaceRoutes,
        staticRoutesWithNextHops,
        staticRoutesToNull,
        staticRoutesToCpu,
        staticIp2MplsRoutes,
        staticMplsRoutesWithNextHops,
        staticMplsRoutesToNull,
        staticMplsRoutesToCpu,
        staticMySids,
        ribToSwitchStateFunc,
        cookie,
        ecmpModeChanged,
        interfaceToVrf);
  };
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(updateFn);
}

void RoutingInformationBase::updateRemoteInterfaceRoutes(
    const SwitchIdScopeResolver* resolver,
    const RouterIDAndNetworkToInterfaceRoutes& toAdd,
    const boost::container::flat_map<
        facebook::fboss::RouterID,
        std::vector<
            std::pair<folly::CIDRNetwork, facebook::fboss::InterfaceID>>>&
        toDel,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) {
  ribTables_.updateRemoteInterfaceRoutes(
      resolver, toAdd, toDel, ribToSwitchStateFunc, cookie);
}

template <typename TraitsType>
RoutingInformationBase::UpdateStatistics RoutingInformationBase::updateImpl(
    const SwitchIdScopeResolver* resolver,
    RouterID routerID,
    ClientID clientID,
    AdminDistance adminDistanceFromClientID,
    const std::vector<typename TraitsType::ThriftRoute>& toAdd,
    const std::vector<typename TraitsType::ThriftRouteId>& toDelete,
    bool resetClientsRoutes,
    folly::StringPiece updateType,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    void* cookie) {
  ensureRunning();
  UpdateStatistics stats;
  std::chrono::microseconds duration;
  std::shared_ptr<SwitchState> appliedState;
  Timer updateTimer(&duration);
  std::exception_ptr updateException;
  auto updateFn = [&]() {
    std::vector<typename TraitsType::RibRoute> toAddRoutes;
    toAddRoutes.reserve(toAdd.size());

    try {
      std::for_each(
          toAdd.begin(),
          toAdd.end(),
          [adminDistanceFromClientID, &stats, &toAddRoutes, this](
              const auto& route) {
            toAddRoutes.push_back(
                TraitsType::ToAddFn(
                    route, adminDistanceFromClientID, stats, ribTables_));
          });
      std::vector<typename TraitsType::RibRouteId> toDelPrefixes;
      toDelPrefixes.reserve(toDelete.size());
      std::for_each(
          toDelete.begin(),
          toDelete.end(),
          [&stats, &toDelPrefixes](const auto& prefix) {
            toDelPrefixes.push_back(TraitsType::ToDelFn(prefix, stats));
          });

      ribTables_.update(
          resolver,
          routerID,
          clientID,
          adminDistanceFromClientID,
          toAddRoutes,
          toDelPrefixes,
          resetClientsRoutes,
          updateType,
          ribToSwitchStateFunc,
          cookie,
          &stats.resolutionCyclesDetected);
    } catch (const std::exception&) {
      updateException = std::current_exception();
    }
  };
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(updateFn);
  if (updateException) {
    std::rethrow_exception(updateException);
  }
  stats.duration = duration;
  return stats;
}

void RoutingInformationBase::setClassIDImpl(
    const SwitchIdScopeResolver* resolver,
    RouterID rid,
    const std::vector<folly::CIDRNetwork>& prefixes,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    std::optional<cfg::AclLookupClass> classId,
    void* cookie,
    bool async) {
  ensureRunning();
  auto updateFn = [=, this]() {
    ribTables_.setClassID(
        resolver, rid, prefixes, ribToSwitchStateFunc, classId, cookie);
  };
  if (async) {
    ribUpdateEventBase_.runInFbossEventBaseThread(updateFn);
  } else {
    ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(updateFn);
  }
}

bool RibRouteTables::backfillNextHopIds(
    const SynchronizedRouteTables::WLockedPtr& lockedRouteTables) {
  if (!lockedRouteTables->nextHopIDManager) {
    return false;
  }
  bool backfilled{false};
  auto& manager = *lockedRouteTables->nextHopIDManager;
  const uint32_t ecmpWidth = lockedRouteTables->ecmpWidth;

  auto backfillOneRoute = [&manager, ecmpWidth, &backfilled](auto& route) {
    // Per-client: snapshot updates first (route->update rebuilds the
    // nexthopsmulti map and invalidates iterators).
    std::vector<std::pair<ClientID, std::shared_ptr<RouteNextHopEntry>>>
        clientUpdates;
    for (const auto& [clientId, entry] :
         std::as_const(route->getEntryForClients())) {
      if (entry->getClientNextHopSetID().has_value()) {
        continue; // already has an ID
      }
      const auto& nhopSet = entry->getNextHopSet();
      if (nhopSet.empty()) {
        continue; // DROP / TO_CPU; no nexthops to track
      }
      auto allocResult = manager.getOrAllocRouteNextHopSetID(nhopSet);
      std::optional<NextHopSetID> newId(
          allocResult.nextHopIdSetIter->second.id);
      auto newEntry = entry->clone();
      newEntry->setClientNextHopSetID(newId);
      XLOG(DBG3) << "[NextHop ID Manager] backfilling clientNextHopSetID="
                 << *newId << " for client=" << static_cast<int>(clientId)
                 << " route=" << route->str();
      clientUpdates.emplace_back(clientId, std::move(newEntry));
    }

    // Resolved-side: stamp on the fwd info if it has nexthops.
    const auto& fwd = route->getForwardInfo();
    auto fwdNexthops = fwd.getNextHopSet();
    std::shared_ptr<RouteNextHopEntry> fwdReplacement;
    if (!fwdNexthops.empty()) {
      std::optional<NextHopSetID> newResolvedId = fwd.getResolvedNextHopSetID();
      std::optional<NextHopSetID> newNormalizedId =
          fwd.getNormalizedResolvedNextHopSetID();
      if (!newResolvedId.has_value()) {
        auto allocResult = manager.getOrAllocRouteNextHopSetID(fwdNexthops);
        newResolvedId = allocResult.nextHopIdSetIter->second.id;
        XLOG(DBG3) << "[NextHop ID Manager] backfilling resolvedNextHopSetID="
                   << *newResolvedId << " route=" << route->str();
      }
      // POP_AND_LOOKUP MPLS routes forward via inner-header lookup — no
      // mergeable nexthops, so no normalized set ID. Matches the
      // RouteUpdater allocation path (RouteUpdater.cpp `if
      // (!labelPopandLookup)`).
      bool isPopAndLookup =
          fwdNexthops.size() == 1 && fwdNexthops.begin()->isPopAndLookup();
      if (!newNormalizedId.has_value() && !isPopAndLookup) {
        auto allocResult = manager.getOrAllocRouteNextHopSetID(
            fwd.nonOverrideNormalizedNextHops(ecmpWidth));
        newNormalizedId = allocResult.nextHopIdSetIter->second.id;
        XLOG(DBG3)
            << "[NextHop ID Manager] backfilling normalizedResolvedNextHopSetID="
            << *newNormalizedId << " route=" << route->str();
      }
      if (newResolvedId != fwd.getResolvedNextHopSetID() ||
          newNormalizedId != fwd.getNormalizedResolvedNextHopSetID()) {
        fwdReplacement = fwd.clone();
        fwdReplacement->setResolvedNextHopSetID(newResolvedId);
        fwdReplacement->setNormalizedResolvedNextHopSetID(newNormalizedId);
      }
    }

    if (clientUpdates.empty() && !fwdReplacement) {
      return;
    }
    backfilled = true;
    if (route->isPublished()) {
      route = route->clone();
    }
    for (auto& [clientId, newEntry] : clientUpdates) {
      route->update(clientId, *newEntry);
    }
    if (fwdReplacement) {
      route->setResolved(*fwdReplacement);
    }
    route->publish();
  };

  // v4/v6 NetworkToRouteMap is a RadixTree — iterator yields nodes
  // dereferenced via .value().
  auto backfillIpRoutes = [&backfillOneRoute](auto& routes) {
    for (auto ritr = routes.begin(); ritr != routes.end(); ++ritr) {
      backfillOneRoute(ritr->value());
    }
  };
  // LabelToRouteMap is an unordered_map — range-for yields
  // pair<LabelID, shared_ptr<Route<LabelID>>>.
  auto backfillMplsRoutes = [&backfillOneRoute](auto& routes) {
    for (auto& [_label, route] : routes) {
      backfillOneRoute(route);
    }
  };

  for (auto& [_vrf, table] : lockedRouteTables->routerIDToRouteTable) {
    backfillIpRoutes(table.v4NetworkToRoute);
    backfillIpRoutes(table.v6NetworkToRoute);
    backfillMplsRoutes(table.labelToRoute);
  }
  return backfilled;
}

RibRouteTables RibRouteTables::fromThrift(
    const std::map<int32_t, state::RouteTableFields>& ribThrift,
    const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
    const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
    const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
    uint32_t ecmpWidth,
    const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
        classBasedPolicyMaps) {
  RibRouteTables rib;
  auto lockedRouteTables = rib.synchronizedRouteTables_.wlock();
  lockedRouteTables->ecmpWidth = ecmpWidth;

  for (const auto& [rid, table] : ribThrift) {
    VrfRouteTable rtable = VrfRouteTable::fromThrift(table);
    auto vrf = RouterID(rid);
    lockedRouteTables->routerIDToRouteTable.emplace(vrf, std::move(rtable));
  }

  if (fibsInfoMap) {
    rib.importFibs(lockedRouteTables, fibsInfoMap, labelFib);
  }
  if (mySidMap) {
    reconstructMySidTableFromSwitchState(
        mySidMap, &lockedRouteTables->mySidTable);
  }
  // Reconstruct NextHopIDManager state from FIB and MySid table during warm
  // boot
  if (lockedRouteTables->nextHopIDManager && fibsInfoMap) {
    // setIdRemap is non-empty ONLY on a cross-version warm boot where a field
    // was added to the NextHop thrift: the writer build sets that field and
    // uses it to tell two nexthops apart, but this build predates it and
    // deserializes both to the same NextHop. reconstruct then sees two
    // persisted NextHopIDs mapping to one NextHop, so it keeps a single
    // primary NextHopID and reclaims the duplicate. Because members collapse
    // like that, two persisted NextHopSetIDs can end up with identical member
    // sets, so a SetID is retired and remapped -- onto the surviving SetID if
    // its primary members already exist, or onto a freshly minted SetID if
    // its members changed but match no existing set. The RIB routes (and MySid
    // entries) still reference the retired SetIDs, so here we repoint each
    // reference onto its surviving SetID, leaving the RIB consistent with the
    // rebuilt manager.
    //
    // On a same-version warm boot no nexthops collapse, the remap is empty, and
    // this whole block is skipped. Refcounts are left untouched -- reconstruct
    // already counted each reference against the surviving SetID. The remap is
    // reconstruction scratch local to this warm boot: reconstruct fills it via
    // the out-param and it dies when this scope exits.
    std::unordered_map<NextHopSetID, NextHopSetID> setIdRemap;
    lockedRouteTables->nextHopIDManager->reconstructFromSwitchStateMaps(
        fibsInfoMap,
        mySidMap,
        labelFib,
        &rib,
        classBasedPolicyMaps,
        &setIdRemap);
    rib.nextHopIdsChangedAtWarmboot_ = !setIdRemap.empty();
    if (!setIdRemap.empty()) {
      for (auto& [_vrf, routeTable] : lockedRouteTables->routerIDToRouteTable) {
        routeTable.v4NetworkToRoute.forAll([&setIdRemap](auto& ritr) {
          ritr.value() = remapRouteNextHopSetIds(ritr.value(), setIdRemap);
        });
        routeTable.v6NetworkToRoute.forAll([&setIdRemap](auto& ritr) {
          ritr.value() = remapRouteNextHopSetIds(ritr.value(), setIdRemap);
        });
        if (FLAGS_mpls_rib) {
          for (auto& [_label, route] : routeTable.labelToRoute) {
            route = remapRouteNextHopSetIds(route, setIdRemap);
          }
        }
      }
      for (auto& [_prefix, mySid] : lockedRouteTables->mySidTable) {
        auto resolvedId = mySid->getResolvedNextHopsId();
        auto unresolvedId = mySid->getUnresolveNextHopsId();
        auto backupResolvedId = mySid->getBackupResolvedNextHopsId();
        auto backupUnresolvedId = mySid->getBackupUnresolveNextHopsId();
        bool changed = remapNextHopSetId(resolvedId, setIdRemap);
        changed |= remapNextHopSetId(unresolvedId, setIdRemap);
        changed |= remapNextHopSetId(backupResolvedId, setIdRemap);
        changed |= remapNextHopSetId(backupUnresolvedId, setIdRemap);
        if (changed) {
          mySid = mySid->clone();
          mySid->setResolvedNextHopsId(resolvedId);
          mySid->setUnresolveNextHopsId(unresolvedId);
          mySid->setBackupResolvedNextHopsId(backupResolvedId);
          mySid->setBackupUnresolveNextHopsId(backupUnresolvedId);
          mySid->publish();
        }
      }
    }
  }
  // Seed the per-VRF unresolved-routes index from the loaded RIB.
  for (auto& [_, routeTable] : lockedRouteTables->routerIDToRouteTable) {
    refreshUnresolvedIndex(
        routeTable.v4NetworkToRoute, &routeTable.unresolvedV4Routes);
    refreshUnresolvedIndex(
        routeTable.v6NetworkToRoute, &routeTable.unresolvedV6Routes);
    if (FLAGS_mpls_rib) {
      refreshUnresolvedMplsIndex(
          routeTable.labelToRoute, &routeTable.unresolvedMplsRoutes);
    }
  }

  // Stamp any missing IDs on warmboot-loaded routes before readers see them.
  if (backfillNextHopIds(lockedRouteTables)) {
    rib.nextHopIdsChangedAtWarmboot_ = true;
  }
  return rib;
}

std::unique_ptr<RoutingInformationBase> RoutingInformationBase::fromThrift(
    const std::map<int32_t, state::RouteTableFields>& ribThrift,
    const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
    const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
    const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
    uint32_t ecmpWidth,
    const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
        classBasedPolicyMaps) {
  auto rib = std::make_unique<RoutingInformationBase>();
  rib->ribTables_ = RibRouteTables::fromThrift(
      ribThrift,
      fibsInfoMap,
      labelFib,
      mySidMap,
      ecmpWidth,
      classBasedPolicyMaps);
  return rib;
}

std::vector<MplsRouteDetails> RibRouteTables::getMplsRouteTableDetails() const {
  std::vector<MplsRouteDetails> mplsRouteDetails;
  synchronizedRouteTables_.withRLock([&](const auto& synchronizedRouteTables) {
    const auto it =
        synchronizedRouteTables.routerIDToRouteTable.find(RouterID(0));
    if (it != synchronizedRouteTables.routerIDToRouteTable.end()) {
      auto* manager = synchronizedRouteTables.nextHopIDManager.get();
      ClientNextHopsResolver resolveClient =
          [manager](const RouteNextHopEntry& entry) {
            return getClientNextHopsFromRib(manager, entry);
          };
      for (auto rit = it->second.labelToRoute.begin();
           rit != it->second.labelToRoute.end();
           ++rit) {
        MplsRouteDetails mplsRouteDetail;
        auto routeDetails = rit->second->toRouteDetails(
            getResolvedNextHopsFromRib(manager, rit->second->getForwardInfo()),
            std::nullopt,
            resolveClient);
        mplsRouteDetail.topLabel() = rit->first;
        mplsRouteDetail.nextHopMulti() = *routeDetails.nextHopMulti();
        mplsRouteDetail.nextHops() = *routeDetails.nextHops();
        if (routeDetails.adminDistance().has_value()) {
          mplsRouteDetail.adminDistance() = *routeDetails.adminDistance();
        }
        mplsRouteDetails.emplace_back(mplsRouteDetail);
      }
    }
  });
  return mplsRouteDetails;
}

std::vector<RouteDetails> RibRouteTables::getRouteTableDetails(
    RouterID rid) const {
  std::vector<RouteDetails> routeDetails;
  synchronizedRouteTables_.withRLock([&](const auto& synchronizedRouteTables) {
    const auto it = synchronizedRouteTables.routerIDToRouteTable.find(rid);
    if (it != synchronizedRouteTables.routerIDToRouteTable.end()) {
      auto* manager = synchronizedRouteTables.nextHopIDManager.get();
      // Per-client entries carry only clientNextHopSetID; resolve them through
      // the manager rather than inline.
      ClientNextHopsResolver resolveClient =
          [manager](const RouteNextHopEntry& entry) {
            return getClientNextHopsFromRib(manager, entry);
          };
      for (auto rit = it->second.v4NetworkToRoute.begin();
           rit != it->second.v4NetworkToRoute.end();
           ++rit) {
        routeDetails.emplace_back(rit->value()->toRouteDetails(
            getResolvedNextHopsFromRib(manager, rit->value()->getForwardInfo()),
            std::nullopt,
            resolveClient));
      }
      for (auto rit = it->second.v6NetworkToRoute.begin();
           rit != it->second.v6NetworkToRoute.end();
           ++rit) {
        routeDetails.emplace_back(rit->value()->toRouteDetails(
            getResolvedNextHopsFromRib(manager, rit->value()->getForwardInfo()),
            std::nullopt,
            resolveClient));
      }
    }
  });
  return routeDetails;
}

std::unordered_map<folly::CIDRNetworkV6, state::MySidFields>
RibRouteTables::getMySidTableCopy() const {
  std::unordered_map<folly::CIDRNetworkV6, state::MySidFields> result;
  synchronizedRouteTables_.withRLock([&](const auto& synchronizedRouteTables) {
    for (const auto& [cidr, mySidPtr] : synchronizedRouteTables.mySidTable) {
      result.emplace(cidr, mySidPtr->toThrift());
    }
  });
  return result;
}

RoutingInformationBase::UpdateStatistics RoutingInformationBase::update(
    const SwitchIdScopeResolver* resolver,
    RouterID routerID,
    ClientID clientID,
    AdminDistance adminDistanceFromClientID,
    const std::vector<UnicastRoute>& toAdd,
    const std::vector<IpPrefix>& toDelete,
    bool resetClientsRoutes,
    folly::StringPiece updateType,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    void* cookie) {
  return updateImpl<RibIpRouteUpdate>(
      resolver,
      routerID,
      clientID,
      adminDistanceFromClientID,
      toAdd,
      toDelete,
      resetClientsRoutes,
      updateType,
      ribToSwitchStateFunc,
      cookie);
}

RoutingInformationBase::UpdateStatistics RoutingInformationBase::update(
    const SwitchIdScopeResolver* resolver,
    RouterID routerID,
    ClientID clientID,
    AdminDistance adminDistanceFromClientID,
    const std::vector<MplsRoute>& toAdd,
    const std::vector<MplsLabel>& toDelete,
    bool resetClientsRoutes,
    folly::StringPiece updateType,
    RibToSwitchStateFunction ribToSwitchStateFunc,
    void* cookie) {
  return updateImpl<RibMplsRouteUpdate>(
      resolver,
      routerID,
      clientID,
      adminDistanceFromClientID,
      toAdd,
      toDelete,
      resetClientsRoutes,
      updateType,
      ribToSwitchStateFunc,
      cookie);
}

void RibRouteTables::update(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidEntry>& toAdd,
    const std::vector<IpPrefix>& toDelete,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  // Pre-validate and construct all MySid objects before touching mySidTable.
  // If any entry is invalid, mySidFromEntry throws here before any partial
  // state is written.
  std::vector<MySidWithNextHops> mySidsWithNextHops;
  mySidsWithNextHops.reserve(toAdd.size());
  for (const auto& entry : toAdd) {
    auto mySid = mySidFromEntry(entry);
    auto nhops = entry.nextHops()->empty()
        ? RouteNextHopSet{}
        : util::toRouteNextHopSet(*entry.nextHops(), true);
    std::optional<std::string> nhgName;
    if (entry.namedNextHops().has_value()) {
      nhgName = *entry.namedNextHops()->nextHopGroup();
    }
    mySidsWithNextHops.emplace_back(
        MySidWithNextHops{
            std::move(mySid), std::move(nhops), std::move(nhgName)});
  }
  updateMySidsImpl(
      resolver,
      mySidsWithNextHops,
      {} /* toUnresolveIfMatch */,
      toDelete,
      ribMySidToSwitchStateFunc,
      cookie);
}

void RibRouteTables::update(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidWithNextHops>& toAdd,
    const std::vector<MySidNeighborRemoved>& toUnresolveIfMatch,
    const std::vector<IpPrefix>& toDelete,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  // Clone the MySid state objects since we may mutate them (set
  // unresolvedNextHopsId). Keep the parallel next-hop set as-is.
  std::vector<MySidWithNextHops> cloned;
  cloned.reserve(toAdd.size());
  for (const auto& entry : toAdd) {
    cloned.emplace_back(
        MySidWithNextHops{
            entry.mySid->clone(), entry.nextHopSet, entry.nextHopGroupName});
  }
  updateMySidsImpl(
      resolver,
      cloned,
      toUnresolveIfMatch,
      toDelete,
      ribMySidToSwitchStateFunc,
      cookie);
}

void RibRouteTables::updateMySidFrrProtection(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidFrrProtectionUpdate>& toAddOrUpdate,
    const std::vector<folly::CIDRNetwork>& toDelete,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  updateRibMySids([&](const RibMySidUpdater::VrfRouteTables& routeTables,
                      MySidTable* mySidTable,
                      NextHopIDManager* nextHopIDManager,
                      uint32_t ecmpWidth) {
    if (!nextHopIDManager) {
      throw FbossError("NextHopIDManager not initialized");
    }
    const auto validateMySid = [&](const folly::CIDRNetwork& prefix) {
      const auto prefixStr = folly::IPAddress::networkToString(prefix);
      if (!prefix.first.isV6()) {
        throw FbossError("MySid ", prefixStr, " does not exist");
      }
      const folly::CIDRNetworkV6 prefixV6{prefix.first.asV6(), prefix.second};
      const auto mySidIt = mySidTable->find(prefixV6);
      if (mySidIt == mySidTable->end()) {
        throw FbossError("MySid ", prefixStr, " does not exist");
      }
      if (mySidIt->second->getType() != MySidType::ADJACENCY_MICRO_SID) {
        throw FbossError("MySid ", prefixStr, " is not an adjacency MySid");
      }
    };
    for (const auto& update : toAddOrUpdate) {
      validateMySid(update.mySidPrefix);
    }
    for (const auto& prefix : toDelete) {
      validateMySid(prefix);
    }

    const auto setBackupNextHops = [&](const folly::CIDRNetwork& cidr,
                                       const RouteNextHopSet* nextHops) {
      const folly::CIDRNetworkV6 prefix{cidr.first.asV6(), cidr.second};
      auto& mySid = mySidTable->at(prefix);
      const auto oldUnresolvedId = mySid->getBackupUnresolveNextHopsId();
      const auto oldResolvedId = mySid->getBackupResolvedNextHopsId();
      std::optional<NextHopSetID> newUnresolvedId;
      if (nextHops && !nextHops->empty()) {
        newUnresolvedId =
            nextHopIDManager->getOrAllocRouteNextHopSetID(*nextHops)
                .nextHopIdSetIter->second.id;
      }
      auto updatedMySid = mySid->clone();
      updatedMySid->setBackupUnresolveNextHopsId(newUnresolvedId);
      if (!newUnresolvedId.has_value()) {
        updatedMySid->setBackupResolvedNextHopsId(std::nullopt);
      }
      mySid = std::move(updatedMySid);
      if (oldUnresolvedId.has_value()) {
        nextHopIDManager->decrOrDeallocRouteNextHopSetID(*oldUnresolvedId);
      }
      if (!newUnresolvedId.has_value() && oldResolvedId.has_value()) {
        nextHopIDManager->decrOrDeallocRouteNextHopSetID(*oldResolvedId);
      }
    };
    std::set<folly::CIDRNetwork> mySidsToResolve;
    for (const auto& update : toAddOrUpdate) {
      setBackupNextHops(update.mySidPrefix, &update.nextHops);
      mySidsToResolve.insert(update.mySidPrefix);
    }
    for (const auto& prefix : toDelete) {
      setBackupNextHops(prefix, nullptr);
    }
    if (!mySidsToResolve.empty()) {
      RibMySidUpdater updater(
          routeTables, nextHopIDManager, mySidTable, ecmpWidth);
      updater.resolve(mySidsToResolve);
    }
  });
  updateFib(resolver, ribMySidToSwitchStateFunc, cookie);
}

void RibRouteTables::updateMySidsImpl(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidWithNextHops>& toAdd,
    const std::vector<MySidNeighborRemoved>& toUnresolveIfMatch,
    const std::vector<IpPrefix>& toDelete,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  updateRibMySids([&](const RibMySidUpdater::VrfRouteTables& routeTables,
                      MySidTable* mySidTable,
                      NextHopIDManager* nextHopIDManager,
                      uint32_t ecmpWidth) {
    auto toAddWithNextHops = toAdd;
    for (auto& entry : toAddWithNextHops) {
      if (!entry.nextHopGroupName.has_value()) {
        entry.mySid->setNamedNextHopGroup(std::nullopt);
        validateMySidNextHops(entry.mySid->getType(), entry.nextHopSet);
        continue;
      }
      entry.mySid->setNamedNextHopGroup(*entry.nextHopGroupName);
      auto namedNextHops = nextHopIDManager
          ? nextHopIDManager->getNextHopsForName(*entry.nextHopGroupName)
          : std::nullopt;
      if (!namedNextHops.has_value()) {
        throw FbossError(
            "Named next-hop group '",
            *entry.nextHopGroupName,
            "' does not exist");
      }
      entry.nextHopSet = std::move(*namedNextHops);
      validateMySidNextHops(entry.mySid->getType(), entry.nextHopSet);
    }

    // Conditional unresolve runs first so that any same-prefix entry
    // appearing in both vectors (observer-driven IP-change path) gets
    // clear-then-add semantics — toAdd's freshly-allocated nhop id can
    // never be matched and rolled back by a stale toUnresolveIfMatch
    // entry below.
    for (const auto& [cidrV6, removedIp] : toUnresolveIfMatch) {
      if (!nextHopIDManager) {
        continue;
      }
      auto it = mySidTable->find(cidrV6);
      if (it == mySidTable->end()) {
        continue;
      }
      const auto& existing = it->second;
      const auto unresolvedIdOpt = existing->getUnresolveNextHopsId();
      if (!unresolvedIdOpt.has_value()) {
        continue;
      }
      // Direct membership check — avoids materializing the entire
      // RouteNextHopSet just to scan it.
      if (!nextHopIDManager->nextHopSetContainsAddr(
              *unresolvedIdOpt, removedIp)) {
        continue;
      }
      // Snapshot the ids before mutating the table; `existing` becomes
      // invalid after the assignment below.
      const NextHopSetID oldUnresolvedId = *unresolvedIdOpt;
      const auto oldResolvedIdOpt = existing->getResolvedNextHopsId();
      auto cloned = existing->clone();
      cloned->setUnresolveNextHopsId(std::nullopt);
      if (oldResolvedIdOpt.has_value()) {
        cloned->setResolvedNextHopsId(std::nullopt);
      }
      (*mySidTable)[cidrV6] = std::move(cloned);
      nextHopIDManager->decrOrDeallocRouteNextHopSetID(oldUnresolvedId);
      if (oldResolvedIdOpt.has_value()) {
        nextHopIDManager->decrOrDeallocRouteNextHopSetID(*oldResolvedIdOpt);
      }
    }
    std::set<folly::CIDRNetwork> addedPrefixes;
    for (const auto& entry : toAddWithNextHops) {
      auto mySid = entry.mySid;
      const auto cidr = mySid->getMySid();
      addedPrefixes.emplace(cidr.first, cidr.second);
      const folly::CIDRNetworkV6 cidrV6(cidr.first.asV6(), cidr.second);
      if (nextHopIDManager) {
        nextHopIDManager->removeMySidFromNamedNhgs(cidrV6);
        if (entry.nextHopGroupName.has_value()) {
          nextHopIDManager->addMySidForNamedNhg(
              *entry.nextHopGroupName, cidrV6);
        }
        // Alloc-then-release: get a ref on the new unresolved set first so
        // that a same-set alloc+release on a refcounted entry is a no-op
        // (rather than a deallocate/reallocate cycle).
        std::optional<NextHopSetID> newUnresolvedId;
        if (!entry.nextHopSet.empty()) {
          newUnresolvedId =
              nextHopIDManager->getOrAllocRouteNextHopSetID(entry.nextHopSet)
                  .nextHopIdSetIter->second.id;
        }
        mySid->setUnresolveNextHopsId(newUnresolvedId);

        // Release the old entry's refs, if any. resolve() below will
        // re-allocate a resolvedId for the new entry when applicable.
        if (const auto existingIt = mySidTable->find(cidrV6);
            existingIt != mySidTable->end()) {
          const auto& existing = existingIt->second;
          // Adjacency FRR protection is managed independently through
          // updateMySidFrrProtection(). A primary-neighbor rebind replaces
          // this MySid entry, but must transfer the existing backup IDs and
          // their references to the replacement rather than releasing them.
          const bool preserveFrrProtection =
              existing->getType() == MySidType::ADJACENCY_MICRO_SID &&
              mySid->getType() == MySidType::ADJACENCY_MICRO_SID;
          if (preserveFrrProtection) {
            mySid->setBackupUnresolveNextHopsId(
                existing->getBackupUnresolveNextHopsId());
            mySid->setBackupResolvedNextHopsId(
                existing->getBackupResolvedNextHopsId());
          }
          if (const auto oldUnresolvedId = existing->getUnresolveNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*oldUnresolvedId);
          }
          if (const auto oldResolvedId = existing->getResolvedNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*oldResolvedId);
          }
          if (!preserveFrrProtection) {
            if (const auto oldBackupUnresolvedId =
                    existing->getBackupUnresolveNextHopsId()) {
              nextHopIDManager->decrOrDeallocRouteNextHopSetID(
                  *oldBackupUnresolvedId);
            }
            if (const auto oldBackupResolvedId =
                    existing->getBackupResolvedNextHopsId()) {
              nextHopIDManager->decrOrDeallocRouteNextHopSetID(
                  *oldBackupResolvedId);
            }
          }
        }
      }
      (*mySidTable)[cidrV6] = std::move(mySid);
    }
    for (const auto& prefix : toDelete) {
      auto ip = network::toIPAddress(*prefix.ip());
      auto mask = static_cast<uint8_t>(*prefix.prefixLength());
      const folly::CIDRNetworkV6 cidr(ip.asV6(), mask);
      if (nextHopIDManager) {
        const auto it = mySidTable->find(cidr);
        if (it != mySidTable->end()) {
          if (const auto id = it->second->getUnresolveNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*id);
          }
          if (const auto id = it->second->getResolvedNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*id);
          }
          if (const auto id = it->second->getBackupUnresolveNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*id);
          }
          if (const auto id = it->second->getBackupResolvedNextHopsId()) {
            nextHopIDManager->decrOrDeallocRouteNextHopSetID(*id);
          }
          nextHopIDManager->removeMySidFromNamedNhgs(cidr);
        }
      }
      mySidTable->erase(cidr);
    }
    if (nextHopIDManager && !addedPrefixes.empty()) {
      RibMySidUpdater updater(
          routeTables, nextHopIDManager, mySidTable, ecmpWidth);
      updater.resolve(addedPrefixes);
    }
  });
  updateFib(resolver, ribMySidToSwitchStateFunc, cookie);
}

void RoutingInformationBase::update(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidEntry>& toAdd,
    const std::vector<IpPrefix>& toDelete,
    folly::StringPiece updateType,
    const RibMySidToSwitchStateFunction ribMySidToSwitchStateFunc,
    void* cookie) {
  ensureRunning();
  std::exception_ptr updateException;
  auto updateFn = [&]() {
    try {
      ribTables_.update(
          resolver, toAdd, toDelete, ribMySidToSwitchStateFunc, cookie);
    } catch (const std::exception&) {
      updateException = std::current_exception();
    }
  };
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(updateFn);
  if (updateException) {
    std::rethrow_exception(updateException);
  }
}

void RoutingInformationBase::updateMySidImpl(
    const SwitchIdScopeResolver* resolver,
    std::vector<MySidWithNextHops> toAdd,
    std::vector<MySidNeighborRemoved> toUnresolveIfMatch,
    std::vector<IpPrefix> toDelete,
    folly::StringPiece updateType,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie,
    bool async) {
  ensureRunning();
  auto updateException = std::make_shared<std::exception_ptr>();
  auto updateFn = [resolver,
                   toAdd = std::move(toAdd),
                   toUnresolveIfMatch = std::move(toUnresolveIfMatch),
                   toDelete = std::move(toDelete),
                   ribMySidToSwitchStateFunc,
                   cookie,
                   async,
                   updateException,
                   this]() {
    try {
      ribTables_.update(
          resolver,
          toAdd,
          toUnresolveIfMatch,
          toDelete,
          ribMySidToSwitchStateFunc,
          cookie);
    } catch (const std::exception&) {
      if (async) {
        throw;
      }
      *updateException = std::current_exception();
    }
  };
  if (async) {
    ribUpdateEventBase_.runInFbossEventBaseThread(std::move(updateFn));
    return;
  }
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(updateFn);
  if (*updateException) {
    std::rethrow_exception(*updateException);
  }
}

void RoutingInformationBase::updateMySidFrrProtection(
    const SwitchIdScopeResolver* resolver,
    const std::vector<MySidFrrProtectionUpdate>& toAddOrUpdate,
    const std::vector<folly::CIDRNetwork>& toDelete,
    const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
    void* cookie) {
  updateStateInRibThread([&]() {
    ribTables_.updateMySidFrrProtection(
        resolver, toAddOrUpdate, toDelete, ribMySidToSwitchStateFunc, cookie);
  });
}

void RoutingInformationBase::updateStateInRibThread(
    const std::function<void()>& fn) {
  ensureRunning();
  // An exception escaping the event base thread would terminate the process
  // instead of reaching the caller, so hand it back the way the sibling RIB
  // entry points do.
  std::exception_ptr exceptionPtr;
  ribUpdateEventBase_.runInEventBaseThreadAndWait([&]() {
    try {
      fn();
    } catch (const std::exception&) {
      exceptionPtr = std::current_exception();
    }
  });
  if (exceptionPtr) {
    std::rethrow_exception(exceptionPtr);
  }
}

template <typename RibUpdateFn>
void RibRouteTables::updateRibNamedNextHopGroups(
    const RibUpdateFn& updateRibFn) {
  auto lockedRouteTables = synchronizedRouteTables_.wlock();
  if (!lockedRouteTables->nextHopIDManager) {
    throw FbossError("NextHopIDManager not initialized");
  }
  updateRibFn(lockedRouteTables->nextHopIDManager.get());
}

void RibRouteTables::updateFibNamedNextHopGroups(
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  try {
    auto lockedRouteTables = synchronizedRouteTables_.rlock();
    stateUpdateFn(lockedRouteTables->nextHopIDManager.get());
  } catch (const FbossHwUpdateError& hwUpdateError) {
    {
      SCOPE_FAIL {
        XLOG(FATAL) << " RIB Rollback failed, aborting program";
      };
      auto lockedRouteTables = synchronizedRouteTables_.wlock();
      // Reconstruct NextHopIDManager from the applied state to restore
      // consistency between RIB and switch state
      if (lockedRouteTables->nextHopIDManager) {
        lockedRouteTables->nextHopIDManager->reconstructFromSwitchStateMaps(
            hwUpdateError.appliedState->getFibsInfoMap(),
            hwUpdateError.appliedState->getMySids(),
            hwUpdateError.appliedState->getLabelForwardingInformationBase(),
            this,
            hwUpdateError.appliedState->getClassBasedPolicies());
      }
    }
    throw;
  }
}

void RibRouteTables::addOrUpdateNamedNextHopGroups(
    const SwitchIdScopeResolver* resolver,
    const std::vector<std::pair<std::string, RouteNextHopSet>>& groups,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) {
  using RouteKey = std::pair<RouterID, ClientID>;
  std::map<RouteKey, std::vector<RibRouteUpdater::RouteEntry>>
      routesToReprogram;
  std::unordered_map<std::string, NextHopIDManager::MySidSet>
      nameToAffectedMySids;
  std::set<folly::CIDRNetwork> mySidsToReresolve;

  {
    auto lockedRouteTables = synchronizedRouteTables_.wlock();
    if (!lockedRouteTables->nextHopIDManager) {
      throw FbossError("NextHopIDManager not initialized");
    }
    auto* nhIdManager = lockedRouteTables->nextHopIDManager.get();

    for (const auto& [name, nextHopSet] : groups) {
      if (!nhIdManager->hasNamedNextHopGroup(name)) {
        continue;
      }
      const auto& affectedMySids = nhIdManager->getMySidsForNamedNhg(name);
      auto& affectedMySidsSnapshot = nameToAffectedMySids[name];
      for (const auto& cidr : affectedMySids) {
        auto it = lockedRouteTables->mySidTable.find(cidr);
        if (it == lockedRouteTables->mySidTable.end()) {
          throw FbossError(
              "Named next-hop group '",
              name,
              "' references missing MySid ",
              folly::IPAddress(cidr.first),
              "/",
              static_cast<int>(cidr.second));
        }
        validateMySidNextHops(it->second->getType(), nextHopSet);
        affectedMySidsSnapshot.insert(cidr);
      }
    }

    for (const auto& [name, nextHopSet] : groups) {
      auto result = nhIdManager->allocateNamedNextHopGroup(name, nextHopSet);
      if (result.isNew) {
        continue;
      }
      const auto& affectedRoutes = nhIdManager->getRoutesForNamedNhg(name);
      for (const auto& [rid, prefix] : affectedRoutes) {
        auto vrfIt = lockedRouteTables->routerIDToRouteTable.find(rid);
        if (vrfIt == lockedRouteTables->routerIDToRouteTable.end()) {
          continue;
        }
        auto updateRouteNextHops = [&](const auto& route) {
          for (const auto& [cid, entry] : route->getEntryForClients()) {
            auto nhg = entry->getNamedNextHopGroup();
            if (nhg.has_value() && *nhg == name) {
              RouteNextHopEntry nhopEntry(
                  RouteForwardAction::NEXTHOPS,
                  entry->getAdminDistance(),
                  entry->getCounterID(),
                  entry->getClassID(),
                  entry->getOverrideEcmpSwitchingMode(),
                  entry->getOverrideNextHops());
              nhopEntry.setNamedNextHopGroup(name);
              routesToReprogram[{rid, ClientID(cid)}].emplace_back(
                  prefix, nhopEntry);
            }
          }
        };

        if (prefix.first.isV4()) {
          auto it = vrfIt->second.v4NetworkToRoute.exactMatch(
              prefix.first.asV4(), prefix.second);
          if (it != vrfIt->second.v4NetworkToRoute.end()) {
            updateRouteNextHops(it->value());
          }
        } else {
          auto it = vrfIt->second.v6NetworkToRoute.exactMatch(
              prefix.first.asV6(), prefix.second);
          if (it != vrfIt->second.v6NetworkToRoute.end()) {
            updateRouteNextHops(it->value());
          }
        }
      }

      for (const auto& cidr : nameToAffectedMySids[name]) {
        auto it = lockedRouteTables->mySidTable.find(cidr);
        CHECK(it != lockedRouteTables->mySidTable.end());
        auto mySid = it->second->clone();
        const auto oldUnresolvedId = mySid->getUnresolveNextHopsId();
        mySid->setUnresolveNextHopsId(
            nhIdManager->getOrAllocRouteNextHopSetID(nextHopSet)
                .nextHopIdSetIter->second.id);
        it->second = std::move(mySid);
        if (oldUnresolvedId.has_value()) {
          nhIdManager->decrOrDeallocRouteNextHopSetID(*oldUnresolvedId);
        }
        mySidsToReresolve.emplace(cidr.first, cidr.second);
      }
    }

    auto routeTables =
        makeVrfRouteTables(lockedRouteTables->routerIDToRouteTable);
    if (!mySidsToReresolve.empty()) {
      RibMySidUpdater updater(
          routeTables,
          nhIdManager,
          &lockedRouteTables->mySidTable,
          lockedRouteTables->ecmpWidth);
      updater.resolve(mySidsToReresolve);
    }
    std::set<std::string> groupNames;
    for (const auto& [name, _nextHopSet] : groups) {
      groupNames.insert(name);
    }
    resolveNamedNhgs(
        routeTables, nhIdManager, groupNames, lockedRouteTables->ecmpWidth);
  }

  for (auto& [key, routes] : routesToReprogram) {
    auto& [rid, clientID] = key;
    std::vector<folly::CIDRNetwork> emptyDel;
    update(
        resolver,
        rid,
        clientID,
        AdminDistance::MAX_ADMIN_DISTANCE,
        routes,
        emptyDel,
        false,
        "named nhg update",
        ribToSwitchStateFunc,
        cookie);
  }

  if (routesToReprogram.empty()) {
    auto lockedRouteTables = synchronizedRouteTables_.rlock();
    if (!lockedRouteTables->routerIDToRouteTable.empty()) {
      auto vrf = lockedRouteTables->routerIDToRouteTable.begin()->first;
      lockedRouteTables.unlock();
      updateFib(resolver, vrf, ribToSwitchStateFunc, cookie);
    }
  }
}

void RibRouteTables::deleteNamedNextHopGroups(
    const std::vector<std::string>& names,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  updateRibNamedNextHopGroups([&](NextHopIDManager* nextHopIDManager) {
    for (const auto& name : names) {
      if (nextHopIDManager->hasNamedNextHopGroup(name)) {
        nextHopIDManager->deallocateNamedNextHopGroup(name);
      }
    }
  });
  updateFibNamedNextHopGroups(stateUpdateFn);
}

void RibRouteTables::addOrUpdatePolicies(
    const std::vector<ClassBasedPolicy>& policies,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  {
    auto lockedRouteTables = synchronizedRouteTables_.wlock();
    if (!lockedRouteTables->nextHopIDManager) {
      throw FbossError("NextHopIDManager not initialized");
    }
    auto* nhIdManager = lockedRouteTables->nextHopIDManager.get();
    // Reject the whole batch before storing any of it.
    auto requireNamedNhg = [&](const std::string& policyName,
                               const std::string& nhgName) {
      if (!nhIdManager->hasNamedNextHopGroup(nhgName)) {
        throw FbossError(
            "Class-based policy '",
            policyName,
            "' references non-existent named next-hop group '",
            nhgName,
            "'");
      }
    };
    std::set<std::string> names;
    for (const auto& policy : policies) {
      requireNamedNhg(*policy.name(), *policy.defaultNexthopGroup());
      names.insert(*policy.defaultNexthopGroup());
      for (const auto& [_fc, nhg] : *policy.class2NextHopGroup()) {
        const auto nhgName = nhg.name().value_or("");
        requireNamedNhg(*policy.name(), nhgName);
        names.insert(nhgName);
      }
    }
    for (const auto& policy : policies) {
      nhIdManager->addOrUpdatePolicy(policy);
    }

    auto routeTables =
        makeVrfRouteTables(lockedRouteTables->routerIDToRouteTable);
    resolveNamedNhgs(
        routeTables, nhIdManager, names, lockedRouteTables->ecmpWidth);
  }
  updateFibNamedNextHopGroups(stateUpdateFn);
}

void RibRouteTables::removePolicies(
    const std::vector<std::string>& policyNames,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  updateRibNamedNextHopGroups([&](NextHopIDManager* nextHopIDManager) {
    for (const auto& name : policyNames) {
      nextHopIDManager->removePolicy(name);
    }
  });
  updateFibNamedNextHopGroups(stateUpdateFn);
}

void RoutingInformationBase::addOrUpdateNamedNextHopGroups(
    const SwitchIdScopeResolver* resolver,
    const std::vector<std::pair<std::string, RouteNextHopSet>>& groups,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) {
  // Pre-validate all groups before entering the RIB thread. If any group is
  // invalid, throw before any state is mutated. This matches the MySid batch
  // validation pattern (mySidFromEntry pre-validates all entries before
  // updateRibMySids is called).
  for (const auto& [name, nextHopSet] : groups) {
    if (name.empty()) {
      throw FbossError("Named next-hop group name cannot be empty");
    }
    if (nextHopSet.empty()) {
      throw FbossError(
          "Named next-hop group '", name, "' has empty nexthop set");
    }
  }
  ensureRunning();
  std::exception_ptr exceptionPtr;
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait([&]() {
    try {
      ribTables_.addOrUpdateNamedNextHopGroups(
          resolver, groups, ribToSwitchStateFunc, cookie);
    } catch (const std::exception&) {
      exceptionPtr = std::current_exception();
    }
  });
  if (exceptionPtr) {
    std::rethrow_exception(exceptionPtr);
  }
}

void RoutingInformationBase::deleteNamedNextHopGroups(
    const std::vector<std::string>& names,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  ensureRunning();
  std::exception_ptr exceptionPtr;
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait([&]() {
    try {
      ribTables_.deleteNamedNextHopGroups(names, stateUpdateFn);
    } catch (const std::exception&) {
      exceptionPtr = std::current_exception();
    }
  });
  if (exceptionPtr) {
    std::rethrow_exception(exceptionPtr);
  }
}

void RoutingInformationBase::addOrUpdatePolicies(
    const std::vector<ClassBasedPolicy>& policies,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  // Pre-validate before entering the RIB thread so no state is mutated on a bad
  // request.
  for (const auto& policy : policies) {
    if (policy.name()->empty()) {
      throw FbossError("Class-based policy name cannot be empty");
    }
    if (policy.defaultNexthopGroup()->empty()) {
      throw FbossError(
          "Class-based policy '",
          *policy.name(),
          "' must specify a default next-hop group");
    }
    for (const auto& [fc, nhg] : *policy.class2NextHopGroup()) {
      if (!nhg.name().has_value() || nhg.name()->empty()) {
        throw FbossError(
            "Class-based policy '",
            *policy.name(),
            "' has no next-hop group name for traffic class ",
            apache::thrift::util::enumNameSafe(fc));
      }
    }
  }
  updateStateInRibThread(
      [&]() { ribTables_.addOrUpdatePolicies(policies, stateUpdateFn); });
}

void RoutingInformationBase::removePolicies(
    const std::vector<std::string>& policyNames,
    const std::function<void(const NextHopIDManager*)>& stateUpdateFn) {
  if (policyNames.empty()) {
    return;
  }
  // Pre-validate before entering the RIB thread so no state is mutated on a bad
  // request.
  for (const auto& name : policyNames) {
    if (name.empty()) {
      throw FbossError("Class-based policy name cannot be empty");
    }
  }
  updateStateInRibThread(
      [&]() { ribTables_.removePolicies(policyNames, stateUpdateFn); });
}

std::map<int32_t, state::RouteTableFields> RibRouteTables::toThrift() const {
  std::map<int32_t, state::RouteTableFields> obj{};
  auto routeTables = synchronizedRouteTables_.rlock();
  for (const auto& [rid, routeTable] : routeTables->routerIDToRouteTable) {
    obj.emplace(rid, routeTable.toThrift());
  }
  return obj;
}

std::map<int32_t, state::RouteTableFields> RibRouteTables::warmBootState()
    const {
  std::map<int32_t, state::RouteTableFields> obj{};
  const auto& routeTables = synchronizedRouteTables_.rlock();
  for (const auto& [rid, routeTable] : routeTables->routerIDToRouteTable) {
    obj.emplace(rid, routeTable.warmBootState());
  }
  return obj;
}

RibRouteTables RibRouteTables::fromThrift(
    const std::map<int32_t, state::RouteTableFields>& obj,
    uint32_t ecmpWidth) {
  RibRouteTables ribRouteTables;
  auto routeTables = ribRouteTables.synchronizedRouteTables_.wlock();
  routeTables->ecmpWidth = ecmpWidth;
  for (const auto& [rid, routeTableFields] : obj) {
    // @lint-ignore CLANGTIDY
    routeTables->routerIDToRouteTable.emplace(
        RouterID(rid), VrfRouteTable::fromThrift(routeTableFields));
  }
  return ribRouteTables;
}

std::map<int32_t, state::RouteTableFields> RoutingInformationBase::toThrift()
    const {
  return ribTables_.toThrift();
}
std::unique_ptr<RoutingInformationBase> RoutingInformationBase::fromThrift(
    const std::map<int32_t, state::RouteTableFields>& obj,
    uint32_t ecmpWidth) {
  auto rib = std::make_unique<RoutingInformationBase>();
  rib->ribTables_ = RibRouteTables::fromThrift(obj, ecmpWidth);
  return rib;
}

void RibRouteTables::importFibs(
    const SynchronizedRouteTables::WLockedPtr& lockedRouteTables,
    const std::shared_ptr<MultiSwitchFibInfoMap>& multiSwitchfibsInfoMap,
    const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFibs) {
  auto importRoutes = [](const auto& fib, auto* addrToRoute) {
    for (const auto& iter : std::as_const(*fib)) {
      const auto& route = iter.second;
      auto [itr, inserted] = addrToRoute->insert(route->prefix(), route);
      if (!inserted) {
        // If RIB already had a route, replace it with FIB route so we
        // share the same objects. The only case where this can occur is
        // when we WB from old style RIB ser (all routes ser) to FIB assisted
        // ser/deser
        itr->value() = route;
      }
      DCHECK_EQ(
          addrToRoute
              ->exactMatch(route->prefix().network(), route->prefix().mask())
              ->value(),
          route);
    }
  };

  // Iterate through all FibInfo objects in the map
  for (const auto& [_, fibInfo] : std::as_const(*multiSwitchfibsInfoMap)) {
    // Get the ForwardingInformationBaseMap for this switch
    auto fibsMap = fibInfo->getfibsMap();
    if (!fibsMap) {
      continue;
    }

    // Import routes from each FIB container
    for (const auto& iter : std::as_const(*fibsMap)) {
      const auto& fibContainer = iter.second;
      auto& routeTables =
          lockedRouteTables->routerIDToRouteTable[fibContainer->getID()];
      importRoutes(fibContainer->getFibV6(), &routeTables.v6NetworkToRoute);
      importRoutes(fibContainer->getFibV4(), &routeTables.v4NetworkToRoute);
      auto mplsTable = &routeTables.labelToRoute;
      if (FLAGS_mpls_rib && labelFibs) {
        for (const auto& [_, labelFib] : std::as_const(*labelFibs)) {
          for (const auto& entry : std::as_const(*labelFib)) {
            const auto& route = entry.second;
            auto [itr, inserted] = mplsTable->insert(route->prefix(), route);
            if (!inserted) {
              itr->second = route;
            }
            DCHECK_EQ(mplsTable->find(route->getID())->second, route);
          }
        }
      }
    }
  }
}

std::map<int32_t, state::RouteTableFields>
RoutingInformationBase::warmBootState() const {
  return ribTables_.warmBootState();
}

template <typename AddressT>
std::optional<std::pair<std::shared_ptr<Route<AddressT>>, RouteNextHopSet>>
RibRouteTables::getRouteAndNextHops(
    const AddressT& address,
    RouterID vrf,
    bool normalized) const {
  auto lockedRouteTables = synchronizedRouteTables_.rlock();
  auto vrfIt = lockedRouteTables->routerIDToRouteTable.find(vrf);
  if (vrfIt == lockedRouteTables->routerIDToRouteTable.end()) {
    return std::nullopt;
  }
  auto route = vrfIt->second.longestMatch(address);
  if (!route) {
    return std::nullopt;
  }
  const auto& fwdInfo = route->getForwardInfo();
  std::optional<std::pair<std::shared_ptr<Route<AddressT>>, RouteNextHopSet>>
      result;
  auto* nextHopIDManager = lockedRouteTables->nextHopIDManager.get();
  if (FLAGS_resolve_nexthops_from_id && nextHopIDManager) {
    auto setId = normalized ? fwdInfo.getNormalizedResolvedNextHopSetID()
                            : fwdInfo.getResolvedNextHopSetID();
    if (!setId.has_value()) {
      result = std::make_pair(route, RouteNextHopSet{});
    } else {
      result = std::make_pair(
          route,
          nextHopIDManager->getNextHops(static_cast<NextHopSetID>(*setId)));
    }
  } else {
    result = std::make_pair(
        route,
        normalized ? fwdInfo.normalizedNextHops(lockedRouteTables->ecmpWidth)
                   : fwdInfo.getNextHopSet());
  }
  return result;
}

template std::shared_ptr<Route<folly::IPAddressV4>>
RibRouteTables::longestMatch(const folly::IPAddressV4& address, RouterID vrf)
    const;
template std::shared_ptr<Route<folly::IPAddressV6>>
RibRouteTables::longestMatch(const folly::IPAddressV6& address, RouterID vrf)
    const;

template void reconstructRib<
    folly::IPAddressV4,
    ForwardingInformationBase<folly::IPAddressV4>,
    std::unordered_map<
        folly::CIDRNetworkV4,
        std::shared_ptr<Route<folly::IPAddressV4>>>>(
    const std::shared_ptr<ForwardingInformationBase<folly::IPAddressV4>>& fib,
    NetworkToRouteMap<folly::IPAddressV4>* addrToRoute,
    const std::unordered_map<
        folly::CIDRNetworkV4,
        std::shared_ptr<Route<folly::IPAddressV4>>>& unresolvedIndex);
template void reconstructRib<
    folly::IPAddressV6,
    ForwardingInformationBase<folly::IPAddressV6>,
    std::unordered_map<
        folly::CIDRNetworkV6,
        std::shared_ptr<Route<folly::IPAddressV6>>>>(
    const std::shared_ptr<ForwardingInformationBase<folly::IPAddressV6>>& fib,
    NetworkToRouteMap<folly::IPAddressV6>* addrToRoute,
    const std::unordered_map<
        folly::CIDRNetworkV6,
        std::shared_ptr<Route<folly::IPAddressV6>>>& unresolvedIndex);
template void reconstructRib<
    LabelID,
    MultiLabelForwardingInformationBase,
    std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>>(
    const std::shared_ptr<MultiLabelForwardingInformationBase>& fib,
    NetworkToRouteMap<LabelID>* addrToRoute,
    const std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>&
        unresolvedIndex);

template std::optional<
    std::pair<std::shared_ptr<Route<folly::IPAddressV4>>, RouteNextHopSet>>
RibRouteTables::getRouteAndNextHops(
    const folly::IPAddressV4& address,
    RouterID vrf,
    bool normalized) const;
template std::optional<
    std::pair<std::shared_ptr<Route<folly::IPAddressV6>>, RouteNextHopSet>>
RibRouteTables::getRouteAndNextHops(
    const folly::IPAddressV6& address,
    RouterID vrf,
    bool normalized) const;

RouteNextHopSet getNextHopsFromRib(
    const NextHopIDManager* manager,
    NextHopSetID id) {
  CHECK(manager) << "Manager required for getNextHopsFromRib";
  auto nhops = manager->getNextHopsIf(id);
  if (!nhops.has_value()) {
    throw FbossError(
        "NextHopSetID ",
        static_cast<int64_t>(id),
        " not found in NextHopIDManager");
  }
  return *nhops;
}

RouteNextHopSet getClientNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry) {
  if (FLAGS_resolve_nexthops_from_id) {
    CHECK(FLAGS_enable_nexthop_id_manager)
        << "FLAGS_resolve_nexthops_from_id requires FLAGS_enable_nexthop_id_manager";
    auto clientSetId = entry.getClientNextHopSetID();
    if (!clientSetId.has_value()) {
      CHECK(entry.getNextHopSet().empty())
          << "FLAGS_resolve_nexthops_from_id is on but per-client entry "
          << "has nexthops and no clientNextHopSetID";
      return {};
    }
    return getNextHopsFromRib(manager, NextHopSetID(*clientSetId));
  }
  return entry.getNextHopSet();
}

RouteNextHopSet getResolvedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry) {
  if (FLAGS_resolve_nexthops_from_id) {
    CHECK(FLAGS_enable_nexthop_id_manager)
        << "FLAGS_resolve_nexthops_from_id requires FLAGS_enable_nexthop_id_manager";
    auto resolvedSetId = entry.getResolvedNextHopSetID();
    if (!resolvedSetId.has_value()) {
      CHECK(entry.getAction() != RouteForwardAction::NEXTHOPS)
          << "FLAGS_resolve_nexthops_from_id is on but NEXTHOPS-action "
          << "entry has no resolvedNextHopSetID";
      return {};
    }
    return getNextHopsFromRib(manager, *resolvedSetId);
  }
  return entry.getNextHopSet();
}

void RibRouteTables::setEcmpWidth(uint32_t ecmpWidth) {
  synchronizedRouteTables_.wlock()->ecmpWidth = ecmpWidth;
}

uint32_t RibRouteTables::getEcmpWidth() const {
  return synchronizedRouteTables_.rlock()->ecmpWidth;
}

void RoutingInformationBase::setEcmpWidth(uint32_t ecmpWidth) {
  ribTables_.setEcmpWidth(ecmpWidth);
}

uint32_t RoutingInformationBase::getEcmpWidth() const {
  return ribTables_.getEcmpWidth();
}

void RibRouteTables::updateEcmpOverrides(const StateDelta& delta) {
  updateEcmpOverrides(getVrfList(), delta);
}

void RoutingInformationBase::updateEcmpOverrides(const StateDelta& delta) {
  ensureRunning();
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait(
      [this, &delta]() { ribTables_.updateEcmpOverrides(delta); });
}

void RibRouteTables::syncStateFromRib(
    const SwitchIdScopeResolver* resolver,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) const {
  auto lockedRouteTables = synchronizedRouteTables_.rlock();
  std::vector<RibVrfFib> vrfFibs;
  for (const auto& [vrf, routeTable] :
       lockedRouteTables->routerIDToRouteTable) {
    vrfFibs.push_back(
        RibVrfFib{
            vrf,
            &routeTable.v4NetworkToRoute,
            &routeTable.v6NetworkToRoute,
            &routeTable.labelToRoute});
  }
  ribToSwitchStateFunc(
      resolver,
      vrfFibs,
      lockedRouteTables->nextHopIDManager.get(),
      lockedRouteTables->mySidTable,
      cookie);
}

void RoutingInformationBase::syncStateFromRib(
    const SwitchIdScopeResolver* resolver,
    const RibToSwitchStateFunction& ribToSwitchStateFunc,
    void* cookie) {
  ensureRunning();
  ribUpdateEventBase_.runInFbossEventBaseThreadAndWait([&]() {
    ribTables_.syncStateFromRib(resolver, ribToSwitchStateFunc, cookie);
  });
}

RouteNextHopSet getNonOverrideNormalizedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry,
    uint32_t ecmpWidth) {
  if (FLAGS_resolve_nexthops_from_id) {
    CHECK(FLAGS_enable_nexthop_id_manager)
        << "FLAGS_resolve_nexthops_from_id requires FLAGS_enable_nexthop_id_manager";
    auto normalizedSetId = entry.getNormalizedResolvedNextHopSetID();
    if (!normalizedSetId.has_value()) {
      CHECK(entry.getAction() != RouteForwardAction::NEXTHOPS)
          << "FLAGS_resolve_nexthops_from_id is on but NEXTHOPS-action "
          << "entry has no normalizedResolvedNextHopSetID";
      return {};
    }
    return getNextHopsFromRib(manager, NextHopSetID(*normalizedSetId));
  }
  return entry.nonOverrideNormalizedNextHops(ecmpWidth);
}

RouteNextHopSet getNormalizedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry,
    uint32_t ecmpWidth) {
  if (entry.getOverrideNextHops().has_value()) {
    // Override nexthops are inline for now;
    // normalizedNextHops() handles the override normalization path correctly.
    return entry.normalizedNextHops(ecmpWidth);
  }
  // No overrides, delegate to ID-aware non-override path.
  return getNonOverrideNormalizedNextHopsFromRib(manager, entry, ecmpWidth);
}

RouteNextHopSet resolveNextHopSetFromRib(
    const VrfRouteTables& routeTables,
    const NextHopIDManager* manager,
    const RouteNextHopSet& nhops,
    uint32_t ecmpWidth) {
  RouteNextHopSet resolved;
  for (const auto& nh : nhops) {
    auto nhResolved = resolveNhopFromRib(routeTables, manager, nh, ecmpWidth);
    resolved.insert(nhResolved.begin(), nhResolved.end());
  }
  return resolved;
}

void resolveNamedNhgs(
    const VrfRouteTables& routeTables,
    NextHopIDManager* manager,
    const std::set<std::string>& names,
    uint32_t ecmpWidth) {
  for (const auto& name : names) {
    auto nhops = manager->getNextHopsForName(name);
    if (!nhops.has_value()) {
      XLOG(WARN) << "no named next-hop group '" << name
                 << "'; leaving its normalized id in place";
      continue;
    }
    manager->updateNormalizedNamedNhgId(
        name,
        resolveNextHopSetFromRib(routeTables, manager, *nhops, ecmpWidth),
        ecmpWidth);
  }
}

void resolveNamedNhgs(
    const VrfRouteTables& routeTables,
    NextHopIDManager* manager,
    uint32_t ecmpWidth) {
  // Runs on every route update, so walk the policies rather than every named
  // group - only groups a policy names carry a normalized id.
  std::set<std::string> names;
  for (const auto& [_policyName, policy] : manager->getPbrPolicyToNamedNhg()) {
    names.insert(policy.defaultNexthopGroup);
    for (const auto& [_fc, nhgName] : policy.class2NextHopGroup) {
      names.insert(nhgName);
    }
  }
  resolveNamedNhgs(routeTables, manager, names, ecmpWidth);
}

} // namespace facebook::fboss
