/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/SwSwitchRouteUpdateWrapper.h"

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"

#include "fboss/agent/state/SwitchState.h"

#include <memory>

namespace facebook::fboss {

RibToSwitchStateFunction createRibToSwitchStateFunction(
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  return [deltaApplicationBehavior](
             const facebook::fboss::SwitchIdScopeResolver* resolver,
             facebook::fboss::RouterID vrf,
             const facebook::fboss::IPv4NetworkToRouteMap& v4NetworkToRoute,
             const facebook::fboss::IPv6NetworkToRouteMap& v6NetworkToRoute,
             const facebook::fboss::LabelToRouteMap& labelToRoute,
             const NextHopIDManager* nextHopIDManager,
             const MySidTable& mySidTable,
             void* cookie,
             bool vrfDeleted) -> StateDelta {
    folly::Synchronized<facebook::fboss::RibToSwitchStateUpdater>
        ribToSwitchStateUpdater(
            std::in_place,
            resolver,
            vrf,
            v4NetworkToRoute,
            v6NetworkToRoute,
            labelToRoute,
            nextHopIDManager,
            mySidTable,
            facebook::fboss::RibToSwitchStateUpdater::kAllActions,
            vrfDeleted);

    auto wRibToSwitchStateUpdater = ribToSwitchStateUpdater.wlock();
    auto sw = static_cast<facebook::fboss::SwSwitch*>(cookie);

    sw->updateStateWithHwFailureProtection(
        "update fib",
        [&wRibToSwitchStateUpdater](const std::shared_ptr<SwitchState>& in) {
          return (*wRibToSwitchStateUpdater)(in);
        },
        deltaApplicationBehavior);

    auto lastDelta = wRibToSwitchStateUpdater->getLastDelta();
    // Fib update could get cancelled - e.g. when SwSwitch already started its
    // exit. In that case last delta will be empty. Account for this case
    auto oldState =
        lastDelta ? lastDelta->oldState() : std::make_shared<SwitchState>();
    return StateDelta(oldState, sw->getState());
  };
}

std::function<void(const NextHopIDManager*)> createNextHopIdStateUpdate(
    SwSwitch* sw,
    std::string name) {
  return
      [sw, name = std::move(name)](const NextHopIDManager* nextHopIDManager) {
        SwitchStateNextHopIdUpdater nhopUpdater(nextHopIDManager);
        ClassBasedPolicyUpdater policyUpdater(nextHopIDManager);
        sw->updateStateWithHwFailureProtection(
            name, [&](const std::shared_ptr<SwitchState>& state) {
              return policyUpdater(nhopUpdater(state));
            });
      };
}

SwSwitchRouteUpdateWrapper::SwSwitchRouteUpdateWrapper(
    SwSwitch* sw,
    RoutingInformationBase* rib,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior)
    : RouteUpdateWrapper(
          sw->getScopeResolver(),
          rib,
          rib ? createRibToSwitchStateFunction(deltaApplicationBehavior)
              : std::optional<RibToSwitchStateFunction>(),
          rib ? sw : nullptr),
      sw_(sw) {}

void SwSwitchRouteUpdateWrapper::updateStats(
    const RoutingInformationBase::UpdateStatistics& stats) {
  sw_->stats()->addRoutesV4(stats.v4RoutesAdded);
  sw_->stats()->addRoutesV6(stats.v6RoutesAdded);
  sw_->stats()->delRoutesV4(stats.v4RoutesDeleted);
  sw_->stats()->delRoutesV6(stats.v6RoutesDeleted);
  sw_->stats()->ribRouteProgrammingTimeUs(stats.duration);
  sw_->stats()->ribResolutionCyclesDetected(stats.resolutionCyclesDetected);
}

AdminDistance SwSwitchRouteUpdateWrapper::clientIdToAdminDistance(
    ClientID clientID) const {
  return sw_->clientIdToAdminDistance(static_cast<int>(clientID));
}

} // namespace facebook::fboss
