/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/FibHelpers.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBase.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"

#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {

template <typename AddrT>
std::shared_ptr<Route<AddrT>> findInFib(
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<ForwardingInformationBase<AddrT>>& fibRoutes) {
  if constexpr (std::is_same_v<AddrT, folly::IPAddressV6>) {
    return fibRoutes->exactMatch({prefix.first.asV6(), prefix.second});
  } else {
    return fibRoutes->exactMatch({prefix.first.asV4(), prefix.second});
  }
}

template <typename AddrT>
std::shared_ptr<Route<AddrT>> findRouteInSwitchState(
    RouterID rid,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<SwitchState>& state,
    bool exactMatch) {
  if (!exactMatch) {
    CHECK_EQ(prefix.second, prefix.first.bitCount())
        << " Longest match must pass in a IPAddress";
  }
  CHECK(exactMatch)
      << "Switch state api should only be called for exact match lookups";

  auto& fib = state->getFibsInfoMap()->getFibContainer(rid)->getFib<AddrT>();

  return findInFib(prefix, fib);
}
} // namespace

template <typename AddrT>
std::shared_ptr<Route<AddrT>> findRoute(
    RouterID rid,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<SwitchState>& state) {
  return findRouteInSwitchState<AddrT>(rid, prefix, state, true);
}

template <typename AddrT>
std::shared_ptr<Route<AddrT>> findLongestMatchRoute(
    const RoutingInformationBase* rib,
    RouterID rid,
    const AddrT& addr,
    const std::shared_ptr<SwitchState>& state) {
  return rib->longestMatch(addr, rid);
}

std::pair<uint64_t, uint64_t> getRouteCount(
    const std::shared_ptr<SwitchState>& state) {
  uint64_t v6Count{0}, v4Count{0};
  std::tie(v4Count, v6Count) = state->getFibsInfoMap()->getRouteCount();
  return std::make_pair(v4Count, v6Count);
}

template <typename NeighborEntryT>
bool isNoHostRoute(const std::shared_ptr<NeighborEntryT>& entry) {
  /*
   * classID could be associated with a Host entry. However, there is no
   * Neighbor entry for Link Local addresses, thus don't assign classID for
   * Link Local addresses.
   * SAI implementations typically fail creation of SAI Neighbor Entry with
   * both classID and NoHostRoute (set for IPv6 link local only) are set.
   */
  if constexpr (std::is_same_v<NeighborEntryT, NdpEntry>) {
    if (entry->getIP().isLinkLocal()) {
      XLOG(DBG2) << "No classID for IPv6 linkLocal: " << entry->str();
      return true;
    }
  }
  return false;
}

// Explicit instantiations
template std::shared_ptr<Route<folly::IPAddressV6>> findRoute(
    RouterID rid,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<SwitchState>& state);

template std::shared_ptr<Route<folly::IPAddressV4>> findRoute(
    RouterID rid,
    const folly::CIDRNetwork& prefix,
    const std::shared_ptr<SwitchState>& state);

template std::shared_ptr<Route<folly::IPAddressV4>> findLongestMatchRoute(
    const RoutingInformationBase* rib,
    RouterID rid,
    const folly::IPAddressV4& addr,
    const std::shared_ptr<SwitchState>& state);

template std::shared_ptr<Route<folly::IPAddressV6>> findLongestMatchRoute(
    const RoutingInformationBase* rib,
    RouterID rid,
    const folly::IPAddressV6& addr,
    const std::shared_ptr<SwitchState>& state);

template bool isNoHostRoute(const std::shared_ptr<MacEntry>& entry);
template bool isNoHostRoute(const std::shared_ptr<NdpEntry>& entry);
template bool isNoHostRoute(const std::shared_ptr<ArpEntry>& entry);

std::vector<NextHop> getNextHops(
    const std::shared_ptr<FibInfo>& fibInfo,
    NextHopSetId id) {
  return fibInfo->resolveNextHopSetFromId(id);
}

std::vector<NextHop> getNextHops(
    const std::shared_ptr<SwitchState>& state,
    NextHopSetId id) {
  auto fibsInfoMap = state->getFibsInfoMap();
  if (!fibsInfoMap || fibsInfoMap->empty()) {
    throw FbossError("FibsInfoMap is not initialized or empty");
  }

  // If there's only one FibInfo, use it directly without lookup
  if (fibsInfoMap->size() == 1) {
    return getNextHops(fibsInfoMap->cbegin()->second, id);
  }

  // Multiple FibInfo entries: search for the one containing this ID
  for (const auto& [_, fibInfo] : std::as_const(*fibsInfoMap)) {
    auto idToNextHopIdSetMap = fibInfo->getIdToNextHopIdSetMap();
    if (idToNextHopIdSetMap && idToNextHopIdSetMap->getNextHopIdSetIf(id)) {
      return getNextHops(fibInfo, id);
    }
  }

  throw FbossError("NextHopSetId ", id, " not found in any FibInfo");
}

RouteNextHopSet getNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (FLAGS_resolve_nexthops_from_id) {
    CHECK(FLAGS_enable_nexthop_id_manager)
        << "FLAGS_resolve_nexthops_from_id requires FLAGS_enable_nexthop_id_manager";
    auto resolvedSetId = entry.getResolvedNextHopSetID();
    if (!resolvedSetId.has_value()) {
      CHECK(entry.isDrop() || entry.isToCPU())
          << "FLAGS_resolve_nexthops_from_id is on but NEXTHOPS-action route "
          << "has no resolvedNextHopSetID";
      return {};
    }
    auto nhops = getNextHops(state, static_cast<NextHopSetId>(*resolvedSetId));
    return RouteNextHopSet(nhops.begin(), nhops.end());
  }
  return entry.getNextHopSet();
}

RouteNextHopSet getNonOverrideNormalizedNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (FLAGS_resolve_nexthops_from_id) {
    CHECK(FLAGS_enable_nexthop_id_manager)
        << "FLAGS_resolve_nexthops_from_id requires FLAGS_enable_nexthop_id_manager";
    auto normalizedSetId = entry.getNormalizedResolvedNextHopSetID();
    if (!normalizedSetId.has_value()) {
      CHECK(entry.isDrop() || entry.isToCPU())
          << "FLAGS_resolve_nexthops_from_id is on but NEXTHOPS-action route "
          << "has no normalizedResolvedNextHopSetID";
      return {};
    }
    auto nhops =
        getNextHops(state, static_cast<NextHopSetId>(*normalizedSetId));
    return RouteNextHopSet(nhops.begin(), nhops.end());
  }
  return RouteNextHopEntry::normalizeNextHops(
      entry.getNextHopSet(), getEcmpWidth(state));
}

RouteNextHopSet getClientNextHops(
    const std::shared_ptr<SwitchState>& state,
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
    auto nhops = getNextHops(state, static_cast<NextHopSetId>(*clientSetId));
    return RouteNextHopSet(nhops.begin(), nhops.end());
  }
  return entry.getNextHopSet();
}

std::shared_ptr<SwitchState> getNewStateWithOldFibInfo(
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState) {
  auto result = newState->clone();
  if (oldState->getFibsInfoMap() && !oldState->getFibsInfoMap()->empty()) {
    for (const auto& [matcherStr, oldFibInfo] :
         std::as_const(*oldState->getFibsInfoMap())) {
      auto fibInfoPtr =
          result->getFibsInfoMap()->getNodeIf(matcherStr)->modify(&result);
      fibInfoPtr->resetFibsMap(oldFibInfo->getfibsMap());
      if (FLAGS_enable_nexthop_id_manager) {
        // Reset ID maps to old state so old routes still in
        // intermediate deltas can resolve their IDs.
        fibInfoPtr->setIdToNextHopIdSetMap(
            oldFibInfo->getIdToNextHopIdSetMap());
        fibInfoPtr->setIdToNextHopMap(oldFibInfo->getIdToNextHopMap());
      }
    }
  } else {
    // Cater for when old state is empty - e.g. warmboot, rollback
    auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    for (const auto& [matcherStr, curFibInfo] :
         std::as_const(*newState->getFibsInfoMap())) {
      auto fibInfo = std::make_shared<FibInfo>();
      auto curFibsMap = curFibInfo->getfibsMap();
      if (curFibsMap) {
        for (const auto& [rid, _] : std::as_const(*curFibsMap)) {
          fibInfo->updateFibContainer(
              std::make_shared<ForwardingInformationBaseContainer>(
                  RouterID(rid)),
              &result);
        }
      }
      fibInfoMap->addNode(matcherStr, fibInfo);
    }
    result->resetFibsInfoMap(std::move(fibInfoMap));
  }
  return result;
}

std::pair<std::shared_ptr<IdToNextHopIdSetMap>, std::shared_ptr<IdToNextHopMap>>
mergeNextHopIdMaps(
    const std::shared_ptr<IdToNextHopIdSetMap>& primarySetMap,
    const std::shared_ptr<IdToNextHopMap>& primaryNhMap,
    const std::shared_ptr<IdToNextHopIdSetMap>& secondarySetMap,
    const std::shared_ptr<IdToNextHopMap>& secondaryNhMap) {
  CHECK(!secondarySetMap || secondaryNhMap)
      << "idToNextHopMap missing but idToNextHopIdSetMap exists";
  auto mergedSetMap = primarySetMap ? primarySetMap->clone()
                                    : std::make_shared<IdToNextHopIdSetMap>();
  auto mergedNhMap =
      primaryNhMap ? primaryNhMap->clone() : std::make_shared<IdToNextHopMap>();
  if (secondarySetMap) {
    for (const auto& [setId, setNode] : std::as_const(*secondarySetMap)) {
      if (mergedSetMap->getNextHopIdSetIf(setId)) {
        continue;
      }
      auto nextHopIds = setNode->toThrift();
      mergedSetMap->addNextHopIdSet(setId, nextHopIds);
      for (auto nhId : nextHopIds) {
        if (!mergedNhMap->getNextHopIf(nhId)) {
          auto nhNode = secondaryNhMap->getNextHopIf(nhId);
          CHECK(nhNode) << "NextHopId " << nhId
                        << " missing from secondary idToNextHopMap";
          mergedNhMap->addNextHop(nhId, nhNode->toThrift());
        }
      }
    }
  }
  return {std::move(mergedSetMap), std::move(mergedNhMap)};
}

std::shared_ptr<SwitchState> syncIdMapsFromState(
    const std::shared_ptr<SwitchState>& sourceState,
    const std::shared_ptr<SwitchState>& dstState) {
  auto srcFibsInfoMap = sourceState->getFibsInfoMap();
  auto result = dstState->isPublished() ? dstState->clone() : dstState;
  for (const auto& [matcherStr, srcFibInfo] : std::as_const(*srcFibsInfoMap)) {
    auto dstFibInfo = result->getFibsInfoMap()->getNodeIf(matcherStr);
    CHECK(dstFibInfo) << "FibInfo for " << matcherStr
                      << " not found in dstState";
    auto dstFibInfoPtr =
        result->getFibsInfoMap()->getNodeIf(matcherStr)->modify(&result);
    dstFibInfoPtr->setIdToNextHopIdSetMap(srcFibInfo->getIdToNextHopIdSetMap());
    dstFibInfoPtr->setIdToNextHopMap(srcFibInfo->getIdToNextHopMap());
  }
  return result;
}

template <typename AddrT>
void populateIdMapsForRoute(
    const std::shared_ptr<Route<AddrT>>& route,
    const std::shared_ptr<SwitchState>& sourceState,
    std::shared_ptr<IdToNextHopIdSetMap>& dstIdToSetMap,
    std::shared_ptr<IdToNextHopMap>& dstIdToNhMap) {
  const auto& fwdInfo = route->getForwardInfo();
  auto resolvedSetId = fwdInfo.getResolvedNextHopSetID();
  auto normalizedSetId = fwdInfo.getNormalizedResolvedNextHopSetID();
  if (!resolvedSetId.has_value() && !normalizedSetId.has_value()) {
    return;
  }

  auto populateForSetId =
      [&](NextHopSetId setId) {
        if (dstIdToSetMap->getNextHopIdSetIf(setId)) {
          return;
        }
        auto sourceFibsInfoMap = sourceState->getFibsInfoMap();
        for (const auto& [matcherStr, srcFibInfo] :
             std::as_const(*sourceFibsInfoMap)) {
          auto srcIdToSetMap = srcFibInfo->getIdToNextHopIdSetMap();
          if (!srcIdToSetMap) {
            continue;
          }
          auto srcSetNode = srcIdToSetMap->getNextHopIdSetIf(setId);
          if (!srcSetNode) {
            continue;
          }
          auto srcIdToNhMap = srcFibInfo->getIdToNextHopMap();
          CHECK(srcIdToNhMap)
              << "idToNextHopMap missing but idToNextHopIdSetMap exists";
          std::set<NextHopId> nextHopIds;
          for (const auto& elem : std::as_const(*srcSetNode)) {
            nextHopIds.insert((*elem).toThrift());
          }
          dstIdToSetMap->addNextHopIdSet(setId, nextHopIds);

          for (auto nhId : nextHopIds) {
            if (!dstIdToNhMap->getNextHopIf(nhId)) {
              auto srcNhNode = srcIdToNhMap->getNextHopIf(nhId);
              CHECK(srcNhNode)
                  << "NextHopId " << nhId << " missing from idToNextHopMap";
              dstIdToNhMap->addNextHop(nhId, srcNhNode->toThrift());
            }
          }
          return;
        }
        throw FbossError(
            "NextHopSetId ",
            setId,
            " not found in sourceState idToNextHopIdSetMap");
      };

  if (resolvedSetId.has_value()) {
    populateForSetId(static_cast<NextHopSetId>(*resolvedSetId));
  }
  if (normalizedSetId.has_value()) {
    populateForSetId(static_cast<NextHopSetId>(*normalizedSetId));
  }
}

template void populateIdMapsForRoute<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& route,
    const std::shared_ptr<SwitchState>& sourceState,
    std::shared_ptr<IdToNextHopIdSetMap>& dstIdToSetMap,
    std::shared_ptr<IdToNextHopMap>& dstIdToNhMap);

template void populateIdMapsForRoute<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& route,
    const std::shared_ptr<SwitchState>& sourceState,
    std::shared_ptr<IdToNextHopIdSetMap>& dstIdToSetMap,
    std::shared_ptr<IdToNextHopMap>& dstIdToNhMap);

RouteNextHopSet getNormalizedNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (entry.getOverrideNextHops().has_value()) {
    // Override nexthops are inline for now
    // normalizedNextHops() handles the override normalization path correctly.
    return entry.normalizedNextHops(getEcmpWidth(state));
  }
  // No overrides, delegate to ID-aware non-override path.
  return getNonOverrideNormalizedNextHops(state, entry);
}

RouteNextHopSet getMplsNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (!FLAGS_mpls_rib) {
    return entry.getNextHopSet();
  }
  return getNextHops(state, entry);
}

RouteNextHopSet getMplsClientNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (!FLAGS_mpls_rib) {
    return entry.getNextHopSet();
  }
  return getClientNextHops(state, entry);
}

RouteNextHopSet getMplsNormalizedNextHops(
    const std::shared_ptr<SwitchState>& state,
    const RouteNextHopEntry& entry) {
  if (!FLAGS_mpls_rib) {
    return entry.normalizedNextHops();
  }
  return getNormalizedNextHops(state, entry);
}

} // namespace facebook::fboss
