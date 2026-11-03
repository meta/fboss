/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/rib/ConfigApplier.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/MySid.h"

#include <folly/logging/xlog.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace facebook::fboss {

namespace {

// True if config entry `incoming` needs no replace of `existing`: type,
// adjacencyInterfaceId, isV6 and the unresolved next-hop set match. The
// caller matches prefix and clientId. A config uA has no next hops, so the
// existing uA's next hops and FRR backup (owned by MySidNeighborObserver and
// the FRR API) are kept. Add any new config-set MySidFields field here, or
// changes to it are silently skipped.
bool mySidEntryUnchanged(
    const MySid& existing,
    const MySid& incoming,
    const RouteNextHopSet& incomingUnresolvedNhops,
    const NextHopIDManager* nextHopIDManager) {
  if (existing.getType() != incoming.getType() ||
      existing.getAdjacencyInterfaceId() !=
          incoming.getAdjacencyInterfaceId() ||
      existing.getIsV6() != incoming.getIsV6()) {
    return false;
  }
  if (incoming.getType() == MySidType::ADJACENCY_MICRO_SID &&
      incomingUnresolvedNhops.empty()) {
    return true;
  }
  // Compare unresolved next-hop sets. Existing carries an id into the
  // manager; incoming carries the raw set.
  const auto existingId = existing.getUnresolveNextHopsId();
  if (existingId.has_value() != !incomingUnresolvedNhops.empty()) {
    return false;
  }
  if (!existingId.has_value()) {
    return true; // both empty
  }
  // Without a manager we cannot read back the existing nhops to compare —
  // be conservative and treat as changed so the caller does the work.
  if (!nextHopIDManager) {
    return false;
  }
  auto existingNhops = nextHopIDManager->getNextHopsIf(*existingId);
  return existingNhops.has_value() && *existingNhops == incomingUnresolvedNhops;
}

} // namespace

ConfigApplier::ConfigApplier(
    RouterID vrf,
    IPv4NetworkToRouteMap* v4NetworkToRoute,
    IPv6NetworkToRouteMap* v6NetworkToRoute,
    LabelToRouteMap* labelToRoute,
    folly::Range<DirectlyConnectedRouteIterator> directlyConnectedRouteRange,
    folly::Range<StaticRouteNoNextHopsIterator> staticCpuRouteRange,
    folly::Range<StaticRouteNoNextHopsIterator> staticDropRouteRange,
    folly::Range<StaticRouteWithNextHopsIterator> staticRouteRange,
    folly::Range<StaticIp2MplsRouteIterator> staticIp2MplsRouteRange,
    folly::Range<StaticMplsRouteWithNextHopsIterator> staticMplsRouteRange,
    folly::Range<StaticMplsRouteNoNextHopsIterator> staticMplsDropRouteRange,
    folly::Range<StaticMplsRouteNoNextHopsIterator> staticMplsCpuRouteRange,
    folly::Range<StaticMySidIterator> staticMySidRange,
    NextHopIDManager* nextHopIDManager,
    MySidTable* mySidTable,
    uint32_t ecmpWidth)
    : vrf_(vrf),
      v4NetworkToRoute_(v4NetworkToRoute),
      v6NetworkToRoute_(v6NetworkToRoute),
      labelToRoute_(labelToRoute),
      directlyConnectedRouteRange_(directlyConnectedRouteRange),
      staticCpuRouteRange_(staticCpuRouteRange),
      staticDropRouteRange_(staticDropRouteRange),
      staticRouteRange_(staticRouteRange),
      staticIp2MplsRouteRange_(staticIp2MplsRouteRange),
      staticMplsRouteRange_(staticMplsRouteRange),
      staticMplsDropRouteRange_(staticMplsDropRouteRange),
      staticMplsCpuRouteRange_(staticMplsCpuRouteRange),
      staticMySidRange_(staticMySidRange),
      nextHopIDManager_(nextHopIDManager),
      mySidTable_(mySidTable),
      ecmpWidth_(ecmpWidth) {
  CHECK_NOTNULL(v4NetworkToRoute_);
  CHECK_NOTNULL(v6NetworkToRoute_);
  CHECK_NOTNULL(labelToRoute_);
}

void ConfigApplier::apply() {
  RibRouteUpdater updater(
      v4NetworkToRoute_,
      v6NetworkToRoute_,
      labelToRoute_,
      nextHopIDManager_,
      mySidTable_,
      ecmpWidth_);

  // Update static routes
  std::vector<RibRouteUpdater::RouteEntry> staticRoutes;
  staticRoutes.reserve(
      staticCpuRouteRange_.size() + staticDropRouteRange_.size() +
      staticRouteRange_.size());
  auto fillInStaticRoutes = [this, &staticRoutes](
                                const auto& routeRange, const auto& nhopFn) {
    for (const auto& staticRoute : routeRange) {
      if (RouterID(*staticRoute.routerID()) != vrf_) {
        continue;
      }
      auto prefix = folly::IPAddress::createNetwork(*staticRoute.prefix());
      staticRoutes.push_back({prefix, nhopFn(staticRoute)});
    }
  };
  fillInStaticRoutes(staticCpuRouteRange_, [](const auto& /*route*/) {
    return RouteNextHopEntry::createToCpu();
  });
  fillInStaticRoutes(staticDropRouteRange_, [](const auto& /*route*/) {
    return RouteNextHopEntry::createDrop();
  });
  fillInStaticRoutes(staticRouteRange_, [](const auto& route) {
    return RouteNextHopEntry::fromStaticRoute(route);
  });
  fillInStaticRoutes(staticIp2MplsRouteRange_, [](const auto& route) {
    return RouteNextHopEntry::fromStaticIp2MplsRoute(route);
  });
  // Update link local routes
  std::vector<RibRouteUpdater::RouteEntry> linkLocalRoutes;
  if (!directlyConnectedRouteRange_.empty()) {
    // Add link-local routes
    RouteNextHopEntry nextHop(
        RouteForwardAction::TO_CPU, AdminDistance::DIRECTLY_CONNECTED);
    linkLocalRoutes.push_back({{folly::IPAddress{"fe80::"}, 64}, nextHop});
  }

  // Update interface routes
  std::vector<RibRouteUpdater::RouteEntry> interfaceRoutes;
  for (const auto& directlyConnectedRoute : directlyConnectedRouteRange_) {
    auto network = directlyConnectedRoute.first;
    auto interfaceID = directlyConnectedRoute.second.first;
    auto address = directlyConnectedRoute.second.second;
    ResolvedNextHop resolvedNextHop(address, interfaceID, UCMP_DEFAULT_WEIGHT);
    RouteNextHopEntry nextHop(
        static_cast<NextHop>(resolvedNextHop),
        AdminDistance::DIRECTLY_CONNECTED);
    interfaceRoutes.emplace_back(network, nextHop);
  }
  updater.update(
      {{ClientID::STATIC_ROUTE, staticRoutes},
       {ClientID::LINKLOCAL_ROUTE, linkLocalRoutes},
       {ClientID::INTERFACE_ROUTE, interfaceRoutes}},
      {},
      {ClientID::STATIC_ROUTE,
       ClientID::LINKLOCAL_ROUTE,
       ClientID::INTERFACE_ROUTE});

  if (FLAGS_mpls_rib) {
    // Update static mpls routes
    std::vector<RibRouteUpdater::MplsRouteEntry> staticMplsRoutes;
    staticMplsRoutes.reserve(
        staticMplsCpuRouteRange_.size() + staticMplsDropRouteRange_.size() +
        staticMplsRouteRange_.size());
    auto fillInStaticMplsRoutes =
        [&staticMplsRoutes](const auto& routeRange, const auto& nhopFn) {
          for (const auto& staticRoute : routeRange) {
            staticMplsRoutes.push_back(
                {LabelID(staticRoute.get_ingressLabel()), nhopFn(staticRoute)});
          }
        };
    fillInStaticMplsRoutes(staticMplsCpuRouteRange_, [](const auto& /*route*/) {
      return RouteNextHopEntry::createToCpu();
    });
    fillInStaticMplsRoutes(
        staticMplsDropRouteRange_,
        [](const auto& /*route*/) { return RouteNextHopEntry::createDrop(); });
    fillInStaticMplsRoutes(staticMplsRouteRange_, [](const auto& route) {
      return RouteNextHopEntry::fromStaticMplsRoute(route);
    });
    updater.update(
        ClientID::STATIC_ROUTE, staticMplsRoutes, std::vector<LabelID>(), true);
  }

  applyStaticMySids();
}

void ConfigApplier::applyStaticMySids() {
  if (!mySidTable_) {
    return;
  }
  // Helper: release the primary next-hop IDs and, unless their ownership is
  // being transferred to a replacement adjacency MySid, the FRR backup IDs.
  auto releaseEntryNextHopIds = [&](const std::shared_ptr<MySid>& entry,
                                    bool releaseFrrProtection) {
    if (!nextHopIDManager_) {
      return;
    }
    if (const auto id = entry->getUnresolveNextHopsId()) {
      nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
    }
    if (const auto id = entry->getResolvedNextHopsId()) {
      nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
    }
    if (releaseFrrProtection) {
      if (const auto id = entry->getBackupUnresolveNextHopsId()) {
        nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
      }
      if (const auto id = entry->getBackupResolvedNextHopsId()) {
        nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
      }
    }
  };
  // Helper: build a fresh MySid from the incoming pair and allocate its
  // unresolveNextHopsId on the manager (if any next hops are present).
  auto buildAndAllocateEntry =
      [&](const std::shared_ptr<MySid>& mySidIn,
          const RouteNextHopSet& unresolvedNextHops) -> std::shared_ptr<MySid> {
    auto mySid = mySidIn->clone();
    if (nextHopIDManager_ && !unresolvedNextHops.empty()) {
      const auto newId =
          nextHopIDManager_->getOrAllocRouteNextHopSetID(unresolvedNextHops)
              .nextHopIdSetIter->second.id;
      mySid->setUnresolveNextHopsId(newId);
    }
    return mySid;
  };

  std::unordered_map<folly::CIDRNetworkV6, const MySidWithNextHops*> incoming;
  incoming.reserve(staticMySidRange_.size());
  for (const auto& pair : staticMySidRange_) {
    const auto cidr = pair.mySid->getMySid();
    incoming.emplace(
        folly::CIDRNetworkV6{cidr.first.asV6(), cidr.second}, &pair);
  }

  // Pass 1: walk the existing table. For each STATIC_ROUTE entry decide:
  //   - not in incoming → delete (release nhop ids)
  //   - in incoming AND unchanged → leave in place (no churn, no realloc)
  //   - in incoming AND changed → release old, install new
  // Mark the incoming CIDRs we've handled so pass 2 only adds the rest.
  std::unordered_set<folly::CIDRNetworkV6> handledIncomingCidrs;
  for (auto it = mySidTable_->begin(); it != mySidTable_->end();) {
    if (it->second->getClientId() != ClientID::STATIC_ROUTE) {
      ++it;
      continue;
    }
    auto incomingIt = incoming.find(it->first);
    if (incomingIt == incoming.end()) {
      releaseEntryNextHopIds(it->second, true /* releaseFrrProtection */);
      it = mySidTable_->erase(it);
      continue;
    }
    const auto& entry = *incomingIt->second;
    if (mySidEntryUnchanged(
            *it->second, *entry.mySid, entry.nextHopSet, nextHopIDManager_)) {
      handledIncomingCidrs.insert(it->first);
      ++it;
      continue;
    }
    auto newEntry = buildAndAllocateEntry(entry.mySid, entry.nextHopSet);
    const bool preserveFrrProtection =
        it->second->getType() == MySidType::ADJACENCY_MICRO_SID &&
        newEntry->getType() == MySidType::ADJACENCY_MICRO_SID;
    if (preserveFrrProtection) {
      newEntry->setBackupUnresolveNextHopsId(
          it->second->getBackupUnresolveNextHopsId());
      newEntry->setBackupResolvedNextHopsId(
          it->second->getBackupResolvedNextHopsId());
    }
    releaseEntryNextHopIds(it->second, !preserveFrrProtection);
    it->second = std::move(newEntry);
    handledIncomingCidrs.insert(it->first);
    ++it;
  }

  // Pass 2: install incoming entries that didn't match an existing
  // STATIC_ROUTE entry. If a non-STATIC_ROUTE entry sits at the same
  // prefix (e.g., a TE_AGENT RPC entry), release its nhop ids before
  // overwriting to avoid a leak — config-driven STATIC_ROUTE wins.
  for (const auto& entry : staticMySidRange_) {
    const auto cidr = entry.mySid->getMySid();
    const folly::CIDRNetworkV6 cidrV6(cidr.first.asV6(), cidr.second);
    if (handledIncomingCidrs.contains(cidrV6)) {
      continue;
    }
    auto newEntry = buildAndAllocateEntry(entry.mySid, entry.nextHopSet);
    if (auto it = mySidTable_->find(cidrV6); it != mySidTable_->end()) {
      // Non-STATIC_ROUTE entry was at this prefix (Pass 1 filtered those out
      // and left them in the table). Surface the takeover so the operator/
      // RPC client knows their entry was clobbered by config.
      XLOG(WARNING) << "Config-driven STATIC_ROUTE MySid at "
                    << folly::IPAddress::networkToString(
                           std::make_pair(
                               folly::IPAddress(cidrV6.first), cidrV6.second))
                    << " is overwriting existing entry with clientId="
                    << static_cast<int>(it->second->getClientId());
      releaseEntryNextHopIds(it->second, true /* releaseFrrProtection */);
      it->second = std::move(newEntry);
    } else {
      mySidTable_->emplace(cidrV6, std::move(newEntry));
    }
  }
}

} // namespace facebook::fboss
