// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/HwSwitchRouteUpdateWrapper.h"
#include "fboss/agent/rib/NextHopIDManager.h"
#include "fboss/agent/rib/RibToSwitchStateUpdater.h"
#include "fboss/agent/rib/RouteUpdater.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/Platform.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/state/StateDelta.h"

namespace facebook::fboss {

StateDelta hwSwitchFibUpdate(
    const SwitchIdScopeResolver* resolver,
    facebook::fboss::RouterID vrf,
    const facebook::fboss::IPv4NetworkToRouteMap& v4NetworkToRoute,
    const facebook::fboss::IPv6NetworkToRouteMap& v6NetworkToRoute,
    const facebook::fboss::LabelToRouteMap& labelToRoute,
    const NextHopIDManager* nextHopIDManager,
    const MySidTable& mySidTable,
    const std::shared_ptr<SwitchState> oldState,
    bool vrfDeleted) {
  facebook::fboss::RibToSwitchStateUpdater ribToSwitchStateUpdater(
      resolver,
      vrf,
      v4NetworkToRoute,
      v6NetworkToRoute,
      labelToRoute,
      nextHopIDManager,
      mySidTable,
      facebook::fboss::RibToSwitchStateUpdater::kAllActions,
      vrfDeleted);
  ribToSwitchStateUpdater(oldState);
  auto lastDelta = ribToSwitchStateUpdater.getLastDelta();
  CHECK(lastDelta.has_value());
  return StateDelta(lastDelta->oldState(), lastDelta->newState());
}

HwSwitchRouteUpdateWrapper::HwSwitchRouteUpdateWrapper(
    HwSwitch* hw,
    RoutingInformationBase* rib,
    std::function<std::shared_ptr<SwitchState>(const StateDelta&)> apply)
    : RouteUpdateWrapper(
          hw->getPlatform()->scopeResolver(),
          rib,
          [apply = std::move(apply)](
              const SwitchIdScopeResolver* resolver,
              facebook::fboss::RouterID vrf,
              const facebook::fboss::IPv4NetworkToRouteMap& v4NetworkToRoute,
              const facebook::fboss::IPv6NetworkToRouteMap& v6NetworkToRoute,
              const facebook::fboss::LabelToRouteMap& labelToRoute,
              const NextHopIDManager* nextHopIDManager,
              const MySidTable& mySidTable,
              void* cookie,
              bool vrfDeleted) {
            auto hwSwitch = static_cast<HwSwitch*>(cookie);
            auto oldState = hwSwitch->getProgrammedState();
            auto newState = hwSwitchFibUpdate(
                                resolver,
                                vrf,
                                v4NetworkToRoute,
                                v6NetworkToRoute,
                                labelToRoute,
                                nextHopIDManager,
                                mySidTable,
                                oldState,
                                vrfDeleted)
                                .newState();
            if (apply) {
              apply(StateDelta(oldState, newState));
            }
            return StateDelta(oldState, newState);
          },
          hw),
      hw_(hw) {}

AdminDistance HwSwitchRouteUpdateWrapper::clientIdToAdminDistance(
    ClientID clientId) const {
  auto config = hw_->getPlatform()->getConfig()->thrift.sw();
  return getAdminDistanceForClientId(
      config.value(), static_cast<int>(clientId));
}
} // namespace facebook::fboss
