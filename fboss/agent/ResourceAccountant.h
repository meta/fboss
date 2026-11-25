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

#include "fboss/agent/AclResourceAccountant.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/L2ResourceAccountant.h"
#include "fboss/agent/MySidResourceAccountant.h"
#include "fboss/agent/NeighborResourceAccountant.h"
#include "fboss/agent/RouteCounterResourceAccountant.h"
#include "fboss/agent/RouteResourceAccountant.h"
#include "fboss/agent/Srv6NextHopResourceAccountant.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/VrfResourceAccountant.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

#include <gtest/gtest.h>

namespace facebook::fboss {

class ResourceAccountant {
 public:
  ResourceAccountant(
      const HwAsicTable* asicTable,
      const SwitchIdScopeResolver* scopeResolver);

  bool isValidUpdate(const StateDelta& delta);
  void stateChanged(const StateDelta& delta);
  void setMinWidthForArsVirtualGroup(
      std::optional<int32_t> minWidthForArsVirtualGroup);
  void setMaxArsVirtualGroups(std::optional<int32_t> maxArsVirtualGroups);
  void setMaxArsVirtualGroupWidth(
      std::optional<int32_t> maxArsVirtualGroupWidth);

 private:
  size_t getMemberCountForEcmpGroup(
      const RouteNextHopEntry& fwd,
      const std::shared_ptr<SwitchState>& state) const;
  bool checkEcmpResource(bool intermediateState) const;
  size_t getEcmpGroupUsage() const;
  bool checkArsResource(bool intermediateState) const;
  bool wouldExceedSuperGroupLimit(
      const RouteNextHopEntry::NextHopSet& nhSet) const;
  bool isVirtualArsGroup(
      const RouteNextHopEntry& fwd,
      const std::shared_ptr<SwitchState>& state) const;
  bool isVirtualArsGroup(
      const RouteNextHopEntry& fwd,
      const RouteNextHopEntry::NextHopSet& nhSet) const;
  void updateArsVirtualGroupConfig(const StateDelta& delta);
  bool routeAndEcmpStateChangedImpl(const StateDelta& delta);
  bool isValidRouteUpdate(const StateDelta& delta);
  bool shouldCheckRouteUpdate() const;
  bool isEcmp(
      const RouteNextHopEntry& fwd,
      const std::shared_ptr<SwitchState>& state) const;
  size_t computeWeightedEcmpMemberCount(
      const RouteNextHopEntry& fwd,
      const cfg::AsicType& asicType,
      const std::shared_ptr<SwitchState>& state) const;

  template <typename AddrT>
  bool checkAndUpdateGenericEcmpResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  template <typename AddrT>
  bool checkAndUpdateArsEcmpResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  template <typename AddrT>
  bool checkAndUpdateEcmpResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  // Per-nhSet ECMP refmap entry
  struct EcmpGroupRefEntry {
    uint32_t refCountVirtual{0};
    uint32_t refCountNonVirtual{0};
    uint32_t memberContribution{0};
  };
  std::map<RouteNextHopEntry::NextHopSet, EcmpGroupRefEntry> ecmpGroupRefMap_;
  std::map<RouteNextHopEntry::NextHopSet, uint32_t> arsEcmpGroupRefMap_;
  // Tracks refcount of each unique nexthop contributed to the virtual ARS
  // supergroup across all active virtual nhSets. Map size = unique member
  // count.
  std::map<RouteNextHopEntry::NextHopSet::value_type, uint32_t>
      virtualArsSuperGroupMemberRefMap_;

  const HwAsicTable* asicTable_;
  AclResourceAccountant aclResourceAccountant_;
  VrfResourceAccountant vrfResourceAccountant_;
  MySidResourceAccountant mySidResourceAccountant_;
  L2ResourceAccountant l2ResourceAccountant_;
  NeighborResourceAccountant neighborResourceAccountant_;
  RouteCounterResourceAccountant routeCounterResourceAccountant_;
  RouteResourceAccountant routeResourceAccountant_;
  Srv6NextHopResourceAccountant srv6NextHopResourceAccountant_;

  bool nativeWeightedEcmp_{true};
  // Yuba (G200) and G202x program protection backup next hops as ordinary
  // group members. Other asics keep them in hw reserved space, so they are not
  // charged as members.
  bool countBackupNextHopMembers_{false};
  bool checkRouteUpdate_;
  uint32_t ecmpMemberUsage_{0};
  // Split horizon gives every dynamic ARS group a secondary plain ECMP group.
  // It shares the next hop set of its DLB group, so it cannot be a separate
  // entry in ecmpGroupRefMap_ and is derived from arsEcmpGroupRefMap_ instead.
  bool arsSplitHorizon_{false};
  uint32_t virtualArsGroupCount_{0};
  std::optional<int32_t> minWidthForArsVirtualGroup_;
  std::optional<int32_t> maxArsVirtualGroups_;
  std::optional<int32_t> maxArsVirtualGroupWidth_;

  FRIEND_TEST(ResourceAccountantTest, getMemberCountForEcmpGroup);
  FRIEND_TEST(ResourceAccountantTest, checkArsResource);
  FRIEND_TEST(ResourceAccountantTest, checkEcmpResource);
  FRIEND_TEST(ResourceAccountantTest, checkAndUpdateEcmpResource);
  FRIEND_TEST(ResourceAccountantTest, checkEcmpResourceForUcmpWeights);
  FRIEND_TEST(ResourceAccountantTest, checkAndUpdateGenericEcmpResource);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceAccounting);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceSharing);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceEdgeCases);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceLimits);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceLifecycle);
  FRIEND_TEST(ResourceAccountantTest, protectionEcmpResourceArsExcluded);
  FRIEND_TEST(
      ResourceAccountantTest,
      checkAndUpdateGenericEcmpResourceForUcmpWeights);
  FRIEND_TEST(ResourceAccountantTest, checkAndUpdateArsEcmpResource);
  FRIEND_TEST(ResourceAccountantTest, arsEcmpResourceSourcePortPrune);
  FRIEND_TEST(ResourceAccountantTest, virtualArsGroups);
  FRIEND_TEST(ResourceAccountantTest, virtualArsSuperGroupMemberLimit);
  FRIEND_TEST(ResourceAccountantTest, virtualArsGroupOverrideExcluded);
  FRIEND_TEST(ResourceAccountantTest, virtualArsGroupOverrideFlipLifecycle);
  FRIEND_TEST(
      ResourceAccountantTest,
      virtualArsGroupOverrideDropPromotesClassification);
  FRIEND_TEST(ResourceAccountantTest, computeWeightedEcmpMemberCount);
  FRIEND_TEST(ResourceAccountantTest, routeWithAdjustedWeightZero);
  FRIEND_TEST(ResourceAccountantTest, resolvedAndUnresolvedRoutes);
  FRIEND_TEST(MacTableManagerTest, MacLearnedBulkCb);
  FRIEND_TEST(StaticMacConfigTest, ApplyConfigAccountsStaticMacsOnce);
  FRIEND_TEST(StaticMacConfigTest, IsValidStateUpdateDryRun);
};
} // namespace facebook::fboss
