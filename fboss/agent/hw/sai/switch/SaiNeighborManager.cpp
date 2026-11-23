/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiNeighborManager.h"
#include "fboss/agent/hw/sai/api/NeighborApi.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiBridgeManager.h"
#include "fboss/agent/hw/sai/switch/SaiLagManager.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiNextHopGroupManager.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiRouterInterfaceManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"
#include "fboss/agent/hw/sai/switch/SaiSystemPortManager.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/platforms/sai/SaiPlatform.h"
#include "fboss/agent/state/ArpEntry.h"
#include "fboss/agent/state/NdpEntry.h"
#include "folly/IPAddress.h"

#include <folly/ScopeGuard.h>

#include <utility>

namespace facebook::fboss {

SaiNeighborManager::SaiNeighborManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {}

// Helper function to create a SAI NeighborEntry from an FBOSS SwitchState
// NeighborEntry (e.g., NeighborEntry<IPAddressV6, NDPTable>)
template <typename NeighborEntryT>
SaiNeighborTraits::NeighborEntry SaiNeighborManager::saiEntryFromSwEntry(
    const std::shared_ptr<NeighborEntryT>& swEntry) {
  folly::IPAddress ip(swEntry->getIP());
  SaiRouterInterfaceHandle* routerInterfaceHandle =
      managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
          swEntry->getIntfID());
  if (!routerInterfaceHandle) {
    throw FbossError(
        "Failed to create sai_neighbor_entry from NeighborEntry. "
        "No SaiRouterInterface for InterfaceID: ",
        swEntry->getIntfID());
  }
  return SaiNeighborTraits::NeighborEntry(
      getSwitchSaiId(), routerInterfaceHandle->adapterKey(), ip);
}

SwitchSaiId SaiNeighborManager::getSwitchSaiId() const {
  return managerTable_->switchManager().getSwitchSaiId();
}

template <typename NeighborEntryT>
void SaiNeighborManager::changeNeighbor(
    const std::shared_ptr<NeighborEntryT>& oldSwEntry,
    const std::shared_ptr<NeighborEntryT>& newSwEntry) {
  if (oldSwEntry->isPending() && newSwEntry->isPending()) {
    // We don't maintain pending entries so nothing to do here
  }
  if (oldSwEntry->isPending() && !newSwEntry->isPending()) {
    addNeighbor(newSwEntry);
  }
  if (!oldSwEntry->isPending() && newSwEntry->isPending()) {
    removeNeighbor(oldSwEntry);
  }
  if (!oldSwEntry->isPending() && !newSwEntry->isPending()) {
    if (*oldSwEntry != *newSwEntry) {
      removeNeighbor(oldSwEntry);
      addNeighbor(newSwEntry);
    } else {
      /* attempt to resolve next hops if not already resolved, if already
       * resolved, it would be no-op */
      auto iter = neighbors_.find(saiEntryFromSwEntry(newSwEntry));
      CHECK(iter != neighbors_.end());
      iter->second->notifySubscribers();
    }
  }

  XLOG(DBG2) << "Change Neighbor:: old Neighbor: " << oldSwEntry->str()
             << " new Neighbor: " << newSwEntry->str();
}

template <typename NeighborEntryT>
void SaiNeighborManager::addNeighbor(
    const std::shared_ptr<NeighborEntryT>& swEntry) {
  if (swEntry->isPending()) {
    XLOG(DBG2) << "skip adding unresolved neighbor " << swEntry->getIP();
    return;
  }
  XLOG(DBG2) << "addNeighbor " << swEntry->getIP();
  auto subscriberKey = saiEntryFromSwEntry(swEntry);
  if (neighbors_.contains(subscriberKey)) {
    throw FbossError(
        "Attempted to add duplicate neighbor: ", swEntry->getIP().str());
  }

  SaiPortDescriptor saiPortDesc;
  switch (swEntry->getPort().type()) {
    case PortDescriptor::PortType::PHYSICAL:
      saiPortDesc = SaiPortDescriptor(swEntry->getPort().phyPortID());
      break;
    case PortDescriptor::PortType::AGGREGATE:
      saiPortDesc = SaiPortDescriptor(swEntry->getPort().aggPortID());
      break;
    case PortDescriptor::PortType::SYSTEM_PORT:
      saiPortDesc = SaiPortDescriptor(swEntry->getPort().sysPortID());
      break;
  }

  std::optional<sai_uint32_t> metadata;
  if (swEntry->getClassID()) {
    metadata = static_cast<sai_uint32_t>(swEntry->getClassID().value());
  }

  std::optional<sai_uint32_t> encapIndex;
  if (swEntry->getEncapIndex()) {
    encapIndex = static_cast<sai_uint32_t>(swEntry->getEncapIndex().value());
  }
  auto saiRouterIntf =
      managerTable_->routerInterfaceManager().getRouterInterfaceHandle(
          swEntry->getIntfID());

  if (saiRouterIntf->type() == cfg::InterfaceType::SYSTEM_PORT &&
      platform_->getAsic()->isSupported(
          HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)) {
    CHECK(encapIndex) << " Encap index must be set for neighbors";
  }
  auto neighbor = std::make_unique<SaiNeighborEntry>(
      this,
      std::make_tuple(saiPortDesc, saiRouterIntf->adapterKey()),
      std::make_tuple(
          swEntry->getIntfID(), swEntry->getIP(), swEntry->getMac()),
      metadata,
      encapIndex,
      swEntry->getIsLocal(),
      swEntry->getNoHostRoute(),
      saiRouterIntf->type());

  // Record the entry BEFORE publishing it. Publishing cascades synchronously
  // into next hop and then next hop group member creation, and those consumers
  // look the neighbor back up (a protection member reads its MONITORED_OBJECT
  // from it), so it has to be findable by then. Rolled back if publish throws.
  auto [itr, inserted] = neighbors_.emplace(subscriberKey, std::move(neighbor));
  // Duplicates are rejected above, so the guard below always owns what it
  // erases.
  CHECK(inserted);
  auto guard = folly::makeGuard(
      [this, &subscriberKey]() { neighbors_.erase(subscriberKey); });
  itr->second->publish();
  guard.dismiss();
  XLOG(DBG2) << "Add Neighbor: create neighbor" << swEntry->str();
}

template <typename NeighborEntryT>
void SaiNeighborManager::removeNeighbor(
    const std::shared_ptr<NeighborEntryT>& swEntry) {
  if (swEntry->isPending()) {
    XLOG(DBG2) << "skip removing unresolved neighbor " << swEntry->getIP();
    return;
  }
  XLOG(DBG2) << "removeNeighbor " << swEntry->getIP();
  auto subscriberKey = saiEntryFromSwEntry(swEntry);
  if (!neighbors_.contains(subscriberKey)) {
    throw FbossError(
        "Attempted to remove non-existent neighbor: ", swEntry->getIP());
  }
  neighbors_.erase(subscriberKey);
  XLOG(DBG2) << "Remove Neighbor: " << swEntry->str();
}

void SaiNeighborEntry::publish() {
  if (auto* vlanRif =
          std::get_if<std::shared_ptr<ManagedVlanRifNeighbor>>(&neighbor_)) {
    // Subscribing can fire createObject immediately when the FDB entry already
    // exists, so this too must happen after the manager records the entry.
    SaiObjectEventPublisher::getInstance()->get<SaiFdbTraits>().subscribe(
        *vlanRif);
  } else if (
      auto* portRif =
          std::get_if<std::shared_ptr<PortRifNeighbor>>(&neighbor_)) {
    (*portRif)->createNeighbor();
  }
}

void SaiNeighborManager::clear() {
  neighbors_.clear();
}

std::shared_ptr<SaiNeighbor> SaiNeighborManager::createSaiObject(
    const SaiNeighborTraits::AdapterHostKey& key,
    const SaiNeighborTraits::CreateAttributes& attributes) {
  auto& store = saiStore_->get<SaiNeighborTraits>();
  return store.setObject(key, attributes);
}

const SaiNeighborHandle* SaiNeighborManager::getNeighborHandle(
    const SaiNeighborTraits::NeighborEntry& saiEntry) const {
  return getNeighborHandleImpl(saiEntry);
}
SaiNeighborHandle* SaiNeighborManager::getNeighborHandle(
    const SaiNeighborTraits::NeighborEntry& saiEntry) {
  return getNeighborHandleImpl(saiEntry);
}
SaiNeighborHandle* SaiNeighborManager::getNeighborHandleImpl(
    const SaiNeighborTraits::NeighborEntry& saiEntry) const {
  auto itr = neighbors_.find(saiEntry);
  if (itr == neighbors_.end()) {
    return nullptr;
  }
  auto subscriber = itr->second.get();
  if (!subscriber) {
    XLOG(FATAL) << "Invalid null neighbor for ip: " << saiEntry.ip().str();
  }
  return subscriber->getHandle();
}

cfg::InterfaceType SaiNeighborManager::getNeighborRifType(
    const SaiNeighborTraits::NeighborEntry& saiEntry) const {
  auto itr = neighbors_.find(saiEntry);
  if (itr != neighbors_.end()) {
    return itr->second->getRifType();
  }
  throw FbossError("Could not find neighbor: ", saiEntry.ip().str());
}

std::optional<sai_object_id_t> SaiNeighborManager::getNeighborPortSaiId(
    const SaiNeighborTraits::NeighborEntry& saiEntry) const {
  auto itr = neighbors_.find(saiEntry);
  if (itr == neighbors_.end()) {
    return std::nullopt;
  }
  // Report a missing handle as nullopt rather than aborting: this is a lookup,
  // and whether an absent egress object is tolerable is the caller's call. The
  // protection-group caller treats it as fatal to the update, because a PRIMARY
  // member with no MONITORED_OBJECT would never fail over.
  const auto port = itr->second->getSaiPortDesc();
  switch (port.type()) {
    case BasePortDescriptor::PortType::PHYSICAL: {
      auto portHandle =
          managerTable_->portManager().getPortHandle(port.phyPortID());
      if (!portHandle) {
        return std::nullopt;
      }
      return static_cast<sai_object_id_t>(portHandle->port->adapterKey());
    }
    case BasePortDescriptor::PortType::AGGREGATE: {
      auto lagHandle =
          managerTable_->lagManager().getLagHandle(port.aggPortID());
      if (!lagHandle) {
        return std::nullopt;
      }
      return static_cast<sai_object_id_t>(lagHandle->lag->adapterKey());
    }
    case BasePortDescriptor::PortType::SYSTEM_PORT: {
      auto sysPortHandle =
          managerTable_->systemPortManager().getSystemPortHandle(
              port.sysPortID());
      if (!sysPortHandle) {
        return std::nullopt;
      }
      return static_cast<sai_object_id_t>(
          sysPortHandle->systemPort->adapterKey());
    }
  }
  XLOG(FATAL) << "Unknown port type";
}

std::string SaiNeighborManager::listManagedObjects() const {
  std::string output{};
  for (const auto& entry : neighbors_) {
    output += entry.second->toString();
    output += "\n";
  }
  return output;
}

SaiNeighborEntry::SaiNeighborEntry(
    SaiNeighborManager* manager,
    std::tuple<SaiPortDescriptor, RouterInterfaceSaiId> saiPortAndIntf,
    std::tuple<InterfaceID, folly::IPAddress, folly::MacAddress>
        intfIDAndIpAndMac,
    std::optional<sai_uint32_t> metadata,
    std::optional<sai_uint32_t> encapIndex,
    bool isLocal,
    std::optional<bool> noHostRoute,
    cfg::InterfaceType intfType) {
  switch (intfType) {
    case cfg::InterfaceType::VLAN: {
      auto subscriber = std::make_shared<ManagedVlanRifNeighbor>(
          manager,
          saiPortAndIntf,
          intfIDAndIpAndMac,
          metadata,
          encapIndex,
          isLocal,
          noHostRoute);
      neighbor_ = subscriber;
    } break;
    case cfg::InterfaceType::PORT:
      // isLocal=true is unsupported on XGS
      neighbor_ = std::make_shared<PortRifNeighbor>(
          manager,
          saiPortAndIntf,
          intfIDAndIpAndMac,
          metadata,
          encapIndex,
          std::nullopt,
          noHostRoute,
          intfType);
      break;
    case cfg::InterfaceType::SYSTEM_PORT:
      neighbor_ = std::make_shared<PortRifNeighbor>(
          manager,
          saiPortAndIntf,
          intfIDAndIpAndMac,
          metadata,
          encapIndex,
          isLocal,
          noHostRoute,
          intfType);
      break;
  }
}

cfg::InterfaceType SaiNeighborEntry::getRifType() const {
  return std::visit(
      folly::overload(
          [](const std::shared_ptr<ManagedVlanRifNeighbor>& /*handle*/) {
            return cfg::InterfaceType::VLAN;
          },
          [](const std::shared_ptr<PortRifNeighbor>& handle) {
            return handle->getRifType();
          }),
      neighbor_);
  CHECK(false) << " Unknown neighbor rif type: ";
}

PortRifNeighbor::PortRifNeighbor(
    SaiNeighborManager* manager,
    std::tuple<SaiPortDescriptor, RouterInterfaceSaiId> saiPortAndIntf,
    std::tuple<InterfaceID, folly::IPAddress, folly::MacAddress>
        intfIDAndIpAndMac,
    std::optional<sai_uint32_t> metadata,
    std::optional<sai_uint32_t> encapIndex,
    std::optional<bool> isLocal,
    std::optional<bool> noHostRoute,
    cfg::InterfaceType intfType)
    : manager_(manager),
      saiPortAndIntf_(std::move(saiPortAndIntf)),
      handle_(std::make_unique<SaiNeighborHandle>()),
      intfType_(intfType) {
  const auto& ip = std::get<folly::IPAddress>(intfIDAndIpAndMac);
  auto rifSaiId = std::get<RouterInterfaceSaiId>(saiPortAndIntf_);
  auto adapterHostKey = SaiNeighborTraits::NeighborEntry(
      manager_->getSwitchSaiId(), rifSaiId, ip);
  std::optional<SaiNeighborTraits::Attributes::IsLocal> saiIsLocal;
  if (isLocal.has_value()) {
    saiIsLocal = SaiNeighborTraits::Attributes::IsLocal(isLocal.value());
  }
  createAttributes_ = SaiNeighborTraits::CreateAttributes{
      std::get<folly::MacAddress>(intfIDAndIpAndMac),
      metadata,
      encapIndex,
      saiIsLocal,
      noHostRoute};
  adapterHostKey_ = adapterHostKey;
}

void PortRifNeighbor::createNeighbor() {
  neighbor_ = manager_->createSaiObject(adapterHostKey_, createAttributes_);
  handle_->neighbor = neighbor_.get();
}

ManagedVlanRifNeighbor::ManagedVlanRifNeighbor(
    SaiNeighborManager* manager,
    std::tuple<SaiPortDescriptor, RouterInterfaceSaiId> saiPortAndIntf,
    std::tuple<InterfaceID, folly::IPAddress, folly::MacAddress>
        intfIDAndIpAndMac,
    std::optional<sai_uint32_t> metadata,
    std::optional<sai_uint32_t> encapIndex,
    bool isLocal,
    std::optional<bool> noHostRoute)
    : Base(
          std::make_tuple(
              std::get<InterfaceID>(intfIDAndIpAndMac),
              std::get<folly::MacAddress>(intfIDAndIpAndMac))),
      manager_(manager),
      saiPortAndIntf_(saiPortAndIntf),
      intfIDAndIpAndMac_(intfIDAndIpAndMac),
      handle_(std::make_unique<SaiNeighborHandle>()),
      metadata_(metadata),
      noHostRoute_(noHostRoute) {
  if (encapIndex || !isLocal) {
    throw FbossError(
        "Remote nbrs or nbrs with encap index not supported with VLAN RIFs");
  }
}

void ManagedVlanRifNeighbor::createObject(PublisherObjects objects) {
  auto fdbEntry = std::get<FdbWeakptr>(objects).lock();
  const auto& ip = std::get<folly::IPAddress>(intfIDAndIpAndMac_);
  auto adapterHostKey = SaiNeighborTraits::NeighborEntry(
      manager_->getSwitchSaiId(), getRouterInterfaceSaiId(), ip);

  auto createAttributes = SaiNeighborTraits::CreateAttributes{
      fdbEntry->adapterHostKey().mac(),
      metadata_,
      std::nullopt,
      std::nullopt,
      noHostRoute_};
  auto object = manager_->createSaiObject(adapterHostKey, createAttributes);
  this->setObject(object);
  handle_->neighbor = getSaiObject();
  handle_->fdbEntry = fdbEntry.get();

  XLOG(DBG2) << "ManagedNeigbhor::createObject: " << toString();
}

void ManagedVlanRifNeighbor::removeObject(size_t, PublisherObjects) {
  XLOG(DBG2) << "ManagedNeigbhor::removeObject: " << toString();

  this->resetObject();
  handle_->neighbor = nullptr;
  handle_->fdbEntry = nullptr;
}

void ManagedVlanRifNeighbor::notifySubscribers() const {
  auto neighbor = this->getObject();
  if (!neighbor) {
    return;
  }
  neighbor->notifyAfterCreate(neighbor);
}

std::string ManagedVlanRifNeighbor::toString() const {
  auto metadataStr =
      metadata_.has_value() ? std::to_string(metadata_.value()) : "none";
  auto neighborStr = handle_->neighbor
      ? handle_->neighbor->adapterKey().toString()
      : "NeighborEntry: none";
  auto fdbEntryStr = handle_->fdbEntry
      ? handle_->fdbEntry->adapterKey().toString()
      : "FdbEntry: none";

  const auto& ip = std::get<folly::IPAddress>(intfIDAndIpAndMac_);
  auto saiPortDesc = getSaiPortDesc();
  return folly::to<std::string>(
      getObject() ? "active " : "inactive ",
      "managed neighbor: ",
      "ip: ",
      ip.str(),
      saiPortDesc.str(),
      " metadata: ",
      metadataStr,
      " ",
      neighborStr,
      " ",
      fdbEntryStr);
}

void ManagedVlanRifNeighbor::handleLinkDown() {
  auto* object = getSaiObject();
  if (!object) {
    XLOG(DBG2)
        << "neighbor is already unresolved, skip notifying link down to subscribed next hops";
    return;
  }
  XLOGF(
      DBG2,
      "neighbor {} notifying link down to subscribed next hops",
      object->adapterHostKey());
  SaiObjectEventPublisher::getInstance()
      ->get<SaiNeighborTraits>()
      .notifyLinkDown(object->adapterHostKey());
}

void PortRifNeighbor::handleLinkDown() {
  XLOGF(
      DBG2,
      "neighbor {} notifying link down to subscribed next hops",
      neighbor_->adapterHostKey());
  SaiObjectEventPublisher::getInstance()
      ->get<SaiNeighborTraits>()
      .notifyLinkDown(neighbor_->adapterHostKey());
}

void SaiNeighborManager::handleLinkDown(const SaiPortDescriptor& port) {
  for (auto& [nbrEntry, neighbor] : neighbors_) {
    switch (neighbor->getRifType()) {
      case cfg::InterfaceType::VLAN:
        // fdb entries are  reset on link down as a result next hop or next hop
        // group are shrunk. no need to notify subscribers
        continue;
      case cfg::InterfaceType::PORT:
      case cfg::InterfaceType::SYSTEM_PORT:
        if (neighbor->getSaiPortDesc() == port) {
          neighbor->handleLinkDown();
        }
    }
  }
}

template SaiNeighborTraits::NeighborEntry
SaiNeighborManager::saiEntryFromSwEntry<NdpEntry>(
    const std::shared_ptr<NdpEntry>& swEntry);
template SaiNeighborTraits::NeighborEntry
SaiNeighborManager::saiEntryFromSwEntry<ArpEntry>(
    const std::shared_ptr<ArpEntry>& swEntry);

template void SaiNeighborManager::changeNeighbor<NdpEntry>(
    const std::shared_ptr<NdpEntry>& oldSwEntry,
    const std::shared_ptr<NdpEntry>& newSwEntry);
template void SaiNeighborManager::changeNeighbor<ArpEntry>(
    const std::shared_ptr<ArpEntry>& oldSwEntry,
    const std::shared_ptr<ArpEntry>& newSwEntry);

template void SaiNeighborManager::addNeighbor<NdpEntry>(
    const std::shared_ptr<NdpEntry>& swEntry);
template void SaiNeighborManager::addNeighbor<ArpEntry>(
    const std::shared_ptr<ArpEntry>& swEntry);

template void SaiNeighborManager::removeNeighbor<NdpEntry>(
    const std::shared_ptr<NdpEntry>& swEntry);
template void SaiNeighborManager::removeNeighbor<ArpEntry>(
    const std::shared_ptr<ArpEntry>& swEntry);

} // namespace facebook::fboss
