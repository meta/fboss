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
#include "fboss/agent/EcmpResourceManager.h"
#include "fboss/agent/state/StateDelta.h"

namespace facebook::fboss {
class SwitchState;
class RoutingInformationBase;

std::map<RouteNextHopSet, EcmpResourceManager::NextHopGroupIds>
getNhopsToMergedGroups(const EcmpResourceManager& resourceMgr);

std::map<RouteNextHopSet, uint32_t> getPrimaryEcmpTypeGroups2RefCnt(
    const std::shared_ptr<SwitchState>& in);

std::map<RouteNextHopSet, uint32_t> getBackupEcmpTypeGroups2RefCnt(
    const std::shared_ptr<SwitchState>& in);

void assertGroupsAreUnMerged(
    const EcmpResourceManager& resourceMgr,
    const EcmpResourceManager::NextHopGroupIds& unmergedGroups);

void assertMergedGroup(
    const EcmpResourceManager& resourceMgr,
    const EcmpResourceManager::NextHopGroupIds& mergedGroup);

void assertAllGidsClaimed(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state);

void assertFibAndGroupsMatch(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state);

void assertResourceMgrCorrectness(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state);

void assertNumRoutesWithNhopOverrides(
    const std::shared_ptr<SwitchState>& state,
    int numOverrides);

std::set<RouteV6::Prefix> getPrefixesForGroups(
    const EcmpResourceManager& resourceManager,
    const EcmpResourceManager::NextHopGroupIds& grpIds);

void assertDeltasForOverflow(
    const EcmpResourceManager& resourceManager,
    const std::vector<StateDelta>& deltas);

void assertAllRouteIdsResolvable(
    const std::shared_ptr<SwitchState>& state,
    const std::string& context);

void assertAllDeltaIdsResolvable(const std::vector<StateDelta>& deltas);

// Checks FibInfo's override id map and every route's override id against
// resourceMgr; no-op with the NextHop id manager off.
void assertOverrideSetIdsConsistency(
    const EcmpResourceManager& resourceMgr,
    const std::shared_ptr<SwitchState>& state);

// Checks every delta's routes against that delta's override id map.
void assertOverrideSetIdsInDeltas(
    const EcmpResourceManager& resourceMgr,
    const std::vector<StateDelta>& deltas);

void assertRibFibEquivalence(
    const std::shared_ptr<SwitchState>& state,
    const RoutingInformationBase* rib);
} // namespace facebook::fboss
