// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <folly/IPAddress.h>
#include <gtest/gtest_prod.h>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/LabelForwardingInformationBase.h"
#include "fboss/agent/state/MySidMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

class RibRouteTables;
class MultiSwitchClassBasedPolicyMap;

// A set of NextHopIDs (used as key for NextHopIDSetID mapping)
using NextHopIDSet = std::set<NextHopID>;

using PolicySet = std::unordered_set<std::string>;

} // namespace facebook::fboss

// Hash function for NextHopIDSet to be used in unordered_map
namespace std {
template <>
struct hash<facebook::fboss::NextHopIDSet> {
  size_t operator()(const facebook::fboss::NextHopIDSet& idSet) const;
};
} // namespace std

namespace facebook::fboss {

struct ClassBasedPolicyNhgs {
  std::string defaultNexthopGroup;
  // Forwarding class -> redirect NHG name.
  std::map<ForwardingClass, std::string> class2NextHopGroup;
  // One held reference per distinct NHG name this policy references.
  std::unordered_map<std::string, NextHopSetID> normalizedIds;
};

/**
 * NextHopIDManager is responsible for generating and managing unique IDs
 * for NextHops and NextHopSets. It maintains reference counts for each
 * assigned ID to track usage.
 */
class NextHopIDManager {
 public:
  // Structure to hold NextHop and its reference count (maps NextHopID → NextHop
  // + refcount). This enables O(1) refcount decrement by NextHopID without
  // hashing the NextHop object.
  struct NextHopEntry {
    NextHop nextHop;
    uint32_t refCount{0};

    NextHopEntry(NextHop nh, uint32_t rc)
        : nextHop(std::move(nh)), refCount(rc) {}
  };

  // Structure to hold ID and reference count for NextHopSets
  // Must be public as it's exposed through NextHopIdSetIter
  struct NextHopSetIDInfo {
    NextHopSetID id;
    uint32_t count;

    NextHopSetIDInfo(NextHopSetID id_, uint32_t count_)
        : id(id_), count(count_) {}
  };

  // Type alias for the NextHop to NextHopID map iterator
  using NextHopToIDIter =
      std::unordered_map<NextHop, NextHopID>::const_iterator;

  // Type alias for the NextHopIDSet to NextHopSetIDInfo map iterator
  using NextHopIdSetIter =
      std::unordered_map<NextHopIDSet, NextHopSetIDInfo>::const_iterator;

  // Result struct for NextHop allocation operations
  // Contains information about what was allocated so callers can update FibInfo
  // incrementally
  struct NextHopAllocationResult {
    // Const iterator to the NextHopIDSet -> NextHopSetIDInfo entry
    NextHopIdSetIter nextHopIdSetIter;
    // List of newly allocated NextHopIDs (caller can retrieve NextHop from
    // idToNextHop map using these IDs)
    std::vector<NextHopID> addedNextHopIds;
  };

  // Result struct for NextHop deallocation operations
  // Contains information about what was deallocated so callers can update
  // FibInfo incrementally
  struct NextHopDeallocationResult {
    // List of NextHopIDs that were deallocated (refcount reached 0)
    std::vector<NextHopID> removedNextHopIds;
    // Only populated if the set was deallocated (refcount reached 0)
    std::optional<NextHopSetID> removedSetId;
  };

  // Result struct for NextHop update operations
  // Contains both allocation and deallocation information
  struct NextHopUpdateResult {
    NextHopAllocationResult allocation;
    NextHopDeallocationResult deallocation;
  };

  // Result struct for named next-hop group allocation
  struct NamedNextHopGroupAllocationResult {
    NextHopAllocationResult allocation;
    // Name of the next-hop group
    std::string name;
    // Whether this is a new group or an update to existing
    bool isNew{false};
  };

  // Result struct for named next-hop group update
  struct NamedNextHopGroupUpdateResult {
    NextHopAllocationResult allocation;
    NextHopDeallocationResult deallocation;
    std::string name;
  };

  NextHopIDManager() = default;

  // Get or allocate a NextHopID for the given NextHop
  // Returns const iterator to the NextHop -> NextHopID entry
  NextHopToIDIter getOrAllocateNextHopID(const NextHop& nextHop);

  // Get or allocate a NextHopSetID for the given set of NextHopIDs
  // Returns const iterator to the NextHopIDSet -> NextHopSetIDInfo entry
  NextHopIdSetIter getOrAllocateNextHopSetID(const NextHopIDSet& nextHopIDSet);

  // Decrement reference count for a NextHop and deallocate if count reaches 0
  // Returns true if deallocated, false otherwise
  bool decrOrDeallocateNextHop(const NextHop& nextHop);

  // Decrement reference count for a set of NextHopIDs and deallocate if count
  // reaches 0. Returns true if deallocated, false otherwise
  bool decrOrDeallocateNextHopIDSet(const NextHopIDSet& nextHopIDSet);

  // Get the reverse lookup map from NextHopID to NextHopEntry (NextHop +
  // refCount)
  const std::unordered_map<NextHopID, NextHopEntry>& getIdToNextHop() const {
    return idToNextHop_;
  }

  // Get the reverse lookup map from NextHopSetID to NextHopIDSet
  const std::unordered_map<NextHopSetID, NextHopIDSet>& getIdToNextHopIdSet()
      const {
    return idToNextHopIdSet_;
  }

  // Retrieves or allocate NextHopSetID for a RouteNextHopSet
  // Returns NextHopAllocationResult containing the const iterator to the
  // NextHopIDSet entry and any newly allocated NextHopIDs
  NextHopAllocationResult getOrAllocRouteNextHopSetID(
      const RouteNextHopSet& nextHopSet);

  // Decrements or deallocates NextHopSetID for a RouteNextHopSet
  // Returns NextHopDeallocationResult containing any deallocated NextHops
  // and optionally the removed setId (if refcount reached 0)
  NextHopDeallocationResult decrOrDeallocRouteNextHopSetID(
      NextHopSetID nextHopSetID);

  // Lookup the NextHopSetID for a given RouteNextHopSet
  // Returns std::nullopt if the set doesn't exist
  std::optional<NextHopSetID> lookupRouteNextHopSetID(
      const RouteNextHopSet& nextHopSet) const;

  // Update from old RouteNextHopSet to new RouteNextHopSet
  // Decrements old, allocates/increments new
  // Returns NextHopUpdateResult containing both allocation and deallocation
  // info
  NextHopUpdateResult updateRouteNextHopSetID(
      NextHopSetID oldNextHopSetID,
      const RouteNextHopSet& newNextHopSet);

  // Named next-hop group management
  // Allocate or update a named next-hop group with the given nexthops
  // The name to nexthops mapping is maintained internally
  // Returns allocation result and whether it was a new group
  NamedNextHopGroupAllocationResult allocateNamedNextHopGroup(
      const std::string& name,
      const RouteNextHopSet& nextHopSet);

  // Update an existing named next-hop group with new nexthops
  // Deallocates old nexthops and allocates new ones
  // Throws if the group doesn't exist
  NamedNextHopGroupUpdateResult updateNamedNextHopGroup(
      const std::string& name,
      const RouteNextHopSet& newNextHopSet);

  // Deallocate a named next-hop group
  // Removes the name to nexthops mapping and deallocates the nexthops
  // Returns deallocation result
  // Throws if the group doesn't exist
  NextHopDeallocationResult deallocateNamedNextHopGroup(
      const std::string& name);

  /**
   * Point every class-based policy referencing named group `name` at the
   * normalized next-hop set id for that group's resolved next hops. This is
   * the id a PBR ACL synthesized from those policies matches on.
   */
  void updateNormalizedNamedNhgId(
      const std::string& name,
      const RouteNextHopSet& resolvedNextHopSet,
      uint32_t ecmpWidth);

  // Get the NextHopSetID for a named next-hop group
  // Returns std::nullopt if the group doesn't exist
  std::optional<NextHopSetID> getNextHopSetIDForName(
      const std::string& name) const;

  // Only groups a class-based policy references carry a normalized set id.
  std::optional<NextHopSetID> getNormalizedNextHopSetIDForPolicyNhg(
      const std::string& name) const;

  // Get the nexthops for a named next-hop group
  // Returns std::nullopt if the group doesn't exist
  std::optional<RouteNextHopSet> getNextHopsForName(
      const std::string& name) const;

  // Get the RouteNextHopSet for a given NextHopSetID
  // Throws FbossError if the NextHopSetID is not found
  RouteNextHopSet getNextHops(NextHopSetID nextHopSetID) const;

  // Get the RouteNextHopSet for a given NextHopSetID
  // Returns std::nullopt if the NextHopSetID is not found
  std::optional<RouteNextHopSet> getNextHopsIf(NextHopSetID nextHopSetID) const;

  // True iff the next-hop set identified by `nextHopSetID` contains a
  // next-hop whose address equals `ip`. Avoids materializing the full
  // RouteNextHopSet (which getNextHopsIf rebuilds via per-element
  // lookups) — used by the MySid neighbor-removal hot path.
  bool nextHopSetContainsAddr(
      NextHopSetID nextHopSetID,
      const folly::IPAddress& ip) const;

  // Get all named next-hop groups
  const std::unordered_map<std::string, RouteNextHopSet>&
  getNameToNextHopSetMap() const {
    return nameToNextHopSet_;
  }

  // Get all name to NextHopSetID mappings
  const std::unordered_map<std::string, NextHopSetID>& getNameToNextHopSetID()
      const {
    return nameToNextHopSetID_;
  }

  // Check if a named next-hop group exists
  bool hasNamedNextHopGroup(const std::string& name) const {
    return nameToNextHopSet_.find(name) != nameToNextHopSet_.end();
  }

  // Reverse mapping: track which routes reference a named NHG
  using RoutePrefixKey = std::pair<RouterID, folly::CIDRNetwork>;
  using RouteSet = std::set<RoutePrefixKey>;
  using MySidSet = std::set<folly::CIDRNetworkV6>;

  void addRouteForNamedNhg(
      const std::string& name,
      const RouterID& rid,
      const folly::CIDRNetwork& prefix);

  void removeRouteForNamedNhg(
      const std::string& name,
      const RouterID& rid,
      const folly::CIDRNetwork& prefix);

  const RouteSet& getRoutesForNamedNhg(const std::string& name) const;

  bool hasRoutesForNamedNhg(const std::string& name) const;

  void addMySidForNamedNhg(
      const std::string& name,
      const folly::CIDRNetworkV6& mySid);

  void removeMySidForNamedNhg(
      const std::string& name,
      const folly::CIDRNetworkV6& mySid);

  void removeMySidFromNamedNhgs(const folly::CIDRNetworkV6& mySid);

  const MySidSet& getMySidsForNamedNhg(const std::string& name) const;

  bool hasMySidsForNamedNhg(const std::string& name) const;

  void addOrUpdatePolicy(const ClassBasedPolicy& policy);

  void removePolicy(const std::string& name);

  const ClassBasedPolicyNhgs* getPolicy(const std::string& name) const;

  // Empty if no class-based policy references `name`.
  const PolicySet& getPoliciesForNamedNhg(const std::string& name) const;

  // True if any policy references this named NHG, as its default or as a
  // redirect target.
  bool hasPoliciesForNamedNhg(const std::string& name) const;

  const std::unordered_map<std::string, ClassBasedPolicyNhgs>&
  getPbrPolicyToNamedNhg() const {
    return pbrPolicyToNamedNhg_;
  }

  /**
   * Reconstruct the NextHopIDManager for two main scenarios:
   *
   * 1. Warm Boot:
   *    When the RIB is reconstructed back from the FIB, we need to reconstruct
   *    the NextHopIDManager.
   *
   * 2. Rollback:
   *    When the RIB's NextHopIDManager has to be reconstructed from the
   *    previous FIB state. This is when route updates fail to apply.
   *
   * For each FibInfo in MultiSwitchFibInfoMap:
   *   1. Get idToNextHopMap and idToNextHopIdSetMap from this FibInfo
   *   2. For each route in FibV4/FibV6:
   *      a. Collect all NextHopSetIDs from route (resolvedNextHopSetID, and
   *         future: normalizedResolvedNextHopID)
   *      b. For each SetID, lookup NextHopIDSet from this FibInfo's
   *         idToNextHopIdSetMap
   *      c. For each NextHopID in the set, lookup NextHop from this FibInfo's
   *         idToNextHopMap
   *      d. Update internal maps and refcounts
   *
   * We have to reconstruct the IDmanager from all the FibsInfo present on all
   * switches. NextHopID manager is common across all switches, but the NextHop
   * ID maps in the switch state are specific to each switch.
   */
  // ribTables: when non-null, also walks unresolved RIB routes to reconstruct
  // refcounts for clientNextHopSetIDs that the FIB walk cannot see. Pass
  // nullptr from callsites that have no RIB to consult.
  // setIdRemapOut: when non-null, receives the collapsed -> surviving SetID
  // remap built during reconstruction so the caller can repoint route
  // references. Non-empty only on a cross-version warm boot; left empty
  // otherwise. Callers that do not repoint (e.g. rollback) pass nullptr.
  void reconstructFromSwitchStateMaps(
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
      const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
      const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
      const RibRouteTables* ribTables,
      const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
          classBasedPolicyMaps = nullptr,
      std::unordered_map<NextHopSetID, NextHopSetID>* setIdRemapOut = nullptr);

 private:
  static constexpr int64_t kNextHopIDStart = 1;
  static constexpr int64_t kNextHopIDEnd =
      nextHopIdTypeEnd(NextHopIdType::NEXTHOP);
  static constexpr int64_t kNextHopSetIDStart =
      nextHopIdTypeStart(NextHopIdType::NEXTHOP_SET);
  static constexpr int64_t kNextHopSetIDEnd =
      nextHopIdTypeEnd(NextHopIdType::NEXTHOP_SET);

  static constexpr bool isNextHopID(int64_t id) {
    return id >= kNextHopIDStart && id < kNextHopIDEnd;
  }
  static constexpr bool isNextHopSetID(int64_t id) {
    return nextHopIdType(id) == NextHopIdType::NEXTHOP_SET;
  }

  // Decrement reference count for a NextHopID and deallocate if count reaches 0
  // Returns true if deallocated, false otherwise.
  bool decrOrDeallocateNextHopByID(const NextHopID& nextHopID);

  // Move one policy's normalized id for `nhgName` onto the set id for
  // `normalized`, taking a reference for the policy and releasing the one it
  // held. Returns the new id.
  NextHopSetID updatePolicyNormalizedId(
      const std::string& policyName,
      const std::string& nhgName,
      const RouteNextHopSet& normalized);

  // Mutable state threaded through the reconstructFromSwitchStateMaps passes.
  // The member maps the passes update are reached directly; only these running
  // values are shared across the passes.
  struct ReconstructionContext {
    // Highest persisted NextHopID / NextHopSetID seen across all passes; used
    // to seed the next-available watermarks once reconstruction completes.
    NextHopID maxNextHopId{kNextHopIDStart - 1};
    NextHopSetID maxNextHopSetId{kNextHopSetIDStart - 1};
    // Next fresh SetID to mint for a deduped set whose persisted SetID
    // was retired; reseeded above every persisted SetID at the start of
    // reconstruction (see computeFreshSetIdSeed).
    NextHopSetID nextFreshSetId{kNextHopSetIDStart};
    // Collapsed -> surviving NextHopSetID remap for SetIDs retired during
    // cross-version reconstruction (two persisted nexthops rehash to one,
    // collapsing their distinct sets). reconstructFromSwitchStateMaps hands
    // this out via setIdRemapOut so the caller repoints route references;
    // empty on a same-version boot.
    std::unordered_map<NextHopSetID, NextHopSetID> setIdRemap;
    // FibInfo id maps (identical across switches), cached from the first
    // FibInfo by the FIB pass and reused by the MySid / MPLS / unresolved-RIB
    // passes (whose sets may not be referenced by any FIB route).
    std::shared_ptr<IdToNextHopMap> fibId2NhopMap;
    std::shared_ptr<IdToNextHopIdSetMap> fibId2NhopIdSetMap;
  };

  // Reconstruct a single persisted SetID: rebuild its member NextHopIDSet from
  // FibInfo, dedup duplicate members, register/collapse/mint the set,
  // and advance the ctx watermarks.
  void processNextHopSetIdForReconstruction(
      NextHopSetID setId,
      const std::shared_ptr<IdToNextHopMap>& id2NhopMap,
      const std::shared_ptr<IdToNextHopIdSetMap>& id2NhopIdSetMap,
      ReconstructionContext& ctx);

  // FIB pass: reconstruct route nexthop sets and named next-hop groups from
  // every FibInfo, caching the (cross-switch-identical) id maps into ctx.
  void reconstructFibPass(
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap,
      ReconstructionContext& ctx);

  // MySid pass: refcount nexthop sets referenced by MySid entries (which may
  // not be referenced by any FIB route).
  void reconstructMySidPass(
      const std::shared_ptr<MultiSwitchMySidMap>& mySidMap,
      ReconstructionContext& ctx);

  // MPLS FIB pass: refcount resolved/normalized/per-client sets of MPLS routes.
  void reconstructMplsFibPass(
      const std::shared_ptr<MultiLabelForwardingInformationBase>& labelFib,
      ReconstructionContext& ctx);

  // Unresolved-RIB pass: per-client setIDs on unresolved routes that the FIB
  // walk does not see.
  void reconstructUnresolvedRibPass(
      const RibRouteTables* ribTables,
      ReconstructionContext& ctx);

  // Policy pass: rebuild the class-based policy store from the persisted
  // classBasedPolicyMaps, reading each referenced NHG name straight off the
  // policy's persisted NamedNextHopGroupAndID.
  void reconstructClassBasedPolicyPass(
      const std::shared_ptr<MultiSwitchClassBasedPolicyMap>&
          classBasedPolicyMaps,
      ReconstructionContext& ctx);

  // Returns the first SetID strictly above every persisted SetID. Used by
  // reconstructFromSwitchStateMaps to mint fresh SetIDs for deduped
  // member sets whose persisted SetID was retired. The FibInfo
  // idToNextHopIdSetMap is the complete registry of allocated SetIDs
  // (consistent across switches), so its global max + 1 can never collide with
  // a persisted id.
  static NextHopSetID computeFreshSetIdSeed(
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap);

  // Allocates from [kNextHopIDStart, kNextHopIDEnd).
  NextHopID nextAvailableNextHopID_{kNextHopIDStart};

  // Allocates from [kNextHopSetIDStart, kNextHopSetIDEnd).
  NextHopSetID nextAvailableNextHopSetID_{kNextHopSetIDStart};

  // Mapping from NextHop to its assigned NextHopID
  std::unordered_map<NextHop, NextHopID> nextHopToID_;

  // Mapping from set of NextHopIDs to its NextHopSetID and reference count
  std::unordered_map<NextHopIDSet, NextHopSetIDInfo> nextHopIdSetToIDInfo_;

  // Reverse lookup map: NextHopID → {NextHop, refCount}
  std::unordered_map<NextHopID, NextHopEntry> idToNextHop_;

  // Map from NextHopSetID to set of NextHopIDs
  std::unordered_map<NextHopSetID, NextHopIDSet> idToNextHopIdSet_;

  // Named next-hop group mappings
  // Map from name to RouteNextHopSet (the actual nexthops)
  std::unordered_map<std::string, RouteNextHopSet> nameToNextHopSet_;
  // Map from name to NextHopSetID for quick lookup
  std::unordered_map<std::string, NextHopSetID> nameToNextHopSetID_;
  // Reverse mapping: named NHG name to routes referencing it
  std::unordered_map<std::string, RouteSet> nameToRoutes_;
  // Reverse mapping: named NHG name to MySids referencing it
  std::unordered_map<std::string, MySidSet> nameToMySids_;

  // Class-based policy store: policy name -> the NHG names it references.
  std::unordered_map<std::string, ClassBasedPolicyNhgs> pbrPolicyToNamedNhg_;
  // Inverse of pbrPolicyToNamedNhg_.
  std::unordered_map<std::string, PolicySet> namedNhgToPbrPolicies_;

  void addPolicy2NamedNhgMap(
      const std::string& policyName,
      const ClassBasedPolicyNhgs& nhgs);

  void delPolicy2NamedNhgMap(const std::string& policyName);

  // Get the ref count for a given NextHop
  uint32_t getNextHopRefCount(const NextHop& nextHop);

  // Get the ref count for a given set of NextHopIDs
  uint32_t getNextHopIDSetRefCount(const NextHopIDSet& nextHopIDSet);

  // Get the NextHopSetID for a given NextHopIDSet
  // Returns std::nullopt if the set doesn't exist
  std::optional<NextHopSetID> getNextHopSetID(
      const NextHopIDSet& nextHopIDSet) const;

  // Get the NextHopID for a given NextHop (lookup only, no allocation)
  // Returns std::nullopt if the NextHop doesn't exist
  std::optional<NextHopID> getNextHopID(const NextHop& nextHop) const;

  // Clear all NextHop and NextHopIDSet mappings
  void clearNhopIdManagerState();

  FRIEND_TEST(NextHopIDManagerTest, getOrAllocateNextHopID);
  FRIEND_TEST(NextHopIDManagerTest, getOrAllocateNextHopSetID);
  FRIEND_TEST(NextHopIDManagerTest, getOrAllocateNextHopSetIDOrderIndependence);
  FRIEND_TEST(NextHopIDManagerTest, decrOrDeallocateNextHop);
  FRIEND_TEST(NextHopIDManagerTest, decrOrDeallocateNextHopByID);
  FRIEND_TEST(NextHopIDManagerTest, decrOrDeallocateNextHopIDSet);
  FRIEND_TEST(NextHopIDManagerTest, getOrAllocRouteNextHopSetIDWithEmptySet);
  FRIEND_TEST(
      NextHopIDManagerTest,
      getOrAllocRouteNextHopSetIDSubSetSuperSetNextHops);
  FRIEND_TEST(NextHopIDManagerTest, delOrDecrRouteNextHopSetID);
  FRIEND_TEST(NextHopIDManagerTest, updateRouteNextHopSetID);
  FRIEND_TEST(NextHopIDManagerTest, reconstructFromSwitchStateMaps);
  FRIEND_TEST(NextHopIDManagerTest, reconstructMemberDedupMintsFreshSetId);
  FRIEND_TEST(NextHopIDManagerTest, reconstructMemberDedupUnresolvedRoute);
  FRIEND_TEST(NextHopIDManagerTest, reconstructFullCollapseRemapsToSurvivor);
  FRIEND_TEST(
      NextHopIDManagerTest,
      reconstructMultipleRoutesToRetiredSetIdAccumulateFreshCount);
  FRIEND_TEST(
      NextHopIDManagerTest,
      reconstructIntraSetDuplicateShrinksGroupAndDeallocsCleanly);
  FRIEND_TEST(NextHopIDManagerTest, reconstructMySidSetDedupRecordsRemap);
  FRIEND_TEST(NextHopIDManagerTest, reconstructFromSwitchStateMapsMultiSwitch);
  FRIEND_TEST(
      NextHopIDManagerTest,
      reconstructFromSwitchStateMapsClientNextHopSetID);
  FRIEND_TEST(
      NextHopIDManagerTest,
      reconstructFromSwitchStateMaps_MySidResolvedNextHopsId);
  FRIEND_TEST(
      NextHopIDManagerTest,
      reconstructFromSwitchStateMaps_MySidBothNextHopIds);
  FRIEND_TEST(NextHopIDManagerTest, allocateNamedNextHopGroup);
  FRIEND_TEST(NextHopIDManagerTest, updateNamedNextHopGroup);
  FRIEND_TEST(NextHopIDManagerTest, deallocateNamedNextHopGroup);
  FRIEND_TEST(NextHopIDManagerTest, namedNextHopGroupWarmBoot);
  FRIEND_TEST(NextHopIDManagerTest, namedNextHopGroupSharesSetIdWithRoutes);
  FRIEND_TEST(NextHopIDManagerTest, routeReusesNamedNextHopGroupSetId);
  FRIEND_TEST(NextHopIDManagerTest, namedNhgRouteReverseMapping);
  FRIEND_TEST(NextHopIDManagerTest, namedNhgRouteReverseMappingWarmBoot);
  FRIEND_TEST(NextHopIDManagerTest, namedNhgMySidReverseMapping);
  FRIEND_TEST(
      RibMySidUpdaterTest,
      nhopRefCountBumped_afterResolvingNhopWithIntfId);
  FRIEND_TEST(
      RibMySidUpdaterTest,
      twoEntriesSameNhops_resolvedSetIdSharedAndRefCountIsThree);
  FRIEND_TEST(
      RibMySidUpdaterTest,
      secondResolve_differentNhops_oldSetRetainedByRoute);
  FRIEND_TEST(
      RibMySidNextHopTest,
      replaceNodeMySidWithDecapReleasesBothUnresolvedAndResolvedIds);
};

} // namespace facebook::fboss
