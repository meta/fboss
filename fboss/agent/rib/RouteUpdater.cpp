/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/rib/RouteUpdater.h"

#include <numeric>

#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/integer/common_factor.hpp>
#include <folly/logging/xlog.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RouteUpdaterUtils.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/NodeBase-defs.h"
#include "fboss/agent/state/Route.h"

#include <algorithm>
#include <type_traits>
#include "fboss/agent/Utils.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/RouteTypes.h"
#include "folly/IPAddressV4.h"

DEFINE_int32(nsf_rack_id, 1, "Rack id inNSF pod");
DEFINE_int32(nsf_num_racks_per_pod, 6, "Number of racks in NSF pod");
DEFINE_int32(nsf_num_parallel_rack_links, 6, "Parallel links in NSF pod");
DEFINE_int32(
    nsf_num_spine_failures_to_skip,
    0,
    "Spine failure count to skip pruning");
DEFINE_int32(
    nsf_spine_prune_step_count,
    1,
    "Step count for next spine failure to prune");
DEFINE_bool(
    enable_capacity_pruning,
    false,
    "Enable path pruning based on capacity");
DEFINE_bool(
    enable_fpf_capacity_pruning,
    false,
    "Enable FPF (GTSW/STSW) per-STSW path pruning based on capacity");

using boost::container::flat_map;
using boost::container::flat_set;
using folly::CIDRNetwork;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;

namespace facebook::fboss {

namespace {
template <typename AddressT>
std::shared_ptr<Route<AddressT>> writableRoute(
    typename NetworkToRouteMap<AddressT>::Iterator ritr) {
  if (value<AddressT>(ritr)->isPublished()) {
    value<AddressT>(ritr) = value<AddressT>(ritr)->clone();
  }
  return value<AddressT>(ritr);
}

template <typename AddressT>
std::shared_ptr<Route<AddressT>> writableRoute(
    std::shared_ptr<Route<AddressT>> route) {
  if (route->isPublished()) {
    route = route->clone();
  }
  return route;
}
} // namespace
static const RoutePrefixV6 kIPv6LinkLocalPrefix{
    folly::IPAddressV6("fe80::"),
    64};
static const auto kInterfaceRouteClientId = ClientID::INTERFACE_ROUTE;
static const auto kRemoteInterfaceRouteClientId =
    ClientID::REMOTE_INTERFACE_ROUTE;

RibRouteUpdater::RibRouteUpdater(
    IPv4NetworkToRouteMap* v4Routes,
    IPv6NetworkToRouteMap* v6Routes,
    NextHopIDManager* nextHopIDManager,
    MySidTable* mySidTable,
    uint32_t ecmpWidth,
    RouterID routerID)
    : v4Routes_(v4Routes),
      v6Routes_(v6Routes),
      nextHopIDManager_(nextHopIDManager),
      mySidTable_(mySidTable),
      ecmpWidth_(ecmpWidth),
      routerID_(routerID),
      weightNormalizer_(
          FLAGS_nsf_num_racks_per_pod,
          FLAGS_nsf_num_parallel_rack_links,
          FLAGS_nsf_rack_id,
          FLAGS_nsf_num_spine_failures_to_skip,
          FLAGS_nsf_spine_prune_step_count,
          FLAGS_enable_fpf_capacity_pruning) {}

RibRouteUpdater::RibRouteUpdater(
    IPv4NetworkToRouteMap* v4Routes,
    IPv6NetworkToRouteMap* v6Routes,
    LabelToRouteMap* mplsRoutes,
    NextHopIDManager* nextHopIDManager,
    MySidTable* mySidTable,
    uint32_t ecmpWidth,
    RouterID routerID)
    : v4Routes_(v4Routes),
      v6Routes_(v6Routes),
      mplsRoutes_(mplsRoutes),
      nextHopIDManager_(nextHopIDManager),
      mySidTable_(mySidTable),
      ecmpWidth_(ecmpWidth),
      routerID_(routerID),
      weightNormalizer_(
          FLAGS_nsf_num_racks_per_pod,
          FLAGS_nsf_num_parallel_rack_links,
          FLAGS_nsf_rack_id,
          FLAGS_nsf_num_spine_failures_to_skip,
          FLAGS_nsf_spine_prune_step_count,
          FLAGS_enable_fpf_capacity_pruning) {}

void RibRouteUpdater::update(
    const std::map<ClientID, std::vector<RouteEntry>>& toAdd,
    const std::map<ClientID, std::vector<folly::CIDRNetwork>>& toDel,
    const std::set<ClientID>& resetClientsRoutesFor) {
  std::set<ClientID> clients;
  std::for_each(toAdd.begin(), toAdd.end(), [&clients](const auto& entry) {
    clients.insert(entry.first);
  });
  std::for_each(toDel.begin(), toDel.end(), [&clients](const auto& entry) {
    clients.insert(entry.first);
  });
  clients.insert(resetClientsRoutesFor.begin(), resetClientsRoutesFor.end());
  for (auto client : clients) {
    auto addItr = toAdd.find(client);
    auto delItr = toDel.find(client);
    updateImpl(
        client,
        (addItr == toAdd.end() ? std::vector<RouteEntry>() : addItr->second),
        (delItr == toDel.end() ? std::vector<folly::CIDRNetwork>()
                               : delItr->second),
        resetClientsRoutesFor.contains(client));
  }
  updateDone();
}

void RibRouteUpdater::updateImpl(
    ClientID client,
    const std::vector<RouteEntry>& toAdd,
    const std::vector<folly::CIDRNetwork>& toDel,
    bool resetClientsRoutes) {
  std::for_each(
      toAdd.begin(), toAdd.end(), [this, client](const auto& routeEntry) {
        addOrReplaceRoute(
            routeEntry.prefix.first,
            routeEntry.prefix.second,
            client,
            routeEntry.nhopEntry);
      });
  std::for_each(toDel.begin(), toDel.end(), [this, client](const auto& prefix) {
    delRoute(prefix.first, prefix.second, client);
  });
  if (resetClientsRoutes) {
    removeAllUnclaimedRoutesForClient(client, toAdd);
  }
}

void RibRouteUpdater::updateImpl(
    ClientID client,
    const std::vector<MplsRouteEntry>& toAdd,
    const std::vector<LabelID>& toDel,
    bool resetClientsRoutes) {
  std::for_each(
      toAdd.begin(), toAdd.end(), [this, client](const auto& routeEntry) {
        addOrReplaceRoute(routeEntry.label, client, routeEntry.nhopEntry);
      });
  std::for_each(toDel.begin(), toDel.end(), [this, client](const auto& label) {
    delRoute(label, client);
  });
  if (resetClientsRoutes) {
    removeAllUnclaimedMplsRoutesForClient(client, toAdd);
  }
}

void RibRouteUpdater::releaseFwdSideNexthopSetIDs(
    const RouteNextHopEntry& fwd) {
  if (!nextHopIDManager_) {
    return;
  }
  if (auto id = fwd.getResolvedNextHopSetID()) {
    nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
  }
  if (auto id = fwd.getNormalizedResolvedNextHopSetID()) {
    nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*id);
  }
}

std::optional<RouteNextHopEntry> RibRouteUpdater::stampClientNextHopSetID(
    ClientID clientID,
    const std::shared_ptr<const RouteNextHopEntry>& existingRouteForClient,
    const RouteNextHopEntry& entry,
    const std::string& routeDescription) {
  if (!nextHopIDManager_) {
    return std::nullopt;
  }
  std::optional<NextHopSetID> oldClientNextHopSetID = existingRouteForClient
      ? existingRouteForClient->getClientNextHopSetID()
      : std::nullopt;
  // Resolve the existing entry's nexthops via the manager so the empty-check
  // and same-nhops compare below stay correct once
  // FLAGS_resolve_nexthops_from_id is on (inline storage would be empty).
  const RouteNextHopSet existingNhops = existingRouteForClient
      ? getClientNextHopsFromRib(nextHopIDManager_, *existingRouteForClient)
      : RouteNextHopSet{};
  // Existing per-client entry with non-empty nexthops must already carry
  // an ID (allocated here or backfilled at warmboot).
  if (existingRouteForClient && !existingNhops.empty()) {
    CHECK(oldClientNextHopSetID.has_value())
        << "Existing per-client entry for " << routeDescription << " client "
        << static_cast<int>(clientID)
        << " has non-empty nexthops but no clientNextHopSetID";
  }
  RouteNextHopEntry entryWithId(entry.toThrift());
  std::optional<NextHopSetID> newClientNextHopSetID;
  if (existingRouteForClient && oldClientNextHopSetID.has_value() &&
      existingNhops == entry.getNextHopSet()) {
    // Same nexthops as before: reuse existing ID, no manager call.
    newClientNextHopSetID = oldClientNextHopSetID;
  } else if (!entry.getNextHopSet().empty()) {
    // Nexthop changes: alloc new (refcount++), then release old (refcount--).
    auto allocResult =
        nextHopIDManager_->getOrAllocRouteNextHopSetID(entry.getNextHopSet());
    newClientNextHopSetID = allocResult.nextHopIdSetIter->second.id;
    if (oldClientNextHopSetID.has_value()) {
      nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*oldClientNextHopSetID);
    }
  } else if (oldClientNextHopSetID.has_value()) {
    // New entry has empty nexthops (DROP / TO_CPU); release old ID.
    nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*oldClientNextHopSetID);
  }
  entryWithId.setClientNextHopSetID(newClientNextHopSetID);
  return entryWithId;
}

template <typename AddressT>
void RibRouteUpdater::addOrReplaceRouteImpl(
    const Prefix<AddressT>& prefix,
    NetworkToRouteMap<AddressT>* routes,
    ClientID clientID,
    const RouteNextHopEntry& entry) {
  auto it = routes->exactMatch(prefix.network(), prefix.mask());

  std::shared_ptr<const RouteNextHopEntry> existingRouteForClient;
  if (it != routes->end()) {
    existingRouteForClient = it->value()->getEntryForClient(clientID);
  }
  auto stamped = stampClientNextHopSetID(
      clientID, existingRouteForClient, entry, prefix.str());
  const RouteNextHopEntry& finalEntry = stamped ? *stamped : entry;

  if (it != routes->end()) {
    auto route = it->value();
    // Compare against the (possibly enriched) finalEntry so the no-op
    // short-circuit still works after the ID has been stamped (operator==
    // includes the ID).
    if (!existingRouteForClient || *existingRouteForClient != finalEntry) {
      route = writableRoute<AddressT>(it);
      route->update(clientID, finalEntry);
    }
    return;
  }

  routes->insert(
      prefix, std::make_shared<Route<AddressT>>(prefix, clientID, finalEntry));
}

void RibRouteUpdater::addOrReplaceRoute(
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientID,
    const RouteNextHopEntry& entry) {
  if (network.isV4()) {
    RoutePrefixV4 prefix{network.asV4().mask(mask), mask};
    addOrReplaceRouteImpl(prefix, v4Routes_, clientID, entry);
  } else {
    RoutePrefixV6 prefix{network.asV6().mask(mask), mask};
    addOrReplaceRouteImpl(prefix, v6Routes_, clientID, entry);
  }
}

void RibRouteUpdater::addOrReplaceRoute(
    LabelID label,
    ClientID clientID,
    const RouteNextHopEntry& entry) {
  XLOG(DBG3) << "Add mpls route for label " << label << " nh " << entry.str();
  auto iter = mplsRoutes_->find(label);
  std::shared_ptr<const RouteNextHopEntry> existingRouteForClient;
  if (iter != mplsRoutes_->end()) {
    existingRouteForClient = iter->second->getEntryForClient(clientID);
  }
  auto stamped = stampClientNextHopSetID(
      clientID,
      existingRouteForClient,
      entry,
      folly::to<std::string>("label=", static_cast<int>(label)));
  const RouteNextHopEntry& finalEntry = stamped ? *stamped : entry;

  if (iter == mplsRoutes_->end()) {
    mplsRoutes_->emplace(
        std::make_pair(
            label,
            std::make_shared<Route<LabelID>>(
                Route<LabelID>::makeThrift(label, clientID, finalEntry))));
  } else {
    auto& route = iter->second;
    if (!existingRouteForClient || *existingRouteForClient != finalEntry) {
      route = writableRoute<LabelID>(route);
      route->update(clientID, finalEntry);
    }
  }
}

template <typename AddressT>
void RibRouteUpdater::maybeRemoveNamedNhgMapping(
    const std::shared_ptr<Route<AddressT>>& route,
    const RouteNextHopEntry& clientEntry) {
  if constexpr (!std::is_same_v<AddressT, LabelID>) {
    if (!nextHopIDManager_) {
      return;
    }
    auto nhgName = clientEntry.getNamedNextHopGroup();
    if (!nhgName.has_value()) {
      return;
    }
    if (route->numClientsForNamedNhg(*nhgName) <= 1) {
      nextHopIDManager_->removeRouteForNamedNhg(
          *nhgName, routerID_, route->prefix().toCidrNetwork());
    }
  }
}

template <typename AddressT>
void RibRouteUpdater::delRouteImpl(
    const Prefix<AddressT>& prefix,
    NetworkToRouteMap<AddressT>* routes,
    ClientID clientID) {
  auto it = routes->exactMatch(prefix.network(), prefix.mask());
  if (it == routes->end()) {
    XLOG(DBG3) << "Failed to delete route: " << prefix.str()
               << " does not exist";
    return;
  }

  auto& route = it->value();
  auto clientNhopEntry = route->getEntryForClient(clientID);
  if (!clientNhopEntry) {
    return;
  }
  if (nextHopIDManager_) {
    auto clientNhopSetId = clientNhopEntry->getClientNextHopSetID();
    if (clientNhopSetId.has_value()) {
      nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*clientNhopSetId);
    }
  }
  maybeRemoveNamedNhgMapping(route, *clientNhopEntry);
  if (route->numClientEntries() == 1) {
    // If this client's the only entry, simply erase
    XLOG(DBG3) << "Deleting route: " << route->str();
    releaseFwdSideNexthopSetIDs(route->getForwardInfo());
    routes->erase(it);
  } else {
    route = writableRoute<AddressT>(it);
    route->delEntryForClient(clientID);

    XLOG(DBG3) << "Deleted next-hops for prefix " << prefix.str()
               << "from client " << folly::to<std::string>(clientID);
  }
}

void RibRouteUpdater::delRoute(
    const folly::IPAddress& network,
    uint8_t mask,
    ClientID clientID) {
  if (network.isV4()) {
    RoutePrefixV4 prefix{network.asV4().mask(mask), mask};
    delRouteImpl(prefix, v4Routes_, clientID);
  } else {
    CHECK(network.isV6());
    RoutePrefixV6 prefix{network.asV6().mask(mask), mask};
    delRouteImpl(prefix, v6Routes_, clientID);
  }
}

void RibRouteUpdater::delRoute(const LabelID& label, const ClientID clientID) {
  XLOG(DBG3) << "Delete mpls route for " << label;
  auto it = mplsRoutes_->find(label);
  if (it == mplsRoutes_->end()) {
    XLOG(DBG3) << "Failed to delete mpls route: label " << label
               << " does not exist";
    return;
  }

  auto& route = it->second;
  auto clientNhopEntry = route->getEntryForClient(clientID);
  if (!clientNhopEntry) {
    return;
  }
  // Release per-client clientNextHopSetID (symmetric with allocation in
  // addOrReplaceRoute(LabelID, ...)).
  if (nextHopIDManager_) {
    auto clientNhopSetId = clientNhopEntry->getClientNextHopSetID();
    if (clientNhopSetId.has_value()) {
      nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*clientNhopSetId);
    }
  }
  if (route->numClientEntries() == 1) {
    // If this client's the only entry, simply erase
    XLOG(DBG3) << "Deleting route: " << route->str();
    releaseFwdSideNexthopSetIDs(route->getForwardInfo());
    mplsRoutes_->erase(it);
  } else {
    route = writableRoute<LabelID>(route);
    route->delEntryForClient(clientID);

    XLOG(DBG3) << "Deleted next-hops for label " << label << "from client "
               << folly::to<std::string>(clientID);
  }
}

template <typename AddressT>
void RibRouteUpdater::removeAllRoutesFromClientImpl(
    NetworkToRouteMap<AddressT>* routes,
    ClientID clientID) {
  std::vector<typename NetworkToRouteMap<AddressT>::Iterator> toDelete;

  for (auto it = routes->begin(); it != routes->end(); ++it) {
    auto route = value<AddressT>(it);
    auto nhopEntry = route->getEntryForClient(clientID);
    if (!nhopEntry) {
      continue;
    }
    if (nextHopIDManager_) {
      auto clientNhopSetId = nhopEntry->getClientNextHopSetID();
      if (clientNhopSetId.has_value()) {
        nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*clientNhopSetId);
      }
    }
    maybeRemoveNamedNhgMapping(route, *nhopEntry);
    if (route->numClientEntries() == 1) {
      // This client's is the only entry avoid unnecessary cloning
      // we are going to prune the route anyways. Release fwd-side IDs first.
      releaseFwdSideNexthopSetIDs(route->getForwardInfo());
      toDelete.push_back(it);
    } else {
      route = writableRoute<AddressT>(it);
      route->delEntryForClient(clientID);
      if (route->hasNoEntry()) {
        // The nexthops we removed was the only one.  Delete the route->
        releaseFwdSideNexthopSetIDs(route->getForwardInfo());
        toDelete.push_back(it);
      }
    }
  }

  // Now, delete whatever routes went from 1 nexthoplist to 0.
  for (auto it : toDelete) {
    routes->erase(it);
  }
}

template <typename AddressT, typename FilterFunc>
void RibRouteUpdater::removeAllUnclaimedRoutesFromClientImpl(
    const FilterFunc& isClaimedFunc,
    NetworkToRouteMap<AddressT>* routes,
    ClientID clientID) {
  std::vector<typename NetworkToRouteMap<AddressT>::Iterator> toDelete;

  for (auto it = routes->begin(); it != routes->end(); ++it) {
    auto route = value<AddressT>(it);
    if (isClaimedFunc(*route)) {
      continue;
    }
    auto nhopEntry = route->getEntryForClient(clientID);
    if (!nhopEntry) {
      continue;
    }
    if (nextHopIDManager_) {
      auto clientNhopSetId = nhopEntry->getClientNextHopSetID();
      if (clientNhopSetId.has_value()) {
        nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*clientNhopSetId);
      }
    }
    maybeRemoveNamedNhgMapping(route, *nhopEntry);
    if (route->numClientEntries() == 1) {
      // This client's is the only entry avoid unnecessary cloning
      // we are going to prune the route anyways. Release fwd-side IDs first.
      releaseFwdSideNexthopSetIDs(route->getForwardInfo());
      toDelete.push_back(it);
    } else {
      route = writableRoute<AddressT>(it);
      route->delEntryForClient(clientID);
      if (route->hasNoEntry()) {
        // The nexthops we removed was the only one.  Delete the route->
        releaseFwdSideNexthopSetIDs(route->getForwardInfo());
        toDelete.push_back(it);
      }
    }
  }

  // Now, delete whatever routes went from 1 nexthoplist to 0.
  for (auto it : toDelete) {
    routes->erase(it);
  }
}

void RibRouteUpdater::removeAllUnclaimedRoutesForClient(
    ClientID clientID,
    const std::vector<RouteEntry>& claimed) {
  std::unordered_set<folly::CIDRNetwork> v4Claimed, v6Claimed;
  std::for_each(
      claimed.begin(),
      claimed.end(),
      [&v4Claimed, &v6Claimed](const RouteEntry& route) {
        if (route.prefix.first.isV6()) {
          v6Claimed.insert(route.prefix);
        } else {
          v4Claimed.insert(route.prefix);
        }
      });
  removeAllUnclaimedRoutesFromClientImpl<IPAddressV4>(
      [&v4Claimed](const Route<folly::IPAddressV4>& inRoute) {
        return v4Claimed.contains(inRoute.prefix().toCidrNetwork());
      },
      v4Routes_,
      clientID);
  removeAllUnclaimedRoutesFromClientImpl<IPAddressV6>(
      [&v6Claimed](const Route<folly::IPAddressV6>& inRoute) {
        return v6Claimed.contains(inRoute.prefix().toCidrNetwork());
      },
      v6Routes_,
      clientID);
}
void RibRouteUpdater::removeAllUnclaimedMplsRoutesForClient(
    ClientID clientID,
    const std::vector<MplsRouteEntry>& claimed) {
  std::unordered_set<LabelID> claimedMplsRoutes;
  std::for_each(
      claimed.begin(),
      claimed.end(),
      [&claimedMplsRoutes](const MplsRouteEntry& route) {
        claimedMplsRoutes.insert(route.label);
      });
  removeAllUnclaimedRoutesFromClientImpl<LabelID>(
      [&claimedMplsRoutes](const Route<LabelID>& inRoute) {
        return claimedMplsRoutes.contains(inRoute.getID());
      },
      mplsRoutes_,
      clientID);
}

// Some helper functions for recursive weight resolution
// These aren't really usefully reusable, but structuring them
// this way helps with clarifying their meaning.
namespace {
using NextHopForwardInfos = std::map<NextHop, RouteNextHopSet>;

struct NextHopCombinedWeightsKey {
  explicit NextHopCombinedWeightsKey(NextHop nhop)
      : ip(nhop.addr()),
        intfId(nhop.intf()), // must be resolved next hop
        action(nhop.labelForwardingAction()),
        disableTTLDecrement(nhop.disableTTLDecrement()),
        topologyInfo(nhop.topologyInfo()),
        srv6SegmentList(nhop.srv6SegmentList()),
        tunnelType(nhop.tunnelType()),
        tunnelId(nhop.tunnelId()),
        cost(nhop.cost()),
        role(nhop.role()),
        overlayDstMac(nhop.overlayDstMac()) {
    /* "weightless" next hop, consider all attrs of L3 next hop except its
     * weight, this is used in computing number of required paths to next hop,
     * for correct programming of unequal cost multipath */
  }
  bool operator<(const NextHopCombinedWeightsKey& other) const {
    return std::tie(
               ip,
               intfId,
               action,
               disableTTLDecrement,
               topologyInfo,
               srv6SegmentList,
               tunnelType,
               tunnelId,
               cost,
               role,
               overlayDstMac) <
        std::tie(
               other.ip,
               other.intfId,
               other.action,
               other.disableTTLDecrement,
               other.topologyInfo,
               other.srv6SegmentList,
               other.tunnelType,
               other.tunnelId,
               other.cost,
               other.role,
               other.overlayDstMac);
  }
  folly::IPAddress ip;
  InterfaceID intfId;
  std::optional<LabelForwardingAction> action;
  std::optional<bool> disableTTLDecrement;
  std::optional<NetworkTopologyInformation> topologyInfo;
  std::vector<folly::IPAddressV6> srv6SegmentList;
  std::optional<TunnelType> tunnelType;
  std::optional<std::string> tunnelId;
  std::optional<int64_t> cost;
  NextHopRole role;
  std::optional<folly::MacAddress> overlayDstMac;
};
using NextHopCombinedWeights =
    boost::container::flat_map<NextHopCombinedWeightsKey, NextHopWeight>;

// A next hop at ECMP_WEIGHT states no weight of its own but still takes a
// share alongside its siblings, which normalizeNextHops settles at one before
// either hardware layer. Weight arithmetic uses that same value, so that a
// share of nothing never reaches a multiply or a gcd: zero annihilates an LCM
// and over-reduces a GCD, either of which silently discards a weighting the
// route did ask for.
NextHopWeight effectiveWeight(const NextHop& nhop) {
  return std::max(nhop.weight(), NextHopWeight(1));
}

NextHopWeight totalEffectiveWeight(const RouteNextHopSet& nhops) {
  return std::accumulate(
      nhops.begin(),
      nhops.end(),
      NextHopWeight(0),
      [](NextHopWeight w, const NextHop& nhop) {
        return w + effectiveWeight(nhop);
      });
}

NextHopWeight totalWeightsLcm(const NextHopForwardInfos& nhToFwds) {
  auto lcmAccumFn =
      [](NextHopWeight l,
         const std::pair<NextHop, RouteNextHopSet>& nhToFwd) -> NextHopWeight {
    return boost::integer::lcm(l, totalEffectiveWeight(nhToFwd.second));
  };
  return std::accumulate(
      nhToFwds.begin(), nhToFwds.end(), NextHopWeight(1), lcmAccumFn);
}

// Whether the route states a weighting across its own next hops. A route
// that does is resolved by combineWeights(), which scales each next hop's
// share by the weights of what it resolves onto. A route that does not has
// no split of its own to express, so its resolved next hop sets are brought
// in as they are.
bool hasUcmpNextHop(const NextHopForwardInfos& nhToFwds) {
  for (const auto& nhToFwd : nhToFwds) {
    if (nhToFwd.first.weight() != ECMP_WEIGHT) {
      return true;
    }
  }
  return false;
}

/*
 * for each recursively resolved next hop set, combine the next hops into
 * a map from (IP,InterfaceID)->weight, normalizing weights to the scale of
 * the LCM of the total weights of each resolved next hop set. The odd map
 * representation is needed to combine weights for different instances of
 * the same next hop coming from different recursively resolved next hop sets.
 *
 * ECMP_WEIGHT counts as a single share at either level, so a next hop that
 * states no weight neither disappears from the result nor discards the
 * weighting of its siblings.
 */
NextHopCombinedWeights combineWeights(const NextHopForwardInfos& nhToFwds) {
  NextHopCombinedWeights cws;
  NextHopWeight l = totalWeightsLcm(nhToFwds);
  for (const auto& nhToFwd : nhToFwds) {
    const NextHop& unh = nhToFwd.first;
    const RouteNextHopSet& fw = nhToFwd.second;
    if (fw.empty()) {
      continue;
    }
    // Every share counts for at least one, so this total is never 0 and the
    // LCM it feeds is never 0 either. The route's own weighting therefore
    // survives a recursively resolved next hop set that states none of its
    // own, instead of being annihilated by it.
    const NextHopWeight normalization = l / totalEffectiveWeight(fw);
    for (const auto& fnh : fw) {
      cws[NextHopCombinedWeightsKey(fnh)] +=
          effectiveWeight(fnh) * normalization * effectiveWeight(unh);
    }
  }
  return cws;
}

/*
 * Given a map from (IP,InterfaceID)->weight, take the gcd of the weights
 * and return a next hop set with IP, intf, weight/GCD to minimize the weights
 */
RouteNextHopSet optimizeWeights(const NextHopCombinedWeights& cws) {
  RouteNextHopSet fwd;
  auto gcdAccumFn =
      [](NextHopWeight g,
         const std::pair<NextHopCombinedWeightsKey, NextHopWeight>& cw) {
        NextHopWeight w = cw.second;
        return (g ? boost::integer::gcd(g, w) : w);
      };
  auto fwdWeightGcd = std::accumulate(cws.begin(), cws.end(), 0, gcdAccumFn);
  for (const auto& cw : cws) {
    const folly::IPAddress& addr = cw.first.ip;
    const InterfaceID& intf = cw.first.intfId;
    const auto& action = cw.first.action;
    const auto& disableTTLDecrement = cw.first.disableTTLDecrement;
    const auto& topologyInfo = cw.first.topologyInfo;
    NextHopWeight w = fwdWeightGcd ? cw.second / fwdWeightGcd : 0;
    fwd.emplace(ResolvedNextHop(
        addr,
        intf,
        w,
        action,
        disableTTLDecrement,
        topologyInfo,
        std::nullopt, /* adjustedWeight */
        cw.first.srv6SegmentList,
        cw.first.tunnelType,
        cw.first.tunnelId,
        cw.first.cost,
        cw.first.role,
        cw.first.overlayDstMac));
  }
  return fwd;
}

/*
 * Resolution for a route that states no weighting across its own next hops.
 *
 * Stating none is not the same as stating an even one. If the routes it
 * resolves onto are weighted, the route has expressed no preference that
 * would override them, so their weighting is kept and scaled against the
 * other resolved sets by the same arithmetic a weighted route uses. Only
 * when nothing below is weighted either does the route resolve to plain
 * ECMP.
 */
template <typename AddressT>
RouteNextHopSet mergeForwardInfosEcmp(
    const NextHopForwardInfos& nhToFwds,
    const std::shared_ptr<Route<AddressT>>& route) {
  // A resolved next hop at a single share is not stating a weighting:
  // resolving through a connected route mints one at UCMP_DEFAULT_WEIGHT
  // whether or not anyone asked for a split.
  auto carriesAWeighting = [](const auto& nhToFwd) {
    return std::any_of(
        nhToFwd.second.begin(), nhToFwd.second.end(), [](const NextHop& fnh) {
          return fnh.weight() > UCMP_DEFAULT_WEIGHT;
        });
  };

  if (std::none_of(nhToFwds.begin(), nhToFwds.end(), carriesAWeighting)) {
    // Keep ECMP_WEIGHT rather than emitting a uniform one share each. Both
    // program the same split, but next hop group identity orders on weight,
    // so rewriting it would churn dedup for routes that carry no weighting
    // at any level. Every weight here is equal, so the set collapses next
    // hops that resolved onto a shared destination without any help.
    RouteNextHopSet fwd;
    for (const auto& nhToFwd : nhToFwds) {
      for (const auto& fnh : nhToFwd.second) {
        fwd.emplace(ResolvedNextHop(
            fnh.addr(),
            fnh.intf(),
            ECMP_WEIGHT,
            fnh.labelForwardingAction(),
            fnh.disableTTLDecrement(),
            fnh.topologyInfo(),
            std::nullopt, /* adjustedWeight */
            fnh.srv6SegmentList(),
            fnh.tunnelType(),
            fnh.tunnelId(),
            fnh.cost(),
            fnh.role(),
            fnh.overlayDstMac()));
      }
    }
    return fwd;
  }

  XLOG(DBG4) << "While resolving " << route->str()
             << " keeping the weighting of the recursively resolved next hops,"
             << " since the route states none of its own";
  return optimizeWeights(combineWeights(nhToFwds));
}

/*
 * Take the resolved forwarding info (NextHopSet containing ResolvedNextHops)
 * from each recursively resolved next hop and merge them together.
 *
 * There are two cases:
 * 1) None of this route's next hops is weighted. If a recursively resolved
 *    set carries a weighting, combineWeights() preserves its internal ratio
 *    while giving each top-level next hop an equal aggregate share. If no
 *    recursively resolved set is weighted either, everything is brought in
 *    at ECMP_WEIGHT.
 * 2) At least one of this route's next hops is weighted. In this case, we run
 *    through algorithm to normalize all the weights for the next hops resolved
 *    for each next hop (combineWeights) then minimize the weights
 *    (optimizeWeights). Both of these algorithms will preserve the critical
 *    ratio w_i/w_j for any two weights w_i, w_j of next hops i, j in both
 *    the current route's weights and in the recursively resolved route's
 *    weights.
 *
 * NOTE: A route with no weighting of its own does not overwrite explicit
 *       weighting below it; only an entirely unweighted resolution tree stays
 *       ECMP. A weighted route no longer loses its weighting because one of
 *       its own next hops is at ECMP_WEIGHT, nor because a route it resolves
 *       onto is unweighted: ECMP_WEIGHT counts as a single share in case 2),
 *       so neither the LCM nor the GCD is driven to zero by it.
 *
 * An example to illustrate the requirement:
 * We will represent the recursive route resolution tree for three routes
 * R1, R2, and R3. Numbers on the edges represent weights for next hops,
 * and I1, I2, I3, and I4 are connected interfaces.
 *
 *             R1
 *          3/    2\
 *         R2       R3
 *       5/  4\   3/  2\
 *      I1    I2 I3    I4
 *
 * In resolving R1, resolveOne and getFwdInfoFromNhop will mutually recursively
 * get to a point where we have resolved R2 and R3 to having next hops I1 with
 * weight 5, I2 with weight 4, and I3 with weight 3 and I4 with weight 2
 * respectively: {R2x3:{I1x5, I2x4}, R3x2:{I3x3, I4x2}}
 * We need to preserve the following ratios in the final result:
 * I1/I2=5/4; I3/I4=3/2; (I1+I2)/(I3+I4)=3/2
 * To do this, we normalize the weights by finding the LCM of the total
 * weights of the recursively resolved next hop sets:
 * LCM(9, 5) = 45, then bring in each next hop into the final next hop
 * set with its weight multiplied by LCM/TotalWeight(its next hop)
 * and by the weight of its top level next hop:
 * I1: 5*(45/9)*3=75, I2: 4*(45/9)*3=60, I3: 3*(45/5)*2=54, I4: 2*(45/5)*2=36
 * I1/I2=75/60=5/4, I3/I4=54/36=3/2, (I1+I2)/(I3+I4)=135/90=3/2
 * as required. Finally, we can find the gcds of top level weights to minimize
 * these weights:
 * GCD(75, 60, 54, 36) = 3 so we reach a final next hop set of:
 * {I1x25, I2x20, I3x18, I4x12}
 */
template <typename AddressT>
RouteNextHopSet mergeForwardInfos(
    const NextHopForwardInfos& nhToFwds,
    const std::shared_ptr<Route<AddressT>>& route) {
  RouteNextHopSet fwd;
  if (!hasUcmpNextHop(nhToFwds)) {
    fwd = mergeForwardInfosEcmp(nhToFwds, route);
  } else {
    NextHopCombinedWeights cws = combineWeights(nhToFwds);
    fwd = optimizeWeights(cws);
  }
  return fwd;
}

struct Srv6Encap {
  std::vector<folly::IPAddressV6> segmentList;
  std::optional<TunnelType> tunnelType;
  std::optional<std::string> tunnelId;
};

// Prefer the parent next-hop's SRv6 encap; if the parent has none, inherit the
// child's.
template <typename NextHopT>
Srv6Encap resolveSrv6Encap(
    const std::vector<folly::IPAddressV6>& parentSegmentList,
    const std::optional<TunnelType>& parentTunnelType,
    const std::optional<std::string>& parentTunnelId,
    const NextHopT& child) {
  if (!parentSegmentList.empty()) {
    return {parentSegmentList, parentTunnelType, parentTunnelId};
  }
  return {child.srv6SegmentList(), child.tunnelType(), child.tunnelId()};
}
} // anonymous namespace

template <typename AddressT>
void RibRouteUpdater::getFwdInfoFromNhop(
    NetworkToRouteMap<AddressT>* routes,
    const AddressT& nh,
    const std::optional<LabelForwardingAction>& labelAction,
    bool* hasToCpu,
    bool* hasDrop,
    const std::optional<bool>& disableTTLDecrement,
    const std::optional<NetworkTopologyInformation>& topologyInfo,
    const std::vector<folly::IPAddressV6>& srv6SegmentList,
    const std::optional<TunnelType>& tunnelType,
    const std::optional<std::string>& tunnelId,
    const std::optional<int64_t>& cost,
    NextHopRole role,
    const std::optional<folly::MacAddress>& overlayDstMac,
    std::optional<RouteCounterID>* inheritedCounterID,
    RouteNextHopSet& fwd) {
  auto it = routes->longestMatch(nh, nh.bitCount());
  if (it == routes->end()) {
    XLOG(DBG3) << "Could not find subnet for next-hop:  " << nh;
    // Unresolvable next hop
    return;
  }

  auto route = it->value();
  CHECK(route);

  if (resolving_.contains(route.get())) {
    XLOG(DBG2) << "Cycle detected resolving nexthop " << nh
               << " — route is already being resolved";
    ++cyclesDetected_;
    return;
  }

  if (needResolve(route)) {
    route = resolveOne<AddressT>(it);
    CHECK(route);
  }

  if (route->isResolved()) {
    const auto& fwdInfo = route->getForwardInfo();
    if (const auto childCounterID = fwdInfo.getCounterID();
        childCounterID && !*inheritedCounterID) {
      *inheritedCounterID = childCounterID;
    }
    if (fwdInfo.isDrop()) {
      *hasDrop = true;
    } else if (fwdInfo.isToCPU()) {
      *hasToCpu = true;
    } else {
      const auto nhops = getResolvedNextHopsFromRib(nextHopIDManager_, fwdInfo);
      // if the route used to resolve the nexthop is directly connected
      if (route->isConnected()) {
        const auto& rtNh = *nhops.begin();
        const auto srv6Encap =
            resolveSrv6Encap(srv6SegmentList, tunnelType, tunnelId, rtNh);
        // NOTE: we need to use a UCMP compatible weight so that this can
        // be a leaf in the recursive resolution defined in the comment
        // describing mergeForwardInfos above.
        fwd.emplace(ResolvedNextHop(
            nh,
            rtNh.intf(),
            UCMP_DEFAULT_WEIGHT,
            LabelForwardingAction::combinePushLabelStack(
                labelAction, rtNh.labelForwardingAction()),
            disableTTLDecrement.has_value() ? disableTTLDecrement
                                            : rtNh.disableTTLDecrement(),
            topologyInfo,
            std::nullopt, /* adjustedWeight */
            srv6Encap.segmentList,
            srv6Encap.tunnelType,
            srv6Encap.tunnelId,
            cost,
            role,
            overlayDstMac));
      } else {
        std::for_each(
            nhops.begin(),
            nhops.end(),
            [&fwd,
             labelAction,
             disableTTLDecrement,
             topologyInfo,
             &srv6SegmentList,
             &tunnelType,
             &tunnelId,
             &cost,
             role,
             &overlayDstMac](const auto& nhop) {
              const auto srv6Encap =
                  resolveSrv6Encap(srv6SegmentList, tunnelType, tunnelId, nhop);
              fwd.insert(ResolvedNextHop(
                  nhop.addr(),
                  nhop.intf(),
                  nhop.weight(),
                  LabelForwardingAction::combinePushLabelStack(
                      labelAction, nhop.labelForwardingAction()),
                  disableTTLDecrement.has_value() ? disableTTLDecrement
                                                  : nhop.disableTTLDecrement(),
                  topologyInfo,
                  std::nullopt, /* adjustedWeight */
                  srv6Encap.segmentList,
                  srv6Encap.tunnelType,
                  srv6Encap.tunnelId,
                  cost,
                  role,
                  overlayDstMac));
            });
      }
    }
  }
}
/*
 * Policy for inheriting next hop properties during resolution.
 * --------------------------------------------------------------
 *
 * For all the cases below consider the following resolution
 * DST via NHA
 * NHA via NHB
 * There are 3 types of properties to consider here
 * 1. Encapsulation properties
 * Examples of these are SidLists and Labels. Here
 * we prefer the values from root next hops, but if not set
 * we inherit them from the child next hops. E.g.
 * DST via NHA via NHB (SidListX) results in DST via NHB (SidListX)
 * DST via NHA (SidListY) via NHB (SidListX) results in DST via NHB (SidListY)
 * The latter is actually ambiguous - but we have to resolve it one way,
 * so we resolve in favor of the root.
 * The reasoning here is that where ever the SidList is set, it actually
 * dictates how the route to that next hop must manifest itself. Hence
 * we inherit the encapsulation behaviors.
 * TODO: Fix this for Labeled routes
 * 2. Route Counters - We inherit the same way as 1. This is more
 * a use case based decision. For recursive resolution is to also count
 * traffic against recursively resolving route. We can make this customizable
 * in the future.
 * 3. Role (PRIMARY, BACKUP) - This is viewed as a policy decision and
 * the root role carries all the way through. Taking our example
 * from above
 * DST via NHA (PRIMARY) via NHB (BACKUP) becomes DST -> NHB (PRIMARY)
 * DST via NHA (BACKUP) via NHB (PRIMARY) becomes DST -> NHB (BACKUP)
 */
template <typename AddressT>
std::shared_ptr<Route<AddressT>> RibRouteUpdater::resolveOne(
    typename NetworkToRouteMap<AddressT>::Iterator ritr) {
  auto route = value<AddressT>(ritr);
  // Starting resolution for this route, remove from resolution queue
  needsResolution_.erase(route.get());
  resolving_.insert(route.get());
  SCOPE_EXIT {
    resolving_.erase(route.get());
  };

  bool hasToCpu{false};
  bool hasDrop{false};
  RouteNextHopSet* fwd{nullptr};

  auto bestPair = route->getBestEntry();
  const auto clientId = bestPair.first;
  const auto& bestEntryVal = *bestPair.second;
  const auto bestEntry = &bestEntryVal;
  const auto action = bestEntry->getAction();
  auto counterID = bestEntry->getCounterID();
  const auto classID = bestEntry->getClassID();
  bool labelPopandLookup = false;
  if (action == RouteForwardAction::DROP) {
    hasDrop = true;
  } else if (action == RouteForwardAction::TO_CPU) {
    hasToCpu = true;
  } else {
    // Resolve once via the manager so the cache lookup and per-nh iteration
    // below stay correct once FLAGS_resolve_nexthops_from_id is on.
    const RouteNextHopSet bestEntryNhops =
        getClientNextHopsFromRib(nextHopIDManager_, *bestEntry);
    std::optional<RouteCounterID> inheritedCounterID;
    auto fwItr = unresolvedToResolvedNhops_.find(bestEntryNhops);
    if (fwItr == unresolvedToResolvedNhops_.end()) {
      NextHopForwardInfos nhToFwds;
      // loop through all nexthops to find out the forward info
      for (const auto& nh : bestEntryNhops) {
        const auto& addr = nh.addr();
        // There are two reasons why InterfaceID is specified in the next hop.
        // 1) The nexthop was generated for interface route.
        //    In this case, the clientId is INTERFACE_ROUTE
        // 2) The nexthop was for v6 link-local address.
        // In both cases, this nexthop is resolved.
        if (nh.intfID().has_value()) {
          // It is either an interface route or v6 link-local
          CHECK(
              clientId == kInterfaceRouteClientId ||
              clientId == kRemoteInterfaceRouteClientId ||
              (addr.isV6() && addr.isLinkLocal()))
              << "Next hop:" << nh << " cannot be associated with a RIF";
          nhToFwds[nh].emplace(nh);
          continue;
        }

        // For pop and lookup, forwarding is based on inner
        // header. There should be only one nhop in this case.
        if (nh.labelForwardingAction().has_value() &&
            nh.labelForwardingAction().value().type() ==
                MplsActionCode::POP_AND_LOOKUP) {
          if (bestEntryNhops.size() > 1) {
            throw FbossError(
                "MPLS pop and lookup forwarding action has more than one nexthop");
          }
          labelPopandLookup = true;
          nhToFwds[nh].emplace(nh);
          break;
        }

        if (addr.isV4()) {
          getFwdInfoFromNhop(
              v4Routes_,
              nh.addr().asV4(),
              nh.labelForwardingAction(),
              &hasToCpu,
              &hasDrop,
              nh.disableTTLDecrement(),
              nh.topologyInfo(),
              nh.srv6SegmentList(),
              nh.tunnelType(),
              nh.tunnelId(),
              nh.cost(),
              nh.role(),
              nh.overlayDstMac(),
              &inheritedCounterID,
              nhToFwds[nh]);
        } else {
          CHECK(addr.isV6());
          getFwdInfoFromNhop(
              v6Routes_,
              nh.addr().asV6(),
              nh.labelForwardingAction(),
              &hasToCpu,
              &hasDrop,
              nh.disableTTLDecrement(),
              nh.topologyInfo(),
              nh.srv6SegmentList(),
              nh.tunnelType(),
              nh.tunnelId(),
              nh.cost(),
              nh.role(),
              nh.overlayDstMac(),
              &inheritedCounterID,
              nhToFwds[nh]);
        }
      }

      // For label pop and lookup, the label lookup result instructs
      // the hw to perform another lookup on inner header and
      // forward packet based on inner header result. This means
      // that label pop and lookup will not have a valid nhop ip
      // or interface and any merge operation has to be skipped.
      RouteNextHopSet nhSet = labelPopandLookup
          ? bestEntryNhops
          : mergeForwardInfos(nhToFwds, route);
      nhSet = removeBackupNextHopsWithMatchingPrimary(std::move(nhSet));

      // normalize weight information for capacity matching if needed
      if (FLAGS_enable_capacity_pruning) {
        nhSet = weightNormalizer_.getNormalizedNexthops(nhSet);
      }

      fwItr =
          unresolvedToResolvedNhops_
              .insert(
                  {bestEntryNhops,
                   ResolvedForwardInfo{std::move(nhSet), inheritedCounterID}})
              .first;
    } else {
      // This is done so that we dont miss updating label pop and lookup on
      // cache hits.
      const auto& nhSet = bestEntryNhops;
      if (nhSet.size() == 1) {
        const auto& nh = *nhSet.begin();
        if (nh.labelForwardingAction().has_value() &&
            nh.labelForwardingAction().value().type() ==
                MplsActionCode::POP_AND_LOOKUP) {
          labelPopandLookup = true;
        }
      }
    }
    fwd = &fwItr->second.nextHops;
    // A route can carry only one counter. Preserve an explicitly configured
    // parent counter; otherwise inherit the first recursively resolved child
    // counter and ignore counters on subsequent children.
    if (!counterID) {
      counterID = fwItr->second.counterID;
    }
  }

  std::shared_ptr<Route<AddressT>> updatedRoute;
  auto updateRoute = [this,
                      clientId,
                      &updatedRoute,
                      classID,
                      labelPopandLookup](
                         typename NetworkToRouteMap<AddressT>::Iterator ritr,
                         std::optional<RouteNextHopEntry> nhop) {
    updatedRoute = writableRoute<AddressT>(ritr);
    auto oldNextHopSetID =
        value<AddressT>(ritr)->getForwardInfo().getResolvedNextHopSetID();
    auto oldNormalizedNextHopSetID = value<AddressT>(ritr)
                                         ->getForwardInfo()
                                         .getNormalizedResolvedNextHopSetID();
    if (nhop) {
      std::optional<NextHopSetID> newResolvedNextHopSetId;
      std::optional<NextHopSetID> newNormalizedResolvedNextHopSetId;
      if (nextHopIDManager_) {
        auto updateNextHopSetIDs =
            [this](
                const RouteNextHopSet& newNextHopSet,
                const std::optional<NextHopSetID>& oldNextHopSetID)
            -> std::optional<NextHopSetID> {
          if (!newNextHopSet.empty()) {
            if (oldNextHopSetID.has_value()) {
              auto updateResult = nextHopIDManager_->updateRouteNextHopSetID(
                  *oldNextHopSetID, newNextHopSet);
              return updateResult.allocation.nextHopIdSetIter->second.id;
            } else {
              auto allocResult =
                  nextHopIDManager_->getOrAllocRouteNextHopSetID(newNextHopSet);
              return allocResult.nextHopIdSetIter->second.id;
            }
          } else if (oldNextHopSetID.has_value()) {
            nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*oldNextHopSetID);
          }
          return std::nullopt;
        };

        newResolvedNextHopSetId =
            updateNextHopSetIDs(nhop->getNextHopSet(), oldNextHopSetID);
        // For label pop and lookup routes, skip normalized nexthops
        // allocation but deallocate any existing old ID
        if (!labelPopandLookup) {
          newNormalizedResolvedNextHopSetId = updateNextHopSetIDs(
              RouteNextHopEntry::normalizeNextHops(
                  nhop->getNextHopSet(), ecmpWidth_),
              oldNormalizedNextHopSetID);
        } else if (oldNormalizedNextHopSetID.has_value()) {
          // Route transitioned to POP_AND_LOOKUP - deallocate old normalized ID
          nextHopIDManager_->decrOrDeallocRouteNextHopSetID(
              *oldNormalizedNextHopSetID);
        }
      }
      nhop->setResolvedNextHopSetID(newResolvedNextHopSetId);
      nhop->setNormalizedResolvedNextHopSetID(
          newNormalizedResolvedNextHopSetId);
      updatedRoute->setResolved(*nhop);
      if ((clientId == kInterfaceRouteClientId ||
           clientId == kRemoteInterfaceRouteClientId) &&
          !getResolvedNextHopsFromRib(nextHopIDManager_, *nhop).empty()) {
        updatedRoute->setConnected();
      }
    } else {
      if (nextHopIDManager_) {
        if (oldNextHopSetID.has_value()) {
          nextHopIDManager_->decrOrDeallocRouteNextHopSetID(*oldNextHopSetID);
        }
        if (oldNormalizedNextHopSetID.has_value()) {
          nextHopIDManager_->decrOrDeallocRouteNextHopSetID(
              *oldNormalizedNextHopSetID);
        }
        // Assert clientNextHopSetID not present in fwd entry so the invariant
        // can't be silently broken.
        CHECK(!updatedRoute->getForwardInfo()
                   .getClientNextHopSetID()
                   .has_value());

        // Clear the fwd so the unresolvable route retains no freed set IDs.
        updatedRoute->clearForward();
      }
      updatedRoute->setUnresolvable();
    }
    updatedRoute->updateClassID(classID);
    updatedRoute->publish();
    XLOG(DBG3) << (updatedRoute->isResolved() ? "Resolved" : "Cannot resolve")
               << " route " << updatedRoute->str();
  };
  if (fwd && !fwd->empty()) {
    if (getResolvedNextHopsFromRib(
            nextHopIDManager_, route->getForwardInfo()) != *fwd ||
        route->getForwardInfo().getCounterID() != counterID ||
        route->getForwardInfo().getClassID() != classID) {
      updateRoute(
          ritr,
          std::make_optional<RouteNextHopEntry>(
              *fwd, bestEntry->getAdminDistance(), counterID, classID));
    }
  } else if (hasToCpu) {
    if (!route->isToCPU() ||
        route->getForwardInfo().getCounterID() != counterID ||
        route->getForwardInfo().getClassID() != classID) {
      updateRoute(
          ritr,
          std::make_optional<RouteNextHopEntry>(
              RouteForwardAction::TO_CPU,
              AdminDistance::MAX_ADMIN_DISTANCE,
              counterID,
              classID));
    }
  } else if (hasDrop) {
    if (!route->isDrop() ||
        route->getForwardInfo().getCounterID() != counterID ||
        route->getForwardInfo().getClassID() != classID) {
      updateRoute(
          ritr,
          std::make_optional<RouteNextHopEntry>(
              RouteForwardAction::DROP,
              AdminDistance::MAX_ADMIN_DISTANCE,
              counterID,
              classID));
    }
  } else {
    updateRoute(ritr, std::nullopt);
  }
  if (!updatedRoute) {
    route->publish();
    XLOG(DBG3) << " Retained resolution :"
               << (route->isResolved() ? "Resolved" : "Cannot resolve")
               << " route " << route->str();
  }
  return updatedRoute ? updatedRoute : route;
}

template <typename AddressT>
void RibRouteUpdater::resolve(NetworkToRouteMap<AddressT>* routes) {
  for (auto ritr = routes->begin(); ritr != routes->end(); ++ritr) {
    if (needResolve(value(*ritr))) {
      resolveOne<AddressT>(ritr);
      DCHECK(resolving_.empty());
    }
  }
}

template <typename AddressT>
bool RibRouteUpdater::needResolve(
    const std::shared_ptr<Route<AddressT>>& route) const {
  return needsResolution_.contains(route.get());
}

void RibRouteUpdater::updateDone() {
  // Record all routes as needing resolution
  auto markForResolution = [this](const auto& routes) {
    std::for_each(routes->begin(), routes->end(), [this](auto& route) {
      needsResolution_.insert(value(route).get());
    });
  };
  markForResolution(v4Routes_);
  markForResolution(v6Routes_);
  if (mplsRoutes_) {
    markForResolution(mplsRoutes_);
  }
  SCOPE_EXIT {
    needsResolution_.clear();
    unresolvedToResolvedNhops_.clear();
    resolving_.clear();
  };
  resolve(v4Routes_);
  resolve(v6Routes_);
  if (mplsRoutes_) {
    resolve(mplsRoutes_);
  }
}
} // namespace facebook::fboss
