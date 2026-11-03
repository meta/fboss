/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/state/FibDeltaHelpers.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

RibToSwitchStateUpdater::RibToSwitchStateUpdater(
    const SwitchIdScopeResolver* resolver,
    RouterID vrf,
    const IPv4NetworkToRouteMap& v4NetworkToRoute,
    const IPv6NetworkToRouteMap& v6NetworkToRoute,
    const LabelToRouteMap& labelToRoute,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    int actions,
    bool vrfDeleted)
    : actions_(actions),
      fibUpdater_(
          resolver,
          vrf,
          v4NetworkToRoute,
          v6NetworkToRoute,
          labelToRoute,
          nextHopIDManager,
          vrfDeleted),
      mySidUpdater_(resolver, mySidTable),
      nhopStateUpdater_(nextHopIDManager),
      classBasedPolicyUpdater_(nextHopIDManager) {}

std::shared_ptr<SwitchState> RibToSwitchStateUpdater::operator()(
    const std::shared_ptr<SwitchState>& state) {
  auto nextState = state;
  if (actions_ & UPDATE_FIB) {
    nextState = fibUpdater_(nextState);
  }
  if (actions_ & UPDATE_MYSID) {
    nextState = mySidUpdater_(nextState);
  }
  nextState = nhopStateUpdater_(nextState);
  nextState = classBasedPolicyUpdater_(nextState);
  StateDelta delta(state, nextState);
  /*
   * We validate nhops here and not in fibUpdater_ as
   * - With nhop ID assignment, the fibInfoMaps don't have the updated nhopIds
   *   during FIB construction. So we can't really lookup nhop contents.
   * - Invalid nhops may get associated with mysids as well.
   */
  if (auto invalidNextHop = nhopStateUpdater_.getInvalidLinkLocalNextHop()) {
    throw FbossHwUpdateError(
        nextState, state, "Invalid link-local next hop: ", *invalidNextHop);
  }

  // Route resolution is complete and next-hop ID maps are populated here.
  // Reject through FbossHwUpdateError so updateFib() reconstructs the RIB and
  // NextHopIDManager from the previously applied switch state.
  auto validateRouteNextHops = [&](RouterID routeVrf, const auto& route) {
    if (!FLAGS_srv6 || !route->isResolved()) {
      return;
    }
    const auto& forwarding = route->getForwardInfo();
    if (forwarding.getAction() != RouteForwardAction::NEXTHOPS) {
      return;
    }

    const auto nextHops = getNormalizedNextHops(nextState, forwarding);
    if (!hasSrv6AndNonSrv6NextHops(nextHops)) {
      return;
    }

    size_t srv6Count = 0;
    for (const auto& nextHop : nextHops) {
      srv6Count += !nextHop.srv6SegmentList().empty();
    }
    throw FbossHwUpdateError(
        nextState,
        state,
        "Mixed SRv6 and non-SRv6 next hops are unsupported: vrf=",
        routeVrf,
        " prefix=",
        route->prefix().str(),
        " srv6=",
        srv6Count,
        " nonSrv6=",
        nextHops.size() - srv6Count);
  };
  forEachChangedRoute(
      delta,
      [&](RouterID routeVrf, const auto& /* oldRoute */, const auto& newRoute) {
        validateRouteNextHops(routeVrf, newRoute);
      },
      validateRouteNextHops,
      [](RouterID /* routeVrf */, const auto& /* oldRoute */) {});

  if (!FLAGS_verify_fib_nexthop_id_consistency) {
    DCHECK(nhopStateUpdater_.verifyNextHopIdConsistency(nextState));
  } else {
    CHECK(nhopStateUpdater_.verifyNextHopIdConsistency(nextState));
  }
  lastDelta_ = std::move(delta);
  return nextState;
}

} // namespace facebook::fboss
