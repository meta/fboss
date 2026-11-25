/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/ResourceAccountant.h"
#include "fboss/agent/AgentFeatures.h"

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/state/SwitchState.h"

namespace {
constexpr auto kHundredPercentage = 100;

bool hasBackupNextHop(const facebook::fboss::RouteNextHopSet& nhops) {
  return std::any_of(nhops.begin(), nhops.end(), [](const auto& nhop) {
    return nhop.role() == facebook::fboss::NextHopRole::BACKUP;
  });
}

bool isBackupNextHop(const facebook::fboss::NextHop& nhop) {
  return nhop.role() == facebook::fboss::NextHopRole::BACKUP;
}

size_t primaryNextHopCount(const facebook::fboss::RouteNextHopSet& nhops) {
  return std::count_if(nhops.begin(), nhops.end(), [](const auto& nhop) { //
    return !isBackupNextHop(nhop);
  });
}

// Whether the backup next hops of a protection group occupy members of the
// group. Some asics program them as ordinary members; others keep them in hw
// reserved space, where they are not charged here.
size_t memberNextHopCount(
    const facebook::fboss::RouteNextHopSet& nhops,
    bool countBackupNextHops) {
  return countBackupNextHops ? nhops.size() : primaryNextHopCount(nhops);
}

} // namespace

namespace facebook::fboss {

ResourceAccountant::ResourceAccountant(
    const HwAsicTable* asicTable,
    const SwitchIdScopeResolver* scopeResolver)
    : asicTable_(asicTable),
      aclResourceAccountant_(asicTable),
      vrfResourceAccountant_(asicTable),
      mySidResourceAccountant_(asicTable),
      neighborResourceAccountant_(asicTable, scopeResolver),
      routeCounterResourceAccountant_(asicTable),
      routeResourceAccountant_(asicTable) {
  CHECK_EQ(
      asicTable->isFeatureSupportedOnAnyAsic(
          HwAsic::Feature::WEIGHTED_NEXTHOPGROUP_MEMBER),
      asicTable->isFeatureSupportedOnAllAsic(
          HwAsic::Feature::WEIGHTED_NEXTHOPGROUP_MEMBER));
  nativeWeightedEcmp_ = asicTable->isFeatureSupportedOnAllAsic(
      HwAsic::Feature::WEIGHTED_NEXTHOPGROUP_MEMBER);
  // Charge backup members if any asic needs them charged, so a mixed table
  // accounts for the worst case.
  const auto asics = asicTable->getHwAsics();
  countBackupNextHopMembers_ =
      std::any_of(asics.begin(), asics.end(), [](const auto& idAndAsic) {
        const auto asicType = idAndAsic.second->getAsicType();
        return asicType == cfg::AsicType::ASIC_TYPE_YUBA ||
            asicType == cfg::AsicType::ASIC_TYPE_G202X;
      });
  for (const auto& [_, asic] : asics) {
    if (auto limit = asic->getMaxSrv6UcmpDistinctWeights()) {
      maxSrv6UcmpDistinctWeights_ =
          std::min(maxSrv6UcmpDistinctWeights_.value_or(*limit), *limit);
    }
  }
  checkRouteUpdate_ = shouldCheckRouteUpdate();
}

bool ResourceAccountant::isVirtualArsGroup(
    const RouteNextHopEntry& fwd,
    const RouteNextHopEntry::NextHopSet& nhSet) const {
  if (!FLAGS_dlbResourceCheckEnable ||
      !minWidthForArsVirtualGroup_.has_value() || hasBackupNextHop(nhSet)) {
    return false;
  }
  // ERM-overridden routes are in backup ECMP mode and don't use the DLB pool.
  if (FLAGS_enable_ecmp_resource_manager &&
      fwd.getOverrideEcmpSwitchingMode().has_value()) {
    return false;
  }
  return nhSet.size() >=
      static_cast<size_t>(minWidthForArsVirtualGroup_.value());
}

bool ResourceAccountant::isVirtualArsGroup(
    const RouteNextHopEntry& fwd,
    const std::shared_ptr<SwitchState>& state) const {
  return isVirtualArsGroup(fwd, getNormalizedNextHops(state, fwd));
}

bool ResourceAccountant::isEcmp(
    const RouteNextHopEntry& fwd,
    const std::shared_ptr<SwitchState>& state) const {
  for (const auto& nhop : getNormalizedNextHops(state, fwd)) {
    if (nhop.weight() && nhop.weight() > 1) {
      return false;
    }
  }
  return true;
}

size_t ResourceAccountant::computeWeightedEcmpMemberCount(
    const RouteNextHopEntry& fwd,
    const cfg::AsicType& asicType,
    const std::shared_ptr<SwitchState>& state) const {
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK4:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      // UCMP members take 4x of ECMP members in the same table.
      return 4 * getNormalizedNextHops(state, fwd).size();
    case cfg::AsicType::ASIC_TYPE_YUBA:
    case cfg::AsicType::ASIC_TYPE_G202X:
      // Yuba asic natively supports UCMP members with no extra cost.
      return getNormalizedNextHops(state, fwd).size();
    default:
      XLOG(
          WARNING,
          "Unsupported ASIC type for Ucmp member resource computation. Assuming UCMP member usage is computed by ECMP replication");
      auto totalWeight = 0;
      for (const auto& nhop : getNormalizedNextHops(state, fwd)) {
        totalWeight += nhop.weight() ? nhop.weight() : 1;
      }
      return totalWeight;
  }
}

size_t ResourceAccountant::getMemberCountForEcmpGroup(
    const RouteNextHopEntry& fwd,
    const std::shared_ptr<SwitchState>& state) const {
  const auto nhSet = getNormalizedNextHops(state, fwd);
  if (hasBackupNextHop(nhSet)) {
    // Sai programs member weights only for ordinary ECMP groups, so protection
    // members are unweighted and each costs a single member.
    return memberNextHopCount(nhSet, countBackupNextHopMembers_);
  }
  if (isEcmp(fwd, state)) {
    return nhSet.size();
  }
  if (nativeWeightedEcmp_) {
    // Different asic supports native WeightedEcmp in different ways.
    // Therefore we will have asic-specific logic to compute the member count.
    const auto asics = asicTable_->getHwAsics();
    const auto asicType = asics.begin()->second->getAsicType();
    // Ensure that all ASICs have the same type.
    CHECK(
        std::all_of(
            asics.begin(), asics.end(), [&asicType](const auto& idAndAsic) {
              return idAndAsic.second->getAsicType() == asicType;
            }));
    return computeWeightedEcmpMemberCount(fwd, asicType, state);
  }
  // No native weighted ECMP support. Members are replicated to support
  // weighted ECMP.
  auto totalWeight = 0;
  for (const auto& nhop : nhSet) {
    totalWeight += nhop.weight() ? nhop.weight() : 1;
  }
  return totalWeight;
}

// Each dynamic ARS group is paired with a secondary plain ECMP group when
// split horizon is on, so the secondary groups need no bookkeeping of their
// own.
size_t ResourceAccountant::getEcmpGroupUsage() const {
  return ecmpGroupRefMap_.size() +
      (arsSplitHorizon_ ? arsEcmpGroupRefMap_.size() : 0);
}

bool ResourceAccountant::checkEcmpResource(bool intermediateState) const {
  // There are two checks needed for ECMP resource:
  // 1) Post each route add/update, check if intermediate state exceeds HW
  // limit. 2) Post entire state update, check if total usage is lower than
  // ecmp_resource_percentage.
  uint32_t resourcePercentage =
      intermediateState ? kHundredPercentage : FLAGS_ecmp_resource_percentage;

  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    const auto ecmpGroupLimit = hwAsic->getMaxEcmpGroups();
    const auto ecmpMemberLimit = hwAsic->getMaxEcmpMembers();
    std::optional<int> ecmpGroupEnforcedLimit, ecmpMemberEnforcedLimit;
    if (ecmpGroupLimit.has_value()) {
      ecmpGroupEnforcedLimit =
          (ecmpGroupLimit.value() * resourcePercentage / kHundredPercentage);
    }
    if (ecmpMemberLimit.has_value()) {
      ecmpMemberEnforcedLimit =
          (ecmpMemberLimit.value() * resourcePercentage / kHundredPercentage);
    }

    const auto ecmpGroupUsage = getEcmpGroupUsage();
    if (ecmpGroupEnforcedLimit.has_value() &&
        ecmpGroupUsage > static_cast<size_t>(*ecmpGroupEnforcedLimit)) {
      XLOG(DBG2) << " Ecmp group limit exceeded. Ecmp demand from this update: "
                 << ecmpGroupUsage
                 << " ASIC limit: " << *ecmpGroupEnforcedLimit;
      return false;
    }
    // Virtual ARS groups share a single DLB pool that collectively occupies
    // maxArsVirtualGroupWidth_ ECMP member table entries regardless of the
    // number of virtual groups active.
    auto virtualArsEcmpMemberUsage =
        (virtualArsGroupCount_ > 0 && maxArsVirtualGroupWidth_.has_value())
        ? static_cast<uint32_t>(maxArsVirtualGroupWidth_.value())
        : 0;
    auto totalEcmpMemberUsage = ecmpMemberUsage_ + virtualArsEcmpMemberUsage;
    if (ecmpMemberEnforcedLimit.has_value() &&
        totalEcmpMemberUsage >
            static_cast<uint32_t>(*ecmpMemberEnforcedLimit)) {
      XLOG(DBG2)
          << " Ecmp member limit exceeded. Ecmp demand from this update: "
          << totalEcmpMemberUsage
          << " ASIC Limit: " << *ecmpMemberEnforcedLimit;
      return false;
    }
  }
  return true;
}

bool ResourceAccountant::checkArsResource(bool intermediateState) const {
  if (FLAGS_dlbResourceCheckEnable && FLAGS_flowletSwitchingEnable) {
    uint32_t resourcePercentage =
        intermediateState ? kHundredPercentage : FLAGS_ars_resource_percentage;

    for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
      const auto arsGroupLimit = hwAsic->getMaxArsGroups();
      if (arsGroupLimit.has_value()) {
        uint32_t enforcedLimit =
            (arsGroupLimit.value() * resourcePercentage) / kHundredPercentage;
        // Non-virtual groups each use one DLB entry.
        // Virtual groups collectively use maxArsVirtualGroupWidth_/maxArsWidth
        // DLB entries.
        uint32_t virtualArsDlbUsage = 0;
        if (virtualArsGroupCount_ > 0 && maxArsVirtualGroupWidth_.has_value()) {
          auto maxArsWidth = hwAsic->getMaxArsWidth();
          if (maxArsWidth.has_value() && maxArsWidth.value() > 0) {
            virtualArsDlbUsage = static_cast<uint32_t>(
                maxArsVirtualGroupWidth_.value() / maxArsWidth.value());
          }
        }
        uint32_t totalDlbUsage =
            arsEcmpGroupRefMap_.size() + virtualArsDlbUsage;
        if (totalDlbUsage > enforcedLimit) {
          XLOG(DBG2) << " ARS group limit exceeded. ARS groups: "
                     << arsEcmpGroupRefMap_.size()
                     << ", virtual ARS groups: " << virtualArsGroupCount_
                     << ", total DLB usage: " << totalDlbUsage
                     << " ASIC limit: " << arsGroupLimit.value()
                     << ", resource percentage: " << resourcePercentage;
          return false;
        }
      }

      if (maxArsVirtualGroups_.has_value()) {
        uint32_t enforcedVirtualArsLimit =
            (static_cast<uint32_t>(maxArsVirtualGroups_.value()) *
             resourcePercentage) /
            kHundredPercentage;
        if (virtualArsGroupCount_ > enforcedVirtualArsLimit) {
          XLOG(DBG2)
              << " Virtual ARS group limit exceeded. Virtual ARS groups: "
              << virtualArsGroupCount_
              << " max: " << maxArsVirtualGroups_.value()
              << ", resource percentage: " << resourcePercentage;
          return false;
        }
      }

      if (maxArsVirtualGroupWidth_.has_value() &&
          maxArsVirtualGroupWidth_.value() >= 0 &&
          virtualArsSuperGroupMemberRefMap_.size() >
              static_cast<size_t>(maxArsVirtualGroupWidth_.value())) {
        XLOG(DBG2) << " Virtual ARS supergroup unique member limit exceeded. "
                   << "Unique members: "
                   << virtualArsSuperGroupMemberRefMap_.size()
                   << " limit: " << maxArsVirtualGroupWidth_.value();
        return false;
      }
    }
  }
  return true;
}

bool ResourceAccountant::wouldExceedSuperGroupLimit(
    const RouteNextHopEntry::NextHopSet& nhSet) const {
  if (!maxArsVirtualGroupWidth_.has_value() ||
      maxArsVirtualGroupWidth_.value() < 0) {
    return false;
  }
  size_t projected = virtualArsSuperGroupMemberRefMap_.size();
  for (const auto& nh : nhSet) {
    if (!virtualArsSuperGroupMemberRefMap_.count(nh)) {
      ++projected;
    }
  }
  if (projected > static_cast<size_t>(maxArsVirtualGroupWidth_.value())) {
    XLOG(DBG2)
        << "Virtual ARS supergroup unique member limit would be exceeded."
        << " Projected: " << projected
        << " limit: " << maxArsVirtualGroupWidth_.value();
    return true;
  }
  return false;
}

template <typename AddrT>
bool ResourceAccountant::checkAndUpdateGenericEcmpResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  const auto& fwd = route->getForwardInfo();

  const auto nhSet = getNormalizedNextHops(state, fwd);
  // Forwarding to nextHops and more than one nextHop - use ECMP
  if (fwd.getAction() == RouteForwardAction::NEXTHOPS && nhSet.size() > 1) {
    const bool isVirtual = isVirtualArsGroup(fwd, nhSet);
    if (auto it = ecmpGroupRefMap_.find(nhSet); it != ecmpGroupRefMap_.end()) {
      auto& entry = it->second;
      if (add) {
        // Virtual ARS nhSets also have a non-hierarchical companion ECMP
        // object for non-ARS traffic; charge member cost on first ref of
        // any kind, release when all refs are gone.
        const bool wasEmpty =
            (entry.refCountVirtual == 0 && entry.refCountNonVirtual == 0);
        if (isVirtual) {
          if (entry.refCountVirtual == 0) {
            if (wouldExceedSuperGroupLimit(nhSet)) {
              return false;
            }
            virtualArsGroupCount_++;
            for (const auto& nh : nhSet) {
              virtualArsSuperGroupMemberRefMap_[nh]++;
            }
          }
          entry.refCountVirtual++;
        } else {
          entry.refCountNonVirtual++;
        }
        if (wasEmpty) {
          entry.memberContribution =
              static_cast<uint32_t>(getMemberCountForEcmpGroup(fwd, state));
          ecmpMemberUsage_ += entry.memberContribution;
        }
      } else {
        if (isVirtual) {
          CHECK_GT(entry.refCountVirtual, 0u);
          entry.refCountVirtual--;
          if (entry.refCountVirtual == 0) {
            virtualArsGroupCount_--;
            for (const auto& nh : nhSet) {
              auto nhIt = virtualArsSuperGroupMemberRefMap_.find(nh);
              CHECK(nhIt != virtualArsSuperGroupMemberRefMap_.end());
              if (--nhIt->second == 0) {
                virtualArsSuperGroupMemberRefMap_.erase(nhIt);
              }
            }
          }
        } else {
          CHECK_GT(entry.refCountNonVirtual, 0u);
          entry.refCountNonVirtual--;
        }
        if (entry.refCountVirtual == 0 && entry.refCountNonVirtual == 0) {
          ecmpMemberUsage_ -= entry.memberContribution;
          ecmpGroupRefMap_.erase(it);
        }
      }
      return true;
    }
    // ECMP group does not exist in hw — check if any usage exceeds ASIC limit
    CHECK(add);
    EcmpGroupRefEntry entry;
    if (isVirtual) {
      if (wouldExceedSuperGroupLimit(nhSet)) {
        return false;
      }
      entry.refCountVirtual = 1;
      virtualArsGroupCount_++;
      for (const auto& nh : nhSet) {
        virtualArsSuperGroupMemberRefMap_[nh]++;
      }
    } else {
      entry.refCountNonVirtual = 1;
    }
    entry.memberContribution =
        static_cast<uint32_t>(getMemberCountForEcmpGroup(fwd, state));
    ecmpMemberUsage_ += entry.memberContribution;
    ecmpGroupRefMap_[nhSet] = entry;
    return checkEcmpResource(true /* intermediateState */);
  }
  return true;
}

template <typename AddrT>
bool ResourceAccountant::checkAndUpdateArsEcmpResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  if (FLAGS_dlbResourceCheckEnable && FLAGS_flowletSwitchingEnable) {
    const auto& fwd = route->getForwardInfo();
    const auto nhSet = getNormalizedNextHops(state, fwd);
    // Forwarding to nextHops and more than one nextHop - use ECMP
    if (fwd.getAction() == RouteForwardAction::NEXTHOPS && nhSet.size() > 1) {
      if (hasBackupNextHop(nhSet)) {
        return true;
      }
      arsSplitHorizon_ = state->getSplitHorizonEnabled(cfg::EcmpGroupType::ARS)
                             .value_or(false);
      // If ERM were disabled, then arsEcmpGroupRefMap_ and ecmpGroupRefMap_
      // will be identical since primary and backup groups are
      // indistinguishable.
      //
      // No need to check backup groups since they don't use dynamic groups
      if (FLAGS_enable_ecmp_resource_manager &&
          fwd.getOverrideEcmpSwitchingMode().has_value()) {
        return true;
      }
      if (isVirtualArsGroup(fwd, nhSet)) {
        return checkArsResource(true /* intermediateState */) &&
            checkEcmpResource(true /* intermediateState */);
      }
      if (auto it = arsEcmpGroupRefMap_.find(nhSet);
          it != arsEcmpGroupRefMap_.end()) {
        it->second = it->second + (add ? 1 : -1);
        CHECK(it->second >= 0);
        if (!add && it->second == 0) {
          arsEcmpGroupRefMap_.erase(it);
          if (arsSplitHorizon_) {
            ecmpMemberUsage_ -=
                static_cast<uint32_t>(getMemberCountForEcmpGroup(fwd, state));
          }
        }
        return true;
      }
      // ECMP group does not exists in hw - Check if any usage exceeds ASIC
      // limit
      CHECK(add);
      arsEcmpGroupRefMap_[nhSet] = 1;
      if (arsSplitHorizon_) {
        // The secondary group holds its own copy of the members.
        ecmpMemberUsage_ +=
            static_cast<uint32_t>(getMemberCountForEcmpGroup(fwd, state));
      }
      return checkArsResource(true /* intermediateState */) &&
          checkEcmpResource(true /* intermediateState */);
    }
  }
  return true;
}

template <typename AddrT>
bool ResourceAccountant::checkAndUpdateEcmpResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  bool valid = true;
  valid &= checkAndUpdateGenericEcmpResource(route, add, state);
  valid &= checkAndUpdateArsEcmpResource(route, add, state);
  if (FLAGS_enable_srv6_nexthop_resource_protection) {
    valid &= checkAndUpdateSrv6NextHopResource(route, add, state);
  }
  return valid;
}

bool ResourceAccountant::shouldCheckRouteUpdate() const {
  if (!FLAGS_enable_route_resource_protection) {
    return false;
  }
  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    if (hwAsic->getMaxEcmpGroups().has_value() ||
        hwAsic->getMaxEcmpMembers().has_value() ||
        hwAsic->getMaxRoutes().has_value() ||
        hwAsic->getMaxRouteCounters().has_value()) {
      return true;
    }
  }
  return false;
}

bool ResourceAccountant::routeAndEcmpStateChangedImpl(const StateDelta& delta) {
  if (!checkRouteUpdate_ || !FLAGS_enable_route_resource_protection) {
    return true;
  }
  bool validRouteUpdate = true;
  auto oldState = delta.oldState();
  auto newState = delta.newState();

  processFibsDeltaInHwSwitchOrder(
      delta,
      [&](RouterID /*rid*/, const auto& oldRoute, const auto& newRoute) {
        if (!oldRoute->isResolved() && !newRoute->isResolved()) {
          return;
        }
        if (oldRoute->isResolved() && !newRoute->isResolved()) {
          validRouteUpdate &=
              checkAndUpdateEcmpResource(oldRoute, false /* add */, oldState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  false /* add */);
          validRouteUpdate &= routeCounterResourceAccountant_
                                  .checkAndUpdateRouteCounterResource(
                                      oldRoute, false /* add */);
          return;
        }
        if (!oldRoute->isResolved() && newRoute->isResolved()) {
          validRouteUpdate &=
              checkAndUpdateEcmpResource(newRoute, true /* add */, newState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  true /* add */);
          validRouteUpdate &=
              routeCounterResourceAccountant_
                  .checkAndUpdateRouteCounterResource(newRoute, true /* add */);
          return;
        }
        // Both old and new are resolved
        CHECK(oldRoute->isResolved() && newRoute->isResolved());
        validRouteUpdate &=
            checkAndUpdateEcmpResource(newRoute, true, newState);
        validRouteUpdate &=
            checkAndUpdateEcmpResource(oldRoute, false, oldState);
        validRouteUpdate &=
            routeCounterResourceAccountant_.checkAndUpdateRouteCounterResource(
                newRoute, true);
        validRouteUpdate &=
            routeCounterResourceAccountant_.checkAndUpdateRouteCounterResource(
                oldRoute, false);
      },
      [&](RouterID /*rid*/, const auto& newRoute) {
        if (newRoute->isResolved()) {
          validRouteUpdate &=
              checkAndUpdateEcmpResource(newRoute, true /* add */, newState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  true /* add */);
          validRouteUpdate &=
              routeCounterResourceAccountant_
                  .checkAndUpdateRouteCounterResource(newRoute, true /* add */);
        }
      },
      [&](RouterID /*rid*/, const auto& delRoute) {
        if (delRoute->isResolved()) {
          validRouteUpdate &=
              checkAndUpdateEcmpResource(delRoute, false /* add */, oldState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  false /* add */);
          validRouteUpdate &= routeCounterResourceAccountant_
                                  .checkAndUpdateRouteCounterResource(
                                      delRoute, false /* add */);
        }
      });

  // Ensure new state usage does not exceed ecmp_resource_percentage
  validRouteUpdate &= checkEcmpResource(false /* intermediateState */);
  validRouteUpdate &= checkArsResource(false /* intermediateState */);
  if (FLAGS_enable_srv6_nexthop_resource_protection) {
    validRouteUpdate &= checkSrv6NextHopResource(false /* intermediateState */);
  }
  if (FLAGS_enable_route_counter_resource_protection) {
    validRouteUpdate &=
        routeCounterResourceAccountant_.checkRouteCounterResource(
            false /* intermediateState */);
  }
  return validRouteUpdate;
}

bool ResourceAccountant::isValidRouteUpdate(const StateDelta& delta) {
  bool validRouteUpdate = routeAndEcmpStateChangedImpl(delta);

  if (FLAGS_dlbResourceCheckEnable && FLAGS_flowletSwitchingEnable &&
      !validRouteUpdate) {
    for (const auto& [switchId, hwAsic] : asicTable_->getHwAsics()) {
      const auto dlbGroupLimit = hwAsic->getMaxArsGroups();
      if (dlbGroupLimit && arsEcmpGroupRefMap_.size() > dlbGroupLimit.value()) {
        XLOG(WARNING)
            << "Invalid route update - exceeding DLB resource limits. New state consumes "
            << arsEcmpGroupRefMap_.size() << " DLB ECMP groups";

        XLOG(WARNING) << "DLB ECMP resource limits for Switch " << switchId
                      << ": max DLB groups="
                      << (dlbGroupLimit.has_value()
                              ? folly::to<std::string>(dlbGroupLimit.value())
                              : "None");
        return validRouteUpdate;
      }
    }
  }
  if (!validRouteUpdate) {
    XLOG(WARNING)
        << "Invalid route update - exceeding route or ECMP resource limits. New state consumes "
        << routeResourceAccountant_.getUsage() << " routes, "
        << ecmpMemberUsage_ << " ECMP members and " << getEcmpGroupUsage()
        << " ECMP groups.";
    for (const auto& [switchId, hwAsic] : asicTable_->getHwAsics()) {
      const auto ecmpGroupLimit = hwAsic->getMaxEcmpGroups();
      const auto ecmpMemberLimit = hwAsic->getMaxEcmpMembers();
      const auto routeLimit = hwAsic->getMaxRoutes();
      XLOG(WARNING) << "ECMP resource limits for Switch " << switchId
                    << ": max routes="
                    << (routeLimit.has_value()
                            ? folly::to<std::string>(routeLimit.value())
                            : "None")
                    << ": max ECMP groups="
                    << (ecmpGroupLimit.has_value()
                            ? folly::to<std::string>(ecmpGroupLimit.value())
                            : "None")
                    << ", max ECMP members="
                    << (ecmpMemberLimit.has_value()
                            ? folly::to<std::string>(ecmpMemberLimit.value())
                            : "None");
    }
  }
  return validRouteUpdate;
}

template bool
ResourceAccountant::checkAndUpdateEcmpResource<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

template bool
ResourceAccountant::checkAndUpdateEcmpResource<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

size_t ResourceAccountant::countSrv6NextHops(
    const RouteNextHopSet& nhSet) const {
  size_t count = 0;
  for (const auto& nhop : nhSet) {
    if (nhop.tunnelType() == TunnelType::SRV6_ENCAP) {
      count++;
    }
  }
  return count;
}

bool ResourceAccountant::checkSrv6UcmpDistinctWeights(
    const RouteNextHopSet& nhSet) const {
  if (!maxSrv6UcmpDistinctWeights_.has_value() || hasBackupNextHop(nhSet)) {
    return true;
  }
  std::set<NextHopWeight> weights;
  for (const auto& nhop : nhSet) {
    weights.insert(nhop.weight());
  }
  return weights.size() <= *maxSrv6UcmpDistinctWeights_;
}

template <typename AddrT>
bool ResourceAccountant::checkAndUpdateSrv6NextHopResource(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  const auto& fwd = route->getForwardInfo();
  if (fwd.getAction() != RouteForwardAction::NEXTHOPS) {
    return true;
  }

  auto nhSetId = fwd.getNormalizedResolvedNextHopSetID();
  if (!nhSetId.has_value()) {
    return true;
  }
  auto setIdVal = static_cast<int64_t>(nhSetId.value());

  // Check if this NextHopSetID is already tracked
  if (auto it = srv6NextHopSetRefMap_.find(setIdVal);
      it != srv6NextHopSetRefMap_.end()) {
    it->second.refCount += (add ? 1 : -1);
    CHECK_GE(it->second.refCount, 0);
    if (!add && it->second.refCount == 0) {
      if (it->second.isEcmp) {
        srv6EcmpNextHopUsage_ -= it->second.srv6Count;
      } else {
        srv6SingleNextHopUsage_ -= it->second.srv6Count;
      }
      srv6NextHopSetRefMap_.erase(it);
    }
    return true;
  }

  // NextHopSetID not in map. For remove, this means the route had no SRv6
  // next hops when it was added (srv6Count was 0), so nothing to undo.
  if (!add) {
    return true;
  }

  // New NextHopSetID on add — resolve nhops to count SRv6
  auto nhSet = getNormalizedNextHops(state, fwd);
  auto srv6Count = countSrv6NextHops(nhSet);
  if (srv6Count == 0) {
    return true;
  }

  bool isEcmpRoute = nhSet.size() > 1;
  srv6NextHopSetRefMap_[setIdVal] = {1, srv6Count, isEcmpRoute};
  if (isEcmpRoute) {
    srv6EcmpNextHopUsage_ += srv6Count;
  } else {
    srv6SingleNextHopUsage_ += srv6Count;
  }
  if (!checkSrv6UcmpDistinctWeights(nhSet)) {
    XLOG(DBG2) << "Route " << route->prefix().str()
               << " SRv6 UCMP group exceeds ASIC limit of "
               << *maxSrv6UcmpDistinctWeights_ << " distinct weights";
    return false;
  }
  return true;
}

template bool
ResourceAccountant::checkAndUpdateSrv6NextHopResource<folly::IPAddressV6>(
    const std::shared_ptr<Route<folly::IPAddressV6>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

template bool
ResourceAccountant::checkAndUpdateSrv6NextHopResource<folly::IPAddressV4>(
    const std::shared_ptr<Route<folly::IPAddressV4>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state);

bool ResourceAccountant::checkSrv6NextHopResource(
    bool intermediateState) const {
  uint32_t resourcePercentage = intermediateState
      ? kHundredPercentage
      : FLAGS_srv6_nexthop_resource_percentage;

  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    auto ecmpLimit = hwAsic->getMaxSrv6EcmpNextHops();
    if (ecmpLimit.has_value()) {
      uint32_t enforcedLimit =
          (ecmpLimit.value() * resourcePercentage) / kHundredPercentage;
      if (srv6EcmpNextHopUsage_ > enforcedLimit) {
        XLOG(DBG2) << "SRv6 ECMP next hop limit exceeded. Usage: "
                   << srv6EcmpNextHopUsage_ << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
    auto singleLimit = hwAsic->getMaxSrv6SingleNextHops();
    if (singleLimit.has_value()) {
      uint32_t enforcedLimit =
          (singleLimit.value() * resourcePercentage) / kHundredPercentage;
      if (srv6SingleNextHopUsage_ > enforcedLimit) {
        XLOG(DBG2) << "SRv6 single next hop limit exceeded. Usage: "
                   << srv6SingleNextHopUsage_
                   << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
  }
  return true;
}

void ResourceAccountant::updateArsVirtualGroupConfig(const StateDelta& delta) {
  if (auto flowletConfig = delta.newState()->getFlowletSwitchingConfig()) {
    minWidthForArsVirtualGroup_ =
        flowletConfig->getMinWidthForArsVirtualGroup();
    maxArsVirtualGroups_ = flowletConfig->getMaxArsVirtualGroups();
    maxArsVirtualGroupWidth_ = flowletConfig->getMaxArsVirtualGroupWidth();
  } else {
    minWidthForArsVirtualGroup_ = std::nullopt;
    maxArsVirtualGroups_ = std::nullopt;
    maxArsVirtualGroupWidth_ = std::nullopt;
  }
}

// stateChanged is called when the ResourceAccountant needs to be updated
void ResourceAccountant::stateChanged(const StateDelta& delta) {
  updateArsVirtualGroupConfig(delta);
  routeAndEcmpStateChangedImpl(delta);

  if (FLAGS_enable_hw_update_protection) {
    l2ResourceAccountant_.stateChanged(delta);
    neighborResourceAccountant_.stateChanged(delta);
  }

  if (FLAGS_enable_mysid_resource_protection) {
    mySidResourceAccountant_.stateChanged(delta);
  }

  vrfResourceAccountant_.stateChanged(delta);
}

// check if the resource is available for the update as per fboss limits
bool ResourceAccountant::isValidUpdate(const StateDelta& delta) {
  updateArsVirtualGroupConfig(delta);
  bool isValidUpdate = isValidRouteUpdate(delta);

  if (FLAGS_enable_hw_update_protection) {
    isValidUpdate &= l2ResourceAccountant_.isValidUpdate(delta);
    isValidUpdate &= neighborResourceAccountant_.isValidUpdate(delta);
  }

  if (FLAGS_enable_mysid_resource_protection) {
    isValidUpdate &= mySidResourceAccountant_.isValidUpdate(delta);
  }

  isValidUpdate &= vrfResourceAccountant_.isValidUpdate(delta);
  if (FLAGS_enable_pbr_acl_resource_protection) {
    isValidUpdate &= aclResourceAccountant_.isValidUpdate(delta);
  }

  return isValidUpdate;
}

void ResourceAccountant::setMinWidthForArsVirtualGroup(
    std::optional<int32_t> minWidthForArsVirtualGroup) {
  minWidthForArsVirtualGroup_ = minWidthForArsVirtualGroup;
}

void ResourceAccountant::setMaxArsVirtualGroups(
    std::optional<int32_t> maxArsVirtualGroups) {
  maxArsVirtualGroups_ = maxArsVirtualGroups;
}

void ResourceAccountant::setMaxArsVirtualGroupWidth(
    std::optional<int32_t> maxArsVirtualGroupWidth) {
  maxArsVirtualGroupWidth_ = maxArsVirtualGroupWidth;
}

} // namespace facebook::fboss
