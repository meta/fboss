// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"

#include <gtest/gtest_prod.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>

namespace facebook::fboss {

class HwAsicTable;
class StateDelta;
class SwitchState;

class EcmpResourceAccountant {
 public:
  explicit EcmpResourceAccountant(const HwAsicTable* asicTable);

  template <typename AddrT>
  bool checkAndUpdateEcmpResource(
      const std::shared_ptr<Route<AddrT>>& route,
      bool add,
      const std::shared_ptr<SwitchState>& state);

  bool checkEcmpResource(bool intermediateState) const;
  bool checkArsResource(bool intermediateState) const;
  void updateArsVirtualGroupConfig(const StateDelta& delta);
  size_t getEcmpGroupUsage() const;
  uint32_t getEcmpMemberUsage() const;
  size_t getArsEcmpGroupUsage() const;

 private:
  size_t getMemberCountForEcmpGroup(
      const RouteNextHopEntry& fwd,
      const std::shared_ptr<SwitchState>& state) const;
  bool wouldExceedSuperGroupLimit(
      const RouteNextHopEntry::NextHopSet& nhSet) const;
  bool isVirtualArsGroup(
      const RouteNextHopEntry& fwd,
      const RouteNextHopEntry::NextHopSet& nhSet) const;
  bool isEcmp(
      const RouteNextHopEntry& fwd,
      const std::shared_ptr<SwitchState>& state) const;
  size_t computeWeightedEcmpMemberCount(
      const RouteNextHopEntry& fwd,
      const cfg::AsicType& asicType,
      const std::shared_ptr<SwitchState>& state) const;
  void setMinWidthForArsVirtualGroup(
      std::optional<int32_t> minWidthForArsVirtualGroup);
  void setMaxArsVirtualGroups(std::optional<int32_t> maxArsVirtualGroups);
  void setMaxArsVirtualGroupWidth(
      std::optional<int32_t> maxArsVirtualGroupWidth);

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

  bool nativeWeightedEcmp_{true};
  // Yuba (G200) and G202x program protection backup next hops as ordinary
  // group members. Other asics keep them in hw reserved space, so they are not
  // charged as members.
  bool countBackupNextHopMembers_{false};
  uint32_t ecmpMemberUsage_{0};
  // Split horizon gives every dynamic ARS group a secondary plain ECMP group.
  // It shares the next hop set of its DLB group, so it cannot be a separate
  // entry in ecmpGroupRefMap_ and is derived from arsEcmpGroupRefMap_ instead.
  bool arsSplitHorizon_{false};
  uint32_t virtualArsGroupCount_{0};
  std::optional<int32_t> minWidthForArsVirtualGroup_;
  std::optional<int32_t> maxArsVirtualGroups_;
  std::optional<int32_t> maxArsVirtualGroupWidth_;

  FRIEND_TEST(EcmpResourceAccountantTest, getMemberCountForEcmpGroup);
  FRIEND_TEST(EcmpResourceAccountantTest, checkArsResource);
  FRIEND_TEST(EcmpResourceAccountantTest, checkEcmpResource);
  FRIEND_TEST(EcmpResourceAccountantTest, checkEcmpResourceForUcmpWeights);
  FRIEND_TEST(EcmpResourceAccountantTest, checkAndUpdateGenericEcmpResource);
  FRIEND_TEST(
      EcmpResourceAccountantTest,
      checkAndUpdateGenericEcmpResourceForUcmpWeights);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceAccounting);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceSharing);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceEdgeCases);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceLimits);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceLifecycle);
  FRIEND_TEST(EcmpResourceAccountantTest, protectionEcmpResourceArsExcluded);
  FRIEND_TEST(EcmpResourceAccountantTest, arsEcmpResourceSourcePortPrune);
  FRIEND_TEST(EcmpResourceAccountantTest, checkAndUpdateArsEcmpResource);
  FRIEND_TEST(EcmpResourceAccountantTest, computeWeightedEcmpMemberCount);
  FRIEND_TEST(EcmpResourceAccountantTest, routeWithAdjustedWeightZero);
  FRIEND_TEST(EcmpResourceAccountantTest, virtualArsGroups);
  FRIEND_TEST(EcmpResourceAccountantTest, virtualArsSuperGroupMemberLimit);
  FRIEND_TEST(EcmpResourceAccountantTest, virtualArsGroupOverrideExcluded);
  FRIEND_TEST(EcmpResourceAccountantTest, virtualArsGroupOverrideFlipLifecycle);
  FRIEND_TEST(
      EcmpResourceAccountantTest,
      virtualArsGroupOverrideDropPromotesClassification);
};

} // namespace facebook::fboss
