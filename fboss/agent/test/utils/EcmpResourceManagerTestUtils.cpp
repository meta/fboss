/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/utils/EcmpResourceManagerTestUtils.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/OverrideSetIdManager.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/RouteNextHop.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

namespace facebook::fboss {
namespace {
const std::shared_ptr<ForwardingInformationBaseV6> cfib(
    const std::shared_ptr<SwitchState>& newState) {
  return newState->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV6();
}

const std::shared_ptr<ForwardingInformationBaseV4> cfib4(
    const std::shared_ptr<SwitchState>& newState) {
  return newState->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV4();
}

bool hasBackupNextHop(const RouteNextHopSet& nhops) {
  return std::any_of(nhops.begin(), nhops.end(), [](const auto& nhop) {
    return nhop.role() == NextHopRole::BACKUP;
  });
}

// Also expects every switch to carry the same map.
IdToNextHopIdSetMapThriftType getOverrideIdMap(
    const std::shared_ptr<SwitchState>& state) {
  IdToNextHopIdSetMapThriftType overrideMap;
  const auto& fibsInfoMap = state->getFibsInfoMap();
  if (fibsInfoMap->empty()) {
    return overrideMap;
  }
  if (auto map =
          fibsInfoMap->cbegin()->second->getOverrideIdToNextHopIdSetMap()) {
    overrideMap = map->toThrift();
  }
  for (const auto& [matcher, fibInfo] : std::as_const(*fibsInfoMap)) {
    auto map = fibInfo->getOverrideIdToNextHopIdSetMap();
    EXPECT_EQ(
        map ? map->toThrift() : IdToNextHopIdSetMapThriftType{}, overrideMap)
        << "Override id map differs on switch: " << matcher;
  }
  return overrideMap;
}

RouteNextHopSet resolveMemberIds(
    const std::shared_ptr<SwitchState>& state,
    const std::set<NextHopId>& memberIds) {
  RouteNextHopSet nhops;
  auto idToNhop =
      state->getFibsInfoMap()->cbegin()->second->getIdToNextHopMap();
  EXPECT_NE(idToNhop, nullptr);
  if (!idToNhop) {
    return nhops;
  }
  for (auto memberId : memberIds) {
    auto nhop = idToNhop->getNextHopIf(memberId);
    EXPECT_NE(nhop, nullptr) << "dangling member NextHopID: " << memberId;
    if (nhop) {
      nhops.insert(
          util::fromThrift(nhop->toThrift(), true /* allowV6NonLinkLocal */));
    }
  }
  return nhops;
}

void assertRouteOverrideSetIds(
    const std::shared_ptr<SwitchState>& state,
    const IdToNextHopIdSetMapThriftType& overrideMap) {
  if (state->getFibsInfoMap()->empty()) {
    return;
  }
  auto checkFib = [&](const auto& fib) {
    for (const auto& [_, route] : std::as_const(*fib)) {
      if (!route->isResolved()) {
        continue;
      }
      const auto& fwd = route->getForwardInfo();
      auto id = fwd.getOverrideNextHopSetID();
      if (!fwd.hasOverrideNextHops()) {
        EXPECT_FALSE(id.has_value())
            << "override id without override nhops: " << route->str();
        continue;
      }
      ASSERT_TRUE(id.has_value())
          << "override nhops without id: " << route->str();
      EXPECT_TRUE(OverrideSetIdManager::isOverrideSetID(*id)) << route->str();
      auto it = overrideMap.find(static_cast<NextHopSetId>(*id));
      ASSERT_NE(it, overrideMap.end())
          << "override id missing from FibInfo: " << route->str();
      EXPECT_EQ(resolveMemberIds(state, it->second), *fwd.getOverrideNextHops())
          << route->str();
    }
  };
  checkFib(cfib(state));
  checkFib(cfib4(state));
}

void assertMergedGroupOverrideSetIds(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state,
    const IdToNextHopIdSetMapThriftType& overrideMap) {
  std::set<NextHopSetID> liveIds;
  for (const auto& mgroup : resourceMgr.getMergedGroups()) {
    auto info = resourceMgr.getMergeGroupConsolidationInfo(*mgroup.begin());
    ASSERT_TRUE(info.has_value() && info->overrideSetId.has_value())
        << "No override id for merged group: " << folly::join(", ", mgroup);
    auto id = *info->overrideSetId;
    EXPECT_TRUE(OverrideSetIdManager::isOverrideSetID(id)) << id;
    EXPECT_TRUE(liveIds.insert(id).second)
        << "Override id shared by merged groups: " << id;
    auto it = overrideMap.find(static_cast<NextHopSetId>(id));
    ASSERT_NE(it, overrideMap.end()) << "Unpublished override id: " << id;
    EXPECT_EQ(resolveMemberIds(state, it->second), info->mergedNhops)
        << "Override id: " << id;
  }
  // Nothing outlives its merged group.
  EXPECT_EQ(
      liveIds.size(),
      resourceMgr.getOverrideSetIdManager().getIdToMembers().size());
}

std::map<RouteNextHopSet, EcmpResourceManager::NextHopGroupId>
getNhops2IdSansMergedOnlyGroups(const EcmpResourceManager& resourceMgr) {
  const auto& nhops2Id = resourceMgr.getNhopsToId();
  auto nonMergedNhops2Id = nhops2Id;
  std::for_each(
      nhops2Id.begin(),
      nhops2Id.end(),
      [&resourceMgr, &nonMergedNhops2Id](const auto& nhopsAndId) {
        auto grpInfo = resourceMgr.getGroupInfo(nhopsAndId.second);
        ASSERT_NE(grpInfo, nullptr);
        if (grpInfo->getState() ==
            NextHopGroupInfo::NextHopGroupState::MERGED_NHOPS_ONLY) {
          nonMergedNhops2Id.erase(nhopsAndId.first);
        }
      });
  return nonMergedNhops2Id;
}

} // namespace

std::map<RouteNextHopSet, uint32_t> getEcmpGroups2RefCnt(
    const std::shared_ptr<SwitchState>& in,
    const std::function<bool(const RouteNextHopEntry&)>&& filter) {
  std::map<RouteNextHopSet, uint32_t> ecmpGroups2RefCnt;
  auto fill = [&ecmpGroups2RefCnt, &filter, &in](const auto& fibIn) {
    for (const auto& [_, route] : std::as_const(*fibIn)) {
      if (!route->isResolved()) {
        continue;
      }
      auto nhops = getNormalizedNextHops(in, route->getForwardInfo());
      if (nhops.size() <= 1) {
        continue;
      }
      if (hasBackupNextHop(nhops)) {
        continue;
      }
      if (!filter(route->getForwardInfo())) {
        auto pitr = ecmpGroups2RefCnt.find(nhops);
        if (pitr != ecmpGroups2RefCnt.end()) {
          ++pitr->second;
          XLOG(DBG4) << "Processed route: " << route->str()
                     << " primary ECMP groups count unchanged: "
                     << ecmpGroups2RefCnt.size();
        } else {
          ecmpGroups2RefCnt.insert({nhops, 1});
          XLOG(DBG4) << "Processed route: " << route->str()
                     << " primary ECMP groups count incremented: "
                     << ecmpGroups2RefCnt.size();
        }
      }
    }
  };
  fill(cfib(in));
  fill(cfib4(in));
  return ecmpGroups2RefCnt;
}
std::map<RouteNextHopSet, uint32_t> getPrimaryEcmpTypeGroups2RefCnt(
    const std::shared_ptr<SwitchState>& in) {
  return getEcmpGroups2RefCnt(in, [](const RouteNextHopEntry& info) {
    return info.hasOverrideSwitchingMode();
  });
}

std::map<RouteNextHopSet, uint32_t> getBackupEcmpTypeGroups2RefCnt(
    const std::shared_ptr<SwitchState>& in) {
  return getEcmpGroups2RefCnt(in, [](const RouteNextHopEntry& info) {
    return !info.hasOverrideSwitchingMode();
  });
}

void assertGroupsAreUnMerged(
    const EcmpResourceManager& resourceMgr,
    const EcmpResourceManager::NextHopGroupIds& unmergedGroups) {
  if (!resourceMgr.getEcmpCompressionThresholdPct()) {
    return;
  }
  auto allUnmergedGroups = resourceMgr.getUnMergedGids();
  auto allMergedGroups = resourceMgr.getMergedGroups();
  // Each unmerged group has a candidate merge group with every other unmerged
  // group and with every merged group.
  auto expectedCandidateMergeForEachUnmerged =
      (allUnmergedGroups.size() - 1) + allMergedGroups.size();

  XLOG(DBG2) << " Asserting for unmerged group: " << "["
             << folly::join(", ", unmergedGroups) << "]"
             << " Num existing merged groups: " << allMergedGroups.size()
             << " Existing unmerged groups: "
             << folly::join(", ", unmergedGroups)
             << " Expect : " << expectedCandidateMergeForEachUnmerged
             << " candidate merges";
  std::for_each(
      unmergedGroups.begin(),
      unmergedGroups.end(),
      [&resourceMgr, expectedCandidateMergeForEachUnmerged](auto gid) {
        XLOG(DBG2) << " Checking candidate merges for unmerged group: " << gid;
        auto candidateMergeToConsolidationInfo =
            resourceMgr.getCandidateMergeConsolidationInfo(gid);
        EXPECT_EQ(
            candidateMergeToConsolidationInfo.size(),
            expectedCandidateMergeForEachUnmerged);
        for (const auto& [candidateMerge, consInfo] :
             candidateMergeToConsolidationInfo) {
          EXPECT_EQ(
              consInfo, resourceMgr.computeConsolidationInfo(candidateMerge));
        }
        // Groups from  unmerge set should no longer
        // be in merge sets.
        EXPECT_FALSE(
            resourceMgr.getMergeGroupConsolidationInfo(gid).has_value());
      });
}

void assertMergedGroup(
    const EcmpResourceManager& resourceMgr,
    const EcmpResourceManager::NextHopGroupIds& mergedGroup) {
  if (!resourceMgr.getEcmpCompressionThresholdPct()) {
    return;
  }
  auto allUnmergedGroups = resourceMgr.getUnMergedGids();
  // Each merged group has a candidate merge group with every unmerged
  // group
  auto expectedCandidateMergeForEachMerged = allUnmergedGroups.size();
  XLOG(DBG2) << " Asserting for merged group: " << "["
             << folly::join(", ", mergedGroup) << "]"
             << " Have unmerged groups: "
             << folly::join(", ", allUnmergedGroups)
             << " Expect : " << expectedCandidateMergeForEachMerged
             << " candidate merges";
  auto expectedMergeGroupConsInfo =
      resourceMgr.computeConsolidationInfo(mergedGroup);
  std::for_each(
      mergedGroup.begin(),
      mergedGroup.end(),
      [&resourceMgr,
       expectedCandidateMergeForEachMerged,
       &expectedMergeGroupConsInfo](auto gid) {
        auto candidateMergeToConsolidationInfo =
            resourceMgr.getCandidateMergeConsolidationInfo(gid);
        EXPECT_EQ(
            candidateMergeToConsolidationInfo.size(),
            expectedCandidateMergeForEachMerged);
        for (const auto& [candidateMerge, consInfo] :
             candidateMergeToConsolidationInfo) {
          EXPECT_EQ(
              consInfo, resourceMgr.computeConsolidationInfo(candidateMerge));
        }
        auto mergedGroupConsInfo =
            resourceMgr.getMergeGroupConsolidationInfo(gid);
        ASSERT_TRUE(mergedGroupConsInfo.has_value());
        EXPECT_EQ(*mergedGroupConsInfo, expectedMergeGroupConsInfo);
      });
  bool found{false};
  for (auto mgroup : resourceMgr.getMergedGroups()) {
    if (mgroup == mergedGroup) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Merged group : " << folly::join(", ", mergedGroup)
                     << " not found";
}

std::map<RouteNextHopSet, EcmpResourceManager::NextHopGroupIds>
getNhopsToMergedGroups(const EcmpResourceManager& resourceMgr) {
  std::map<RouteNextHopSet, EcmpResourceManager::NextHopGroupIds>
      nhopsToMergedGroups;
  auto mergedGroups = resourceMgr.getMergedGroups();
  for (const auto& mgroup : resourceMgr.getMergedGroups()) {
    for (auto mGid : mgroup) {
      auto consInfo = resourceMgr.getMergeGroupConsolidationInfo(mGid);
      nhopsToMergedGroups.insert({consInfo->mergedNhops, mgroup});
    }
  }
  return nhopsToMergedGroups;
}

void assertAllGidsClaimed(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state) {
  // Assert that union of all merged and unmerged GIDs == all gids
  auto allUnmergedGids = resourceMgr.getUnMergedGids();
  auto allMergedAndUnmergedGids = resourceMgr.getMergedGids();
  allMergedAndUnmergedGids.insert(
      allUnmergedGids.begin(), allUnmergedGids.end());
  auto nhops2Id = getNhops2IdSansMergedOnlyGroups(resourceMgr);
  EcmpResourceManager::NextHopGroupIds allGids;
  std::for_each(
      nhops2Id.begin(), nhops2Id.end(), [&allGids](const auto& nhopsAndId) {
        allGids.insert(nhopsAndId.second);
      });
  EXPECT_EQ(allMergedAndUnmergedGids, allGids);
  auto nhopsToMergedGroups = getNhopsToMergedGroups(resourceMgr);
  // Assert that all distinct merged nhops we see map to mergedGroups
  EXPECT_EQ(nhopsToMergedGroups.size(), resourceMgr.getMergedGroups().size());
}

void assertFibAndGroupsMatch(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state) {
  auto nhops2Id = getNhops2IdSansMergedOnlyGroups(resourceMgr);
  auto nhopsToMergedGroups = getNhopsToMergedGroups(resourceMgr);
  std::map<EcmpResourceManager::NextHopGroupId, int> unmergedGroupToRouteRef;
  std::map<EcmpResourceManager::NextHopGroupIds, int> mergedGroupToRouteRef;
  auto getRouteRef = [&](auto inFib) {
    for (auto [_, route] : std::as_const(*inFib)) {
      if (!route->isResolved()) {
        // Some tests deliberately add unresolved routes to
        // FIB and run them through local consolidator_ state
        continue;
      }
      ASSERT_NE(route, nullptr);
      bool isEcmpRoute = route->isResolved() &&
          getNextHops(state, route->getForwardInfo()).size() > 1;
      if (!isEcmpRoute) {
        continue;
      }
      const auto& fwdInfo = route->getForwardInfo();
      auto nonOverrideNormalizedHops =
          getNonOverrideNormalizedNextHops(state, fwdInfo);
      if (hasBackupNextHop(nonOverrideNormalizedHops)) {
        EXPECT_EQ(
            resourceMgr.getGroupInfo(
                RouterID(0), route->prefix().toCidrNetwork()),
            nullptr);
        continue;
      }
      auto pfxGrpInfo = resourceMgr.getGroupInfo(
          RouterID(0), route->prefix().toCidrNetwork());
      if (fwdInfo.hasOverrideNextHops()) {
        auto mergeInfoItr = pfxGrpInfo->getMergedGroupInfoItr();
        EXPECT_TRUE(mergeInfoItr.has_value());
        auto grpOverrideNhops = pfxGrpInfo->getOverrideNextHops();
        ASSERT_TRUE(grpOverrideNhops.has_value());
        EXPECT_EQ(fwdInfo.getOverrideNextHops(), grpOverrideNhops);
        // Assert that override nhops map to existing merged group
        auto nmitr =
            nhopsToMergedGroups.find(getNormalizedNextHops(state, fwdInfo));
        ASSERT_NE(nmitr, nhopsToMergedGroups.end());
        // Bump up route ref to merged groups
        auto mGroupRefItr =
            mergedGroupToRouteRef.insert({(*mergeInfoItr)->first, 0}).first;
        ++mGroupRefItr->second;
      }
      EXPECT_EQ(
          fwdInfo.hasOverrideSwitchingMode(),
          pfxGrpInfo->isBackupEcmpGroupType());
      // Non override nhops must map to a entry in nhops2Id. Confirming
      // that nhops map to a existing group in resourceMgr
      auto nitr = nhops2Id.find(nonOverrideNormalizedHops);
      ASSERT_NE(nitr, nhops2Id.end());
      auto umGroupRefItr =
          unmergedGroupToRouteRef.insert({nitr->second, 0}).first;
      ++umGroupRefItr->second;
    }
  };
  getRouteRef(cfib(state));
  getRouteRef(cfib4(state));
  EXPECT_EQ(mergedGroupToRouteRef.size(), resourceMgr.getMergedGroups().size());
  EXPECT_EQ(unmergedGroupToRouteRef.size(), nhops2Id.size());
}

void assertResourceMgrCorrectness(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state) {
  assertAllGidsClaimed(resourceMgr, state);
  assertFibAndGroupsMatch(resourceMgr, state);
  // All unmerged groups should have candidate merge sets with other unmerged
  // groups
  assertGroupsAreUnMerged(resourceMgr, resourceMgr.getUnMergedGids());
  auto allMergedGroups = resourceMgr.getMergedGroups();
  std::for_each(
      allMergedGroups.begin(),
      allMergedGroups.end(),
      [&resourceMgr](const auto& mgroup) {
        assertMergedGroup(resourceMgr, mgroup);
      });
  auto primaryEcmpTypeGroups2RefCnt = getPrimaryEcmpTypeGroups2RefCnt(state);
  auto backupEcmpTypeGroups2RefCnt = getBackupEcmpTypeGroups2RefCnt(state);
  auto countEcmpMembers = [](const auto& nhops2RefCnt) {
    auto cnt{0};
    for (const auto& [nhops, _] : nhops2RefCnt) {
      cnt += nhops.size();
    }
    return cnt;
  };
  auto ecmpMembers = countEcmpMembers(primaryEcmpTypeGroups2RefCnt) +
      countEcmpMembers(backupEcmpTypeGroups2RefCnt);
  auto [mgrPrimaryEcmpGroups, mgrVirtualEcmpGroups, mgrEcmpMembers] =
      resourceMgr.getPrimaryEcmpAndMemberCounts();
  EXPECT_EQ(mgrPrimaryEcmpGroups, primaryEcmpTypeGroups2RefCnt.size());
  EXPECT_EQ(mgrEcmpMembers, ecmpMembers);
}

void assertNumRoutesWithNhopOverrides(
    const std::shared_ptr<SwitchState>& state,
    int expectedNumOverrides) {
  auto getRouteOverrideCount = [&](auto inFib) {
    int count{0};
    for (auto [_, route] : std::as_const(*inFib)) {
      if (!route->isResolved()) {
        // Some tests deliberately add unresolved routes to
        // FIB and run them through local consolidator_ state
        continue;
      }
      CHECK(route);
      bool isEcmpRoute = route->isResolved() &&
          getNormalizedNextHops(state, route->getForwardInfo()).size() > 1;
      if (!isEcmpRoute) {
        continue;
      }
      count += route->getForwardInfo().hasOverrideNextHops() ? 1 : 0;
    }
    return count;
  };
  auto overrideCount =
      getRouteOverrideCount(cfib(state)) + getRouteOverrideCount(cfib4(state));
  EXPECT_EQ(overrideCount, expectedNumOverrides);
}

std::set<RouteV6::Prefix> getPrefixesForGroups(
    const EcmpResourceManager& resourceMgr,
    const EcmpResourceManager::NextHopGroupIds& grpIds) {
  auto grpId2Prefixes = resourceMgr.getGidToPrefixes();
  std::set<RouteV6::Prefix> prefixes;
  for (auto grpId : grpIds) {
    for (const auto& [_, pfx] : grpId2Prefixes.at(grpId)) {
      prefixes.insert(RouteV6::Prefix(pfx.first.asV6(), pfx.second));
    }
  }
  return prefixes;
}

void assertDeltasForOverflow(
    const EcmpResourceManager& resourceManager,
    const std::vector<StateDelta>& deltas) {
  auto primaryEcmpTypeGroups2RefCnt =
      getPrimaryEcmpTypeGroups2RefCnt(deltas.begin()->oldState());
  XLOG(DBG2) << " Primary ECMP groups : "
             << primaryEcmpTypeGroups2RefCnt.size();
  // ECMP groups should not exceed the limit.
  EXPECT_LE(
      primaryEcmpTypeGroups2RefCnt.size(),
      resourceManager.getMaxPrimaryEcmpGroups());
  auto routeDeleted = [&primaryEcmpTypeGroups2RefCnt](
                          const auto& oldRoute,
                          const std::shared_ptr<SwitchState>& state) {
    XLOG(DBG2) << " Route deleted: " << oldRoute->str();
    if (!oldRoute->isResolved()) {
      return;
    }
    auto nhops = getNormalizedNextHops(state, oldRoute->getForwardInfo());
    if (nhops.size() <= 1) {
      return;
    }
    if (hasBackupNextHop(nhops)) {
      return;
    }
    if (oldRoute->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
      return;
    }
    auto pitr = primaryEcmpTypeGroups2RefCnt.find(nhops);
    ASSERT_NE(pitr, primaryEcmpTypeGroups2RefCnt.end());
    EXPECT_GE(pitr->second, 1);
    --pitr->second;
    if (pitr->second == 0) {
      primaryEcmpTypeGroups2RefCnt.erase(pitr);
      XLOG(DBG2) << " Primary ECMP group count decremented to: "
                 << primaryEcmpTypeGroups2RefCnt.size()
                 << " on pfx: " << oldRoute->str();
    }
  };
  auto routeAdded = [&primaryEcmpTypeGroups2RefCnt, &resourceManager](
                        const auto& newRoute,
                        const std::shared_ptr<SwitchState>& state) {
    XLOG(DBG2) << " Route added: " << newRoute->str();
    if (!newRoute->isResolved()) {
      return;
    }
    auto nhops = getNormalizedNextHops(state, newRoute->getForwardInfo());
    if (nhops.size() <= 1) {
      return;
    }
    if (hasBackupNextHop(nhops)) {
      return;
    }
    if (newRoute->getForwardInfo().getOverrideEcmpSwitchingMode().has_value()) {
      return;
    }
    auto pitr = primaryEcmpTypeGroups2RefCnt.find(nhops);
    if (pitr != primaryEcmpTypeGroups2RefCnt.end()) {
      ++pitr->second;
    } else {
      bool inserted{false};
      std::tie(pitr, inserted) =
          primaryEcmpTypeGroups2RefCnt.insert({nhops, 1});
      EXPECT_TRUE(inserted);
      XLOG(DBG2) << " Primary ECMP group count incremented to: "
                 << primaryEcmpTypeGroups2RefCnt.size()
                 << " on pfx: " << newRoute->str();
    }
    // Transiently we can exceed consolidator maxPrimaryEcmpGroups,
    // but we should never exceed
    // maxPrimaryEcmpGroups +
    // FLAGS_ecmp_resource_manager_make_before_break_buffer We deliberately set
    // consolidator's maxPrimaryEcmpGroups to be Actual limit -
    // FLAGS_ecmp_resource_manager_make_before_break_buffer
    EXPECT_LE(
        primaryEcmpTypeGroups2RefCnt.size(),
        resourceManager.getMaxPrimaryEcmpGroups() +
            FLAGS_ecmp_resource_manager_make_before_break_buffer);
  };

  auto idx = 1;
  for (const auto& delta : deltas) {
    XLOG(DBG2) << " Processing delta #" << idx++;
    auto oldState = delta.oldState();
    auto newState = delta.newState();
    processFibsDeltaInHwSwitchOrder(
        delta,
        [=](RouterID /*rid*/, const auto& oldRoute, const auto& newRoute) {
          if (!oldRoute->isResolved() && !newRoute->isResolved()) {
            return;
          }
          if (oldRoute->isResolved() && !newRoute->isResolved()) {
            routeDeleted(oldRoute, oldState);
            return;
          }
          if (!oldRoute->isResolved() && newRoute->isResolved()) {
            routeAdded(newRoute, newState);
            return;
          }
          // Both old and new are resolved
          CHECK(oldRoute->isResolved() && newRoute->isResolved());
          if (oldRoute->getForwardInfo() != newRoute->getForwardInfo()) {
            // First process as a add route, since ECMP is make before break
            routeAdded(newRoute, newState);
            routeDeleted(oldRoute, oldState);
          }
        },
        [=](RouterID /*rid*/, const auto& newRoute) {
          if (newRoute->isResolved()) {
            routeAdded(newRoute, newState);
          }
        },
        [=](RouterID /*rid*/, const auto& oldRoute) {
          if (oldRoute->isResolved()) {
            routeDeleted(oldRoute, oldState);
          }
        });
    EXPECT_LE(
        primaryEcmpTypeGroups2RefCnt.size(),
        resourceManager.getMaxPrimaryEcmpGroups() +
            FLAGS_ecmp_resource_manager_make_before_break_buffer);
  }
  EXPECT_LE(
      primaryEcmpTypeGroups2RefCnt.size(),
      resourceManager.getMaxPrimaryEcmpGroups());
}

void assertAllRouteIdsResolvable(
    const std::shared_ptr<SwitchState>& state,
    const std::string& context) {
  if (!FLAGS_resolve_nexthops_from_id) {
    return;
  }
  auto fibsInfoMap = state->getFibsInfoMap();
  ASSERT_NE(fibsInfoMap, nullptr) << context;
  for (const auto& [matcherStr, fibInfo] : std::as_const(*fibsInfoMap)) {
    auto fibsMap = fibInfo->getfibsMap();
    if (!fibsMap) {
      continue;
    }
    for (const auto& [rid, fibContainer] : std::as_const(*fibsMap)) {
      auto checkFib = [&](const auto& routeFib) {
        if (!routeFib) {
          return;
        }
        for (const auto& [_, route] : std::as_const(*routeFib)) {
          if (!route->isResolved()) {
            continue;
          }
          const auto& fwdInfo = route->getForwardInfo();
          if (fwdInfo.getAction() != RouteNextHopEntry::Action::NEXTHOPS) {
            continue;
          }
          auto resolvedSetId = fwdInfo.getResolvedNextHopSetID();
          auto normalizedSetId = fwdInfo.getNormalizedResolvedNextHopSetID();
          if (!resolvedSetId.has_value() && !normalizedSetId.has_value()) {
            continue;
          }
          if (resolvedSetId.has_value()) {
            EXPECT_NO_THROW(
                getNextHops(state, static_cast<NextHopSetId>(*resolvedSetId)))
                << context << " route=" << route->str()
                << " resolvedSetId=" << *resolvedSetId;
          }
          if (normalizedSetId.has_value()) {
            EXPECT_NO_THROW(
                getNextHops(state, static_cast<NextHopSetId>(*normalizedSetId)))
                << context << " route=" << route->str()
                << " normalizedSetId=" << *normalizedSetId;
          }
        }
      };
      checkFib(fibContainer->getFibV6());
      checkFib(fibContainer->getFibV4());
    }
  }
}

void assertAllDeltaIdsResolvable(const std::vector<StateDelta>& deltas) {
  for (size_t i = 0; i < deltas.size(); ++i) {
    auto context = "delta[" + std::to_string(i) + "].newState()";
    assertAllRouteIdsResolvable(deltas[i].newState(), context);
  }
}

void assertRibFibEquivalence(
    const std::shared_ptr<SwitchState>& state,
    const RoutingInformationBase* rib) {
  auto validateFib = [rib](const auto fib) {
    for (const auto& [_, route] : std::as_const(*fib)) {
      auto ribRoute = rib->longestMatch(route->prefix().network(), RouterID(0));
      ASSERT_NE(ribRoute, nullptr);
      // TODO - check why are the pointers different even though the
      // forwarding info matches. This is true with or w/o consolidator
      EXPECT_EQ(ribRoute->getForwardInfo(), route->getForwardInfo());
    }
  };

  validateFib(
      state->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV6());
  validateFib(
      state->getFibsInfoMap()->getFibContainer(RouterID(0))->getFibV4());
}
void assertOverrideSetIdsConsistency(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state) {
  if (!FLAGS_enable_nexthop_id_manager) {
    return;
  }
  auto overrideMap = getOverrideIdMap(state);
  EXPECT_EQ(overrideMap, resourceMgr.getOverrideSetIdManager().toThrift());
  assertMergedGroupOverrideSetIds(resourceMgr, state, overrideMap);
  assertRouteOverrideSetIds(state, overrideMap);
}

void assertOverrideSetIdsInDeltas(
    const EcmpResourceManager& resourceMgr,
    const std::vector<StateDelta>& deltas) {
  if (!FLAGS_enable_nexthop_id_manager || deltas.empty()) {
    return;
  }
  assertRouteOverrideSetIds(
      deltas[0].oldState(), getOverrideIdMap(deltas[0].oldState()));
  for (size_t i = 0; i < deltas.size(); ++i) {
    SCOPED_TRACE("delta[" + std::to_string(i) + "]");
    assertRouteOverrideSetIds(
        deltas[i].newState(), getOverrideIdMap(deltas[i].newState()));
  }
  EXPECT_EQ(
      getOverrideIdMap(deltas.back().newState()),
      resourceMgr.getOverrideSetIdManager().toThrift());
}
} // namespace facebook::fboss
