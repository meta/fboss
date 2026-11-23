// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/ShelManager.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

std::vector<StateDelta> ShelManager::modifyState(
    const std::vector<StateDelta>& deltas) {
  return modifyStateImpl(deltas);
}

std::vector<StateDelta> ShelManager::reconstructFromSwitchState(
    const std::shared_ptr<SwitchState>& curState) {
  StopWatch timeIt("ShelManager::reconstructFromSwitchState", false /*json*/);
  XLOG(DBG2) << "ShelManager reconstructing from switch state";
  intf2RefCnt_.wlock()->clear();
  std::vector<StateDelta> deltas;
  deltas.emplace_back(std::make_shared<SwitchState>(), curState);
  return modifyStateImpl(deltas);
}

void ShelManager::updateDone() {
  XLOG(DBG2) << "ShelManager Update done";
}

void ShelManager::updateFailed(const std::shared_ptr<SwitchState>& curState) {
  XLOG(DBG2) << "ShelManager Update failed";
  reconstructFromSwitchState(curState);
}

bool ShelManager::ecmpOverShelDisabledPort(
    const std::map<int, cfg::PortState>& sysPortShelState) {
  for (const auto& [sysPortId, portState] : sysPortShelState) {
    if (portState == cfg::PortState::DISABLED) {
      auto lockedMap = intf2RefCnt_.rlock();
      if (lockedMap->contains(static_cast<InterfaceID>(sysPortId))) {
        return true;
      }
    }
  }
  return false;
}

void ShelManager::updateRefCount(
    const RouteNextHopEntry::NextHopSet& routeNhops,
    const std::shared_ptr<SwitchState>& origState,
    bool add) {
  for (const auto& nhop : routeNhops) {
    if (nhop.intfID().has_value()) {
      auto lockedMap = intf2RefCnt_.wlock();
      auto iter = lockedMap->find(nhop.intf());
      if (add) {
        if (iter == lockedMap->end()) {
          lockedMap->insert_or_assign(nhop.intf(), 1);
        } else {
          iter->second++;
        }
      } else {
        CHECK(iter != lockedMap->end() && iter->second > 0);
        iter->second--;
        if (iter->second == 0) {
          lockedMap->erase(iter);
        }
      }
    }
  }
}

template <typename AddrT>
void ShelManager::routeAdded(
    RouterID rid,
    const std::shared_ptr<Route<AddrT>>& newRoute,
    const std::shared_ptr<SwitchState>& origState) {
  CHECK_EQ(rid, RouterID(0));
  CHECK(newRoute->isResolved());
  CHECK(newRoute->isPublished());
  const auto routeNhops =
      getNormalizedNextHops(origState, newRoute->getForwardInfo());
  if (routeNhops.size() > 1) {
    updateRefCount(routeNhops, origState, true /*add*/);
  }
}

template <typename AddrT>
void ShelManager::routeDeleted(
    RouterID rid,
    const std::shared_ptr<Route<AddrT>>& removedRoute,
    const std::shared_ptr<SwitchState>& origState) {
  CHECK_EQ(rid, RouterID(0));
  CHECK(removedRoute->isResolved());
  CHECK(removedRoute->isPublished());
  const auto routeNhops =
      getNormalizedNextHops(origState, removedRoute->getForwardInfo());
  if (routeNhops.size() > 1) {
    // CAUTION: origState is delta.oldState() here. This is okay since state is
    // unused in updateRefCount
    updateRefCount(routeNhops, origState, false /*add*/);
  }
}

void ShelManager::processRouteUpdates(const StateDelta& delta) {
  processFibsDeltaInHwSwitchOrder(
      delta,
      [this, &delta](RouterID rid, const auto& oldRoute, const auto& newRoute) {
        if (!oldRoute->isResolved() && !newRoute->isResolved()) {
          return;
        }
        if (oldRoute->isResolved() && !newRoute->isResolved()) {
          routeDeleted(rid, oldRoute, delta.oldState());
          return;
        }
        if (!oldRoute->isResolved() && newRoute->isResolved()) {
          routeAdded(rid, newRoute, delta.newState());
          return;
        }
        // Both old and new are resolved
        CHECK(oldRoute->isResolved() && newRoute->isResolved());
        routeDeleted(rid, oldRoute, delta.oldState());
        routeAdded(rid, newRoute, delta.newState());
      },
      [this, &delta](RouterID rid, const auto& newRoute) {
        if (newRoute->isResolved()) {
          routeAdded(rid, newRoute, delta.newState());
        }
      },
      [this, &delta](RouterID rid, const auto& oldRoute) {
        if (oldRoute->isResolved()) {
          routeDeleted(rid, oldRoute, delta.oldState());
        }
      });
}

std::shared_ptr<SwitchState> ShelManager::processDelta(
    const StateDelta& delta) {
  auto beforeIntf2RefCnt = folly::copy(*intf2RefCnt_.rlock());
  processRouteUpdates(delta);
  auto modifiedState = delta.newState()->clone();

  auto processIntfDiff = [&](const auto& fromMap,
                             const auto& toMap,
                             bool enable) {
    for (const auto& [intf, _] : toMap) {
      // Only enable SHEL for local system ports
      if (!fromMap.contains(intf) &&
          delta.newState()->getSystemPorts()->getNodeIf(SystemPortID(intf))) {
        auto portId =
            getPortID(static_cast<SystemPortID>(intf), delta.newState());
        auto port = modifiedState->getPorts()->getNodeIf(portId);
        CHECK(port);
        auto desiredShelState = port->getDesiredSelfHealingECMPLagEnable();
        if (desiredShelState.has_value() && desiredShelState.value()) {
          auto newPort = port->modify(&modifiedState);
          newPort->setSelfHealingECMPLagEnable(enable);
        }
      }
    }
  };

  auto lockedMap = intf2RefCnt_.rlock();
  processIntfDiff(beforeIntf2RefCnt, *lockedMap, true);
  processIntfDiff(*lockedMap, beforeIntf2RefCnt, false);
  modifiedState->publish();
  return modifiedState;
}

std::vector<StateDelta> ShelManager::modifyStateImpl(
    const std::vector<StateDelta>& deltas) {
  StopWatch timeIt("ShelManager::modifyStateImpl", false /*json*/);
  std::vector<StateDelta> retDeltas;
  retDeltas.reserve(deltas.size());
  auto oldState = deltas.begin()->oldState();
  for (const auto& delta : deltas) {
    auto modifiedState = processDelta(StateDelta(oldState, delta.newState()));
    retDeltas.emplace_back(oldState, modifiedState);
    oldState = modifiedState;
  }
  return retDeltas;
}

} // namespace facebook::fboss
