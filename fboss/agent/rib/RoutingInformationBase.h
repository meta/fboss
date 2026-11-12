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

#include "fboss/agent/FbossError.h"
#include "fboss/agent/FbossEventBase.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/rib/VrfRouteTable.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/MySid.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include <folly/Synchronized.h>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

DECLARE_bool(mpls_rib);

namespace facebook::fboss {
class SwitchState;
class MultiSwitchFibInfoMap;
class MultiSwitchMySidMap;
class MultiSwitchClassBasedPolicyMap;
class SwitchIdScopeResolver;
class StateDelta;

// Rebuilds the RIB from a FIB plus an explicit set of unresolved routes.
// Used by the rollback path on FbossHwUpdateError.
template <typename AddressT, typename FibType, typename IndexT>
void reconstructRib(
    const std::shared_ptr<FibType>& fib,
    NetworkToRouteMap<AddressT>* addrToRoute,
    const IndexT& unresolvedIndex);

void reconstructMySidTableFromSwitchState(
    const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
    MySidTable* mySidTable);

// Programs the routes of all given VRFs in one switch state update
using RibToSwitchStateFunction = std::function<StateDelta(
    const SwitchIdScopeResolver* resolver,
    const std::vector<RibVrfFib>& vrfFibs,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie)>;

using RibMySidToSwitchStateFunction = std::function<StateDelta(
    const SwitchIdScopeResolver* resolver,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie)>;

// Each pair is (MySid state object, its unresolved next-hop set). The
// next-hop set is empty for DECAPSULATE_AND_LOOKUP and populated for
// others. Bundling them in a pair keeps the
// MySid with its associated next hops and optional next hop group name.
// These three pieces of data are inseparable across all RIB / ConfigApplier
// call sites that consume config-derived MySid state.
struct MySidWithNextHops {
  std::shared_ptr<MySid> mySid;
  RouteNextHopSet nextHopSet;
  std::optional<std::string> nextHopGroupName;
};

struct MySidFrrProtectionUpdate {
  folly::CIDRNetwork mySidPrefix;
  RouteNextHopSet nextHops;
};

// (prefix-key for the MySid, IP of the removed neighbor). Used by the
// observer-driven neighbor-removal path: RIB clears unresolveNextHopsId
// + resolvedNextHopsId iff the materialized unresolved next-hop set
// contains a next-hop with this IP. The match check happens RIB-side
// because only the rib's NextHopIDManager can materialize a NextHopSetID.
using MySidNeighborRemoved = std::pair<folly::CIDRNetworkV6, folly::IPAddress>;

/*
 * RibRouteTables provides a thread safe abstraction for maintaining Rib data
 * structures and programming them down to the FIB. Its designed to abstract
 * away granular locking logic over RIB data structures to allow for fast
 * lookups that are not encumbered by long HW write cycles
 */
class RibRouteTables {
  // NextHopIDManager iterates unresolved RIB routes inside
  // reconstructFromSwitchStateMaps to rebuild refcounts for per-client
  // clientNextHopSetIDs that the FIB walk can't see.
  friend class NextHopIDManager;

 public:
  RibRouteTables() = default;

  // Returns a deep copy of the NextHopIDManager. This is an expensive
  // operation and is intended only for tests.
  std::unique_ptr<NextHopIDManager> getNextHopIDManagerCopy() const {
    return synchronizedRouteTables_.withRLock(
        [&](const auto& routeTables) -> std::unique_ptr<NextHopIDManager> {
          if (routeTables.nextHopIDManager) {
            return std::make_unique<NextHopIDManager>(
                *routeTables.nextHopIDManager);
          }
          return nullptr;
        });
  }

  // Test-only accessors for the per-VRF unresolved-routes indexes. Return
  // copies so the caller observes a stable snapshot.
  std::unordered_map<
      folly::CIDRNetworkV4,
      std::shared_ptr<Route<folly::IPAddressV4>>>
  getUnresolvedV4RoutesForTest(const RouterID& vrf) const {
    return synchronizedRouteTables_.withRLock([&vrf](const auto& routeTables) {
      return routeTables.routerIDToRouteTable.at(vrf).unresolvedV4Routes;
    });
  }
  std::unordered_map<
      folly::CIDRNetworkV6,
      std::shared_ptr<Route<folly::IPAddressV6>>>
  getUnresolvedV6RoutesForTest(const RouterID& vrf) const {
    return synchronizedRouteTables_.withRLock([&vrf](const auto& routeTables) {
      return routeTables.routerIDToRouteTable.at(vrf).unresolvedV6Routes;
    });
  }
  std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>
  getUnresolvedMplsRoutesForTest(const RouterID& vrf) const {
    return synchronizedRouteTables_.withRLock([&vrf](const auto& routeTables) {
      return routeTables.routerIDToRouteTable.at(vrf).unresolvedMplsRoutes;
    });
  }

  void addOrUpdateNamedNextHopGroups(
      const SwitchIdScopeResolver* resolver,
      const std::vector<std::pair<std::string, RouteNextHopSet>>& groups,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  void deleteNamedNextHopGroups(
      const std::vector<std::string>& names,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  void addOrUpdatePolicies(
      const std::vector<ClassBasedPolicy>& policies,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  void removePolicies(
      const std::vector<std::string>& policyNames,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  template <typename RouteType, typename RouteIdType>
  void update(
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
      std::size_t* cyclesDetectedOut = nullptr);

  void update(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidEntry>& toAdd,
      const std::vector<IpPrefix>& toDelete,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);

  void update(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidWithNextHops>& toAdd,
      const std::vector<MySidNeighborRemoved>& toUnresolveIfMatch,
      const std::vector<IpPrefix>& toDelete,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);

  void updateMySidFrrProtection(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidFrrProtectionUpdate>& toAddOrUpdate,
      const std::vector<folly::CIDRNetwork>& toDelete,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);

  void setClassID(
      const SwitchIdScopeResolver* resolver,
      RouterID rid,
      const std::vector<folly::CIDRNetwork>& prefixes,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      std::optional<cfg::AclLookupClass> classId,
      void* cookie);
  void setOverrideEcmpMode(
      RouterID rid,
      const std::unordered_map<
          folly::CIDRNetwork,
          std::optional<cfg::SwitchingMode>>& prefix2EcmpMode);
  struct OverrideNhops {
    std::optional<RouteNextHopSet> nhops;
    std::optional<NextHopSetID> setId;
  };
  void setOverrideEcmpNhops(
      RouterID rid,
      const std::unordered_map<folly::CIDRNetwork, OverrideNhops>&
          prefix2Nhops);
  /*
   * VrfAndNetworkToInterfaceRoute is conceptually a mapping from the pair
   * (RouterID, folly::CIDRNetwork) to the pair (Interface(1),
   * folly::IPAddress). An example of an element in this map is: (RouterID(0),
   * 169.254.0.0/16) --> (Interface(1), 169.254.0.1) This specifies that the
   * network 169.254.0.0/16 in VRF 0 can be reached via Interface 1, which has
   * an address of 169.254.0.1. in that subnet. Note that the IP address in the
   * key has its mask applied to it while the IP address value doesn't.
   */
  using PrefixToInterfaceIDAndIP = boost::container::
      flat_map<folly::CIDRNetwork, std::pair<InterfaceID, folly::IPAddress>>;
  using RouterIDAndNetworkToInterfaceRoutes =
      boost::container::flat_map<RouterID, PrefixToInterfaceIDAndIP>;
  using InterfaceToVrf = std::unordered_map<InterfaceID, RouterID>;

  void reconfigure(
      const SwitchIdScopeResolver* resolver,
      const RouterIDAndNetworkToInterfaceRoutes&
          configRouterIDToInterfaceRoutes,
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
      bool ecmpModeChanged = false,
      const std::optional<InterfaceToVrf>& interfaceToVrf = std::nullopt);

  // ECMP width used to normalize nexthops in the RIB, sourced from
  // cfg.SwitchSettings.ecmpWidth; set at config apply.
  void setEcmpWidth(uint32_t ecmpWidth);
  uint32_t getEcmpWidth() const;

  // Copies ECMP overrides of routes changed in delta into the RIB, all VRFs.
  void updateEcmpOverrides(const StateDelta& delta);
  // Runs ribToSwitchStateFunc on every VRF outside any route update.
  void syncStateFromRib(
      const SwitchIdScopeResolver* resolver,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie) const;
  // Whether warm boot reconstruct folded or backfilled NextHop ids.
  bool nextHopIdsChangedAtWarmboot() const {
    return nextHopIdsChangedAtWarmboot_;
  }

  void updateRemoteInterfaceRoutes(
      const SwitchIdScopeResolver* resolver,
      const RouterIDAndNetworkToInterfaceRoutes& toAdd,
      const boost::container::flat_map<
          facebook::fboss::RouterID,
          std::vector<
              std::pair<folly::CIDRNetwork, facebook::fboss::InterfaceID>>>&
          toDel,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  void updateInterfaceVrfs(
      const SwitchIdScopeResolver* resolver,
      const InterfaceToVrf& interfaceToVrf,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  /*
   * FIB assisted fromThrift. With shared data structure of routes
   * all except the unresolved routes are shared b/w rib and FIB, so
   * we can simply reconstruct RIB by ser/deser unresolved routes
   * and importing FIB
   */
  static RibRouteTables fromThrift(
      const std::map<int32_t, state::RouteTableFields>& ribThrift,
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
      const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
      const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
      uint32_t ecmpWidth,
      const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
          classBasedPolicyMaps);

  void ensureVrf(RouterID rid);
  std::vector<RouterID> getVrfList() const;
  std::vector<RouteDetails> getRouteTableDetails(RouterID rid) const;
  std::vector<MplsRouteDetails> getMplsRouteTableDetails() const;

  // Returns a copy of the MySidTable as thrift fields.
  // This is expensive and should only be used in tests.
  std::unordered_map<folly::CIDRNetworkV6, state::MySidFields>
  getMySidTableCopy() const;

  template <typename AddressT>
  std::shared_ptr<Route<AddressT>> longestMatch(
      const AddressT& address,
      RouterID vrf) const;

  // Atomically perform longestMatch + NextHopSetID resolution under a single
  // rlock on synchronizedRouteTables_. Returns nullopt if no route found.
  // Otherwise returns {route, resolved nexthops}.
  // When normalized=true (default), returns weight-normalized nexthops.
  // When normalized=false, returns raw resolved nexthops.
  template <typename AddressT>
  std::optional<std::pair<std::shared_ptr<Route<AddressT>>, RouteNextHopSet>>
  getRouteAndNextHops(
      const AddressT& address,
      RouterID vrf,
      bool normalized = true) const;

  std::map<int32_t, state::RouteTableFields> toThrift() const;
  static RibRouteTables fromThrift(
      const std::map<int32_t, state::RouteTableFields>&,
      uint32_t ecmpWidth);
  std::map<int32_t, state::RouteTableFields> warmBootState() const;

  std::string getPolicyDefaultNextHopGroup(const std::string& policyName) const;

 private:
  void updateFib(
      const SwitchIdScopeResolver* resolver,
      RouterID vrf,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie,
      bool ecmpModeChanged = false,
      bool vrfDeleted = false);
  void updateFib(
      const SwitchIdScopeResolver* resolver,
      const std::vector<RouterID>& vrfs,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie,
      bool ecmpModeChanged = false);
  struct VrfFibUpdate {
    RouterID vrf;
    bool deleted{false};
  };
  // Programs the FIBs of all vrfs in one switch state update
  void updateFib(
      const SwitchIdScopeResolver* resolver,
      const std::vector<VrfFibUpdate>& vrfs,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie,
      bool ecmpModeChanged = false);
  template <typename RibUpdateFn>
  void updateRib(RouterID vrf, const RibUpdateFn& updateRib);
  // Like updateRib(vrf, ...), for changes across VRFs
  template <typename RibUpdateFn>
  void updateRib(const RibUpdateFn& updateRib);
  void updateEcmpOverrides(
      const std::vector<RouterID>& vrfs,
      const StateDelta& delta);

  /*
   * MySid updates
   */
  void updateFib(
      const SwitchIdScopeResolver* resolver,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);
  template <typename RibUpdateFn>
  void updateRibMySids(const RibUpdateFn& updateRibFn);

  /*
   * Named next-hop group updates
   */
  template <typename RibUpdateFn>
  void updateRibNamedNextHopGroups(const RibUpdateFn& updateRibFn);
  void updateFibNamedNextHopGroups(
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  /*
   * Currently, route updates to separate VRFs are made to be sequential. In the
   * event FBOSS has to operate in a routing architecture with numerous VRFs,
   * we can avoid a slow down by a factor of number of VRFs by parallelizing
   * route updates across VRFs. This can be accomplished simply by associating
   * the mutex implicit in folly::Synchronized with an individual RouteTable.
   */
  using RouterIDToRouteTable =
      boost::container::flat_map<RouterID, VrfRouteTable>;

  struct RouteTables {
    RouterIDToRouteTable routerIDToRouteTable;
    std::unordered_map<folly::CIDRNetworkV6, std::shared_ptr<MySid>> mySidTable;
    std::unique_ptr<NextHopIDManager> nextHopIDManager{
        FLAGS_enable_nexthop_id_manager ? std::make_unique<NextHopIDManager>()
                                        : nullptr};
    uint32_t ecmpWidth{FLAGS_ecmp_width};
  };

  using SynchronizedRouteTables = folly::Synchronized<RouteTables>;

  // After a change in changedVrf, re-resolves the VRFs whose next hops may
  // resolve in it and adds them to vrfsToProgram
  static void resolveDependentVrfs(
      RouterID changedVrf,
      RouterIDToRouteTable& routeTables,
      NextHopIDManager* nextHopIDManager,
      MySidTable* mySidTable,
      uint32_t ecmpWidth,
      std::size_t* cyclesDetectedOut,
      std::vector<RouterID>& vrfsToProgram);

  void importFibs(
      const SynchronizedRouteTables::WLockedPtr& lockedRouteTables,
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
      const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFibs);

  // Shared implementation for both MySid update() overloads.
  void updateMySidsImpl(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidWithNextHops>& toAdd,
      const std::vector<MySidNeighborRemoved>& toUnresolveIfMatch,
      const std::vector<IpPrefix>& toDelete,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);

  // Stamps missing clientNextHopSetID, resolvedNextHopSetID, and
  // normalizedResolvedNextHopSetID on routes from a pre-feature warmboot
  // snapshot. No-op when manager is null or IDs are already set. Returns
  // whether it backfilled any route.
  // TODO: remove once FLAGS_enable_nexthop_id_manager is permanently on.
  static bool backfillNextHopIds(
      const SynchronizedRouteTables::WLockedPtr& lockedRouteTables);

  SynchronizedRouteTables synchronizedRouteTables_;
  bool nextHopIdsChangedAtWarmboot_{false};
};

class RoutingInformationBase {
 public:
  RoutingInformationBase(const RoutingInformationBase& o) = delete;
  RoutingInformationBase& operator=(const RoutingInformationBase& o) = delete;
  RoutingInformationBase(RoutingInformationBase&&) = delete;
  RoutingInformationBase& operator=(RoutingInformationBase&&) = delete;
  RoutingInformationBase();
  ~RoutingInformationBase();

  struct UpdateStatistics {
    std::size_t v4RoutesAdded{0};
    std::size_t v4RoutesDeleted{0};
    std::size_t v6RoutesAdded{0};
    std::size_t v6RoutesDeleted{0};
    std::size_t mplsRoutesAdded{0};
    std::size_t mplsRoutesDeleted{0};
    std::chrono::microseconds duration{0};
    std::size_t resolutionCyclesDetected{0};
  };

  /*
   * `update()` first acquires exclusive ownership of the RIB and executes the
   * following sequence of actions:
   * 1. Injects and removes routes in `toAdd` and `toDelete`, respectively.
   * 2. Triggers recursive (IP) resolution.
   * 3. Updates the FIB synchronously.
   * NOTE : there is no order guarantee b/w toAdd and toDelete. We may do
   * either first. This does not matter for non overlapping add/del, but
   * can be meaningful for overlaps. If so, the caller is responsible for
   * ensuring this order - e.g. by first calling update with only toAdd
   * routes and then with toDel routes or vice versa. In our fboss agent
   * applications we separate this out by making synchronous calls in
   * response to add/del route thrift calls, which are distinct apis.
   * TODO: Consider breaking down update into add, del, syncClient
   * interfaces
   *
   * If a UnicastRoute does not specify its admin distance, then we derive its
   * admin distance via its clientID.  This is accomplished by a mapping from
   * client IDs to admin distances provided in configuration. Unfortunately,
   * this mapping is exposed via SwSwitch, which we can't a dependency on here.
   * The adminDistanceFromClientID allows callsites to propagate admin distances
   * per client.
   */
  UpdateStatistics update(
      const SwitchIdScopeResolver* resolver,
      RouterID routerID,
      ClientID clientID,
      AdminDistance adminDistanceFromClientID,
      const std::vector<UnicastRoute>& toAdd,
      const std::vector<IpPrefix>& toDelete,
      bool resetClientsRoutes,
      folly::StringPiece updateType,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      void* cookie);

  UpdateStatistics update(
      const SwitchIdScopeResolver* resolver,
      RouterID routerID,
      ClientID clientID,
      AdminDistance adminDistanceFromClientID,
      const std::vector<MplsRoute>& toAdd,
      const std::vector<MplsLabel>& toDelete,
      bool resetClientsRoutes,
      folly::StringPiece updateType,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      void* cookie);
  /*
   * Update mySids in RIB and switchState
   */
  void update(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidEntry>& toAdd,
      const std::vector<IpPrefix>& toDelete,
      folly::StringPiece updateType,
      const RibMySidToSwitchStateFunction ribMySidToSwitchStateFunc,
      void* cookie);
  /*
   * Update mySids in RIB and switchState from pre-built MySid state objects.
   * Used by the config-driven path (SwSwitch::applyMySidConfig).
   * Each entry in toAdd is (MySid state object, its unresolved next-hop set).
   * The next-hop set is empty for DECAPSULATE_AND_LOOKUP entries;
   * populated for others.
   *
   * The synchronous overload blocks until the rib event-base thread finishes
   * applying the update and rethrows any exception on the caller's thread.
   * The async overload enqueues the update fire-and-forget on the rib event
   * base — required for callers that must not block (e.g., StateObservers
   * running on the SwSwitch update thread, where a synchronous rib update
   * would deadlock with the rib's own state-update callbacks).
   */
  void update(
      const SwitchIdScopeResolver* resolver,
      std::vector<MySidWithNextHops> toAdd,
      std::vector<MySidNeighborRemoved> toUnresolveIfMatch,
      std::vector<IpPrefix> toDelete,
      folly::StringPiece updateType,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie) {
    updateMySidImpl(
        resolver,
        std::move(toAdd),
        std::move(toUnresolveIfMatch),
        std::move(toDelete),
        updateType,
        ribMySidToSwitchStateFunc,
        cookie,
        false /* async */);
  }

  void updateAsync(
      const SwitchIdScopeResolver* resolver,
      std::vector<MySidWithNextHops> toAdd,
      std::vector<MySidNeighborRemoved> toUnresolveIfMatch,
      std::vector<IpPrefix> toDelete,
      folly::StringPiece updateType,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie) {
    updateMySidImpl(
        resolver,
        std::move(toAdd),
        std::move(toUnresolveIfMatch),
        std::move(toDelete),
        updateType,
        ribMySidToSwitchStateFunc,
        cookie,
        true /* async */);
  }

  void updateMySidFrrProtection(
      const SwitchIdScopeResolver* resolver,
      const std::vector<MySidFrrProtectionUpdate>& toAddOrUpdate,
      const std::vector<folly::CIDRNetwork>& toDelete,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie);

  /*
   * VrfAndNetworkToInterfaceRoute is conceptually a mapping from the pair
   * (RouterID, folly::CIDRNetwork) to the pair (Interface(1),
   * folly::IPAddress). An example of an element in this map is: (RouterID(0),
   * 169.254.0.0/16) --> (Interface(1), 169.254.0.1) This specifies that the
   * network 169.254.0.0/16 in VRF 0 can be reached via Interface 1, which has
   * an address of 169.254.0.1. in that subnet. Note that the IP address in the
   * key has its mask applied to it while the IP address value doesn't.
   */
  using RouterIDAndNetworkToInterfaceRoutes =
      RibRouteTables::RouterIDAndNetworkToInterfaceRoutes;
  using InterfaceToVrf = RibRouteTables::InterfaceToVrf;

  void reconfigure(
      const SwitchIdScopeResolver* resolver,
      const RouterIDAndNetworkToInterfaceRoutes&
          configRouterIDToInterfaceRoutes,
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
      // Set when config changed the ECMP backup switching mode. Overrides then
      // changed outside any fib update, so sync them for all routes.
      bool ecmpModeChanged = false,
      // VRF of each configured interface; restricts interface-scoped next hops
      const std::optional<InterfaceToVrf>& interfaceToVrf = std::nullopt);

  void setEcmpWidth(uint32_t ecmpWidth);
  uint32_t getEcmpWidth() const;

  // Syncs ECMP overrides changed outside any fib update, e.g. by warm boot
  // reconstruct, into the RIB.
  void updateEcmpOverrides(const StateDelta& delta);
  // Applies the RIB to a state outside any route update, e.g. to bring a warm
  // boot state's NextHop ids in line with the RIB's.
  void syncStateFromRib(
      const SwitchIdScopeResolver* resolver,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);
  bool nextHopIdsChangedAtWarmboot() const {
    return ribTables_.nextHopIdsChangedAtWarmboot();
  }

  void updateRemoteInterfaceRoutes(
      const SwitchIdScopeResolver* resolver,
      const RouterIDAndNetworkToInterfaceRoutes& toAdd,
      const boost::container::flat_map<
          facebook::fboss::RouterID,
          std::vector<
              std::pair<folly::CIDRNetwork, facebook::fboss::InterfaceID>>>&
          toDel,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  // Moves the interfaces in interfaceToVrf to their new VRF: their interface
  // routes and their place in the VRF interface lists. Programs the changed
  // VRFs and the target VRFs in one switch state update.
  void updateInterfaceVrfs(
      const SwitchIdScopeResolver* resolver,
      const InterfaceToVrf& interfaceToVrf,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  void setClassID(
      const SwitchIdScopeResolver* resolver,
      RouterID rid,
      const std::vector<folly::CIDRNetwork>& prefixes,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      std::optional<cfg::AclLookupClass> classId,
      void* cookie) {
    setClassIDImpl(
        resolver, rid, prefixes, ribToSwitchStateFunc, classId, cookie, false);
  }

  void setClassIDAsync(
      const SwitchIdScopeResolver* resolver,
      RouterID rid,
      const std::vector<folly::CIDRNetwork>& prefixes,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      std::optional<cfg::AclLookupClass> classId,
      void* cookie) {
    setClassIDImpl(
        resolver, rid, prefixes, ribToSwitchStateFunc, classId, cookie, true);
  }
  void updateStateInRibThread(const std::function<void()>& fn);

  /*
   * FIB assisted fromThrift. With shared data structure of routes
   * all except the unresolved routes are shared b/w rib and FIB, so
   * we can simply reconstruct RIB by ser/deser unresolved routes
   * and importing FIB
   */
  static std::unique_ptr<RoutingInformationBase> fromThrift(
      const std::map<int32_t, state::RouteTableFields>& ribJson,
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
      const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
      const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
      uint32_t ecmpWidth,
      const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
          classBasedPolicyMaps);

  void ensureVrf(RouterID rid) {
    ribTables_.ensureVrf(rid);
  }
  std::vector<RouterID> getVrfList() const {
    return ribTables_.getVrfList();
  }
  std::vector<RouteDetails> getRouteTableDetails(RouterID rid) const {
    return ribTables_.getRouteTableDetails(rid);
  }
  std::vector<MplsRouteDetails> getMplsRouteTableDetails() const {
    return ribTables_.getMplsRouteTableDetails();
  }
  std::unordered_map<folly::CIDRNetworkV6, state::MySidFields>
  getMySidTableCopy() const {
    return ribTables_.getMySidTableCopy();
  }
  void waitForRibUpdates() {
    ensureRunning();
    ribUpdateEventBase_.runInFbossEventBaseThreadAndWait([] { return; });
  }

  void stop();

  template <typename AddressT>
  std::shared_ptr<Route<AddressT>> longestMatch(
      const AddressT& address,
      const RouterID& vrf) const {
    return ribTables_.longestMatch(address, vrf);
  }

  template <typename AddressT>
  std::optional<std::pair<std::shared_ptr<Route<AddressT>>, RouteNextHopSet>>
  getRouteAndNextHops(
      const AddressT& address,
      RouterID vrf,
      bool normalized = true) const {
    return ribTables_.getRouteAndNextHops(address, vrf, normalized);
  }

  std::map<int32_t, state::RouteTableFields> toThrift() const;
  static std::unique_ptr<RoutingInformationBase> fromThrift(
      const std::map<int32_t, state::RouteTableFields>&,
      uint32_t ecmpWidth);
  std::map<int32_t, state::RouteTableFields> warmBootState() const;

  // Returns a deep copy of the NextHopIDManager. This is an expensive
  // operation and is intended only for tests.
  std::unique_ptr<NextHopIDManager> getNextHopIDManagerCopy() const {
    return ribTables_.getNextHopIDManagerCopy();
  }

  // Test-only accessors for the per-VRF unresolved-routes indexes.
  std::unordered_map<
      folly::CIDRNetworkV4,
      std::shared_ptr<Route<folly::IPAddressV4>>>
  getUnresolvedV4RoutesForTest(const RouterID& vrf) const {
    return ribTables_.getUnresolvedV4RoutesForTest(vrf);
  }
  std::unordered_map<
      folly::CIDRNetworkV6,
      std::shared_ptr<Route<folly::IPAddressV6>>>
  getUnresolvedV6RoutesForTest(const RouterID& vrf) const {
    return ribTables_.getUnresolvedV6RoutesForTest(vrf);
  }
  std::unordered_map<LabelID, std::shared_ptr<Route<LabelID>>>
  getUnresolvedMplsRoutesForTest(const RouterID& vrf) const {
    return ribTables_.getUnresolvedMplsRoutesForTest(vrf);
  }

  // Named next-hop group operations. These run on the RIB thread to ensure
  // thread-safe access to NextHopIDManager. Allocation/deallocation is followed
  // by a state update callback (under rlock) to sync switch state.
  void addOrUpdateNamedNextHopGroups(
      const SwitchIdScopeResolver* resolver,
      const std::vector<std::pair<std::string, RouteNextHopSet>>& groups,
      const RibToSwitchStateFunction& ribToSwitchStateFunc,
      void* cookie);

  void deleteNamedNextHopGroups(
      const std::vector<std::string>& names,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  void addOrUpdatePolicies(
      const std::vector<ClassBasedPolicy>& policies,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

  void removePolicies(
      const std::vector<std::string>& policyNames,
      const std::function<void(const NextHopIDManager*)>& stateUpdateFn);

 private:
  void ensureRunning() const;
  void setClassIDImpl(
      const SwitchIdScopeResolver* resolver,
      RouterID rid,
      const std::vector<folly::CIDRNetwork>& prefixes,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      std::optional<cfg::AclLookupClass> classId,
      void* cookie,
      bool async);
  // Shared body for update(MySidWithNextHops) and
  // updateAsync(MySidWithNextHops). Sync mode (async=false) blocks the caller
  // and rethrows any exception. Async mode (async=true) enqueues the update
  // fire-and-forget; failures are logged on the rib event-base thread.
  void updateMySidImpl(
      const SwitchIdScopeResolver* resolver,
      std::vector<MySidWithNextHops> toAdd,
      std::vector<MySidNeighborRemoved> toUnresolveIfMatch,
      std::vector<IpPrefix> toDelete,
      folly::StringPiece updateType,
      const RibMySidToSwitchStateFunction& ribMySidToSwitchStateFunc,
      void* cookie,
      bool async);

  template <typename TraitsType>
  UpdateStatistics updateImpl(
      const SwitchIdScopeResolver* resolver,
      RouterID routerID,
      ClientID clientID,
      AdminDistance adminDistanceFromClientID,
      const std::vector<typename TraitsType::ThriftRoute>& toAdd,
      const std::vector<typename TraitsType::ThriftRouteId>& toDelete,
      bool resetClientsRoutes,
      folly::StringPiece updateType,
      RibToSwitchStateFunction ribToSwitchStateFunc,
      void* cookie);

  std::unique_ptr<std::thread> ribUpdateThread_;
  FbossEventBase ribUpdateEventBase_{"RibUpdateEventBase"};
  RibRouteTables ribTables_;
};

// ID-only primitive: resolve a NextHopSetID directly against the
// NextHopIDManager. Throws FbossError if the id is not present in the
// manager (mirrors FibInfo::resolveNextHopSetFromId behavior).
// Used by RIB-internal callers that operate before the state is published;
// caller is responsible for the FLAGS_resolve_nexthops_from_id check.
RouteNextHopSet getNextHopsFromRib(
    const NextHopIDManager* manager,
    NextHopSetID id);

// Resolve per-client nexthops from a per-client RouteNextHopEntry via the
// NextHopIDManager directly. Used by RIB-internal callers that operate
// before the state is published. When FLAGS_resolve_nexthops_from_id is on,
// resolves via clientNextHopSetID against the manager. When off, falls
// back to entry.getNextHopSet(). Companion to the state-based overload
// `getClientNextHops(state, entry)` in FibHelpers.h.
RouteNextHopSet getClientNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry);

// Resolve resolved-side (FIB-resolved) nexthops from a RouteNextHopEntry
// via the NextHopIDManager directly. Used by RIB-internal callers operating
// before the state is published. When FLAGS_resolve_nexthops_from_id is on,
// resolves via resolvedNextHopSetID against the manager. When off, falls
// back to entry.getNextHopSet(). Companion to FibHelpers::getNextHops.
RouteNextHopSet getResolvedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry);

// Resolve the non-override normalized nexthops from a RouteNextHopEntry
// via the NextHopIDManager directly. Used by RIB-internal callers operating
// before the state is published. When FLAGS_resolve_nexthops_from_id is on,
// resolves via normalizedResolvedNextHopSetID against the manager. When
// off, falls back to entry.nonOverrideNormalizedNextHops(ecmpWidth). Companion
// to FibHelpers::getNonOverrideNormalizedNextHops.
RouteNextHopSet getNonOverrideNormalizedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry,
    uint32_t ecmpWidth);

// Resolve the normalized nexthops from a RouteNextHopEntry via the
// NextHopIDManager. If the entry has override nexthops (inline for now),
// returns entry.normalizedNextHops(ecmpWidth) so the override is honored;
// otherwise delegates to the ID-aware getNonOverrideNormalizedNextHopsFromRib.
// Companion to FibHelpers::getNormalizedNextHops.
RouteNextHopSet getNormalizedNextHopsFromRib(
    const NextHopIDManager* manager,
    const RouteNextHopEntry& entry,
    uint32_t ecmpWidth);

// Resolve a next-hop set against the RIB the same way route resolution
// resolves a route's next hops: each member is dereferenced to the
// interface-scoped next hops of its longest-match route, trying each VRF in
// order. A member with no resolving route is dropped, so the remaining
// members still forward. Members that already carry an interface pass
// through untouched. Route tables must already be resolved.
RouteNextHopSet resolveNextHopSetFromRib(
    const VrfRouteTables& routeTables,
    const NextHopIDManager* manager,
    const RouteNextHopSet& nhops,
    uint32_t ecmpWidth);

// Resolve each named group's next hops against the RIB and point the policies
// referencing it at the normalized set id for the result. A name with no such
// group is skipped.
void resolveNamedNhgs(
    const VrfRouteTables& routeTables,
    NextHopIDManager* manager,
    const std::set<std::string>& names,
    uint32_t ecmpWidth);

void resolveNamedNhgs(
    const VrfRouteTables& routeTables,
    NextHopIDManager* manager,
    uint32_t ecmpWidth);

} // namespace facebook::fboss
