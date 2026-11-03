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

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <list>
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/EncapIndexAllocator.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/IPv6Handler.h"
#include "fboss/agent/NeighborCacheImpl.h"
#include "fboss/agent/StaticL2ForNeighborSwSwitchUpdater.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/NeighborEntry.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

namespace ncachehelpers {

/*
 * Helper that we can run to check that the interface and vlan
 * for an entry still exist and are valid.
 */
template <typename NTable>
bool checkVlanAndIntf(
    const std::shared_ptr<SwitchState>& state,
    const typename NeighborCacheEntry<NTable>::EntryFields& fields,
    std::optional<VlanID> vlanID) {
  if (vlanID.has_value()) {
    // Make sure vlan exists
    auto* vlan = state->getVlans()->getNodeIf(vlanID.value()).get();
    if (!vlan) {
      // This VLAN no longer exists.  Just ignore the entry update.
      XLOG(DBG3) << "VLAN " << vlanID.value() << " deleted before entry "
                 << fields.ip << " --> " << fields.mac << " could be updated";
      return false;
    }
  }

  // In case the interface subnets have changed, make sure the IP address
  // is still on a locally attached subnet
  if (!Interface::isIpAttached(fields.ip, fields.interfaceID, state)) {
    XLOG(DBG3) << "interface subnets changed before entry " << fields.ip
               << " --> " << fields.mac << " could be updated";
    return false;
  }

  return true;
}

inline cfg::PortDescriptor getNeighborPortDescriptor(
    const PortDescriptor& port) {
  switch (port.type()) {
    case PortDescriptor::PortType::PHYSICAL:
      return PortDescriptor(port.phyPortID()).toThrift();
    case PortDescriptor::PortType::AGGREGATE:
      return PortDescriptor(port.aggPortID()).toThrift();
    case PortDescriptor::PortType::SYSTEM_PORT:
      return PortDescriptor(port.sysPortID()).toThrift();
  }

  throw FbossError("Unnknown port descriptor");
}

template <typename NeighborEntryT>
bool isReachable(const std::shared_ptr<NeighborEntryT>& entry) {
  return entry->isReachable() && !entry->getMac().isBroadcast();
}

template <typename NeighborEntryT>
std::shared_ptr<SwitchState> processChangedNeighborEntry(
    std::shared_ptr<SwitchState> state,
    const std::shared_ptr<NeighborEntryT>& oldEntry,
    const std::shared_ptr<NeighborEntryT>& newEntry,
    VlanID vlanID) {
  if ((isReachable(oldEntry) != isReachable(newEntry)) ||
      (oldEntry->getMac() != newEntry->getMac()) ||
      (oldEntry->getPort() != newEntry->getPort())) {
    if (isReachable(oldEntry)) {
      state = StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
          state, vlanID, oldEntry);
    }
    if (isReachable(newEntry)) {
      state = StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
          state, vlanID, newEntry);
    }
  }
  return state;
}

} // namespace ncachehelpers

template <typename NTable>
bool NeighborCacheImpl<NTable>::isHwUpdateProtected() {
  // this API return true if the platform supports hw protection,
  // for this we are using transactionsSupported() API
  // and return true for SAI switches, failure protection uses transactions
  // support in HW switch which is available only in SAI switches
  return (
      FLAGS_enable_hw_update_protection &&
      sw_->getHwSwitchHandler()->transactionsSupported());
}

template <typename NTable>
bool NeighborCacheImpl<NTable>::programEntry(Entry* entry) {
  SwSwitch::StateUpdateFn updateFn;

  auto switchType = sw_->getSwitchInfoTable().l3SwitchType();
  switch (switchType) {
    case cfg::SwitchType::NPU:
      updateFn = getUpdateFnToProgramEntry(entry, cfg::SwitchType::NPU);
      break;
    case cfg::SwitchType::VOQ:
      updateFn = getUpdateFnToProgramEntry(entry, cfg::SwitchType::VOQ);
      break;
    case cfg::SwitchType::FABRIC:
    case cfg::SwitchType::PHY:
      throw FbossError(
          "Programming entry is not supported for switch type: ", switchType);
  }

  if (isHwUpdateProtected()) {
    try {
      sw_->updateStateWithHwFailureProtection(
          folly::to<std::string>(
              "add neighbor with hw protection failure ",
              entry->getFields().ip),
          std::move(updateFn));
    } catch (const FbossHwUpdateError& e) {
      XLOG(ERR)
          << "Failed to program neighbor entry with hw failure protection "
          << entry->getFields().ip << " with error: " << e.what();
      sw_->stats()->neighborTableUpdateFailure();
      return false;
    }
  } else {
    sw_->updateState(
        folly::to<std::string>("add neighbor ", entry->getFields().ip),
        std::move(updateFn));
  }
  return true;
}

template <typename NTable>
SwSwitch::StateUpdateFn
NeighborCacheImpl<NTable>::getUpdateFnToProgramEntryForVlan(Entry* entry) {
  CHECK(!entry->isPending());

  auto fields = entry->getFields();
  auto vlanID = vlanID_;
  auto updateFn =
      [this, fields, vlanID](const std::shared_ptr<SwitchState>& state) mutable
      -> std::shared_ptr<SwitchState> {
    if (!ncachehelpers::checkVlanAndIntf<NTable>(state, fields, vlanID)) {
      // Either the vlan or intf is no longer valid.
      return nullptr;
    }

    auto vlan = state->getVlans()->getNodeIf(vlanID).get();
    std::shared_ptr<SwitchState> newState{state};
    auto* table = vlan->template getNeighborTable<NTable>().get();
    auto node = table->getNodeIf(fields.ip.str());

    auto isAggregatePort = fields.port.isAggregatePort();

    if (isAggregatePort) {
      auto aggregatePort =
          state->getAggregatePorts()->getNodeIf(fields.port.aggPortID());

      if (!aggregatePort) {
        // if node is not found, it means the AggregatePort is down or deleted,
        // we should not throw exception here for AggregatePort case
        // log the error and return
        XLOG(ERR) << "AggregatePort: " << fields.port.aggPortID()
                  << " does not exist in current state";
        return nullptr;
      }
    }

    auto switchId = isAggregatePort
        ? sw_->getScopeResolver()->scope(state, fields.port).switchId()
        : sw_->getScopeResolver()->scope(fields.port.phyPortID()).switchId();
    auto asic = sw_->getHwAsicTable()->getHwAsicIf(switchId);
    if (asic->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)) {
      fields.encapIndex =
          EncapIndexAllocator::getNextAvailableEncapIdx(state, *asic);
    }
    if (!node) {
      table = table->modify(&vlan, &newState);
      table->addEntry(fields);
      XLOG(DBG2) << "Adding entry for " << fields.ip << " --> " << fields.mac
                 << " on interface " << fields.interfaceID << " for vlan "
                 << vlanID;
      node = table->getNodeIf(fields.ip.str());

      if (needL2EntryForNeighbor_ && ncachehelpers::isReachable(node)) {
        // Also program static l2 entry in the same update
        newState = StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
            newState, vlanID, node);
      }
    } else {
      if (node->getMac() == fields.mac && node->getPort() == fields.port &&
          node->getIntfID() == fields.interfaceID &&
          node->getState() == fields.state && !node->isPending()) {
        // This entry was already updated while we were waiting on the lock.
        return nullptr;
      }
      table = table->modify(&vlan, &newState);
      table->updateEntry(fields);
      XLOG(DBG2) << "Converting pending entry for " << fields.ip << " --> "
                 << fields.mac << " on interface " << fields.interfaceID
                 << " port " << fields.port << " for vlan " << vlanID;

      if (needL2EntryForNeighbor_) {
        auto newNode = table->getNodeIf(fields.ip.str());
        newState = ncachehelpers::processChangedNeighborEntry(
            newState, node, newNode, vlanID);
      }
    }
    return newState;
  };

  return updateFn;
}

template <typename NTable>
SwSwitch::StateUpdateFn NeighborCacheImpl<NTable>::getUpdateFnToProgramEntry(
    Entry* entry,
    cfg::SwitchType switchType) {
  CHECK(!entry->isPending());

  auto fields = entry->getFields();
  auto updateFn = [this, fields, switchType](
                      const std::shared_ptr<SwitchState>& state) mutable
      -> std::shared_ptr<SwitchState> {
    InterfaceID interfaceID;
    std::optional<SystemPortID> systemPortID;

    if (!ncachehelpers::checkVlanAndIntf<NTable>(
            state, fields, std::nullopt /* vlanID */)) {
      // intf is no longer valid.
      return nullptr;
    }

    auto isAggregatePort = fields.port.isAggregatePort();
    auto switchId = isAggregatePort
        ? sw_->getScopeResolver()->scope(state, fields.port).switchId()
        : sw_->getScopeResolver()->scope(fields.port.phyPortID()).switchId();
    auto asic = sw_->getHwAsicTable()->getHwAsicIf(switchId);
    if (asic->isSupported(HwAsic::Feature::RESERVED_ENCAP_INDEX_RANGE)) {
      fields.encapIndex =
          EncapIndexAllocator::getNextAvailableEncapIdx(state, *asic);
    }

    if (switchType == cfg::SwitchType::VOQ) {
      CHECK(!fields.port.isSystemPort());
      interfaceID = state->getInterfaceIDForPort(fields.port);
      // SystemPortID is always same as the InterfaceID
      systemPortID = SystemPortID(interfaceID);
    } else {
      interfaceID = intfID_;
    }

    auto newState = state->clone();
    auto intfMap = newState->getInterfaces()->modify(&newState);
    auto intf = intfMap->getNode(interfaceID);
    auto intfNew = intf->clone();
    auto nbrTable = intf->getTable<NTable>();
    auto updatedNbrTable = nbrTable->toThrift();
    auto nbrEntry = state::NeighborEntryFields();
    nbrEntry.ipaddress() = fields.ip.str();
    nbrEntry.mac() = fields.mac.toString();
    nbrEntry.interfaceId() = static_cast<uint32_t>(interfaceID);
    nbrEntry.state() = static_cast<state::NeighborState>(fields.state);
    if (fields.encapIndex.has_value()) {
      nbrEntry.encapIndex() = fields.encapIndex.value();
    }

    if (switchType == cfg::SwitchType::VOQ) {
      // TODO: Support aggregate ports for VOQ switches
      CHECK(fields.port.isPhysicalPort());
      CHECK(systemPortID.has_value());
      nbrEntry.portId() =
          PortDescriptor(SystemPortID(systemPortID.value())).toThrift();
      nbrEntry.isLocal() = fields.isLocal;
    } else {
      nbrEntry.portId() = ncachehelpers::getNeighborPortDescriptor(fields.port);
    }

    if (fields.classID.has_value()) {
      nbrEntry.classID() = fields.classID.value();
    }

    auto node = nbrTable->getEntryIf(fields.ip);
    if (!node) {
      updatedNbrTable.insert({fields.ip.str(), nbrEntry});
      XLOG(DBG2)
          << "Adding entry for: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 nbrEntry);
    } else {
      if (node->getMac() == fields.mac &&
          ((switchType != cfg::SwitchType::VOQ &&
            node->getPort() == fields.port) ||
           (switchType == cfg::SwitchType::VOQ &&
            node->getPort().sysPortIDIf() == systemPortID)) &&
          node->getIntfID() == interfaceID &&
          node->getState() == fields.state && !node->isPending() &&
          node->getClassID() == fields.classID) {
        return nullptr;
      }
      updatedNbrTable.erase(fields.ip.str());
      updatedNbrTable.insert({fields.ip.str(), nbrEntry});
      XLOG(DBG2)
          << "Updating entry for: "
          << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                 nbrEntry);
    }

    intfNew->setNeighborTable<NTable>(updatedNbrTable);
    intfMap->updateNode(
        intfNew, sw_->getScopeResolver()->scope(intfNew, newState));

    auto newNode = intfNew->getTable<NTable>()->getEntryIf(fields.ip);
    if (needL2EntryForNeighbor_ &&
        intfNew->getType() == cfg::InterfaceType::VLAN) {
      auto vlanID = intfNew->getVlanID();
      if (!node) {
        if (ncachehelpers::isReachable(newNode)) {
          // Also program static l2 entry in the same update
          newState = StaticL2ForNeighborSwSwitchUpdater::ensureMacEntry(
              newState, vlanID, newNode);
        }
      } else {
        newState = ncachehelpers::processChangedNeighborEntry(
            newState, node, newNode, vlanID);
      }
    }

    return newState;
  };

  return updateFn;
}

template <typename NTable>
bool NeighborCacheImpl<NTable>::programPendingEntry(
    Entry* entry,
    PortDescriptor port,
    bool force) {
  SwSwitch::StateUpdateFn updateFn;

  auto switchType = sw_->getSwitchInfoTable().l3SwitchType();
  switch (switchType) {
    case cfg::SwitchType::NPU:
      updateFn = getUpdateFnToProgramPendingEntry(
          entry, port, force, cfg::SwitchType::NPU);
      break;
    case cfg::SwitchType::VOQ:
      updateFn = getUpdateFnToProgramPendingEntry(
          entry, port, force, cfg::SwitchType::VOQ);
      break;
    case cfg::SwitchType::FABRIC:
    case cfg::SwitchType::PHY:
      throw FbossError(
          "Programming entry is not supported for switch type: ", switchType);
  }

  if (isHwUpdateProtected()) {
    try {
      sw_->updateStateWithHwFailureProtection(
          folly::to<std::string>(
              "add pending entry with hw failure protection ",
              entry->getFields().ip),
          std::move(updateFn));
    } catch (const FbossHwUpdateError& e) {
      XLOG(ERR)
          << "Failed to program pending neighbor entry with hw failure protection "
          << entry->getFields().ip << " with error: " << e.what();
      sw_->stats()->neighborTableUpdateFailure();
      return false;
    }
  } else {
    sw_->updateStateNoCoalescing(
        folly::to<std::string>("add pending entry ", entry->getFields().ip),
        std::move(updateFn));
  }
  return true;
}

template <typename NTable>
SwSwitch::StateUpdateFn
NeighborCacheImpl<NTable>::getUpdateFnToProgramPendingEntryForVlan(
    Entry* entry,
    PortDescriptor /*port*/,
    bool force) {
  CHECK(entry->isPending());

  auto fields = entry->getFields();
  auto vlanID = vlanID_;
  auto needL2EntryForNeighbor = needL2EntryForNeighbor_;
  auto updateFn = [fields, vlanID, needL2EntryForNeighbor, force](
                      const std::shared_ptr<SwitchState>& state)
      -> std::shared_ptr<SwitchState> {
    if (!ncachehelpers::checkVlanAndIntf<NTable>(state, fields, vlanID)) {
      // Either the vlan or intf is no longer valid.
      return nullptr;
    }

    auto vlan = state->getVlans()->getNodeIf(vlanID).get();
    std::shared_ptr<SwitchState> newState{state};
    auto* table = vlan->template getNeighborTable<NTable>().get();
    auto node = table->getNodeIf(fields.ip.str());
    table = table->modify(&vlan, &newState);

    if (node) {
      if (!force) {
        // don't replace an existing entry with a pending one unless
        // explicitly allowed
        return nullptr;
      }
      table->removeEntry(fields.ip);
    }
    table->addPendingEntry(fields.ip, fields.interfaceID);

    XLOG(DBG4) << "Adding pending entry for " << fields.ip << " on interface "
               << fields.interfaceID << " for vlan " << vlanID;

    if (needL2EntryForNeighbor && node && ncachehelpers::isReachable(node)) {
      // Remove MAC for existing entry
      newState = StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
          newState, vlanID, node);
    }
    return newState;
  };

  return updateFn;
}

template <typename NTable>
SwSwitch::StateUpdateFn
NeighborCacheImpl<NTable>::getUpdateFnToProgramPendingEntry(
    Entry* entry,
    PortDescriptor port,
    bool force,
    cfg::SwitchType switchType) {
  auto fields = entry->getFields();
  auto updateFn = [this, fields, port, force, switchType](
                      const std::shared_ptr<SwitchState>& state)
      -> std::shared_ptr<SwitchState> {
    if (port.isPhysicalPort() && port.phyPortID() == PortID(0)) {
      // If the entry is pointing to the CPU port, it is not programmed in the
      // hardware, thus no-op.
      return nullptr;
    }

    if (!ncachehelpers::checkVlanAndIntf<NTable>(
            state, fields, std::nullopt /* vlanID */)) {
      // intf is no longer valid.
      return nullptr;
    }

    InterfaceID interfaceID;

    if (switchType == cfg::SwitchType::VOQ) {
      CHECK(!fields.port.isSystemPort());
      CHECK(port.isPhysicalPort());
      interfaceID = state->getInterfaceIDForPort(port);
    } else {
      interfaceID = intfID_;
    }

    auto newState = state->clone();
    auto intfMap = newState->getInterfaces()->modify(&newState);
    auto intf = intfMap->getNode(interfaceID);
    auto intfNew = intf->clone();

    auto nbrTable = intf->getTable<NTable>();
    auto updatedNbrTable = nbrTable->toThrift();

    auto node = nbrTable->getEntryIf(fields.ip);
    if (node) {
      if (!force) {
        // don't replace an existing entry with a pending one unless
        // explicitly allowed
        return nullptr;
      }

      updatedNbrTable.erase(fields.ip.str());
    }

    auto nbrEntry = state::NeighborEntryFields();
    nbrEntry.ipaddress() = fields.ip.str();
    nbrEntry.mac() = fields.mac.toString();
    nbrEntry.interfaceId() = static_cast<uint32_t>(interfaceID);
    nbrEntry.state() =
        static_cast<state::NeighborState>(state::NeighborState::Pending);

    nbrEntry.portId() = PortDescriptor(PortID(0)).toThrift();

    if (switchType == cfg::SwitchType::VOQ) {
      nbrEntry.isLocal() = fields.isLocal;
    }

    if (fields.classID.has_value()) {
      nbrEntry.classID() = fields.classID.value();
    }

    XLOG(DBG2) << "Adding pending entry for " << fields.ip << " on interface "
               << fields.interfaceID << " entry: "
               << apache::thrift::SimpleJSONSerializer::serialize<std::string>(
                      nbrEntry);

    updatedNbrTable.insert({fields.ip.str(), nbrEntry});
    intfNew->setNeighborTable<NTable>(updatedNbrTable);
    intfMap->updateNode(
        intfNew, sw_->getScopeResolver()->scope(intfNew, newState));

    // Remove MAC for existing entry
    if (needL2EntryForNeighbor_ &&
        intfNew->getType() == cfg::InterfaceType::VLAN && node &&
        ncachehelpers::isReachable(node)) {
      newState = StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
          newState, intfNew->getVlanID(), node);
    }

    return newState;
  };

  return updateFn;
}

template <typename NTable>
NeighborCacheImpl<NTable>::~NeighborCacheImpl() {}

template <typename NTable>
void NeighborCacheImpl<NTable>::repopulate(std::shared_ptr<NTable> table) {
  for (auto it = table->cbegin(); it != table->cend(); ++it) {
    auto entry = it->second;
    auto state = entry->isPending() ? NeighborEntryState::INCOMPLETE
                                    : NeighborEntryState::STALE;

    switch (entry->getType()) {
      case state::NeighborEntryType::DYNAMIC_ENTRY: {
        auto port = entry->getPort();
        auto fields = EntryFields::fromThrift(entry->toThrift());
        auto switchType = sw_->getSwitchInfoTable().l3SwitchType();
        // For VOQ switches, the port ID in switchState is system port ID
        // and neighbor cache stores physical port ID. So, we need to
        // convert system port ID to physical port ID
        if (switchType == cfg::SwitchType::VOQ &&
            port.type() == PortDescriptor::PortType::SYSTEM_PORT) {
          auto physPortID =
              getPortID(entry->getPort().sysPortID(), getSw()->getState());

          fields.port = PortDescriptor(physPortID);
        }
        addOrUpdateEntryInternal(
            fields, state, state::NeighborEntryType::DYNAMIC_ENTRY);
        break;
      }
      case state::NeighborEntryType::STATIC_ENTRY:
        // We don't need to run Neighbor entry state machine for static
        // entries. Thus, skip adding to the neighbor cache
        XLOG(DBG2) << "Skip adding static entry to neighbor cache: "
                   << entry->getIP().str();
        break;
    }
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::setEntry(
    AddressType ip,
    folly::MacAddress mac,
    PortDescriptor port,
    NeighborEntryState state) {
  // First update switchState and asic, if it succeeds, then update cache.
  // SwitchState update is transaction protected, if HW_FAILURE_PROTECTION flag
  // is set. Thus we want to update cache only if switchState update succeeds,
  // to avoid switchState and neighbor cache inconsistency

  auto entry = addOrUpdateEntryInternal(
      EntryFields(ip, mac, port, intfID_),
      state,
      state::NeighborEntryType::DYNAMIC_ENTRY);

  if (entry && !programEntry(entry)) {
    XLOG(ERR) << "Failed to program entry: " << ip.str();
    removeEntry(ip);
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::setExistingEntry(
    AddressType ip,
    folly::MacAddress mac,
    PortDescriptor port,
    NeighborEntryState state) {
  auto entry = getCacheEntry(ip);
  if (entry && !entry->fieldsMatch(EntryFields(ip, mac, port, intfID_))) {
    // only program an entry if one exists and the fields have changed
    // make a copy of the entry to update the fields locally
    auto entryCopy = std::make_unique<Entry>(
        EntryFields(ip, mac, port, intfID_),
        evb_,
        cache_,
        state,
        entry->getType());

    if (programEntry(entryCopy.get())) {
      // update the cache entry with the new fields if hw update succeeds
      entry->updateFields(EntryFields(ip, mac, port, intfID_));
      entry->updateState(state);
    } else {
      XLOG(ERR) << "Failed to set Existing entry: " << ip.str();
    }
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::updateEntryState(
    AddressType ip,
    NeighborEntryState state) {
  auto entry = getCacheEntry(ip);
  if (entry) {
    entry->updateState(state);
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::updateEntryClassID(
    AddressType ip,
    std::optional<cfg::AclLookupClass> classID) {
  auto entry = getCacheEntry(ip);

  if (entry) {
    entry->updateClassID(classID);

    auto updateClassIDFn =
        [this, ip, classID](const std::shared_ptr<SwitchState>& state) {
          std::shared_ptr<SwitchState> newState{state};

          auto* intf = state->getInterfaces()->getNodeIf(intfID_).get();
          auto* table = intf->template getNeighborTable<NTable>().get();

          auto node = table->getNodeIf(ip.str());
          if (node) {
            auto fields = EntryFields(
                node->getIP(),
                node->getMac(),
                node->getPort(),
                node->getIntfID(),
                node->getState(),
                classID,
                node->getEncapIndex(),
                node->getIsLocal());

            table = table->modify(&intf, &newState);

            table->updateEntry(fields);
          }

          return newState;
        };

    auto classIDStr = classID.has_value()
        ? folly::to<std::string>(static_cast<int>(classID.value()))
        : "None";
    sw_->updateState(
        folly::to<std::string>(
            "NeighborCache configure lookup classID: ",
            classIDStr,
            " for " + ip.str()),
        std::move(updateClassIDFn));
  }
}

template <typename NTable>
NeighborCacheEntry<NTable>* NeighborCacheImpl<NTable>::addOrUpdateEntryInternal(
    const EntryFields& fields,
    NeighborEntryState state,
    state::NeighborEntryType type) {
  auto entry = getCacheEntry(fields.ip);
  if (entry) {
    auto changed = !entry->fieldsMatch(fields);
    if (changed) {
      entry->updateFields(fields);
    }
    entry->updateState(state);
    return changed ? entry : nullptr;
  } else {
    auto to_store = std::make_shared<Entry>(fields, evb_, cache_, state, type);
    entry = to_store.get();
    setCacheEntry(std::move(to_store));
  }
  return entry;
}

template <typename NTable>
void NeighborCacheImpl<NTable>::setPendingEntry(
    AddressType ip,
    PortDescriptor port,
    bool force) {
  if (!force && getCacheEntry(ip)) {
    // only overwrite an existing entry with a pending entry if we say it is
    // ok with the 'force' parameter
    return;
  }
  auto entry = addOrUpdateEntryInternal(
      EntryFields(ip, intfID_, NeighborState::PENDING),
      NeighborEntryState::INCOMPLETE,
      state::NeighborEntryType::DYNAMIC_ENTRY);

  if (entry && !programPendingEntry(entry, port, force)) {
    XLOG(ERR) << "Failed to program pending entry: " << ip.str();
    removeEntry(ip);
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::processEntry(AddressType ip) {
  auto entry = getCacheEntry(ip);
  if (entry) {
    entry->process();
    if (entry->getState() == NeighborEntryState::EXPIRED) {
      flushEntry(ip);
    }
  }
}

template <typename NTable>
NeighborCacheEntry<NTable>* NeighborCacheImpl<NTable>::getCacheEntry(
    AddressType ip) const {
  auto it = entries_.find(ip);
  if (it != entries_.end()) {
    return it->second.get();
  }
  return nullptr;
}

template <typename NTable>
void NeighborCacheImpl<NTable>::setCacheEntry(std::shared_ptr<Entry> entry) {
  const auto& ip = entry->getIP();
  entries_[ip] = std::move(entry);
}

template <typename NTable>
bool NeighborCacheImpl<NTable>::removeEntry(AddressType ip) {
  auto it = entries_.find(ip);
  if (it == entries_.end()) {
    return false;
  }

  entries_.erase(it);

  return true;
}

template <typename NTable>
template <typename VlanOrIntfT>
bool NeighborCacheImpl<NTable>::flushEntryFromSwitchState(
    std::shared_ptr<SwitchState>* state,
    AddressType ip,
    VlanOrIntfT* vlanOrIntf) {
  auto* table = vlanOrIntf->template getNeighborTable<NTable>().get();
  const auto& entry = table->getNodeIf(ip.str());
  if (!entry) {
    return false;
  }

  table = table->modify(&vlanOrIntf, state);
  table->removeNode(ip.str());
  return true;
}

template <typename NTable>
bool NeighborCacheImpl<NTable>::flushEntryBlocking(AddressType ip) {
  bool flushed{false};
  flushEntry(ip, &flushed);
  return flushed;
}

template <typename NTable>
void NeighborCacheImpl<NTable>::flushEntry(AddressType ip, bool* flushed) {
  // remove from cache
  if (!removeEntry(ip)) {
    if (flushed) {
      *flushed = false;
    }
    return;
  }

  auto needL2EntryForNeighbor = needL2EntryForNeighbor_;

  // flush from SwitchState
  auto updateFn = [this, ip, flushed, needL2EntryForNeighbor](
                      const std::shared_ptr<SwitchState>& state)
      -> std::shared_ptr<SwitchState> {
    std::shared_ptr<SwitchState> newState{state};

    auto* intf = newState->getInterfaces()->getNode(intfID_).get();
    bool flushedEntry = flushEntryFromSwitchState(&newState, ip, intf);

    auto oldEntry = intf->getNeighborTable<NTable>()->getNodeIf(ip.str());
    if (needL2EntryForNeighbor && oldEntry &&
        intf->getType() == cfg::InterfaceType::VLAN &&
        ncachehelpers::isReachable(oldEntry)) {
      newState = StaticL2ForNeighborSwSwitchUpdater::pruneMacEntry(
          newState, intf->getVlanID(), oldEntry);
    }

    if (flushedEntry) {
      if (flushed) {
        *flushed = true;
      }
      return newState;
    }
    return nullptr;
  };

  if (flushed) {
    // need a blocking state update if the caller wants to know if an entry
    // was actually flushed
    if (isHwUpdateProtected()) {
      try {
        sw_->updateStateWithHwFailureProtection(
            "flush neighbor entry with hw failure protection",
            std::move(updateFn));
      } catch (const FbossHwUpdateError& e) {
        XLOG(ERR) << "Failed to program flush neighbor entry: " << e.what();
        sw_->stats()->neighborTableUpdateFailure();
      }
    } else {
      sw_->updateStateBlocking("flush neighbor entry", std::move(updateFn));
    }
  } else {
    sw_->updateState("remove neighbor entry: " + ip.str(), std::move(updateFn));
  }
}

template <typename NTable>
std::unique_ptr<typename NeighborCacheImpl<NTable>::EntryFields>
NeighborCacheImpl<NTable>::cloneEntryFields(AddressType ip) {
  auto entry = getCacheEntry(ip);
  if (entry) {
    return std::make_unique<EntryFields>(entry->getFields());
  }
  return nullptr;
}

template <typename NTable>
void NeighborCacheImpl<NTable>::portDown(PortDescriptor port) {
  for (auto item : entries_) {
    if (item.second->getPort() != port) {
      continue;
    }

    // TODO(aeckert): It would be nicer if we could just mark this
    // entry stale on port down so we don't need to unprogram the
    // entry (for fast port flaps).  However, we have seen packet
    // losses if we start forwarding packets on a port up event before
    // we receive a neighbor reply so it may not be worth leaving it
    // programmed. Also we need to notify the HwSwitch for ECMP expand
    // when the port comes back up and changing an entry from pending
    // to reachable is how we currently do this.
    setPendingEntry(item.second->getIP(), port, true);
  }
}

template <typename NTable>
void NeighborCacheImpl<NTable>::portFlushEntries(PortDescriptor port) {
  std::vector<AddressType> entriesToFlush;
  for (auto item : entries_) {
    if (item.second->getPort() != port) {
      continue;
    }
    entriesToFlush.push_back(item.second->getIP());
  }

  for (const auto& ip : entriesToFlush) {
    XLOG(DBG2) << "Flush neighbor entry " << ip.str() << " on port " << port;
    flushEntry(ip);
  }
}

template <typename NTable>
template <typename NeighborEntryThrift>
std::list<NeighborEntryThrift> NeighborCacheImpl<NTable>::getCacheData() const {
  std::list<NeighborEntryThrift> thriftEntries;
  for (const auto& item : entries_) {
    NeighborEntryThrift thriftEntry;
    item.second->populateThriftEntry(thriftEntry);
    thriftEntries.push_back(thriftEntry);
  }
  return thriftEntries;
}

template <typename NTable>
template <typename NeighborEntryThrift>
std::optional<NeighborEntryThrift> NeighborCacheImpl<NTable>::getCacheData(
    AddressType ip) const {
  std::optional<NeighborEntryThrift> cachedNeighborEntry;
  auto entry = getCacheEntry(ip);
  if (entry) {
    NeighborEntryThrift thriftEntry;
    entry->populateThriftEntry(thriftEntry);
    cachedNeighborEntry = std::move(thriftEntry);
  }
  return cachedNeighborEntry;
}

} // namespace facebook::fboss
