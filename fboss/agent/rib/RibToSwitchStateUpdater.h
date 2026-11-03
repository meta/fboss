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

#include "fboss/agent/rib/ClassBasedPolicyUpdater.h"
#include "fboss/agent/rib/ForwardingInformationBaseUpdater.h"
#include "fboss/agent/rib/MySidMapUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"
#include "fboss/agent/rib/SwitchStateNextHopIdUpdater.h"
#include "fboss/agent/state/StateDelta.h"

#include <memory>
#include <optional>

namespace facebook::fboss {

class SwitchState;
class SwitchIdScopeResolver;

/*
 * RibToSwitchStateUpdater wraps ForwardingInformationBaseUpdater,
 * MySidMapUpdater, and SwitchStateNextHopIdUpdater to apply all RIB state
 * changes to SwitchState in a single operator() call. It stores the lastDelta
 * across the combined update.
 */
class RibToSwitchStateUpdater {
 public:
  enum UpdateAction {
    UPDATE_FIB = 2,
    UPDATE_MYSID = 4,
  };
  static constexpr int kAllActions = UPDATE_FIB | UPDATE_MYSID;

  RibToSwitchStateUpdater(
      const SwitchIdScopeResolver* resolver,
      RouterID vrf,
      const IPv4NetworkToRouteMap& v4NetworkToRoute,
      const IPv6NetworkToRouteMap& v6NetworkToRoute,
      const LabelToRouteMap& labelToRoute,
      const NextHopIDManager* nextHopIDManager,
      const MySidTable& mySidTable,
      int actions = kAllActions,
      bool vrfDeleted = false);

  std::shared_ptr<SwitchState> operator()(
      const std::shared_ptr<SwitchState>& state);

  std::optional<StateDelta> getLastDelta() const {
    return lastDelta_
        ? StateDelta(lastDelta_->oldState(), lastDelta_->newState())
        : std::optional<StateDelta>();
  }

 private:
  int actions_;
  ForwardingInformationBaseUpdater fibUpdater_;
  MySidMapUpdater mySidUpdater_;
  SwitchStateNextHopIdUpdater nhopStateUpdater_;
  ClassBasedPolicyUpdater classBasedPolicyUpdater_;
  std::optional<StateDelta> lastDelta_;
};

} // namespace facebook::fboss
