/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/NeighborUpdaterImpl.h"
#include "fboss/agent/ArpCache.h"
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/NdpCache.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/ArpTable.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/NdpTable.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"

#include <boost/container/flat_map.hpp>
#include <folly/logging/xlog.h>
#include <list>
#include <mutex>
#include <string>
#include <vector>

using boost::container::flat_map;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;
using std::shared_ptr;

namespace facebook::fboss {

using facebook::fboss::DeltaFunctions::forEachChanged;

NeighborUpdaterImpl::NeighborUpdaterImpl(SwSwitch* sw) : sw_(sw) {}

NeighborUpdaterImpl::~NeighborUpdaterImpl() {}

auto NeighborUpdaterImpl::createCaches(
    const SwitchState* state,
    const Vlan* vlan) -> std::shared_ptr<NeighborCaches> {
  auto caches = std::make_shared<NeighborCaches>(
      sw_, state, vlan->getID(), vlan->getName(), vlan->getInterfaceID());

  // We need to populate the caches from the SwitchState when a vlan is added
  // After this, we no longer process Arp or Ndp deltas for this vlan.
  caches->arpCache->repopulate(vlan->getArpTable());
  caches->ndpCache->repopulate(vlan->getNdpTable());

  return caches;
}

auto NeighborUpdaterImpl::createCachesForIntf(
    const SwitchState* state,
    const Interface* intf) -> std::shared_ptr<NeighborCaches> {
  // TODO(skhare) Remove after completely migrating to intfCaches_
  // at the moment, NeighborCacheImpl has vlanID, vlanName.
  // NPU switches have valid vlanID, vlanName, pass it.
  // For VOQ switches, pass some values.
  // These fields will be removed as we migrate to intfCaches_
  VlanID vlanID{0};
  std::string vlanName = "--";
  if (intf->getType() == cfg::InterfaceType::VLAN) {
    auto vlan = state->getVlans()->getNodeIf(intf->getVlanID());
    CHECK(vlan);
    vlanID = intf->getVlanID();
    vlanName = vlan->getName();
  }

  auto caches = std::make_shared<NeighborCaches>(
      sw_, state, vlanID, vlanName, intf->getID());

  // We need to populate the caches from the SwitchState when a vlan is added
  // After this, we no longer process Arp or Ndp deltas for this vlan.
  caches->arpCache->repopulate(intf->getArpTable());
  caches->ndpCache->repopulate(intf->getNdpTable());

  return caches;
}

shared_ptr<ArpCache> NeighborUpdaterImpl::getArpCacheFor(VlanID vlan) {
  return getArpCacheInternal(vlan);
}

shared_ptr<NdpCache> NeighborUpdaterImpl::getNdpCacheFor(VlanID vlan) {
  return getNdpCacheInternal(vlan);
}

std::list<ArpEntryThrift> NeighborUpdaterImpl::getArpCacheData() {
  std::list<ArpEntryThrift> entries;
  for (auto [_, caches] : intfCaches_) {
    entries.splice(entries.end(), caches->arpCache->getArpCacheData());
  }
  return entries;
}

std::list<NdpEntryThrift> NeighborUpdaterImpl::getNdpCacheData() {
  std::list<NdpEntryThrift> entries;
  for (auto [_, caches] : intfCaches_) {
    entries.splice(entries.end(), caches->ndpCache->getNdpCacheData());
  }
  return entries;
}

std::list<ArpEntryThrift> NeighborUpdaterImpl::getArpCacheDataForIntf() {
  std::list<ArpEntryThrift> entries;
  for (auto it = intfCaches_.begin(); it != intfCaches_.end(); ++it) {
    entries.splice(entries.end(), it->second->arpCache->getArpCacheData());
  }
  return entries;
}

std::list<NdpEntryThrift> NeighborUpdaterImpl::getNdpCacheDataForIntf() {
  std::list<NdpEntryThrift> entries;
  for (auto it = intfCaches_.begin(); it != intfCaches_.end(); ++it) {
    entries.splice(entries.end(), it->second->ndpCache->getNdpCacheData());
  }
  return entries;
}

shared_ptr<ArpCache> NeighborUpdaterImpl::getArpCacheInternal(VlanID vlan) {
  auto res = caches_.find(vlan);
  if (res == caches_.end()) {
    throw FbossError("Tried to get Arp cache non-existent vlan ", vlan);
  }
  return res->second->arpCache;
}
shared_ptr<NdpCache> NeighborUpdaterImpl::getNdpCacheInternal(VlanID vlan) {
  auto res = caches_.find(vlan);
  if (res == caches_.end()) {
    throw FbossError("Tried to get Ndp cache non-existent vlan ", vlan);
  }
  return res->second->ndpCache;
}

shared_ptr<ArpCache> NeighborUpdaterImpl::getArpCacheForIntf(
    InterfaceID intfID) {
  return getArpCacheInternalForIntf(intfID);
}

shared_ptr<ArpCache> NeighborUpdaterImpl::getArpCacheInternalForIntf(
    InterfaceID intfID) {
  auto res = intfCaches_.find(intfID);
  if (res == intfCaches_.end()) {
    throw FbossError("Tried to get ARP cache non-existent interface ", intfID);
  }
  return res->second->arpCache;
}

shared_ptr<NdpCache> NeighborUpdaterImpl::getNdpCacheForIntf(
    InterfaceID intfID) {
  return getNdpCacheInternalForIntf(intfID);
}

shared_ptr<NdpCache> NeighborUpdaterImpl::getNdpCacheInternalForIntf(
    InterfaceID intfID) {
  auto res = intfCaches_.find(intfID);
  if (res == intfCaches_.end()) {
    throw FbossError("Tried to get Ndp cache non-existent interface ", intfID);
  }
  return res->second->ndpCache;
}

void NeighborUpdaterImpl::sentNeighborSolicitation(
    VlanID vlan,
    IPAddressV6 ip) {
  auto cache = getNdpCacheFor(vlan);
  cache->sentNeighborSolicitation(ip);
}

void NeighborUpdaterImpl::receivedNdpMine(
    VlanID vlan,
    IPAddressV6 ip,
    MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  auto cache = getNdpCacheFor(vlan);
  cache->receivedNdpMine(ip, mac, port, type, flags);
}

void NeighborUpdaterImpl::receivedNdpNotMine(
    VlanID vlan,
    IPAddressV6 ip,
    MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  auto cache = getNdpCacheFor(vlan);
  cache->receivedNdpNotMine(ip, mac, port, type, flags);
}

void NeighborUpdaterImpl::sentNeighborSolicitationForIntf(
    InterfaceID intfID,
    IPAddressV6 ip) {
  auto cache = getNdpCacheForIntf(intfID);
  cache->sentNeighborSolicitation(ip);
}

void NeighborUpdaterImpl::resolveNdpForIntf(
    InterfaceID intfID,
    IPAddressV6 ip) {
  auto cache = getNdpCacheForIntf(intfID);
  cache->resolve(ip);
}

void NeighborUpdaterImpl::receivedNdpMineForIntf(
    InterfaceID intfID,
    IPAddressV6 ip,
    MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  auto cache = getNdpCacheForIntf(intfID);
  cache->receivedNdpMine(ip, mac, port, type, flags);
}

void NeighborUpdaterImpl::receivedNdpNotMineForIntf(
    InterfaceID intfID,
    IPAddressV6 ip,
    MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  auto cache = getNdpCacheForIntf(intfID);
  cache->receivedNdpNotMine(ip, mac, port, type, flags);
}

void NeighborUpdaterImpl::sentArpRequest(VlanID vlan, IPAddressV4 ip) {
  auto cache = getArpCacheFor(vlan);
  cache->sentArpRequest(ip);
}

void NeighborUpdaterImpl::receivedArpMine(
    VlanID vlan,
    IPAddressV4 ip,
    MacAddress mac,
    PortDescriptor port,
    ArpOpCode op) {
  auto cache = getArpCacheFor(vlan);
  cache->receivedArpMine(ip, mac, port, op);
}

void NeighborUpdaterImpl::receivedArpNotMine(
    VlanID vlan,
    IPAddressV4 ip,
    MacAddress mac,
    PortDescriptor port,
    ArpOpCode op) {
  auto cache = getArpCacheFor(vlan);
  cache->receivedArpNotMine(ip, mac, port, op);
}

void NeighborUpdaterImpl::sentArpRequestForIntf(
    InterfaceID intfID,
    IPAddressV4 ip) {
  auto cache = getArpCacheForIntf(intfID);
  cache->sentArpRequest(ip);
}

void NeighborUpdaterImpl::receivedArpMineForIntf(
    InterfaceID intfID,
    IPAddressV4 ip,
    MacAddress mac,
    PortDescriptor port,
    ArpOpCode op) {
  auto cache = getArpCacheForIntf(intfID);
  cache->receivedArpMine(ip, mac, port, op);
}

void NeighborUpdaterImpl::receivedArpNotMineForIntf(
    InterfaceID intfID,
    IPAddressV4 ip,
    MacAddress mac,
    PortDescriptor port,
    ArpOpCode op) {
  auto cache = getArpCacheForIntf(intfID);
  cache->receivedArpNotMine(ip, mac, port, op);
}

void NeighborUpdaterImpl::portDown(PortDescriptor port) {
  for (auto id2NbrCaches : intfCaches_) {
    auto arpCache = id2NbrCaches.second->arpCache;
    arpCache->portDown(port);

    auto ndpCache = id2NbrCaches.second->ndpCache;
    ndpCache->portDown(port);
  }
}

void NeighborUpdaterImpl::portFlushEntries(PortDescriptor port) {
  for (auto id2NbrCaches : intfCaches_) {
    auto arpCache = id2NbrCaches.second->arpCache;
    arpCache->portFlushEntries(port);

    auto ndpCache = id2NbrCaches.second->ndpCache;
    ndpCache->portFlushEntries(port);
  }
}

bool NeighborUpdaterImpl::flushEntryImpl(VlanID vlan, IPAddress ip) {
  if (ip.isV4()) {
    auto cache = getArpCacheInternal(vlan);
    return cache->flushEntryBlocking(ip.asV4());
  }
  auto cache = getNdpCacheInternal(vlan);
  return cache->flushEntryBlocking(ip.asV6());
}

uint32_t NeighborUpdaterImpl::flushEntry(VlanID vlan, IPAddress ip) {
  uint32_t count{0};
  if (vlan == VlanID(0)) {
    for (auto it = caches_.begin(); it != caches_.end(); ++it) {
      if (flushEntryImpl(it->first, ip)) {
        ++count;
      }
    }
  } else {
    if (flushEntryImpl(vlan, ip)) {
      ++count;
    }
  }
  return count;
}

bool NeighborUpdaterImpl::flushEntryImplForIntf(
    InterfaceID intfID,
    IPAddress ip) {
  if (ip.isV4()) {
    auto cache = getArpCacheInternalForIntf(intfID);
    return cache->flushEntryBlocking(ip.asV4());
  }
  auto cache = getNdpCacheInternalForIntf(intfID);
  return cache->flushEntryBlocking(ip.asV6());
}

uint32_t NeighborUpdaterImpl::flushEntryForIntf(
    InterfaceID intfID,
    IPAddress ip) {
  uint32_t count{0};
  if (intfID == InterfaceID(0)) {
    for (auto it = intfCaches_.begin(); it != intfCaches_.end(); ++it) {
      if (flushEntryImplForIntf(it->first, ip)) {
        ++count;
      }
    }
  } else {
    if (flushEntryImplForIntf(intfID, ip)) {
      ++count;
    }
  }
  return count;
}

void NeighborUpdaterImpl::vlanAdded(
    VlanID vlanID,
    std::shared_ptr<SwitchState> state) {
  auto vlans = state->getVlans();
  auto vlan = vlans->getNode(vlanID);
  caches_.emplace(vlanID, createCaches(state.get(), vlan.get()));
}

void NeighborUpdaterImpl::vlanDeleted(VlanID vlanID) {
  auto iter = caches_.find(vlanID);
  if (iter != caches_.end()) {
    caches_.erase(iter);
  } else {
    // TODO(aeckert): May want to fatal here when a cache doesn't exist for a
    // specific vlan. Need to make sure that caches are correctly created for
    // the initial SwitchState to avoid false positives
    XLOG(DBG0) << "Deleted Vlan with no corresponding NeighborCaches";
  }
}

void NeighborUpdaterImpl::vlanChanged(
    VlanID vlanID,
    InterfaceID intfID,
    std::string vlanName) {
  auto iter = caches_.find(vlanID);
  if (iter != caches_.end()) {
    iter->second->arpCache->setIntfID(intfID);
    iter->second->ndpCache->setIntfID(intfID);
    iter->second->arpCache->setVlanName(vlanName);
    iter->second->ndpCache->setVlanName(vlanName);
  } else {
    // TODO(aeckert): May want to fatal here when a cache doesn't exist for a
    // specific vlan. Need to make sure that caches are correctly created for
    // the initial SwitchState to avoid false positives
    XLOG(DBG0) << "Changed Vlan with no corresponding NeighborCaches";
  }
}

void NeighborUpdaterImpl::interfaceAdded(
    InterfaceID intfID,
    std::shared_ptr<SwitchState> state) {
  auto intf = state->getInterfaces()->getNode(intfID);
  intfCaches_.emplace(intfID, createCachesForIntf(state.get(), intf.get()));
}

void NeighborUpdaterImpl::interfaceRemoved(InterfaceID intfID) {
  auto iter = intfCaches_.find(intfID);
  if (iter != intfCaches_.end()) {
    intfCaches_.erase(iter);
  } else {
    // TODO(aeckert): May want to fatal here when a cache doesn't exist for a
    // specific vlan. Need to make sure that caches are correctly created for
    // the initial SwitchState to avoid false positives
    XLOG(DBG0) << "Deleted Vlan with no corresponding NeighborCaches";
  }
}

void NeighborUpdaterImpl::timeoutsChanged(
    std::chrono::seconds arpTimeout,
    std::chrono::seconds ndpTimeout,
    std::chrono::seconds staleEntryInterval,
    uint32_t maxNeighborProbes) {
  for (auto& id2NbrCaches : intfCaches_) {
    auto& arpCache = id2NbrCaches.second->arpCache;
    auto& ndpCache = id2NbrCaches.second->ndpCache;
    arpCache->setTimeout(arpTimeout);
    arpCache->setMaxNeighborProbes(maxNeighborProbes);
    arpCache->setStaleEntryInterval(staleEntryInterval);
    ndpCache->setTimeout(ndpTimeout);
    ndpCache->setMaxNeighborProbes(maxNeighborProbes);
    ndpCache->setStaleEntryInterval(staleEntryInterval);
  }
}

void NeighborUpdaterImpl::updateArpEntryClassID(
    VlanID vlan,
    folly::IPAddressV4 ip,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  auto cache = getArpCacheFor(vlan);
  cache->updateEntryClassID(ip, classID);
}

void NeighborUpdaterImpl::updateNdpEntryClassID(
    VlanID vlan,
    folly::IPAddressV6 ip,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  auto cache = getNdpCacheFor(vlan);
  cache->updateEntryClassID(ip, classID);
}

void NeighborUpdaterImpl::updateArpEntryClassIDForIntf(
    InterfaceID intfID,
    folly::IPAddressV4 ip,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  auto cache = getArpCacheForIntf(intfID);
  cache->updateEntryClassID(ip, classID);
}

void NeighborUpdaterImpl::updateNdpEntryClassIDForIntf(
    InterfaceID intfID,
    folly::IPAddressV6 ip,
    std::optional<cfg::AclLookupClass> classID = std::nullopt) {
  auto cache = getNdpCacheForIntf(intfID);
  cache->updateEntryClassID(ip, classID);
}

uint32_t NeighborUpdaterImpl::getProbesLeft(
    const InterfaceID& intfID,
    const folly::IPAddressV6& ip) {
  auto cache = getNdpCacheForIntf(intfID);
  auto ndpEntry = cache->getNdpCacheData(ip);
  return ndpEntry.has_value() && ndpEntry->probesLeft().has_value()
      ? *ndpEntry->probesLeft()
      : 0;
}

uint32_t NeighborUpdaterImpl::getProbesLeftIPv4(
    const InterfaceID& intfID,
    const folly::IPAddressV4& ip) {
  auto cache = getArpCacheForIntf(intfID);
  auto arpEntry = cache->getArpCacheData(ip);
  return arpEntry.has_value() && arpEntry->probesLeft().has_value()
      ? *arpEntry->probesLeft()
      : 0;
}

uint32_t NeighborUpdaterImpl::getMaxNeighborProbes(const InterfaceID& intfID) {
  auto cache = getNdpCacheForIntf(intfID);
  return cache->getMaxNeighborProbes();
}

} // namespace facebook::fboss
