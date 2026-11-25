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
      routeResourceAccountant_(asicTable),
      srv6NextHopResourceAccountant_(asicTable),
      ecmpResourceAccountant_(asicTable) {
  checkRouteUpdate_ = shouldCheckRouteUpdate();
}

template <typename AddrT>
bool ResourceAccountant::checkAndUpdateNextHopResources(
    const std::shared_ptr<Route<AddrT>>& route,
    bool add,
    const std::shared_ptr<SwitchState>& state) {
  bool valid = true;
  valid &=
      ecmpResourceAccountant_.checkAndUpdateEcmpResource(route, add, state);
  if (FLAGS_enable_srv6_nexthop_resource_protection) {
    valid &= srv6NextHopResourceAccountant_.checkAndUpdateSrv6NextHopResource(
        route, add, state);
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
          validRouteUpdate &= checkAndUpdateNextHopResources(
              oldRoute, false /* add */, oldState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  false /* add */);
          validRouteUpdate &= routeCounterResourceAccountant_
                                  .checkAndUpdateRouteCounterResource(
                                      oldRoute, false /* add */);
          return;
        }
        if (!oldRoute->isResolved() && newRoute->isResolved()) {
          validRouteUpdate &= checkAndUpdateNextHopResources(
              newRoute, true /* add */, newState);
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
            checkAndUpdateNextHopResources(newRoute, true, newState);
        validRouteUpdate &=
            checkAndUpdateNextHopResources(oldRoute, false, oldState);
        validRouteUpdate &=
            routeCounterResourceAccountant_.checkAndUpdateRouteCounterResource(
                newRoute, true);
        validRouteUpdate &=
            routeCounterResourceAccountant_.checkAndUpdateRouteCounterResource(
                oldRoute, false);
      },
      [&](RouterID /*rid*/, const auto& newRoute) {
        if (newRoute->isResolved()) {
          validRouteUpdate &= checkAndUpdateNextHopResources(
              newRoute, true /* add */, newState);
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
          validRouteUpdate &= checkAndUpdateNextHopResources(
              delRoute, false /* add */, oldState);
          validRouteUpdate &=
              routeResourceAccountant_.checkAndUpdateRouteResource(
                  false /* add */);
          validRouteUpdate &= routeCounterResourceAccountant_
                                  .checkAndUpdateRouteCounterResource(
                                      delRoute, false /* add */);
        }
      });

  // Ensure new state usage does not exceed ecmp_resource_percentage
  validRouteUpdate &=
      ecmpResourceAccountant_.checkEcmpResource(false /* intermediateState */);
  validRouteUpdate &=
      ecmpResourceAccountant_.checkArsResource(false /* intermediateState */);
  if (FLAGS_enable_srv6_nexthop_resource_protection) {
    validRouteUpdate &= srv6NextHopResourceAccountant_.checkSrv6NextHopResource(
        false /* intermediateState */);
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
      if (dlbGroupLimit &&
          ecmpResourceAccountant_.getArsEcmpGroupUsage() >
              dlbGroupLimit.value()) {
        XLOG(WARNING)
            << "Invalid route update - exceeding DLB resource limits. New state consumes "
            << ecmpResourceAccountant_.getArsEcmpGroupUsage()
            << " DLB ECMP groups";

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
        << ecmpResourceAccountant_.getEcmpMemberUsage() << " ECMP members and "
        << ecmpResourceAccountant_.getEcmpGroupUsage() << " ECMP groups.";
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

// stateChanged is called when the ResourceAccountant needs to be updated
void ResourceAccountant::stateChanged(const StateDelta& delta) {
  ecmpResourceAccountant_.updateArsVirtualGroupConfig(delta);
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
  ecmpResourceAccountant_.updateArsVirtualGroupConfig(delta);
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

} // namespace facebook::fboss
