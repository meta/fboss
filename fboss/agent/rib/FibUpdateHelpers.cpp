/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/rib/FibUpdateHelpers.h"

#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/NetworkToRouteMap.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {
StateDelta ribToSwitchStateUpdate(
    const SwitchIdScopeResolver* resolver,
    facebook::fboss::RouterID vrf,
    const IPv4NetworkToRouteMap& v4NetworkToRoute,
    const IPv6NetworkToRouteMap& v6NetworkToRoute,
    const LabelToRouteMap& labelToRoute,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    void* cookie,
    bool vrfDeleted) {
  RibToSwitchStateUpdater ribToSwitchStateUpdater(
      resolver,
      vrf,
      v4NetworkToRoute,
      v6NetworkToRoute,
      labelToRoute,
      nextHopIDManager,
      mySidTable,
      facebook::fboss::RibToSwitchStateUpdater::kAllActions,
      vrfDeleted);

  auto switchState =
      static_cast<std::shared_ptr<facebook::fboss::SwitchState>*>(cookie);
  *switchState = ribToSwitchStateUpdater(*switchState);
  (*switchState)->publish();
  auto lastDelta = ribToSwitchStateUpdater.getLastDelta();
  CHECK(lastDelta.has_value());
  return StateDelta(lastDelta->oldState(), *switchState);
}

StateDelta noopFibUpdate(
    const SwitchIdScopeResolver* /*resolver*/,
    facebook::fboss::RouterID /*vrf*/,
    const IPv4NetworkToRouteMap& /*v4NetworkToRoute*/,
    const IPv6NetworkToRouteMap& /*v6NetworkToRoute*/,
    const LabelToRouteMap& /*labelToRoute*/,
    const NextHopIDManager* /*nextHopIDManager*/,
    const MySidTable& /*mySidTable*/,
    void* /*cookie*/,
    bool /*vrfDeleted*/) {
  return StateDelta(nullptr, nullptr);
}

} // namespace facebook::fboss
