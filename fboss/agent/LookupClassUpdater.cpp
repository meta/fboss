/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/LookupClassUpdater.h"
#include <type_traits>
#include <utility>

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/NeighborTableDeltaCallbackGenerator.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

namespace {

int getClassIDIndexFromMac(folly::MacAddress mac) {
  // find queue id based on physical mac address
  // Assume MH-NIC host mac follow this pattern
  // mac0 : Host0, mac0 is even, e.g. (000 >> 1) = 0, thus firstClassID
  // mac0 + 1 : OOB
  // mac0 + 2 : Host1 e.g. (010 >> 1) = 1, thus second classID
  // mac0 + 4 : Host2 e.g. (100 >> 1) = 2, thus thrid classID
  // mac0 + 6 : Host3 e.g. (110 >> 1) = 3, thus fourth classID
  int lastThreeBits = mac.u64HBO() & 0b111;
  int idx = 0;
  if ((lastThreeBits & 0b1) == 1) {
    // oob mac
    idx = 4;
  } else {
    // physical host mac
    idx = lastThreeBits >> 1;
  }
  return idx;
}

} // namespace

namespace facebook::fboss {

LookupClassUpdater::LookupClassUpdater(SwSwitch* sw) : sw_(sw) {
  sw_->registerStateObserver(this, "LookupClassUpdater");
}
LookupClassUpdater::~LookupClassUpdater() {
  sw_->unregisterStateObserver(this);
}

int LookupClassUpdater::getRefCnt(
    PortID portID,
    const folly::MacAddress mac,
    VlanID vlanID,
    cfg::AclLookupClass classID) {
  auto& macAndVlan2ClassIDAndRefCnt = port2MacAndVlanEntries_[portID];
  auto iter = macAndVlan2ClassIDAndRefCnt.find(std::make_pair(mac, vlanID));
  if (iter == macAndVlan2ClassIDAndRefCnt.end()) {
    return 0;
  } else {
    auto& [_classID, _refCnt] = iter->second;
    CHECK(classID == _classID);
    return _refCnt;
  }
}

cfg::AclLookupClass LookupClassUpdater::getClassIDwithMinimumNeighbors(
    const ClassID2Count& classID2Count) const {
  auto minItr = std::min_element(
      classID2Count.begin(),
      classID2Count.end(),
      [=](const auto& l, const auto& r) { return l.second < r.second; });

  CHECK(minItr != classID2Count.end());
  return minItr->first;
}

cfg::AclLookupClass LookupClassUpdater::getClassIDwithQueuePerPhysicalHost(
    const ClassID2Count& classID2Count,
    const folly::MacAddress& mac) const {
  auto oui = getMacOui(mac);
  if (vendorMacOuis_.find(oui) != vendorMacOuis_.end()) {
    int idx = getClassIDIndexFromMac(mac);
    if (idx >= classID2Count.size()) {
      XLOG(WARN)
          << "unable to use classID at idx " << idx
          << " and the total number of classIDs is " << classID2Count.size()
          << ", use defaut allocated classID instead. Please verify agent config is valid.";
      return getClassIDwithMinimumNeighbors(classID2Count);
    }
    return classID2Count.nth(idx)->first;
  }
  if (metaMacOuis_.find(oui) == metaMacOuis_.end() &&
      !mac.isLocallyAdministered()) {
    XLOG(WARN) << "found outlier mac address " << mac.toString();
  } else {
    XLOG(DBG2) << "found VM mac address " << mac.toString();
  }
  // use old classID allocation scheme for VM macs or outlier macs
  return getClassIDwithMinimumNeighbors(classID2Count);
}

template <typename RemovedEntryT>
void LookupClassUpdater::removeClassIDForPortAndMac(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlan,
    const std::shared_ptr<RemovedEntryT>& removedEntry) {
  CHECK(removedEntry->getPort().isPhysicalPort());
  CHECK(!isNoHostRoute(removedEntry));

  auto portID = removedEntry->getPort().phyPortID();
  auto port = switchState->getPorts()->getNodeIf(portID);

  bool isDropClassID = removedEntry->getClassID().has_value() &&
      removedEntry->getClassID().value() == cfg::AclLookupClass::CLASS_DROP;

  if (!portHasClassID(port) && !isDropClassID) {
    /*
     * Only downlink ports of Yosemite RSW will have lookupClasses
     * configured. For all other ports, control returns here.
     *
     * There is one exception to this: if a traffic to a neighbor is blocked,
     * it has CLASS_DROP associated with it on MH-NIC as well as non-MH-NIC
     * setup. Thus, continue to process CLASS_DROP.
     */
    return;
  }

  removeNeighborFromLocalCacheForEntry(removedEntry, vlan);

  if constexpr (std::is_same_v<RemovedEntryT, MacEntry>) {
    auto removeMacClassIDFn = [vlan, removedEntry](
                                  const std::shared_ptr<SwitchState>& state) {
      return MacTableUtils::removeClassIDForEntry(state, vlan, removedEntry);
    };

    sw_->updateState(
        "remove classID: " + removedEntry->str(),
        std::move(removeMacClassIDFn));
  } else {
    auto updater = sw_->getNeighborUpdater();
    updater->updateEntryClassID(vlan, removedEntry->getIP());
  }
}

bool LookupClassUpdater::isInited(PortID portID) {
  return port2MacAndVlanEntries_.find(portID) !=
      port2MacAndVlanEntries_.end() &&
      port2ClassIDAndCount_.find(portID) != port2ClassIDAndCount_.end();
}

void LookupClassUpdater::initPort(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Port>& port) {
  auto& macAndVlan2ClassIDAndRefCnt = port2MacAndVlanEntries_[port->getID()];
  auto& classID2Count = port2ClassIDAndCount_[port->getID()];

  macAndVlan2ClassIDAndRefCnt.clear();
  classID2Count.clear();

  for (auto classID : port->getLookupClassesToDistributeTrafficOn()) {
    classID2Count[classID] = 0;
  }
  port2MacAndVlanEntriesUpdated_ = true;
}

template <typename NewEntryT>
bool LookupClassUpdater::isPresentInMacAddrsBlockList(
    VlanID vlanID,
    const std::shared_ptr<NewEntryT>& newEntry) {
  // Check if macAddress for the newEntry (macEntry or neighibor entry) is
  // present in the block list.
  auto it = macAddrsToBlock_.find(std::make_pair(vlanID, newEntry->getMac()));
  return it != macAddrsToBlock_.end();
}

template <typename NewEntryT>
void LookupClassUpdater::updateNeighborClassID(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlanID,
    const std::shared_ptr<NewEntryT>& newEntry) {
  CHECK(newEntry->getPort().isPhysicalPort());

  auto portID = newEntry->getPort().phyPortID();
  auto port = switchState->getPorts()->getNodeIf(portID);
  if (!port) {
    return;
  }

  auto noLookupClasses = !portHasClassID(port);

  auto setDropClassID = isPresentInMacAddrsBlockList(vlanID, newEntry);

  auto mac = newEntry->getMac();
  auto& macAndVlan2ClassIDAndRefCnt = port2MacAndVlanEntries_[port->getID()];
  auto& classID2Count = port2ClassIDAndCount_[port->getID()];

  cfg::AclLookupClass classID;

  auto iter = macAndVlan2ClassIDAndRefCnt.find(std::make_pair(mac, vlanID));
  if (iter == macAndVlan2ClassIDAndRefCnt.end()) {
    if (noLookupClasses && !setDropClassID) {
      // For noLookupClasses (non-MH-NIC) only process if CLASS_DROP
      XLOG(DBG2) << "No lookup class and not set drop classID for vlanID "
                 << vlanID << " macAddress " << mac.toString();
      return;
    }

    if (setDropClassID) {
      classID = cfg::AclLookupClass::CLASS_DROP;
    } else {
      classID = getClassIDwithQueuePerPhysicalHost(classID2Count, mac);
    }
    macAndVlan2ClassIDAndRefCnt.insert(
        std::make_pair(
            std::make_pair(mac, vlanID),
            std::make_pair(classID, 1 /* initialize refCnt */)));
    if (!setDropClassID) {
      // classID2Count is maitanined to compute getClassIDwithMinimumNeighbors
      // CLASS_DROP is not part of that computation.
      classID2Count[classID]++;
    }
  } else {
    auto& [_classID, refCnt] = iter->second;
    XLOG(DBG2) << "ClassID and ref count from map" << static_cast<int>(_classID)
               << " " << refCnt << " for vlanID " << vlanID << " macAddress "
               << mac.toString();
    if (noLookupClasses && _classID != cfg::AclLookupClass::CLASS_DROP) {
      // For noLookupClasses (non-MH-NIC) only process if CLASS_DROP
      return;
    }

    classID = _classID;
    refCnt++;
  }

  XLOG(DBG2) << "Updating Qos Policy for Updated Neighbor: port: "
             << newEntry->str() << " classID: " << static_cast<int>(classID);

  if constexpr (std::is_same_v<NewEntryT, MacEntry>) {
    auto updateMacClassIDFn =
        [vlanID, newEntry, classID](const std::shared_ptr<SwitchState>& state) {
          return MacTableUtils::updateOrAddEntryWithClassID(
              state, vlanID, newEntry, classID);
        };

    sw_->updateState(
        folly::to<std::string>(
            "configure lookup classID: ",
            classID,
            " for entry " + newEntry->str()),
        std::move(updateMacClassIDFn));
  } else {
    auto updater = sw_->getNeighborUpdater();
    updater->updateEntryClassID(vlanID, newEntry->getIP(), classID);
  }
  port2MacAndVlanEntriesUpdated_ = true;
}

template <typename NeighborEntryT>
bool LookupClassUpdater::shouldProcessNeighborEntry(
    const std::shared_ptr<NeighborEntryT>& oldEntry,
    const std::shared_ptr<NeighborEntryT>& newEntry) const {
  /*
   * At this point in time, queue-per-host fix is needed (and thus
   * supported) for physical link only.
   */
  if (!(newEntry && newEntry->getPort().isPhysicalPort())) {
    return false;
  }
  bool added;
  if (oldEntry) {
    // from processChanged()
    if constexpr (std::is_same_v<NeighborEntryT, MacEntry>) {
      // new mac entry created by mac move should also be processed
      // as a newly created mac entry. The old entry may be on a non
      // physical port (e.g. LAG), which is always a move.
      added = oldEntry->getPort().phyPortIDIf() !=
          newEntry->getPort().phyPortIDIf();
    } else {
      // pending to reachable state change should be processed as a newly
      // added neighbor entry
      added = !oldEntry->isReachable() && newEntry->isReachable();
    }
  } else {
    // from processAdded()
    added = true;
  }
  /*
   * If newEntry already has classID populated
   * This can happen in three cases, only process third case:
   *  o Warmboot: prior to warmboot, neighbor entries may have a classID
   *    associated with them. updateStateObserverLocalCache() consumes this
   *    info to populate its local cache, so do nothing here.
   *  o Once LookupClassUpdater chooses classID for a neighbor, it
   *    schedules a state update. After the state update is run, all state
   *    observers are notified. At that time, LookupClassUpdater will
   *    receive a stateDelta that contains classID assigned to new neighbor
   *    entry. Since this will be side-effect of state update that
   *    LookupClassUpdater triggered, there is nothing to do here.
   *  o Racing scenarios: might happen when neighbor entry flaps.
   *    LookupClassUpdater assigns class id to mac entry and trigger mac
   *    entry state updateOrAdd(). However, this mac entry might be removed
   *    shortly by StaticL2ForNeighborUpdate. As a result, another new mac
   *    entry with class id is created when processing mac entry updateOrAdd().
   *    We still need to process this new mac entry to keep track of refCnt,
   *    although this entry woud be removed later.
   */
  if (newEntry->getClassID().has_value() &&
      (!inited_ /* case 1 */ || !added /*case 2*/)) {
    return false;
  }

  if (isNoHostRoute(newEntry)) {
    return false;
  }

  return true;
}

template <typename AddedNeighborEntryT>
void LookupClassUpdater::processAdded(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlan,
    const std::shared_ptr<AddedNeighborEntryT>& addedEntry) {
  CHECK(addedEntry);
  if (!shouldProcessNeighborEntry(
          std::shared_ptr<AddedNeighborEntryT>(nullptr), addedEntry)) {
    XLOG(DBG2) << "Skip processing added neighbor entry " << addedEntry->str();
    return;
  }
  CHECK(addedEntry->getPort().isPhysicalPort());
  if constexpr (std::is_same_v<AddedNeighborEntryT, MacEntry>) {
    updateNeighborClassID(switchState, vlan, addedEntry);
  } else {
    if (addedEntry->isReachable()) {
      updateNeighborClassID(switchState, vlan, addedEntry);
    }
  }
}

template <typename RemovedNeighborEntryT>
void LookupClassUpdater::processRemoved(
    const std::shared_ptr<SwitchState>& switchState,
    VlanID vlan,
    const std::shared_ptr<RemovedNeighborEntryT>& removedEntry) {
  CHECK(removedEntry);
  /*
   * At this point in time, queue-per-host fix is needed (and thus
   * supported) for physical link only.
   * Entries with no Host Route do not have classID.
   */
  if (!removedEntry->getPort().isPhysicalPort() ||
      isNoHostRoute(removedEntry)) {
    return;
  }

  if constexpr (!std::is_same_v<RemovedNeighborEntryT, MacEntry>) {
    // Mirror processAdded: Pending entries (broadcast MAC) were never cached.
    if (!removedEntry->isReachable()) {
      return;
    }
  }

  removeClassIDForPortAndMac(switchState, vlan, removedEntry);
}

template <typename ChangedNeighborEntryT>
void LookupClassUpdater::processChanged(
    const StateDelta& stateDelta,
    VlanID vlan,
    const std::shared_ptr<ChangedNeighborEntryT>& oldEntry,
    const std::shared_ptr<ChangedNeighborEntryT>& newEntry) {
  CHECK(oldEntry);
  CHECK(newEntry);
  if (!(shouldProcessNeighborEntry(oldEntry, newEntry) &&
        oldEntry->getPort().isPhysicalPort())) {
    // TODO - ideally we shouldn't care about whether
    // the old port was a non physical port (LAG) or not
    // but unfortunately our logic below assumes that both
    // previous and this new entry ports are physical ports.
    XLOG(DBG2) << "Skipped processing changed neighbor entry from "
               << oldEntry->str() << " to " << newEntry->str();
    return;
  }
  CHECK(oldEntry->getPort().isPhysicalPort());
  CHECK(newEntry->getPort().isPhysicalPort());

  if constexpr (std::is_same_v<ChangedNeighborEntryT, MacEntry>) {
    /*
     * MAC Move
     */
    if (oldEntry->getPort().phyPortID() != newEntry->getPort().phyPortID()) {
      removeClassIDForPortAndMac(stateDelta.oldState(), vlan, oldEntry);
      updateNeighborClassID(stateDelta.newState(), vlan, newEntry);
    }
  } else {
    CHECK_EQ(oldEntry->getIP(), newEntry->getIP());
    if (!oldEntry->isReachable() && newEntry->isReachable()) {
      updateNeighborClassID(stateDelta.newState(), vlan, newEntry);
    } else if (oldEntry->isReachable() && !newEntry->isReachable()) {
      removeClassIDForPortAndMac(stateDelta.oldState(), vlan, oldEntry);
    }
    /*
     * If the neighbor remains reachable as before but resolves to different
     * MAC/port, remove the classID for oldEntry and add classID for newEntry.
     */
    else if (
        (oldEntry->isReachable() && newEntry->isReachable()) &&
        (oldEntry->getPort().phyPortID() != newEntry->getPort().phyPortID() ||
         oldEntry->getMac() != newEntry->getMac())) {
      removeClassIDForPortAndMac(stateDelta.oldState(), vlan, oldEntry);
      updateNeighborClassID(stateDelta.newState(), vlan, newEntry);
    }
  }
}

bool LookupClassUpdater::portHasClassID(const std::shared_ptr<Port>& port) {
  return port && port->getLookupClassesToDistributeTrafficOn().size();
}

template <typename AddrT>
void LookupClassUpdater::clearClassIdsForResolvedNeighbors(
    const std::shared_ptr<SwitchState>& switchState,
    PortID portID) {
  auto port = switchState->getPorts()->getNodeIf(portID);
  if (!port) {
    // from empty old state during SwSwitch init
    return;
  }
  for (auto vlanMember : port->getVlans()) {
    auto vlanID = vlanMember.first;
    auto vlan = switchState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    using EntryType = typename detail::EntryType<AddrT>::type;
    for (auto iter :
         std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
             switchState, vlan))) {
      EntryType entry = nullptr;
      entry = iter.second;
      /*
       * At this point in time, queue-per-host fix is needed (and thus
       * supported) for physical link only.
       */
      if (entry->getPort().isPhysicalPort() &&
          entry->getPort().phyPortID() == portID &&
          entry->getClassID().has_value() && !isNoHostRoute(entry)) {
        removeNeighborFromLocalCacheForEntry(entry, vlanID);
        if constexpr (std::is_same_v<AddrT, MacAddress>) {
          auto removeMacClassIDFn =
              [vlanID, entry](const std::shared_ptr<SwitchState>& state) {
                return MacTableUtils::removeClassIDForEntry(
                    state, vlanID, entry);
              };

          sw_->updateState(
              "remove classID: " + entry->str(), std::move(removeMacClassIDFn));
        } else {
          auto updater = sw_->getNeighborUpdater();
          updater->updateEntryClassID(vlanID, entry.get()->getIP());
        }
      }
    }
  }
}

template <typename AddrT>
void LookupClassUpdater::repopulateClassIdsForResolvedNeighbors(
    const std::shared_ptr<SwitchState>& switchState,
    PortID portID) {
  auto port = switchState->getPorts()->getNodeIf(portID);
  for (auto vlanMember : port->getVlans()) {
    auto vlanID = vlanMember.first;
    auto vlan = switchState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    using EntryType = typename detail::EntryType<AddrT>::type;
    for (auto iter :
         std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
             switchState, vlan))) {
      EntryType entry = nullptr;
      entry = iter.second;
      /*
       * At this point in time, queue-per-host fix is needed (and thus
       * supported) for physical link only.
       */
      if (entry->getPort().isPhysicalPort() &&
          entry->getPort().phyPortID() == portID && !isNoHostRoute(entry)) {
        updateNeighborClassID(switchState, vlanID, entry);
      }
    }
  }
}

template <typename AddrT>
void LookupClassUpdater::validateRemovedPortEntries(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    PortID portID) {
  /*
   * At this point in time, queue-per-host fix is needed (and thus
   * supported) for physical link only.
   */

  using EntryType = typename detail::EntryType<AddrT>::type;
  for (auto iter :
       std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
           switchState, vlan))) {
    EntryType entry = nullptr;
    entry = iter.second;
    if (entry->getPort().isPhysicalPort()) {
      CHECK(entry->getPort().phyPortID() != portID);
    }
  }
}

void LookupClassUpdater::processPortAdded(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Port>& addedPort) {
  CHECK(addedPort);
  if (addedPort->getLookupClassesToDistributeTrafficOn().size() == 0) {
    /*
     * Only downlink ports of Yosemite RSW will have lookupClasses
     * configured. For all other ports, control returns here.
     */
    return;
  }

  if (!isInited(addedPort->getID())) {
    initPort(switchState, addedPort);
  }
}

void LookupClassUpdater::processPortRemoved(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Port>& removedPort) {
  CHECK(removedPort);
  auto portID = removedPort->getID();

  /*
   * The port that is being removed should not be next hop for any neighbor.
   * in the new switch state.
   */
  for (auto vlanMember : removedPort->getVlans()) {
    auto vlanID = vlanMember.first;
    auto vlan = switchState->getVlans()->getNodeIf(vlanID);
    if (!vlan) {
      continue;
    }

    validateRemovedPortEntries<folly::MacAddress>(switchState, vlan, portID);
    validateRemovedPortEntries<folly::IPAddressV6>(switchState, vlan, portID);
    validateRemovedPortEntries<folly::IPAddressV4>(switchState, vlan, portID);
  }

  port2MacAndVlanEntries_.erase(portID);
  port2ClassIDAndCount_.erase(portID);
  port2MacAndVlanEntriesUpdated_ = true;
}

void LookupClassUpdater::processPortChanged(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  CHECK(oldPort && newPort);
  CHECK_EQ(oldPort->getID(), newPort->getID());
  /*
   * If the lists of lookupClasses are identical (common case), do nothing.
   * If the lists are different e.g. fewer/more classes (unlikely - but the
   * list of classes can go from > 0 to 0 if this feature is disabled),
   * clear all the ClassIDs associated with every resolved neighbor for this
   * egress port. Then, repopulate classIDs using the newPort's list of
   * lookupClasses.
   * To avoid unncessary shuffling when order of lookup classes changes,
   * (e.g. due to config change), sort and compare. This requires a deep
   * copy and sorting, but in practice, the list of lookup classes should be
   * small (< 10).
   */
  auto oldLookupClasses{oldPort->getLookupClassesToDistributeTrafficOn()};
  auto newLookupClasses{newPort->getLookupClassesToDistributeTrafficOn()};

  sort(oldLookupClasses.begin(), oldLookupClasses.end());
  sort(newLookupClasses.begin(), newLookupClasses.end());

  /*
   * We don't need to handle port down here: on port down,
   *  - ARP/NDP neighbors go to pending and processChanged()
   *    disassociates classID from the neighbor entry.
   *  - L2 entries remain in hardware L2 table and thus we retain classID
   *    assocaited with L2 entries even when port is down. However, L2
   *    entries on down ports could be aged out. This will be processed by
   *    processRemoved() and the classID would be freed up.
   */
  if (oldLookupClasses == newLookupClasses) {
    return;
  }

  clearAndRepopulateClassIDsForPort(stateDelta, newPort);
}

void LookupClassUpdater::clearAndRepopulateClassIDsForPort(
    const StateDelta& stateDelta,
    const std::shared_ptr<Port>& port) {
  clearClassIdsForResolvedNeighbors<folly::MacAddress>(
      stateDelta.oldState(), port->getID());
  clearClassIdsForResolvedNeighbors<folly::IPAddressV6>(
      stateDelta.oldState(), port->getID());
  clearClassIdsForResolvedNeighbors<folly::IPAddressV4>(
      stateDelta.oldState(), port->getID());

  initPort(stateDelta.newState(), port);

  repopulateClassIdsForResolvedNeighbors<folly::MacAddress>(
      stateDelta.newState(), port->getID());
  repopulateClassIdsForResolvedNeighbors<folly::IPAddressV6>(
      stateDelta.newState(), port->getID());
  repopulateClassIdsForResolvedNeighbors<folly::IPAddressV4>(
      stateDelta.newState(), port->getID());
}

void LookupClassUpdater::processPortUpdates(const StateDelta& stateDelta) {
  for (const auto& delta : stateDelta.getPortsDelta()) {
    auto oldPort = delta.getOld();
    auto newPort = delta.getNew();

    if (!oldPort && newPort) {
      processPortAdded(stateDelta.newState(), newPort);
    } else if (oldPort && !newPort) {
      /*
       * Asserts port being removed is not next hop for any neighbor.
       * in the newState, thus we need to pass newState to PortRemove.
       */
      processPortRemoved(stateDelta.newState(), oldPort);
    } else {
      processPortChanged(stateDelta, oldPort, newPort);
    }
  }
}

template <typename RemovedEntryT>
void LookupClassUpdater::removeNeighborFromLocalCacheForEntry(
    const std::shared_ptr<RemovedEntryT>& removedEntry,
    VlanID vlanID) {
  CHECK(removedEntry->getPort().isPhysicalPort());
  CHECK(!isNoHostRoute(removedEntry));

  auto portID = removedEntry->getPort().phyPortID();
  auto mac = removedEntry->getMac();

  XLOG(DBG2) << "Remove neighbor entry. Port: " << portID
             << " , mac: " << mac.toString() << ", vlan: " << vlanID;
  if (!isInited(portID)) {
    XLOG(ERR) << "LookupClass not even inited for port: " << portID
              << " , mac: " << mac.toString();
    return;
  }

  auto& macAndVlan2ClassIDAndRefCnt = port2MacAndVlanEntries_[portID];
  auto& [classID, refCnt] =
      macAndVlan2ClassIDAndRefCnt[std::make_pair(mac, vlanID)];
  auto& classID2Count = port2ClassIDAndCount_[portID];

  XLOG(DBG2) << "Reference count: " << static_cast<int>(refCnt)
             << " for classID: " << static_cast<int>(classID);
  CHECK_GT(refCnt, 0);

  refCnt--;

  bool isDropClassID = removedEntry->getClassID().has_value() &&
      removedEntry->getClassID().value() == cfg::AclLookupClass::CLASS_DROP;

  if (refCnt == 0) {
    if (!isDropClassID) {
      // classID2Count is maitanined to compute getClassIDwithMinimumNeighbors
      // CLASS_DROP is not part of that computation.
      classID2Count[classID]--;
      CHECK_GE(classID2Count[classID], 0);
    }

    macAndVlan2ClassIDAndRefCnt.erase(std::make_pair(mac, vlanID));
  }
  port2MacAndVlanEntriesUpdated_ = true;
}

template <typename NewEntryT>
void LookupClassUpdater::updateStateObserverLocalCacheForEntry(
    const std::shared_ptr<NewEntryT>& newEntry,
    VlanID vlanID) {
  CHECK(newEntry->getPort().isPhysicalPort());

  auto portID = newEntry->getPort().phyPortID();
  auto& macAndVlan2ClassIDAndRefCnt = port2MacAndVlanEntries_[portID];
  auto& classID2Count = port2ClassIDAndCount_[portID];
  auto classID = newEntry->getClassID().value();
  auto isDropClassID = (classID == cfg::AclLookupClass::CLASS_DROP);
  auto mac = newEntry->getMac();

  auto iter = macAndVlan2ClassIDAndRefCnt.find(std::make_pair(mac, vlanID));
  if (iter == macAndVlan2ClassIDAndRefCnt.end()) {
    macAndVlan2ClassIDAndRefCnt.insert(
        std::make_pair(
            std::make_pair(mac, vlanID),
            std::make_pair(classID, 1 /* initialize refCnt */)));

    if (!isDropClassID) {
      // classID2Count is maitanined to compute getClassIDwithMinimumNeighbors
      // CLASS_DROP is not part of that computation.
      classID2Count[classID]++;
    }

    XLOG(DBG2) << "Create neighbor entry for port: " << portID
               << " , mac: " << mac.toString() << " , vlan: " << vlanID
               << ", classId: " << static_cast<int>(classID);
  } else {
    auto& [classID_, refCnt] = iter->second;
    if (classID_ != classID) {
      // In multi-switch mode, MAC and NDP classID state updates take
      // different paths: MAC uses direct sw_->updateState() while NDP goes
      // through neighborCacheEvb -> sw_->updateState(). During gracefulExit,
      // pending NDP classID updates may be discarded (isExiting() causes
      // applyUpdate to return early), leaving the saved warm boot state with
      // inconsistent classIDs for the same (mac, vlan) pair.
      // This is corrected by processMacAddrsToBlockUpdates() which runs
      // immediately after this function in stateUpdated().
      XLOG(WARNING) << "ClassID mismatch during warmboot cache init for "
                    << newEntry->str()
                    << ": cached classID=" << static_cast<int>(classID_)
                    << " vs entry classID=" << static_cast<int>(classID)
                    << ". Will be corrected by processMacAddrsToBlockUpdates.";
      // Update cached classID to match the latest entry. Since MAC entries
      // are processed before NDP entries, and both will be corrected by
      // processMacAddrsToBlockUpdates, the specific value chosen here
      // doesn't matter for correctness.
      auto wasDropClassID = (classID_ == cfg::AclLookupClass::CLASS_DROP);
      if (!wasDropClassID) {
        classID2Count[classID_]--;
        CHECK_GE(classID2Count[classID_], 0);
      }
      classID_ = classID;
      if (!isDropClassID) {
        classID2Count[classID]++;
      }
    } else {
      XLOG(DBG2) << "Neighbor entry already exists: " << newEntry->str()
                 << ", expected classID by LookupClassUpdater "
                 << static_cast<int>(classID_);
    }
    refCnt++;
  }
  port2MacAndVlanEntriesUpdated_ = true;
}

template <typename AddrT>
void LookupClassUpdater::updateStateObserverLocalCacheHelper(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    const std::shared_ptr<Port>& port) {
  using EntryType = typename detail::EntryType<AddrT>::type;
  auto table =
      NeighborTableDeltaCallbackGenerator::getTable<AddrT>(switchState, vlan);
  for (auto iter : std::as_const(*table)) {
    EntryType entry = nullptr;
    entry = iter.second;
    if (entry->getPort().isPhysicalPort() &&
        entry->getPort().phyPortID() == port->getID() &&
        entry->getClassID().has_value()) {
      updateStateObserverLocalCacheForEntry(entry, vlan->getID());
    }
  }
}

/*
 * During warmboot, switchState may hold neighbor entries that could already
 * have classIDs assigned prior to the warmboot. This method uses the classID
 * info in switchState to populate mac to classID mappings cached in
 * LookupClassUpdater.
 */
void LookupClassUpdater::updateStateObserverLocalCache(
    const std::shared_ptr<SwitchState>& switchState) {
  CHECK(!inited_);

  for (auto portMap : std::as_const(*switchState->getPorts())) {
    for (auto port : std::as_const(*portMap.second)) {
      initPort(switchState, port.second);
      // THRIFT_COPY
      for (auto vlanMember : port.second->getVlans()) {
        auto vlanID = vlanMember.first;
        auto vlan = switchState->getVlans()->getNodeIf(vlanID);
        if (!vlan) {
          continue;
        }
        updateStateObserverLocalCacheHelper<folly::MacAddress>(
            switchState, vlan, port.second);
        updateStateObserverLocalCacheHelper<folly::IPAddressV6>(
            switchState, vlan, port.second);
        updateStateObserverLocalCacheHelper<folly::IPAddressV4>(
            switchState, vlan, port.second);
      }
    }
  }

  populateMacOuis(switchState, vendorMacOuis_, metaMacOuis_);
}

template <typename AddrT>
void LookupClassUpdater::updateClassIDForEveryNeighborForMac(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    folly::MacAddress macAddress) {
  for (auto iter :
       std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
           switchState, vlan))) {
    auto neighborEntry = iter.second;
    if (!isNoHostRoute(neighborEntry) && neighborEntry->isReachable() &&
        neighborEntry->getMac() == macAddress) {
      updateNeighborClassID(switchState, vlan->getID(), neighborEntry);
    }
  }
}

template <typename AddrT>
void LookupClassUpdater::removeClassIDForEveryNeighborForMac(
    const std::shared_ptr<SwitchState>& switchState,
    const std::shared_ptr<Vlan>& vlan,
    folly::MacAddress macAddress) {
  for (auto iter :
       std::as_const(*NeighborTableDeltaCallbackGenerator::getTable<AddrT>(
           switchState, vlan))) {
    auto neighborEntry = iter.second;
    if (!isNoHostRoute(neighborEntry) && neighborEntry->isReachable() &&
        neighborEntry->getMac() == macAddress) {
      removeClassIDForPortAndMac(switchState, vlan->getID(), neighborEntry);
    }
  }
}

void LookupClassUpdater::processMacAddrsToBlockUpdates(
    const StateDelta& stateDelta) {
  auto newState = stateDelta.newState();
  /*
   * Set of currently blocked MACs: set A
   * New set of MACs to block: set B
   *
   * A - B: set of MACs to unblock
   * B - A: set of MACs to block
   *
   * In this case:
   *  A is macAddrsToBlock_
   *  B is newState->getSwitchSettings()->getMacAddrsToBlock()
   *
   * Approach:
   *   - Compute symmetric difference (disjunctive union) i.e. set of elements
   *     which are in either A or B, but not in their intersection.
   *   - Update macAddrsToBlock_ to newState's macAddrsToBlock
   *   - For every MAC in the symmetric difference:
   *   -    Remove classID associated with the MAC.
   *   -    Remove classID associated with every neighbor using the MAC.
   *   -    Re-add classID for the MAC.
   *   -    Re-add classID for every neighbor using the MAC.
   *
   *   Since macAddrsToBlock_ is updated before calling remove/re-add, if a
   *   MAC address is blocked, Remove will remove queue-per-host/NONE classID
   *   and Re-add will assoicate CLASS_DROP.
   *   Conversely, if a MAC addr is being unblocked, remove will remove
   *   CLASS_DROP and re-add will associate queue-per-host/NONE classID.
   */

  std::set<std::pair<VlanID, folly::MacAddress>> newMacAddrsToBlock;
  for ([[maybe_unused]] const auto& [_, switchSettings] :
       std::as_const(*newState->getSwitchSettings())) {
    for (const auto& iter : *(switchSettings->getMacAddrsToBlock())) {
      auto macStr =
          iter->cref<switch_state_tags::macAddrToBlockAddr>()->toThrift();
      auto vlanID = VlanID(
          iter->cref<switch_state_tags::macAddrToBlockVlanID>()->toThrift());
      auto macAddress = folly::MacAddress(macStr);

      newMacAddrsToBlock.insert(std::make_pair(vlanID, macAddress));
      XLOG(DBG2) << "New blocked mac address " << macStr;
    }
  }

  std::vector<std::pair<VlanID, folly::MacAddress>> toBeUpdatedMacAddrsToBlock;
  std::set_symmetric_difference(
      macAddrsToBlock_.begin(),
      macAddrsToBlock_.end(),
      newMacAddrsToBlock.begin(),
      newMacAddrsToBlock.end(),
      std::inserter(
          toBeUpdatedMacAddrsToBlock, toBeUpdatedMacAddrsToBlock.end()));

  macAddrsToBlock_ = newMacAddrsToBlock;
  for (const auto& [vlanID, macAddress] : toBeUpdatedMacAddrsToBlock) {
    removeAndUpdateClassIDForMacVlan(newState, macAddress, vlanID);
  }
}

void LookupClassUpdater::removeAndUpdateClassIDForMacVlan(
    const std::shared_ptr<SwitchState>& switchState,
    folly::MacAddress macAddress,
    VlanID vlanID) {
  auto vlan = switchState->getVlans()->getNodeIf(vlanID);
  XLOG(DBG2) << "Starting to update classID of mac address "
             << macAddress.toString();
  if (!vlan) {
    XLOG(DBG2) << "No vlan found for vlanID " << vlanID << " macAddress "
               << macAddress.toString() << ". Skip updating mac entry classID.";
    return;
  }

  auto macEntry = vlan->getMacTable()->getMacIf(macAddress);
  if (!macEntry) {
    XLOG(DBG2) << "No mac entry found for in mac table for vlanID " << vlanID
               << " macAddress " << macAddress.toString()
               << ". Skip updating mac entry classID.";
    return;
  }

  removeClassIDForPortAndMac(switchState, vlan->getID(), macEntry);
  removeClassIDForEveryNeighborForMac<folly::IPAddressV4>(
      switchState, vlan, macAddress);
  removeClassIDForEveryNeighborForMac<folly::IPAddressV6>(
      switchState, vlan, macAddress);

  updateNeighborClassID(switchState, vlan->getID(), macEntry);
  updateClassIDForEveryNeighborForMac<folly::IPAddressV4>(
      switchState, vlan, macAddress);
  updateClassIDForEveryNeighborForMac<folly::IPAddressV6>(
      switchState, vlan, macAddress);
}

void LookupClassUpdater::populateMacOuis(
    const std::shared_ptr<SwitchState>& switchState,
    std::set<uint64_t>& vendorMacOuis,
    std::set<uint64_t>& metaMacOuis) {
  auto populateFunc = [](const auto& macOuisFromState,
                         std::set<uint64_t>& macOuis) {
    for (const auto& iter : macOuisFromState) {
      auto macStr = iter->toThrift();
      auto macAddress = folly::MacAddress(macStr);
      macOuis.insert(macAddress.u64HBO());
      XLOG(DBG4) << "insert mac OUI " << macStr;
    }
  };

  for ([[maybe_unused]] const auto& [_, switchSettings] :
       std::as_const(*switchState->getSwitchSettings())) {
    XLOG(DBG4) << "populate vendor mac OUIs";
    populateFunc(*(switchSettings->getVendorMacOuis()), vendorMacOuis);
    XLOG(DBG4) << "populate meta mac OUIs";
    populateFunc(*(switchSettings->getMetaMacOuis()), metaMacOuis);
  }
}

void LookupClassUpdater::processMacOuis(const StateDelta& stateDelta) {
  std::set<uint64_t> newVendorMacOuis;
  std::set<uint64_t> newMetaMacOuis;
  populateMacOuis(stateDelta.newState(), newVendorMacOuis, newMetaMacOuis);

  std::set<uint64_t> updatedVendorMacOuis;
  std::set_symmetric_difference(
      newVendorMacOuis.begin(),
      newVendorMacOuis.end(),
      vendorMacOuis_.begin(),
      vendorMacOuis_.end(),
      std::inserter(updatedVendorMacOuis, updatedVendorMacOuis.end()));
  std::set<uint64_t> updatedMetaMacOuis;
  std::set_symmetric_difference(
      newMetaMacOuis.begin(),
      newMetaMacOuis.end(),
      metaMacOuis_.begin(),
      metaMacOuis_.end(),
      std::inserter(updatedMetaMacOuis, updatedMetaMacOuis.end()));

  // only update assigned classIDs once when mac ouis are populated initially
  // or changed during config replace. Do not trigger classID re-assign logics
  // in the following normal switch state updates.
  if (updatedVendorMacOuis.empty() && updatedMetaMacOuis.empty()) {
    return;
  }

  // update classID
  if (!newVendorMacOuis.empty()) {
    XLOG(DBG2) << "queue_per_physical_host feature is enabled";
    std::vector<std::pair<folly::MacAddress, VlanID>> macAndVlanToUpdate;
    for (const auto& [port, macAndVlan2ClassIDAndRefCnt] :
         port2MacAndVlanEntries_) {
      // for each port
      std::unordered_map<cfg::AclLookupClass, int> classID2NumMacs;
      for (const auto& [macAndVlan, classIDAndRefCnt] :
           macAndVlan2ClassIDAndRefCnt) {
        // count each physical host mac
        auto mac = macAndVlan.first;
        auto oui = getMacOui(mac);
        if (updatedVendorMacOuis.find(oui) != updatedVendorMacOuis.end() ||
            updatedMetaMacOuis.find(oui) != updatedMetaMacOuis.end()) {
          // update classID of newly added (or removed) physical vendor host mac
          macAndVlanToUpdate.push_back(macAndVlan);
        }
      }
    }
    vendorMacOuis_ = newVendorMacOuis;
    metaMacOuis_ = newMetaMacOuis;

    for (const auto& macAndVlan : macAndVlanToUpdate) {
      XLOG(DBG2) << "update classID of mac " << macAndVlan.first.toString()
                 << " vlan " << macAndVlan.second;
      removeAndUpdateClassIDForMacVlan(
          stateDelta.newState(), macAndVlan.first, macAndVlan.second);
    }
    return;
  }
  // else if queue_per_physical_host is disabled
  // rebalance when max(cnt in classID2Cnt) - min(cnt in classID2Cnt) > 1,
  // because getClassIDwithMinimumNeighbors() should not produce
  // such imbalanced assignment results in normal scenario
  XLOG(DBG2) << "queue_per_physical_host feature is disabled";
  vendorMacOuis_ = newVendorMacOuis;
  metaMacOuis_ = newMetaMacOuis;
  for (const auto& [portID, classID2Count] : getPort2ClassIDAndCount()) {
    int maxCnt = 0;
    int minCnt = INT_MAX;
    for (auto const& [classID, cnt] : classID2Count) {
      maxCnt = std::max(maxCnt, cnt);
      minCnt = std::min(minCnt, cnt);
    }
    if (maxCnt - minCnt > 1) {
      XLOG(DBG2) << "mac to classID assignment on port " << portID
                 << " is imbalanced. Rebalance.";
      clearAndRepopulateClassIDsForPort(
          stateDelta, stateDelta.newState()->getPorts()->getNodeIf(portID));
    }
  }
}

void LookupClassUpdater::updateMaxNumHostsPerQueueCounter() {
  if (!port2MacAndVlanEntriesUpdated_) {
    // no need to update max number of physical host per queue counter
    // since no change to port2MacAndVlanEntries_;
    return;
  }
  port2MacAndVlanEntriesUpdated_ = false;
  int preMaxNumHostsPerQueue = maxNumHostsPerQueue_;
  maxNumHostsPerQueue_ = 0;
  PortID maxPortID;
  cfg::AclLookupClass maxClassID;
  for (const auto& [port, macAndVlan2ClassIDAndRefCnt] :
       port2MacAndVlanEntries_) {
    // for each port
    std::unordered_map<cfg::AclLookupClass, int> classID2NumMacs;
    for (const auto& [macAndVlan, classIDAndRefCnt] :
         macAndVlan2ClassIDAndRefCnt) {
      // count each physical host mac
      auto mac = macAndVlan.first;
      auto oui = getMacOui(mac);
      auto classID = classIDAndRefCnt.first;
      if (vendorMacOuis_.find(oui) != vendorMacOuis_.end()) {
        classID2NumMacs[classID]++;
      } else if (
          metaMacOuis_.find(oui) == metaMacOuis_.end() &&
          !mac.isLocallyAdministered()) {
        XLOG(DBG4)
            << "found outlier mac, need to update host mac oui or meta mac oui for "
            << mac.toString();
        // also count outlier mac
        classID2NumMacs[classID]++;
      }
      if (classID2NumMacs[classID] >= maxNumHostsPerQueue_) {
        maxNumHostsPerQueue_ = classID2NumMacs[classID];
        maxClassID = classID;
        maxPortID = port;
      }
    }
  }
  XLOG(DBG4) << "new maximum number of hosts per host is "
             << maxNumHostsPerQueue_;
  // warn if more than one physical hosts are assigned to the same
  // class id/queue
  if (maxNumHostsPerQueue_ > 1 &&
      maxNumHostsPerQueue_ != preMaxNumHostsPerQueue) {
    XLOG(WARN) << maxNumHostsPerQueue_
               << " physical hosts are allocated to port " << maxPortID
               << " classID " << static_cast<int>(maxClassID);
  }
}

void LookupClassUpdater::stateUpdated(const StateDelta& stateDelta) {
  // The current LookupClassUpdater logic relies on VLANs, MAC learning
  // callbacks etc. that are not supported on VOQ/Fabric switches. Thus, run
  // this stateObserver for NPU switches only.
  if (!sw_->getSwitchInfoTable().haveL3Switches() ||
      sw_->getSwitchInfoTable().l3SwitchType() != cfg::SwitchType::NPU) {
    return;
  }

  if (!inited_) {
    updateStateObserverLocalCache(stateDelta.newState());
  } else {
    // upgrade classID only when inited, at which point neighbor caches are
    // already populated in neighborUpdater
    processMacOuis(stateDelta);
  }
  NeighborTableDeltaCallbackGenerator::genCallbacks(stateDelta, *this);
  processPortUpdates(stateDelta);
  processMacAddrsToBlockUpdates(stateDelta);
  updateMaxNumHostsPerQueueCounter();
  inited_ = true;
}

} // namespace facebook::fboss
