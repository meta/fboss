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

#include "fboss/agent/hw/HwRouterInterfaceFb303Stats.h"
#include "fboss/agent/hw/sai/api/RouterInterfaceApi.h"
#include "fboss/agent/hw/sai/store/SaiObjectWithCounters.h"
#include "fboss/agent/hw/sai/switch/SaiRouteManager.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/InterfaceVrfMap.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/types.h"

#include "folly/MacAddress.h"

#include "folly/Overload.h"
#include "folly/container/F14Map.h"

#include <memory>
#include <mutex>
#include <variant>
#include <vector>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

using SaiVlanRouterInterface =
    SaiObjectWithCounters<SaiVlanRouterInterfaceTraits>;
using SaiPortRouterInterface =
    SaiObjectWithCounters<SaiPortRouterInterfaceTraits>;

struct SaiRouterInterfaceHandle {
  using SaiRouterInterface = std::variant<
      std::shared_ptr<SaiVlanRouterInterface>,
      std::shared_ptr<SaiPortRouterInterface>>;
  explicit SaiRouterInterfaceHandle(cfg::InterfaceType type);

  RouterInterfaceSaiId adapterKey() const {
    return std::visit(
        [](auto& handle) { return handle->adapterKey(); }, routerInterface);
  }
  cfg::InterfaceType type() const {
    return intfType;
  }
  void setLocal(bool isLocal) {
    isLocalRif = isLocal;
  }
  bool isLocal() const {
    return isLocalRif;
  }
  std::shared_ptr<SaiPortRouterInterface> getPortRouterInterface() {
    return std::get<std::shared_ptr<SaiPortRouterInterface>>(routerInterface);
  }

  std::shared_ptr<SaiVlanRouterInterface> getVlanRouterInterface() {
    return std::get<std::shared_ptr<SaiVlanRouterInterface>>(routerInterface);
  }

  cfg::InterfaceType intfType{cfg::InterfaceType::VLAN};
  bool isLocalRif{true};
  RouterID vrf{0};
  SaiRouterInterface routerInterface{};
  std::vector<std::shared_ptr<SaiRoute>> toMeRoutes{};
};

class SaiRouterInterfaceManager {
 public:
  SaiRouterInterfaceManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  // A local interface's VRF comes from the interface VRF map
  RouterInterfaceSaiId addLocalRouterInterface(
      const std::shared_ptr<Interface>& swInterface,
      const std::shared_ptr<MultiSwitchInterfaceVrfMap>& interfaceVrfs) {
    return addRouterInterface(
        swInterface,
        true /*isLocal*/,
        interfaceVrfs->getVrf(swInterface->getID()));
  }
  void removeLocalRouterInterface(
      const std::shared_ptr<Interface>& swInterface) {
    removeRouterInterface(swInterface);
  }
  void changeLocalRouterInterface(
      const std::shared_ptr<Interface>& oldInterface,
      const std::shared_ptr<Interface>& newInterface,
      const std::shared_ptr<MultiSwitchInterfaceVrfMap>& interfaceVrfs) {
    changeRouterInterface(
        oldInterface,
        newInterface,
        true /*isLocal*/,
        interfaceVrfs->getVrf(newInterface->getID()));
  }

  // Interface VRF map changes move local router interfaces
  void changeInterfaceVrf(
      const std::shared_ptr<InterfaceVrf>& /* oldInterfaceVrf */,
      const std::shared_ptr<InterfaceVrf>& newInterfaceVrf,
      const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces) {
    moveRouterInterface(
        newInterfaceVrf->getID(), newInterfaceVrf->getVrf(), interfaces);
  }
  void addInterfaceVrf(
      const std::shared_ptr<InterfaceVrf>& interfaceVrf,
      const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces) {
    moveRouterInterface(
        interfaceVrf->getID(), interfaceVrf->getVrf(), interfaces);
  }
  void removeInterfaceVrf(
      const std::shared_ptr<InterfaceVrf>& interfaceVrf,
      const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces) {
    moveRouterInterface(interfaceVrf->getID(), RouterID(0), interfaces);
  }

  RouterInterfaceSaiId addRemoteRouterInterface(
      const std::shared_ptr<Interface>& swInterface) {
    return addRouterInterface(
        swInterface, false /*isLocal*/, swInterface->getRemoteIntfRouterID());
  }
  void removeRemoteRouterInterface(
      const std::shared_ptr<Interface>& swInterface) {
    removeRouterInterface(swInterface);
  }
  void changeRemoteRouterInterface(
      const std::shared_ptr<Interface>& oldInterface,
      const std::shared_ptr<Interface>& newInterface) {
    changeRouterInterface(
        oldInterface,
        newInterface,
        false /*isLocal*/,
        newInterface->getRemoteIntfRouterID());
  }

  SaiRouterInterfaceHandle* getRouterInterfaceHandle(const InterfaceID& swId);
  const SaiRouterInterfaceHandle* getRouterInterfaceHandle(
      const InterfaceID& swId) const;

  std::optional<InterfaceID> getRouterPortInterfaceIDIf(PortID port) const;

  std::optional<InterfaceID> getRouterPortInterfaceIDIf(PortSaiId port) const;

  void updateStats();

  void clearStats();

  std::map<InterfaceID, HwRouterInterfaceStats> getRouterInterfaceStats() const;

  HwRouterInterfaceStats getRouterInterfaceStats(
      const InterfaceID& intfID) const;

 private:
  RouterInterfaceSaiId addRouterInterface(
      const std::shared_ptr<Interface>& swInterface,
      bool isLocal,
      RouterID vrf);
  void removeRouterInterface(const std::shared_ptr<Interface>& swInterface);
  void changeRouterInterface(
      const std::shared_ptr<Interface>& oldInterface,
      const std::shared_ptr<Interface>& newInterface,
      bool isLocal,
      RouterID newVrf);
  void moveRouterInterface(
      InterfaceID intfId,
      RouterID vrf,
      const std::shared_ptr<MultiSwitchInterfaceMap>& interfaces);
  RouterInterfaceSaiId addOrUpdateRouterInterface(
      const std::shared_ptr<Interface>& swInterface,
      bool isLocal,
      RouterID vrf);
  RouterInterfaceSaiId addOrUpdateVlanRouterInterface(
      const std::shared_ptr<Interface>& swInterface,
      bool isLocal,
      RouterID vrf);
  RouterInterfaceSaiId addOrUpdatePortRouterInterface(
      const std::shared_ptr<Interface>& swInterface,
      bool isLocal,
      RouterID vrf);
  SaiRouterInterfaceHandle* getRouterInterfaceHandleImpl(
      const InterfaceID& swId) const;
  SaiPortRouterInterfaceTraits::Attributes::PortId getPortId(
      const std::shared_ptr<Interface>& swInterface);
  SaiPortRouterInterfaceTraits::Attributes::PortId getAggregatePortId(
      const std::shared_ptr<Interface>& swInterface);
  SaiPortRouterInterfaceTraits::Attributes::PortId getSystemPortId(
      const std::shared_ptr<Interface>& swInterface);

  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
  folly::F14FastMap<InterfaceID, std::unique_ptr<SaiRouterInterfaceHandle>>
      handles_;
  folly::F14FastMap<InterfaceID, std::unique_ptr<HwRouterInterfaceFb303Stats>>
      rifStats_;
};

} // namespace facebook::fboss
