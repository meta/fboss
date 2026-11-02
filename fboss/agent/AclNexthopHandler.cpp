// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/AclNexthopHandler.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/AclEntry.h"
#include "fboss/agent/state/AclMap.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

using DeltaFunctions::isEmpty;

AclNexthopHandler::AclNexthopHandler(SwSwitch* sw) : sw_(sw) {
  sw_->registerStateObserver(this, "AclNexthopHandler");
}
AclNexthopHandler::~AclNexthopHandler() {
  sw_->unregisterStateObserver(this);
}
bool AclNexthopHandler::hasAclChanges(const StateDelta& delta) {
  bool aclsChanged = (sw_->getState()->getAcls()->numNodes() > 0) &&
      (!isEmpty(delta.getAclsDelta()));

  // Check for FIB map changes within FibsInfo delta
  // This is to keep the same behavior as before, where we only check for
  // FibsMap delta. In the future, FibsInfo will have more fields which we
  // should exclude from this check, hence we only check for FibsMap delta.
  bool fibsMapChanged = false;
  for (const auto& fibInfoDelta : delta.getFibsInfoDelta()) {
    if (!isEmpty(fibInfoDelta.getFibsMapDelta())) {
      fibsMapChanged = true;
      break;
    }
  }

  aclsChanged =
      (aclsChanged || fibsMapChanged ||
       !isEmpty(delta.getLabelForwardingInformationBaseDelta()));
  XLOG(DBG2) << "aclsChanged: " << aclsChanged;
  return aclsChanged;
}

void AclNexthopHandler::stateUpdated(const StateDelta& delta) {
  if (!hasAclChanges(delta)) {
    return;
  }

  auto updateAclsFn = [this](const std::shared_ptr<SwitchState>& state) {
    return handleUpdate(state);
  };
  sw_->updateState("Updating ACLs", updateAclsFn);
}

std::shared_ptr<SwitchState> AclNexthopHandler::handleUpdate(
    const std::shared_ptr<SwitchState>& state) {
  auto newState = state->clone();
  if (!updateAcls(newState)) {
    return std::shared_ptr<SwitchState>(nullptr);
  }
  return newState;
}

void AclNexthopHandler::resolveActionNexthops(MatchAction& action) {
  RouteNextHopSet nexthops;
  const auto& redirect = action.getRedirectToNextHop();

  bool hasTunnelNexthop = false;
  bool hasNonTunnelNexthop = false;
  for (const auto& nh : *redirect.value().first.redirectNextHops()) {
    if (nh.tunnelType().has_value() &&
        nh.tunnelType().value() == TunnelType::IP_IN_IP_ENCAP) {
      hasTunnelNexthop = true;
    } else {
      hasNonTunnelNexthop = true;
    }
  }
  if (hasTunnelNexthop && hasNonTunnelNexthop) {
    throw FbossError(
        "Redirect must not mix tunnel encap and non-tunnel nexthops");
  }

  for (auto& nhIpStruct : *redirect.value().first.redirectNextHops()) {
    auto nhIp = folly::IPAddress(*nhIpStruct.ip());
    auto intfID = nhIpStruct.intfID();
    // Link-local nexthops are only meaningful when scoped to an interface
    // and are inherently resolved (directly connected), so skip RIB lookup
    // and use the configured address + interface directly.
    if (nhIp.isV6() && nhIp.isLinkLocal()) {
      if (!intfID.has_value()) {
        throw FbossError(
            "Link-local nexthop ", nhIp.str(), " must specify an interface");
      }
      nexthops.emplace(ResolvedNextHop(
          nhIp, InterfaceID(intfID.value()), UCMP_DEFAULT_WEIGHT));
      continue;
    }
    if (intfID.has_value()) {
      throw FbossError(
          "Non-link-local nexthop ",
          nhIp.str(),
          " must not specify an interface");
    }
    if (nhIp.isV4()) {
      auto result =
          sw_->getRib()->getRouteAndNextHops(nhIp.asV4(), RouterID(0));
      if (!result || !result->first->isResolved()) {
        continue;
      }
      nexthops.merge(std::move(result->second));
    } else {
      auto result =
          sw_->getRib()->getRouteAndNextHops(nhIp.asV6(), RouterID(0));
      if (!result || !result->first->isResolved()) {
        continue;
      }
      nexthops.merge(std::move(result->second));
    }
  }
  action.setRedirectToNextHop(std::make_pair(redirect.value().first, nexthops));
}

std::shared_ptr<MultiSwitchAclMap> AclNexthopHandler::updateAcls(
    std::shared_ptr<SwitchState>& newState) {
  bool changed = false;
  auto origmultiSwitchAcls = newState->getAcls();
  for (const auto& mIter : std::as_const(*origmultiSwitchAcls)) {
    auto origAcls = mIter.second;
    for (const auto& iter : std::as_const(*origAcls)) {
      if (updateAcl(iter.second, newState)) {
        changed = true;
      }
    }
  }
  if (!changed) {
    return nullptr;
  }
  return newState->getAcls();
}

AclEntry* FOLLY_NULLABLE AclNexthopHandler::updateAcl(
    const std::shared_ptr<AclEntry>& origAclEntry,
    std::shared_ptr<SwitchState>& newState) {
  if (!needsAclResolution(origAclEntry)) {
    return nullptr;
  }
  const auto& origAclAction = origAclEntry->getAclAction();
  auto newAclEntry = origAclEntry->modify(
      &newState, sw_->getScopeResolver()->scope(origAclEntry));
  newAclEntry->setEnabled(true);

  // THRIFT_COPY
  MatchAction action =
      MatchAction::fromThrift(newAclEntry->getAclAction()->toThrift());
  resolveActionNexthops(action);
  newAclEntry->setAclAction(action);
  // Disable acl if there are no nexthops available
  if (action.getRedirectToNextHop().has_value() &&
      !action.getRedirectToNextHop().value().second.size()) {
    XLOG(DBG2) << "Disabling acl as no resolved nexthops are available";
    newAclEntry->setEnabled(false);
  }
  const auto& newAclAction = newAclEntry->getAclAction();
  return ((newAclAction->cref<switch_state_tags::redirectToNextHop>()
               ->toThrift() !=
           origAclAction->cref<switch_state_tags::redirectToNextHop>()
               ->toThrift()) ||
          (newAclEntry->isEnabled() != origAclEntry->isEnabled()))
      ? newAclEntry
      : nullptr;
}

bool AclNexthopHandler::needsAclResolution(
    const std::shared_ptr<AclEntry>& aclEntry) {
  const auto& aclAction = aclEntry->getAclAction();
  if (!aclAction || !aclAction->cref<switch_state_tags::redirectToNextHop>()) {
    return false;
  }
  // Tunnel encap nexthops are resolved by the HW pipeline via a second
  // routing lookup on the outer header, so only non-tunnel nexthops need
  // RIB resolution.
  auto redirectNhops = aclAction->cref<switch_state_tags::redirectToNextHop>()
                           ->cref<switch_state_tags::action>()
                           ->cref<switch_config_tags::redirectNextHops>();
  for (const auto& nh : std::as_const(*redirectNhops)) {
    const auto& tunnelType = nh->cref<switch_config_tags::tunnelType>();
    if (!tunnelType.has_value() ||
        tunnelType->toThrift() != TunnelType::IP_IN_IP_ENCAP) {
      return true;
    }
  }
  return false;
}

} // namespace facebook::fboss
