/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"

#include <folly/logging/xlog.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiSystemPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiVirtualRouterManager.h"
#include "fboss/agent/hw/sai/switch/SaiVlanManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

namespace {
HwRouterInterfaceStats fillHwRouterInterfaceStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& stats) {
  HwRouterInterfaceStats rifStats{};
  for (const auto& [id, value] : stats) {
    switch (id) {
      case SAI_ROUTER_INTERFACE_STAT_IN_OCTETS:
        rifStats.inBytes_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_IN_PACKETS:
        rifStats.inPkts_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_OUT_OCTETS:
        rifStats.outBytes_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_OUT_PACKETS:
        rifStats.outPkts_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_IN_ERROR_OCTETS:
        rifStats.inErrorBytes_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_IN_ERROR_PACKETS:
        rifStats.inErrorPkts_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_OUT_ERROR_OCTETS:
        rifStats.outErrorBytes_() = value;
        break;
      case SAI_ROUTER_INTERFACE_STAT_OUT_ERROR_PACKETS:
        rifStats.outErrorPkts_() = value;
        break;
    }
  }
  return rifStats;
}
} // namespace

SaiRouterInterfaceHandle::SaiRouterInterfaceHandle(cfg::InterfaceType type)
    : intfType(type) {}

SaiRouterInterfaceManager::SaiRouterInterfaceManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

RouterInterfaceSaiId SaiRouterInterfaceManager::addOrUpdateVlanRouterInterface(
    const std::shared_ptr<Interface>& swInterface,
    bool isLocal) {
  CHECK(swInterface->getType() == cfg::InterfaceType::VLAN);
  // compute the virtual router id for this router interface
  RouterID routerId = swInterface->getRouterID();
  SaiVirtualRouterHandle* virtualRouterHandle =
      managerTable_->virtualRouterManager().getVirtualRouterHandle(routerId);
  if (!virtualRouterHandle) {
    throw FbossError("No virtual router for RouterID ", routerId);
  }
  VirtualRouterSaiId saiVirtualRouterId{
      virtualRouterHandle->virtualRouter->adapterKey()};
  SaiVlanRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute{saiVirtualRouterId};

  SaiVlanRouterInterfaceTraits::Attributes::Type typeAttribute{
      SAI_ROUTER_INTERFACE_TYPE_VLAN};

  // compute the VLAN sai id for this router interface
  VlanID swVlanId = swInterface->getVlanID();
  SaiVlanHandle* saiVlanHandle =
      managerTable_->vlanManager().getVlanHandle(swVlanId);
  if (!saiVlanHandle) {
    throw FbossError(
        "Failed to add router interface: no sai vlan for VlanID ", swVlanId);
  }
  SaiVlanRouterInterfaceTraits::Attributes::VlanId vlanIdAttribute{
      saiVlanHandle->vlan->adapterKey()};

  // get the src mac for this router interface
  folly::MacAddress srcMac = swInterface->getMac();
  SaiVlanRouterInterfaceTraits::Attributes::SrcMac srcMacAttribute{srcMac};

  // get MTU
  std::optional<SaiVlanRouterInterfaceTraits::Attributes::Mtu> mtuAttribute =
      std::nullopt;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::L3_INTF_MTU)) {
    mtuAttribute = SaiVlanRouterInterfaceTraits::Attributes::Mtu(
        static_cast<uint32_t>(swInterface->getMtu()));
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  // Ebro (Cisco/leaba) disables MPLS on a router interface by default. Enable
  // it explicitly so MPLS traffic is routed on the RIF.
  std::optional<SaiVlanRouterInterfaceTraits::Attributes::AdminMplsState>
      adminMplsStateAttribute{std::nullopt};
  if (platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_EBRO ||
      platform_->getAsic()->getAsicType() ==
          cfg::AsicType::ASIC_TYPE_TOMAHAWK6 ||
      platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_P200) {
    adminMplsStateAttribute =
        SaiVlanRouterInterfaceTraits::Attributes::AdminMplsState{true};
  }
#endif

  // create the router interface
  SaiVlanRouterInterfaceTraits::CreateAttributes attributes{
      virtualRouterIdAttribute,
      typeAttribute,
      vlanIdAttribute,
      srcMacAttribute,
      mtuAttribute
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      ,
      adminMplsStateAttribute
#endif
  };
  SaiVlanRouterInterfaceTraits::AdapterHostKey k{
      virtualRouterIdAttribute,
      vlanIdAttribute,
  };
  auto& store = saiStore_->get<SaiVlanRouterInterfaceTraits>();
  std::shared_ptr<SaiVlanRouterInterface> vlanRouterInterface =
      store.setObject(k, attributes, swInterface->getID());
  auto vlanRouterInterfaceHandle =
      std::make_unique<SaiRouterInterfaceHandle>(swInterface->getType());
  vlanRouterInterfaceHandle->routerInterface = vlanRouterInterface;
  vlanRouterInterfaceHandle->setLocal(isLocal);

  if (isLocal) {
    // create the ToMe routes for this (local) router interface
    auto toMeRoutes =
        managerTable_->routeManager().makeInterfaceToMeRoutes(swInterface);
    vlanRouterInterfaceHandle->toMeRoutes = std::move(toMeRoutes);
  }

  handles_[swInterface->getID()] = std::move(vlanRouterInterfaceHandle);
  return vlanRouterInterface->adapterKey();
}

RouterInterfaceSaiId SaiRouterInterfaceManager::addOrUpdatePortRouterInterface(
    const std::shared_ptr<Interface>& swInterface,
    bool isLocal) {
  CHECK(
      swInterface->getType() == cfg::InterfaceType::SYSTEM_PORT ||
      swInterface->getType() == cfg::InterfaceType::PORT);
  // compute the virtual router id for this router interface
  RouterID routerId = swInterface->getRouterID();
  SaiVirtualRouterHandle* virtualRouterHandle =
      managerTable_->virtualRouterManager().getVirtualRouterHandle(routerId);
  if (!virtualRouterHandle) {
    throw FbossError("No virtual router for RouterID ", routerId);
  }
  VirtualRouterSaiId saiVirtualRouterId{
      virtualRouterHandle->virtualRouter->adapterKey()};
  SaiPortRouterInterfaceTraits::Attributes::VirtualRouterId
      virtualRouterIdAttribute{saiVirtualRouterId};

  SaiPortRouterInterfaceTraits::Attributes::Type typeAttribute{
      SAI_ROUTER_INTERFACE_TYPE_PORT};

  // get the src mac for this router interface
  folly::MacAddress srcMac = swInterface->getMac();
  SaiPortRouterInterfaceTraits::Attributes::SrcMac srcMacAttribute{srcMac};

  // get MTU
  std::optional<SaiVlanRouterInterfaceTraits::Attributes::Mtu> mtuAttribute =
      std::nullopt;
  if (platform_->getAsic()->isSupported(HwAsic::Feature::L3_INTF_MTU)) {
    mtuAttribute = SaiVlanRouterInterfaceTraits::Attributes::Mtu(
        static_cast<uint32_t>(swInterface->getMtu()));
  }

  // SAI has no LAG specific router interface type: a router interface over an
  // aggregate port is a port router interface whose PortId is a LAG object id.
  SaiPortRouterInterfaceTraits::Attributes::PortId portIdAttribute =
      swInterface->getType() == cfg::InterfaceType::SYSTEM_PORT
      ? getSystemPortId(swInterface)
      : (swInterface->getAggregatePortIDf() ? getAggregatePortId(swInterface)
                                            : getPortId(swInterface));

#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  // SAI_ROUTER_INTERFACE_ATTR_ADMIN_MPLS_STATE defaults to false, so MPLS must
  // be enabled explicitly on a RIF that routes MPLS.
  //
  // TODO: Evaluate whether enabling MPLS routing has any overhead or downside
  // on an interface that will never carry MPLS traffic.
  std::optional<SaiPortRouterInterfaceTraits::Attributes::AdminMplsState>
      adminMplsStateAttribute{std::nullopt};
  if (platform_->getAsic()->getAsicType() == cfg::AsicType::ASIC_TYPE_P200) {
    adminMplsStateAttribute =
        SaiPortRouterInterfaceTraits::Attributes::AdminMplsState{true};
  }
#endif

  // create the router interface
  SaiPortRouterInterfaceTraits::CreateAttributes attributes{
      virtualRouterIdAttribute,
      typeAttribute,
      portIdAttribute,
      srcMacAttribute,
      mtuAttribute
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      ,
      adminMplsStateAttribute
#endif
  };
  SaiPortRouterInterfaceTraits::AdapterHostKey k{
      virtualRouterIdAttribute,
      portIdAttribute,
  };
  auto& store = saiStore_->get<SaiPortRouterInterfaceTraits>();
  std::shared_ptr<SaiPortRouterInterface> portRouterInterface =
      store.setObject(k, attributes, swInterface->getID());
  auto portRouterInterfaceHandle =
      std::make_unique<SaiRouterInterfaceHandle>(swInterface->getType());
  portRouterInterfaceHandle->routerInterface = portRouterInterface;
  portRouterInterfaceHandle->setLocal(isLocal);
  if (isLocal) {
    // create the ToMe routes for this (local) router interface
    auto toMeRoutes =
        managerTable_->routeManager().makeInterfaceToMeRoutes(swInterface);
    portRouterInterfaceHandle->toMeRoutes = std::move(toMeRoutes);
  }

  handles_[swInterface->getID()] = std::move(portRouterInterfaceHandle);

  // Initialize stats if needed.
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ROUTER_INTERFACE_STATISTICS) &&
      swInterface->getType() == cfg::InterfaceType::PORT &&
      !rifStats_.contains(swInterface->getID())) {
    rifStats_.emplace(
        swInterface->getID(),
        std::make_unique<HwRouterInterfaceFb303Stats>(swInterface->getName()));
  }

  return portRouterInterface->adapterKey();
}

void SaiRouterInterfaceManager::removeRouterInterface(
    const std::shared_ptr<Interface>& swInterface) {
  auto swId = swInterface->getID();
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    throw FbossError("Failed to remove non-existent router interface: ", swId);
  }
  handles_.erase(itr);
}

RouterInterfaceSaiId SaiRouterInterfaceManager::addOrUpdateRouterInterface(
    const std::shared_ptr<Interface>& swInterface,
    bool isLocal) {
  switch (swInterface->getType()) {
    case cfg::InterfaceType::VLAN:
      return addOrUpdateVlanRouterInterface(swInterface, isLocal);
    case cfg::InterfaceType::PORT:
    case cfg::InterfaceType::SYSTEM_PORT:
      return addOrUpdatePortRouterInterface(swInterface, isLocal);
  }
  // Should never get here
  throw FbossError("Unknown interface type: ", swInterface->getType());
}

RouterInterfaceSaiId SaiRouterInterfaceManager::addRouterInterface(
    const std::shared_ptr<Interface>& swInterface,
    bool isLocal) {
  // check if the router interface already exists
  InterfaceID swId(swInterface->getID());
  auto handle = getRouterInterfaceHandle(swId);
  if (handle) {
    throw FbossError(
        "Attempted to add duplicate router interface with InterfaceID ", swId);
  }
  return addOrUpdateRouterInterface(swInterface, isLocal);
}

void SaiRouterInterfaceManager::changeRouterInterface(
    const std::shared_ptr<Interface>& oldInterface,
    const std::shared_ptr<Interface>& newInterface,
    bool isLocal) {
  CHECK_EQ(oldInterface->getID(), newInterface->getID());
  InterfaceID swId(newInterface->getID());
  auto handle = getRouterInterfaceHandle(swId);
  if (!handle) {
    throw FbossError(
        "Attempted to change non existent router interface with InterfaceID ",
        swId);
  }
  addOrUpdateRouterInterface(newInterface, isLocal);
}

SaiRouterInterfaceHandle* SaiRouterInterfaceManager::getRouterInterfaceHandle(
    const InterfaceID& swId) {
  return getRouterInterfaceHandleImpl(swId);
}

const SaiRouterInterfaceHandle*
SaiRouterInterfaceManager::getRouterInterfaceHandle(
    const InterfaceID& swId) const {
  return getRouterInterfaceHandleImpl(swId);
}

SaiRouterInterfaceHandle*
SaiRouterInterfaceManager::getRouterInterfaceHandleImpl(
    const InterfaceID& swId) const {
  auto itr = handles_.find(swId);
  if (itr == handles_.end()) {
    return nullptr;
  }
  if (!itr->second) {
    XLOG(FATAL) << "Invalid null router interface for InterfaceID " << swId;
  }
  return itr->second.get();
}

SaiPortRouterInterfaceTraits::Attributes::PortId
SaiRouterInterfaceManager::getSystemPortId(
    const std::shared_ptr<Interface>& swInterface) {
  CHECK(swInterface->getType() == cfg::InterfaceType::SYSTEM_PORT);
  // compute the system port sai id for this router interface
  SystemPortID swSystemPortId{swInterface->getID()};
  SaiSystemPortHandle* saiSystemPortHandle =
      managerTable_->systemPortManager().getSystemPortHandle(swSystemPortId);
  if (!saiSystemPortHandle) {
    throw FbossError(
        "Failed to add router interface: no sai system port for ID ",
        swSystemPortId);
  }
  return SaiPortRouterInterfaceTraits::Attributes::PortId{
      saiSystemPortHandle->systemPort->adapterKey()};
}

SaiPortRouterInterfaceTraits::Attributes::PortId
SaiRouterInterfaceManager::getPortId(
    const std::shared_ptr<Interface>& swInterface) {
  CHECK(swInterface->getType() == cfg::InterfaceType::PORT);
  PortID portID(swInterface->getPortID());
  auto* portHandle = managerTable_->portManager().getPortHandle(portID);
  if (!portHandle) {
    throw FbossError("Failed to add router interface: no sai port ID ", portID);
  }
  return SaiPortRouterInterfaceTraits::Attributes::PortId{
      portHandle->port->adapterKey()};
}

SaiPortRouterInterfaceTraits::Attributes::PortId
SaiRouterInterfaceManager::getAggregatePortId(
    const std::shared_ptr<Interface>& swInterface) {
  CHECK(swInterface->getType() == cfg::InterfaceType::PORT);
  AggregatePortID aggregatePortID(swInterface->getAggregatePortID());
  auto* lagHandle = managerTable_->lagManager().getLagHandleIf(aggregatePortID);
  if (!lagHandle) {
    throw FbossError(
        "Failed to add router interface: no sai lag for aggregate port ",
        aggregatePortID);
  }
  return SaiPortRouterInterfaceTraits::Attributes::PortId{
      lagHandle->lag->adapterKey()};
}

std::optional<InterfaceID>
SaiRouterInterfaceManager::getRouterPortInterfaceIDIf(PortID port) const {
  auto portHandle = managerTable_->portManager().getPortHandle(port);
  if (!portHandle) {
    return std::nullopt;
  }
  return getRouterPortInterfaceIDIf(portHandle->port->adapterKey());
}

std::optional<InterfaceID>
SaiRouterInterfaceManager::getRouterPortInterfaceIDIf(PortSaiId port) const {
  auto itr =
      std::find_if(handles_.begin(), handles_.end(), [port](const auto& entry) {
        const auto& intfHandle = entry.second;
        if (intfHandle->type() != cfg::InterfaceType::PORT) {
          return false;
        }
        auto intf = intfHandle->getPortRouterInterface();
        auto attributes = intf->attributes();
        return std::get<SaiPortRouterInterfaceTraits::Attributes::PortId>(
                   attributes)
                   .value() == port;
      });

  if (itr == handles_.end()) {
    return std::nullopt;
  }

  return itr->first;
}

void SaiRouterInterfaceManager::updateStats() {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::ROUTER_INTERFACE_STATISTICS)) {
    return;
  }
  auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch());

  for (auto& [intfId, handle] : handles_) {
    std::visit(
        [interfaceId = intfId, rifHandle = handle.get(), now, this](auto& rif) {
          rif->updateStats();

          if (auto it = rifStats_.find(interfaceId); it != rifStats_.end()) {
            it->second->updateStats(
                fillHwRouterInterfaceStats(rif->getStats()), now);
          }
        },
        handle->routerInterface);
  }
}

void SaiRouterInterfaceManager::clearStats() {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::ROUTER_INTERFACE_STATISTICS)) {
    return;
  }
  for (auto& [_, handle] : handles_) {
    std::visit(
        [](auto& rif) { return rif->clearStats(); }, handle->routerInterface);
  }
}

std::map<InterfaceID, HwRouterInterfaceStats>
SaiRouterInterfaceManager::getRouterInterfaceStats() const {
  std::map<InterfaceID, HwRouterInterfaceStats> out{};
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::ROUTER_INTERFACE_STATISTICS)) {
    return out;
  }
  for (const auto& [_, handle] : handles_) {
    auto stats = std::visit(
        [](const auto& rif) { return rif->getStats(); },
        handle->routerInterface);

    out.emplace(_, fillHwRouterInterfaceStats(stats));
  }
  return out;
}

HwRouterInterfaceStats SaiRouterInterfaceManager::getRouterInterfaceStats(
    const InterfaceID& intfID) const {
  if (!platform_->getAsic()->isSupported(
          HwAsic::Feature::ROUTER_INTERFACE_STATISTICS)) {
    return HwRouterInterfaceStats{};
  }
  auto stats = getRouterInterfaceStats();
  auto iter = stats.find(intfID);
  if (iter != stats.end()) {
    return iter->second;
  }
  XLOG(DBG4) << "No stats found for interface " << intfID;
  return HwRouterInterfaceStats{};
}
} // namespace facebook::fboss
