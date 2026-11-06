// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/NextHopIdMaps.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

// Mints NextHopSetIDs for EcmpResourceManager's merged (override) groups.
class OverrideSetIdManager {
 public:
  static bool isOverrideSetID(NextHopSetID id) {
    return nextHopIdType(static_cast<int64_t>(id)) ==
        NextHopIdType::OVERRIDE_SET;
  }

  using MembersToIdIter =
      std::unordered_map<NextHopIDSet, NextHopSetID>::const_iterator;

  // Returns the entry for `members`, minting an id on first sight.
  MembersToIdIter getOrAllocate(const NextHopIDSet& members);

  // Frees `id`. Returns false if it was not allocated.
  bool deallocOverrideSetId(NextHopSetID id);

  // Rebuilds both maps from FibInfo's override id map, e.g. on warm boot.
  void reconstructFromSwitchStateMap(
      const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap);

  // Inverse of reconstructFromSwitchStateMap, for FibInfo's override id map.
  IdToNextHopIdSetMapThriftType toThrift() const;

  const std::unordered_map<NextHopIDSet, NextHopSetID>& getMembersToId() const {
    return membersToId_;
  }

  const std::unordered_map<NextHopSetID, NextHopIDSet>& getIdToMembers() const {
    return idToMembers_;
  }

 private:
  static constexpr int64_t kStart =
      nextHopIdTypeStart(NextHopIdType::OVERRIDE_SET);

  std::unordered_map<NextHopIDSet, NextHopSetID> membersToId_;
  std::unordered_map<NextHopSetID, NextHopIDSet> idToMembers_;
  int64_t nextAvailable_{kStart};
};

} // namespace facebook::fboss
