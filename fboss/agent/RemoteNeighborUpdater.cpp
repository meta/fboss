// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/RemoteNeighborUpdater.h"

#include "fboss/agent/DsfStateUpdaterUtil.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

RemoteNeighborUpdater::RemoteNeighborUpdater(SwSwitch* sw) : sw_(sw) {
  sw_->registerStateObserver(this, "RemoteNeighborUpdater");
}

void RemoteNeighborUpdater::stateUpdated(const StateDelta& delta) {
  if (delta.getIntfsDelta().getOld() == delta.getIntfsDelta().getNew()) {
    /* no change */
    return;
  }
  if (!sw_->getScopeResolver()->hasVoq()) {
    XLOG(WARNING) << "No VOQ switches, ignoring interface update";
    return;
  }
  // isolated system port interfaces are treated as remote system port
  // interfaces, they exist on an another voq switch within the same fboss
  // switch
  auto voqScope = sw_->getScopeResolver()->scope(cfg::SwitchType::VOQ);
  if (voqScope.size() == 1) {
    XLOG(WARNING) << "No multiple VOQ switches, ignoring system ports update";
    return;
  }
  std::vector<InterfaceID> changedIntfs;
  DeltaFunctions::forEachChanged(
      delta.getIntfsDelta(),
      [&changedIntfs](auto /*oldNode*/, auto newNode) {
        if (newNode->getType() != cfg::InterfaceType::SYSTEM_PORT) {
          return;
        }
        if (newNode->getScope() != cfg::Scope::GLOBAL) {
          return;
        }
        changedIntfs.emplace_back(newNode->getID());
      },
      [&changedIntfs](auto newNode) {
        if (newNode->getScope() != cfg::Scope::GLOBAL) {
          return;
        }
        changedIntfs.emplace_back(newNode->getID());
      },
      [](auto /*oldNode*/) {
        // nothing to do. Remote interfaces are deleted as part of config
        // apply
      });

  if (changedIntfs.empty()) {
    // no change
    return;
  }
  // Capture by value: the update may run after this updater is destroyed
  // (SwSwitch::stop() resets it before draining the update queue).
  sw_->updateState(
      "update isolated system port interfaces",
      [sw = sw_, changedIntfs = std::move(changedIntfs)](
          const std::shared_ptr<SwitchState>& appliedState) {
        // apply on an applied state
        auto desiredState = appliedState->clone();
        for (const auto& intfID : changedIntfs) {
          auto newInterface = appliedState->getInterfaces()->getNodeIf(intfID);
          CHECK(newInterface);
          processChangedInterface(sw, &desiredState, newInterface);
        }
        return desiredState;
      });
}

void RemoteNeighborUpdater::processChangedInterface(
    SwSwitch* sw,
    std::shared_ptr<SwitchState>* state,
    const std::shared_ptr<Interface>& newInterface) {
  auto remoteInterfaces = (*state)->getRemoteInterfaces()->modify(state);
  auto systemPortID = SystemPortID(static_cast<int64_t>(newInterface->getID()));
  auto switchID = sw->getScopeResolver()->scope(systemPortID).switchId();
  auto scope = sw->getScopeResolver()->scope(cfg::SwitchType::VOQ);
  scope.exclude(switchID);
  auto innerMap = remoteInterfaces->getMapNodeIf(scope);
  CHECK(innerMap);
  innerMap = innerMap->clone();

  auto oldRemoteInterface = innerMap->getNodeIf(newInterface->getID());
  CHECK(oldRemoteInterface);
  auto newRemoteInterface = oldRemoteInterface->clone();
  newRemoteInterface->setArpTable(newInterface->getArpTable()->toThrift());
  newRemoteInterface->setNdpTable(newInterface->getNdpTable()->toThrift());
  DsfStateUpdaterUtil::updateNeighborEntry(
      oldRemoteInterface->getArpTable(), newRemoteInterface->getArpTable());
  DsfStateUpdaterUtil::updateNeighborEntry(
      oldRemoteInterface->getNdpTable(), newRemoteInterface->getNdpTable());

  innerMap->updateNode(newRemoteInterface);
  XLOG(DBG2) << "Updated isolated system port interface "
             << newRemoteInterface->getID() << " from "
             << scope.matcherString();
  remoteInterfaces->updateMapNode(innerMap, scope);
}
} // namespace facebook::fboss
