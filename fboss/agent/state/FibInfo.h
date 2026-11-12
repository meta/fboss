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

#include <optional>

#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/state/ForwardingInformationBaseContainer.h"
#include "fboss/agent/state/ForwardingInformationBaseMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/Thrifty.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

class FibInfo;

USE_THRIFT_COW(FibInfo);

RESOLVE_STRUCT_MEMBER(
    FibInfo,
    switch_state_tags::fibsMap,
    ForwardingInformationBaseMap);

RESOLVE_STRUCT_MEMBER(FibInfo, switch_state_tags::idToNextHop, IdToNextHopMap);

RESOLVE_STRUCT_MEMBER(
    FibInfo,
    switch_state_tags::idToNextHopIdSet,
    IdToNextHopIdSetMap);

RESOLVE_STRUCT_MEMBER(
    FibInfo,
    switch_state_tags::overrideIdToNextHopIdSet,
    IdToNextHopIdSetMap);

class FibInfo : public ThriftStructNode<FibInfo, state::FibInfoFields> {
 public:
  using Base = ThriftStructNode<FibInfo, state::FibInfoFields>;
  using Base::modify;

  FibInfo() = default;

  std::shared_ptr<ForwardingInformationBaseMap> getfibsMap() const {
    return safe_cref<switch_state_tags::fibsMap>();
  }

  FibInfo* modify(std::shared_ptr<SwitchState>* state);

  void updateFibContainer(
      const std::shared_ptr<ForwardingInformationBaseContainer>& fibContainer,
      std::shared_ptr<SwitchState>* state);

  void removeFibContainer(RouterID vrf, std::shared_ptr<SwitchState>* state);

  // Get FibContainer directly when VRF is specified
  std::shared_ptr<ForwardingInformationBaseContainer> getFibContainerIf(
      RouterID vrf) const {
    auto fibsMap = getfibsMap();
    if (!fibsMap) {
      return nullptr;
    }
    return fibsMap->getFibContainerIf(vrf);
  }

  // Get route count (v4, v6)
  std::pair<uint64_t, uint64_t> getRouteCount() const;

  void resetFibsMap(std::shared_ptr<ForwardingInformationBaseMap> fibsMap) {
    if (fibsMap) {
      ref<switch_state_tags::fibsMap>() = fibsMap;
    } else {
      ref<switch_state_tags::fibsMap>().reset();
    }
  }

  // IdToNextHop map accessors
  std::shared_ptr<IdToNextHopMap> getIdToNextHopMap() const {
    return safe_cref<switch_state_tags::idToNextHop>();
  }

  void setIdToNextHopMap(std::shared_ptr<IdToNextHopMap> idToNextHopMap) {
    ref<switch_state_tags::idToNextHop>() = idToNextHopMap;
  }

  // IdToNextHopIdSet map accessors
  std::shared_ptr<IdToNextHopIdSetMap> getIdToNextHopIdSetMap() const {
    return safe_cref<switch_state_tags::idToNextHopIdSet>();
  }

  void setIdToNextHopIdSetMap(
      std::shared_ptr<IdToNextHopIdSetMap> idToNextHopIdSetMap) {
    ref<switch_state_tags::idToNextHopIdSet>() = idToNextHopIdSetMap;
  }

  std::shared_ptr<IdToNextHopIdSetMap> getOverrideIdToNextHopIdSetMap() const {
    return safe_cref<switch_state_tags::overrideIdToNextHopIdSet>();
  }

  void setOverrideIdToNextHopIdSetMap(
      std::shared_ptr<IdToNextHopIdSetMap> overrideIdToNextHopIdSetMap) {
    ref<switch_state_tags::overrideIdToNextHopIdSet>() =
        overrideIdToNextHopIdSetMap;
  }

  // Resolve NextHopSetId to NextHops
  // Takes a NextHopSetId and returns the corresponding NextHops by:
  // 1. Looking up the set of NextHopIds from IdToNextHopIdSetMap
  // 2. For each NextHopId, looking up the NextHop from IdToNextHopMap
  std::vector<NextHop> resolveNextHopSetFromId(NextHopSetId id) const;

  // Named next-hop group accessors
  // Get the NextHopSetId for a named next-hop group
  std::optional<NextHopSetId> getNextHopSetIdIf(const std::string& name) const;
  NextHopSetId getNextHopSetId(const std::string& name) const;

  // Set the NextHopSetId for a named next-hop group
  void setNextHopSetIdForName(const std::string& name, NextHopSetId id);

  // Remove a named next-hop group
  void removeNextHopSetForName(const std::string& name);

  // Resolve a named next-hop group to NextHops
  std::vector<NextHop> resolveNextHopSetFromName(const std::string& name) const;

  // Collect refcounts of NextHopSetIds referenced by FIB routes.
  // Each route referencing a NextHopSetId increments its count.
  std::unordered_map<NextHopSetID, uint32_t>
  getNextHopSetIdRefCountsFromRoutes() const;

  // Collect refcounts of NextHopSetIds referenced by MySID entries.
  // Each MySID entry referencing a NextHopSetId increments its count
  // in the provided refCounts map.
  static void getNextHopSetIdRefCountsFromMySid(
      const std::shared_ptr<SwitchState>& state,
      std::unordered_map<NextHopSetID, uint32_t>& refCounts);

  // Get all name to NextHopSetId mappings
  std::map<std::string, NextHopSetId> getNameToNextHopSetId() const;

  // Replace the entire nameToNextHopSetId map
  void setNameToNextHopSetId(
      const std::map<std::string, NextHopSetId>& nameToSetId);

 private:
  // Inherit constructors required for clone()
  using Base::Base;
  friend class CloneAllocator;
};

} // namespace facebook::fboss
