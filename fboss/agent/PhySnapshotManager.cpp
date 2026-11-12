// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "fboss/agent/PhySnapshotManager.h"

namespace facebook::fboss {

void PhySnapshotManager::updatePhyInfoLocked(
    const SnapshotMapWLockedPtr& lockedSnapshotMap,
    PortID portID,
    const phy::PhyInfo& phyInfo) {
  phy::LinkSnapshot snapshot;
  snapshot.phyInfo() = phyInfo;
  CHECK(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          phyInfo.state()));

  CHECK(!phyInfo.state()->name().value().empty());
  auto result = lockedSnapshotMap->try_emplace(
      portID,
      std::set<std::string>({phyInfo.state()->name().value()}),
      snapshotLogSource_,
      intervalSeconds_);
  auto iter = result.first;
  auto& value = iter->second;
  value.addSnapshot(snapshot);
}

void PhySnapshotManager::updatePhyInfo(
    PortID portID,
    const phy::PhyInfo& phyInfo) {
  auto lockedSnapshotMap = snapshots_.wlock();
  updatePhyInfoLocked(lockedSnapshotMap, portID, phyInfo);
}

void PhySnapshotManager::updatePhyInfos(
    const std::map<PortID, phy::PhyInfo>& phyInfos) {
  auto lockedSnapshotMap = snapshots_.wlock();
  for (const auto& [portID, phyInfo] : phyInfos) {
    updatePhyInfoLocked(lockedSnapshotMap, portID, phyInfo);
  }
}

std::optional<phy::PhyInfo> PhySnapshotManager::getPhyInfoLocked(
    const SnapshotMapRLockedPtr& lockedSnapshotMap,
    PortID portID) const {
  std::optional<phy::PhyInfo> phyInfo;

  if (auto it = lockedSnapshotMap->find(portID);
      it != lockedSnapshotMap->end()) {
    auto& snapshots = it->second.getSnapshots();
    if (!snapshots.empty()) {
      phyInfo = snapshots.last().snapshot_.phyInfo().value();
    }
  }

  return phyInfo;
}

std::optional<phy::PhyInfo> PhySnapshotManager::getPhyInfo(
    PortID portID) const {
  const auto& lockedSnapshotMap = snapshots_.rlock();
  return getPhyInfoLocked(lockedSnapshotMap, portID);
}

std::map<PortID, const phy::PhyInfo> PhySnapshotManager::getPhyInfos(
    const std::vector<PortID>& portIDs) const {
  std::map<PortID, const phy::PhyInfo> infoMap;
  auto lockedSnapshotMap = snapshots_.rlock();
  for (auto portID : portIDs) {
    auto snapshot = getPhyInfoLocked(lockedSnapshotMap, portID);
    if (snapshot) {
      infoMap.emplace(portID, *snapshot);
    }
  }
  return infoMap;
}

std::map<PortID, const phy::PhyInfo> PhySnapshotManager::getAllPhyInfos()
    const {
  std::map<PortID, const phy::PhyInfo> infoMap;
  auto lockedSnapshotMap = snapshots_.rlock();
  for (auto it = lockedSnapshotMap->begin(); it != lockedSnapshotMap->end();
       ++it) {
    auto snapshot = getPhyInfoLocked(lockedSnapshotMap, it->first);
    if (snapshot) {
      infoMap.emplace(it->first, *snapshot);
    }
  }
  return infoMap;
}

void PhySnapshotManager::publishSnapshots(PortID port) {
  auto lockedSnapshotMap = snapshots_.wlock();
  if (auto it = lockedSnapshotMap->find(port); it != lockedSnapshotMap->end()) {
    it->second.publishAllSnapshots();
    it->second.publishFutureSnapshots();
  }
}

} // namespace facebook::fboss
