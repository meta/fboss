// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest.h>

#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/state/FibInfo.h"
#include "fboss/agent/state/FibInfoMap.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/RouteNextHopEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/TestUtils.h"

namespace facebook::fboss {

class ResourceAccountantTestBase : public ::testing::Test {
 public:
  static constexpr auto ecmpWeight = 1;

  static ResolvedNextHop roleNextHop(
      const std::string& address,
      int interfaceId,
      NextHopRole role,
      NextHopWeight weight = ecmpWeight) {
    return ResolvedNextHop(
        folly::IPAddress(address),
        InterfaceID(interfaceId),
        weight,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        {},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        role);
  }

  void SetUp() override {
    FLAGS_enable_nexthop_id_manager = true;
    std::map<int64_t, cfg::SwitchInfo> switchIdToSwitchInfo{
        {0, createSwitchInfo(cfg::SwitchType::NPU)}};
    const std::map<int64_t, cfg::DsfNode> switchIdToDsfNodes;
    asicTable_ = std::make_unique<HwAsicTable>(
        switchIdToSwitchInfo, std::nullopt, switchIdToDsfNodes);
    scopeResolver_ =
        std::make_unique<SwitchIdScopeResolver>(switchIdToSwitchInfo);
    nextHopIDManager_ = std::make_unique<NextHopIDManager>();
    FLAGS_ecmp_width = getMaxEcmpWidth();
    initState();
  }
  void setSplitHorizon(cfg::EcmpGroupType groupType, bool enabled) {
    cfg::EcmpGroupSettings settings;
    settings.enableSplitHorizon() = enabled;
    auto switchSettings = std::make_shared<SwitchSettings>();
    switchSettings->setEcmpGroupSettings({{groupType, settings}});
    auto multiSwitchSettings = std::make_shared<MultiSwitchSettings>();
    multiSwitchSettings->addNode(getScope().matcherString(), switchSettings);
    state_ = state_->clone();
    state_->resetSwitchSettings(multiSwitchSettings);
    state_->publish();
  }
  void initState() {
    state_ = std::make_shared<SwitchState>();
    auto fibInfoMap = std::make_shared<MultiSwitchFibInfoMap>();
    auto fibInfo = std::make_shared<FibInfo>();
    fibInfo->setIdToNextHopMap(std::make_shared<IdToNextHopMap>());
    fibInfo->setIdToNextHopIdSetMap(std::make_shared<IdToNextHopIdSetMap>());
    fibInfoMap->updateFibInfo(fibInfo, getScope());
    state_->resetFibsInfoMap(fibInfoMap);
  }

  HwSwitchMatcher getScope() const {
    return HwSwitchMatcher(std::unordered_set<SwitchID>({SwitchID(0)}));
  }

  std::shared_ptr<FibInfo> getFibInfo() const {
    return state_->getFibsInfoMap()->getFibInfo(getScope());
  }

  void allocAndUpdateIdMaps(
      RouteNextHopEntry& entry,
      const RouteNextHopSet& nhops) {
    if (!FLAGS_enable_nexthop_id_manager) {
      return;
    }
    auto allocResult = nextHopIDManager_->getOrAllocRouteNextHopSetID(nhops);
    std::optional<NextHopSetID> resolvedId =
        allocResult.nextHopIdSetIter->second.id;
    entry.setResolvedNextHopSetID(resolvedId);

    auto normalizedNhops = RouteNextHopEntry::normalizeNextHops(nhops);
    auto normAllocResult =
        nextHopIDManager_->getOrAllocRouteNextHopSetID(normalizedNhops);
    std::optional<NextHopSetID> normalizedId =
        normAllocResult.nextHopIdSetIter->second.id;
    entry.setNormalizedResolvedNextHopSetID(normalizedId);

    // Incrementally add only new entries to the existing maps on state_
    auto fibInfo = getFibInfo();
    auto idToNextHopMap = fibInfo->getIdToNextHopMap();
    auto idToNextHopIdSetMap = fibInfo->getIdToNextHopIdSetMap();
    for (const auto& [id, nhopEntry] : nextHopIDManager_->getIdToNextHop()) {
      if (!idToNextHopMap->getNextHopIf(id)) {
        idToNextHopMap->addNextHop(id, nhopEntry.nextHop.toThrift());
      }
    }
    for (const auto& [id, nhopIdSet] :
         nextHopIDManager_->getIdToNextHopIdSet()) {
      if (!idToNextHopIdSetMap->getNextHopIdSetIf(id)) {
        std::set<int64_t> nhopIds;
        for (const auto& nhopId : nhopIdSet) {
          nhopIds.insert(static_cast<int64_t>(nhopId));
        }
        idToNextHopIdSetMap->addNextHopIdSet(id, nhopIds);
      }
    }
  }

  const std::shared_ptr<Route<folly::IPAddressV6>> makeV6Route(
      const RoutePrefix<folly::IPAddressV6>& prefix,
      const RouteNextHopEntry& entry,
      const RouteNextHopSet& nhops) {
    // Clone via thrift round-trip (copy ctor is deleted), allocate IDs
    RouteNextHopEntry mutableEntry;
    mutableEntry.fromThrift(entry.toThrift());
    allocAndUpdateIdMaps(mutableEntry, nhops);
    auto route = std::make_shared<Route<folly::IPAddressV6>>(prefix);
    route->setResolved(mutableEntry);
    return route;
  }

  uint64_t getMaxEcmpWidth() {
    return asicTable_->getHwAsic(SwitchID(0))->getMaxEcmpSize();
  }
  uint64_t getMaxEcmpGroups() {
    auto maxEcmpGroups = asicTable_->getHwAsic(SwitchID(0))->getMaxEcmpGroups();
    CHECK(maxEcmpGroups.has_value());
    return maxEcmpGroups.value();
  }

  uint64_t getMaxEcmpMembers() {
    auto maxEcmpMembers =
        asicTable_->getHwAsic(SwitchID(0))->getMaxEcmpMembers();
    CHECK(maxEcmpMembers.has_value());
    return maxEcmpMembers.value();
  }

  uint64_t getMaxArsGroups() {
    auto maxArsGroups = asicTable_->getHwAsic(SwitchID(0))->getMaxArsGroups();
    CHECK(maxArsGroups.has_value());
    return maxArsGroups.value();
  }

  std::unique_ptr<HwAsicTable> asicTable_;
  std::unique_ptr<SwitchIdScopeResolver> scopeResolver_;
  std::unique_ptr<NextHopIDManager> nextHopIDManager_;
  std::shared_ptr<SwitchState> state_;
};

} // namespace facebook::fboss
