// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/OverrideSetIdManager.h"

#include <algorithm>
#include <utility>

#include <glog/logging.h>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

OverrideSetIdManager::MembersToIdIter OverrideSetIdManager::getOrAllocate(
    const NextHopIDSet& members) {
  if (members.empty()) {
    throw FbossError("Cannot allocate an override SetID for an empty set");
  }
  auto it = membersToId_.find(members);
  if (it != membersToId_.end()) {
    return it;
  }

  NextHopSetID newID{nextAvailable_++};
  CHECK(isOverrideSetID(newID))
      << "Override NextHopSetID space exhausted, no wrap around: " << newID;
  auto [membersItr, membersInserted] = membersToId_.emplace(members, newID);
  CHECK(membersInserted);
  auto [idItr, idInserted] = idToMembers_.emplace(newID, members);
  CHECK(idInserted);
  return membersItr;
}

bool OverrideSetIdManager::deallocOverrideSetId(NextHopSetID id) {
  auto it = idToMembers_.find(id);
  if (it == idToMembers_.end()) {
    return false;
  }
  CHECK_EQ(membersToId_.erase(it->second), 1u);
  idToMembers_.erase(it);
  return true;
}

void OverrideSetIdManager::reconstructFromSwitchStateMap(
    const std::shared_ptr<MultiSwitchFibInfoMap>& fibsInfoMap) {
  membersToId_.clear();
  idToMembers_.clear();
  nextAvailable_ = kStart;
  if (!fibsInfoMap || fibsInfoMap->empty()) {
    return;
  }
  // Identical on every switch; the first copy suffices.
  auto overrideMap =
      fibsInfoMap->cbegin()->second->getOverrideIdToNextHopIdSetMap();
  if (!overrideMap) {
    return;
  }
  for (const auto& [id, memberNode] : std::as_const(*overrideMap)) {
    NextHopSetID setId{id};
    CHECK(isOverrideSetID(setId)) << "Not an override NextHopSetID: " << setId;
    NextHopIDSet members;
    for (const auto& elem : std::as_const(*memberNode)) {
      members.insert(NextHopID((*elem).toThrift()));
    }
    CHECK(!members.empty())
        << "Empty members for override NextHopSetID: " << setId;
    CHECK(membersToId_.emplace(members, setId).second)
        << "Members shared by two override NextHopSetIDs: " << setId;
    idToMembers_.emplace(setId, std::move(members));
    nextAvailable_ = std::max(nextAvailable_, id + 1);
  }
}

IdToNextHopIdSetMapThriftType OverrideSetIdManager::toThrift() const {
  IdToNextHopIdSetMapThriftType overrideMap;
  for (const auto& [id, members] : idToMembers_) {
    auto& memberIds = overrideMap[static_cast<NextHopSetId>(id)];
    for (auto member : members) {
      memberIds.insert(static_cast<NextHopId>(member));
    }
  }
  return overrideMap;
}

} // namespace facebook::fboss
