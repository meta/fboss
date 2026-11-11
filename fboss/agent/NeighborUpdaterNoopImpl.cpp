/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/NeighborUpdaterNoopImpl.h"

using folly::IPAddress;
using folly::IPAddressV4;
using folly::IPAddressV6;
using folly::MacAddress;

namespace facebook::fboss {

NeighborUpdaterNoopImpl::NeighborUpdaterNoopImpl(SwSwitch* sw) : sw_(sw) {}
NeighborUpdaterNoopImpl::~NeighborUpdaterNoopImpl() {}

std::list<ArpEntryThrift> NeighborUpdaterNoopImpl::getArpCacheData() {
  std::list<ArpEntryThrift> entries{};
  return entries;
}

std::list<NdpEntryThrift> NeighborUpdaterNoopImpl::getNdpCacheData() {
  std::list<NdpEntryThrift> entries{};
  return entries;
}

std::list<ArpEntryThrift> NeighborUpdaterNoopImpl::getArpCacheDataForIntf() {
  std::list<ArpEntryThrift> entries{};
  return entries;
}

std::list<NdpEntryThrift> NeighborUpdaterNoopImpl::getNdpCacheDataForIntf() {
  std::list<NdpEntryThrift> entries{};
  return entries;
}

std::shared_ptr<ArpCache> NeighborUpdaterNoopImpl::getArpCacheForIntf(
    InterfaceID /* intfId */) {
  return nullptr;
}

std::shared_ptr<NdpCache> NeighborUpdaterNoopImpl::getNdpCacheForIntf(
    InterfaceID /* intfId */) {
  return nullptr;
}

std::shared_ptr<ArpCache> NeighborUpdaterNoopImpl::getArpCacheFor(
    VlanID /* vlanId */) {
  return nullptr;
}

std::shared_ptr<NdpCache> NeighborUpdaterNoopImpl::getNdpCacheFor(
    VlanID /* vlanId */) {
  return nullptr;
}

void NeighborUpdaterNoopImpl::portChanged(
    const std::shared_ptr<Port>& /*oldPort*/,
    const std::shared_ptr<Port>& /*newPort*/) {}

void NeighborUpdaterNoopImpl::aggregatePortChanged(
    const std::shared_ptr<AggregatePort>& /*oldAggPort*/,
    const std::shared_ptr<AggregatePort>& /*newAggPort*/) {}

void NeighborUpdaterNoopImpl::sentNeighborSolicitation(
    VlanID /*vlan*/,
    IPAddressV6 /*ip*/) {}

void NeighborUpdaterNoopImpl::receivedNdpMine(
    VlanID /*vlan*/,
    IPAddressV6 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ICMPv6Type /*type*/,
    uint32_t /*flags*/) {}

void NeighborUpdaterNoopImpl::receivedNdpNotMine(
    VlanID /*vlan*/,
    IPAddressV6 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ICMPv6Type /*type*/,
    uint32_t /*flags*/) {}

void NeighborUpdaterNoopImpl::sentNeighborSolicitationForIntf(
    InterfaceID /*intfID*/,
    IPAddressV6 /*ip*/) {}

void NeighborUpdaterNoopImpl::resolveNdpForIntf(
    InterfaceID /*intfID*/,
    IPAddressV6 /*ip*/) {}

void NeighborUpdaterNoopImpl::receivedNdpMineForIntf(
    InterfaceID /*intfID*/,
    IPAddressV6 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ICMPv6Type /*type*/,
    uint32_t /*flags*/) {}

void NeighborUpdaterNoopImpl::receivedNdpNotMineForIntf(
    InterfaceID /*intfID*/,
    IPAddressV6 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ICMPv6Type /*type*/,
    uint32_t /*flags*/) {}

void NeighborUpdaterNoopImpl::sentArpRequest(
    VlanID /*vlan*/,
    IPAddressV4 /*ip*/) {}

void NeighborUpdaterNoopImpl::receivedArpMine(
    VlanID /*vlan*/,
    IPAddressV4 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ArpOpCode /*op*/) {}

void NeighborUpdaterNoopImpl::receivedArpNotMine(
    VlanID /*vlan*/,
    IPAddressV4 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ArpOpCode /*op*/) {}

void NeighborUpdaterNoopImpl::sentArpRequestForIntf(
    InterfaceID /*intfID*/,
    IPAddressV4 /*ip*/) {}

void NeighborUpdaterNoopImpl::resolveArpForIntf(
    InterfaceID /*intfID*/,
    IPAddressV4 /*ip*/) {}

void NeighborUpdaterNoopImpl::receivedArpMineForIntf(
    InterfaceID /*intfID*/,
    IPAddressV4 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ArpOpCode /*op*/) {}

void NeighborUpdaterNoopImpl::receivedArpNotMineForIntf(
    InterfaceID /*intfID*/,
    IPAddressV4 /*ip*/,
    MacAddress /*mac*/,
    PortDescriptor /*port*/,
    ArpOpCode /*op*/) {}

void NeighborUpdaterNoopImpl::portDown(PortDescriptor /*port*/) {}

void NeighborUpdaterNoopImpl::portFlushEntries(PortDescriptor /*port*/) {}

uint32_t NeighborUpdaterNoopImpl::flushEntry(
    VlanID /*vlan*/,
    IPAddress /*ip*/) {
  return 0;
}

uint32_t NeighborUpdaterNoopImpl::flushEntryForIntf(
    InterfaceID /*intfID*/,
    IPAddress /*ip*/) {
  return 0;
}

void NeighborUpdaterNoopImpl::vlanAdded(
    VlanID /*vlanID*/,
    std::shared_ptr<SwitchState> /*state*/) {}

void NeighborUpdaterNoopImpl::vlanDeleted(VlanID /*vlanID*/) {}

void NeighborUpdaterNoopImpl::vlanChanged(
    VlanID /*vlanID*/,
    InterfaceID /*intfID*/,
    std::string /*vlanName*/) {}

void NeighborUpdaterNoopImpl::timeoutsChanged(
    std::chrono::seconds /*arpTimeout*/,
    std::chrono::seconds /*ndpTimeout*/,
    std::chrono::seconds /*staleEntryInterval*/,
    uint32_t /*maxNeighborProbes*/) {}
void NeighborUpdaterNoopImpl::updateArpEntryClassID(
    VlanID /*vlan*/,
    folly::IPAddressV4 /*ip*/,
    std::optional<cfg::AclLookupClass> /*classID*/) {}

void NeighborUpdaterNoopImpl::updateNdpEntryClassID(
    VlanID /*vlan*/,
    folly::IPAddressV6 /*ip*/,
    std::optional<cfg::AclLookupClass> /*classID*/) {}

void NeighborUpdaterNoopImpl::updateArpEntryClassIDForIntf(
    InterfaceID /*intfID*/,
    folly::IPAddressV4 /*ip*/,
    std::optional<cfg::AclLookupClass> /*classID*/) {}

void NeighborUpdaterNoopImpl::updateNdpEntryClassIDForIntf(
    InterfaceID /*intfID*/,
    folly::IPAddressV6 /*ip*/,
    std::optional<cfg::AclLookupClass> /*classID*/) {}

void NeighborUpdaterNoopImpl::interfaceAdded(
    InterfaceID intfID,
    std::shared_ptr<SwitchState> state) {}

void NeighborUpdaterNoopImpl::interfaceRemoved(InterfaceID intfID) {}
uint32_t NeighborUpdaterNoopImpl::getProbesLeft(
    const InterfaceID& /*intfID*/,
    const folly::IPAddressV6& /*ip*/) {
  return 0;
}

uint32_t NeighborUpdaterNoopImpl::getProbesLeftIPv4(
    const InterfaceID& /*intfID*/,
    const folly::IPAddressV4& /*ip*/) {
  return 0;
}

uint32_t NeighborUpdaterNoopImpl::getMaxNeighborProbes(
    const InterfaceID& /*intfID*/) {
  return 0;
}

} // namespace facebook::fboss
