/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/types.h"

#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RibRouteWeightNormalizer.h"
#include "fboss/agent/state/MySid.h"

#include <folly/IPAddress.h>

DECLARE_bool(enable_capacity_pruning);
DECLARE_bool(enable_fpf_capacity_pruning);
namespace facebook::fboss {
class NextHopIDManager;

using MySidTable =
    std::unordered_map<folly::CIDRNetworkV6, std::shared_ptr<MySid>>;

/**
 * Expected behavior of RibRouteUpdater::resolve():
 *
 * RibRouteUpdater::resolve() resolves the route table forwarding information
 * based on the RIB, by doing recursively route table lookup. At the end of
 * the process, every route will be either unresolved or resolved with a
 * ECMP group.
 *
 * There are clear expectation on resolving FIB for a route, when all
 * nexthops are resolved to actual IPs. However, if is not clearly
 * defined and documented expectation if ECMP group has mix of action(s)
 * (i.e. DROP, TO_CPU) and IP nexthops.
 *
 * The following is the current implentation of resolve():
 * 1. No weighted ECMP. Each entry in the ECMP group is unique and has equal
 *    weight.
 * 2. A ECMP group could have either DROP, TO_CPU, or a set of IP nexthops.
 * 3. If DROP and other types (i.e. TO_CPU and IP nexthops) are part of the
 *    results of route resolve process. The finally FIB will be DROP.
 * 4. If TO_CPU and IP nexthops are part of the results of resolving process,
 *    only IP nexthops will be in the final ECMP group.
 * 5. If and only if TO_CPU is the only nexthop (directly or indirectly) of
 *    a route, TO_CPU action will be only path in the resolved ECMP group.
 */
class RibRouteUpdater {
 public:
  RibRouteUpdater(
      IPv4NetworkToRouteMap* v4Routes,
      IPv6NetworkToRouteMap* v6Routes,
      NextHopIDManager* nextHopIDManager,
      MySidTable* mySidTable,
      uint32_t ecmpWidth,
      RouterID routerID = RouterID(0));

  RibRouteUpdater(
      IPv4NetworkToRouteMap* v4Routes,
      IPv6NetworkToRouteMap* v6Routes,
      LabelToRouteMap* mplsRoutes,
      NextHopIDManager* nextHopIDManager,
      MySidTable* mySidTable,
      uint32_t ecmpWidth,
      RouterID routerID = RouterID(0));

  struct RouteEntry {
    folly::CIDRNetwork prefix;
    RouteNextHopEntry nhopEntry;
    RouteEntry(
        folly::CIDRNetwork inPrefix,
        const RouteNextHopEntry& inNhopEntry)
        : prefix(std::move(inPrefix)), nhopEntry(inNhopEntry.toThrift()) {}
    RouteEntry(const RouteEntry& other)
        : prefix(other.prefix), nhopEntry(other.nhopEntry.toThrift()) {}
  };

  struct MplsRouteEntry {
    LabelID label;
    RouteNextHopEntry nhopEntry;
    MplsRouteEntry(LabelID inLabel, const RouteNextHopEntry& inNhopEntry)
        : label(inLabel), nhopEntry(inNhopEntry.toThrift()) {}
    MplsRouteEntry(const MplsRouteEntry& other)
        : label(other.label), nhopEntry(other.nhopEntry.toThrift()) {}
  };

  /*
   * Update routes for a clients and trigger
   * resolution
   */
  template <typename RouteType, typename RouteIdType>
  void update(
      ClientID client,
      const std::vector<RouteType>& toAdd,
      const std::vector<RouteIdType>& toDel,
      bool resetClientsRoutes) {
    updateImpl(client, toAdd, toDel, resetClientsRoutes);
    updateDone();
  }
  /*
   * Update routes for multiple clients and trigger
   * resolution
   */

  void update(
      const std::map<ClientID, std::vector<RouteEntry>>& toAdd,
      const std::map<ClientID, std::vector<folly::CIDRNetwork>>& toDel,
      const std::set<ClientID>& resetClientsRoutesFor);

  std::size_t cyclesDetected() const {
    return cyclesDetected_;
  }

 private:
  void updateImpl(
      ClientID client,
      const std::vector<RouteEntry>& toAdd,
      const std::vector<folly::CIDRNetwork>& toDel,
      bool resetClientsRoutes);
  void updateImpl(
      ClientID client,
      const std::vector<MplsRouteEntry>& toAdd,
      const std::vector<LabelID>& toDel,
      bool resetClientsRoutes);
  void addOrReplaceRoute(
      const folly::IPAddress& network,
      uint8_t mask,
      ClientID clientID,
      const RouteNextHopEntry& entry);
  void addOrReplaceRoute(
      LabelID label,
      ClientID clientID,
      const RouteNextHopEntry& entry);
  void updateDone();

  void
  delRoute(const folly::IPAddress& network, uint8_t mask, ClientID clientID);
  void delRoute(const LabelID& label, const ClientID clientID);
  void removeAllUnclaimedRoutesForClient(
      ClientID clientID,
      const std::vector<RouteEntry>& claimed);
  void removeAllUnclaimedMplsRoutesForClient(
      ClientID clientID,
      const std::vector<MplsRouteEntry>& claimed);

  template <typename AddressT>
  using Prefix = RoutePrefix<AddressT>;

  template <typename AddressT>
  void addOrReplaceRouteImpl(
      const Prefix<AddressT>& prefix,
      NetworkToRouteMap<AddressT>* routes,
      ClientID clientID,
      const RouteNextHopEntry& entry);

  // Stamp clientNextHopSetID on `entry` by either reusing the existing ID
  // (when nexthops haven't changed), allocating a new ID and releasing the
  // old, or releasing the old when the new entry has empty nexthops. Returns
  // nullopt when there's nothing to stamp (no manager). `routeDescription`
  // is used only for the CHECK message.
  std::optional<RouteNextHopEntry> stampClientNextHopSetID(
      ClientID clientID,
      const std::shared_ptr<const RouteNextHopEntry>& existingRouteForClient,
      const RouteNextHopEntry& entry,
      const std::string& routeDescription);
  template <typename AddressT>
  void delRouteImpl(
      const Prefix<AddressT>& prefix,
      NetworkToRouteMap<AddressT>* routes,
      ClientID clientID);
  template <typename AddressT>
  void removeAllRoutesFromClientImpl(
      NetworkToRouteMap<AddressT>* routes,
      ClientID clientID);
  template <typename AddressT, typename FilterFunc>
  void removeAllUnclaimedRoutesFromClientImpl(
      const FilterFunc& isClaimedFunc,
      NetworkToRouteMap<AddressT>* routes,
      ClientID clientID);

  // Release the resolved + normalized nexthop set IDs carried on `fwd` back
  // to the manager. Call this right before erasing a route from a route map
  // so the manager refcounts drop in sync with the route's lifetime.
  void releaseFwdSideNexthopSetIDs(const RouteNextHopEntry& fwd);

  template <typename AddressT>
  void resolve(NetworkToRouteMap<AddressT>* routes);

  template <typename AddressT>
  std::shared_ptr<Route<AddressT>> resolveOne(
      typename NetworkToRouteMap<AddressT>::Iterator ritr);

  template <typename AddressT>
  void getFwdInfoFromNhop(
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
      RouteNextHopSet& fwd);

  template <typename AddressT>
  void maybeRemoveNamedNhgMapping(
      const std::shared_ptr<Route<AddressT>>& route,
      const RouteNextHopEntry& clientEntry);

  template <typename AddressT>
  bool needResolve(const std::shared_ptr<Route<AddressT>>& route) const;

  using NextHopIpToForwardInfo =
      std::unordered_map<folly::IPAddress, RouteNextHopSet>;

  IPv4NetworkToRouteMap* v4Routes_{nullptr};
  IPv6NetworkToRouteMap* v6Routes_{nullptr};
  LabelToRouteMap* mplsRoutes_{nullptr};
  NextHopIDManager* nextHopIDManager_{nullptr};
  MySidTable* mySidTable_{nullptr};
  uint32_t ecmpWidth_;
  RouterID routerID_{0};
  std::unordered_set<void*> needsResolution_;
  std::unordered_set<void*> resolving_;
  std::size_t cyclesDetected_{0};
  /*
   * Cache for next hop to FWD information. For our use case
   * its pretty common for the same next hops to repeat, so
   * cache resolution
   */
  struct ResolvedForwardInfo {
    RouteNextHopSet nextHops;
    std::optional<RouteCounterID> counterID;
  };
  std::map<RouteNextHopSet, ResolvedForwardInfo> unresolvedToResolvedNhops_;
  RibRouteWeightNormalizer weightNormalizer_;
};

} // namespace facebook::fboss
