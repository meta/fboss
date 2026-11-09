// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <set>
#include <string>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/HwSwitchMatcher.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

// Nexthop addresses one client contributed to a RouteDetails or
// MplsRouteDetails.
template <typename Details, typename Client>
std::set<std::string> clientNextHops(const Details& details, Client client) {
  std::set<std::string> nhops;
  for (const auto& clientNhops : *details.nextHopMulti()) {
    if (*clientNhops.clientId() != static_cast<int32_t>(client)) {
      continue;
    }
    for (const auto& nh : *clientNhops.nextHops()) {
      nhops.insert(facebook::network::toIPAddress(*nh.address()).str());
    }
  }
  return nhops;
}

// Allocate resolvedNextHopSetID and normalizedResolvedNextHopSetID on a single
// RouteNextHopEntry. No-op if idManager is null or action is not NEXTHOPS.
// Safe to call multiple times (uses getOrAlloc).
void allocateRouteNextHopIds(
    NextHopIDManager* idManager,
    RouteNextHopEntry& entry);

// Populate idToNextHopMap and idToNextHopIdSetMap in FibInfo from idManager.
// No-op if idManager is null. Call after all IDs are allocated.
void populateFibInfoIdMaps(
    const NextHopIDManager* idManager,
    std::shared_ptr<SwitchState>& state,
    const HwSwitchMatcher& scope =
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));

// Iterate all resolved routes in state, allocate IDs on each, then populate
// FibInfo maps. No-op if idManager is null. One-call convenience for tests
// that build complete states.
void assignNextHopIdsToAllRoutes(
    NextHopIDManager* idManager,
    std::shared_ptr<SwitchState>& state,
    const HwSwitchMatcher& scope =
        HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)})));

} // namespace facebook::fboss
