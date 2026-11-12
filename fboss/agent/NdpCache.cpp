/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/NdpCache.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/types.h"

#include <folly/IPAddressV6.h>
#include <folly/MacAddress.h>
#include <folly/logging/xlog.h>
#include <netinet/icmp6.h>

namespace facebook::fboss {

NdpCache::NdpCache(
    SwSwitch* sw,
    const SwitchState* state,
    VlanID vlanID,
    std::string vlanName,
    InterfaceID intfID)
    : NeighborCache<NdpTable>(
          sw,
          vlanID,
          vlanName,
          intfID,
          state->getNdpTimeout(),
          state->getMaxNeighborProbes(),
          state->getStaleEntryInterval()) {}

void NdpCache::sentNeighborSolicitation(folly::IPAddressV6 ip) {
  // Pending entry points to CPU port
  setPendingEntry(ip, PortDescriptor(PortID(0)));
}

void NdpCache::resolve(folly::IPAddressV6 ip) {
  auto intf = getSw()->getState()->getInterfaces()->getNode(getIntfID());
  // Returns true only if a new pending entry was created.
  if (setPendingEntry(ip, PortDescriptor(PortID(0)))) {
    IPv6Handler::sendMulticastNeighborSolicitation(
        getSw(), ip, intf->getMac(), getSw()->getVlanIDForTx(intf));
  }
}

void NdpCache::receivedNeighborSolicitationMine(
    folly::IPAddressV6 ip,
    folly::MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type) {
  CHECK_EQ(
      static_cast<uint8_t>(type),
      static_cast<uint8_t>(ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION));

  auto fields = cloneEntryFields(ip);
  bool fieldsMatch = fields && fields->mac == mac && fields->port == port;

  // if we receive a neighbor solicitation that has different
  // fields, replace the entry with a new STALE entry with the
  // new fields
  if (!fields || !fieldsMatch) {
    setEntry(ip, mac, port, NeighborEntryState::STALE);
  }
}

void NdpCache::receivedNeighborAdvertisementMine(
    folly::IPAddressV6 ip,
    folly::MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  CHECK_EQ(
      static_cast<uint8_t>(type),
      static_cast<uint8_t>(ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT));

  bool override = flags & ND_NA_FLAG_OVERRIDE;
  bool solicited = flags & ND_NA_FLAG_SOLICITED;

  auto fields = cloneEntryFields(ip);
  bool fieldsMatch = fields && fields->mac == mac && fields->port == port;
  bool isIncomplete = fields && fields->state == NeighborState::PENDING;

  if (!fields || isIncomplete || override) {
    if (solicited) {
      setEntry(ip, mac, port, NeighborEntryState::REACHABLE);
    } else {
      setEntry(ip, mac, port, NeighborEntryState::STALE);
    }
  } else if (!override) {
    if (solicited) {
      if (fieldsMatch) {
        updateEntryState(ip, NeighborEntryState::REACHABLE);
      } else {
        updateEntryState(ip, NeighborEntryState::STALE);
      }
    }
  }
}

void NdpCache::receivedNdpMine(
    folly::IPAddressV6 ip,
    folly::MacAddress mac,
    PortDescriptor port,
    ICMPv6Type type,
    uint32_t flags) {
  switch (type) {
    case ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT:
      receivedNeighborAdvertisementMine(ip, mac, port, type, flags);
      break;
    case ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_SOLICITATION:
      receivedNeighborSolicitationMine(ip, mac, port, type);
      break;
    default:
      throw FbossError(
          "Unexpected NDP packet type ", static_cast<uint8_t>(type));
  }
}

void NdpCache::receivedNdpNotMine(
    folly::IPAddressV6 /* ip */,
    folly::MacAddress /* mac */,
    PortDescriptor /* port */,
    ICMPv6Type /* type */,
    uint32_t /* flags */) {
  // Note that ARP updates the forward entry mapping here if necessary.
  // We could potentially do the same here, although the IPv6 NDP RFC
  // doesn't appear to recommend this--it states that we MUST silently
  // discard the packet if the target IP isn't ours.
}

inline void NdpCache::checkReachability(
    folly::IPAddressV6 targetIP,
    folly::MacAddress targetMac,
    PortDescriptor port) const {
  const auto state = getSw()->getState();

  InterfaceID intfID;
  std::shared_ptr<Interface> srcIntf;
  switch (port.type()) {
    case PortDescriptor::PortType::PHYSICAL:
      intfID = getSw()->getState()->getInterfaceIDForPort(port);
      srcIntf = state->getInterfaces()->getNodeIf(intfID);
      break;
    case PortDescriptor::PortType::AGGREGATE: {
      auto aggregatePort =
          getSw()->getState()->getAggregatePorts()->getNodeIf(port.aggPortID());
      if (!aggregatePort) {
        XLOG(ERR) << "Aggregate port " << port.aggPortID()
                  << " not found. Skip sending probe";
        return;
      }
      intfID = InterfaceID(aggregatePort->getInterfaceIDs()->at(0)->cref());
      srcIntf = state->getInterfaces()->getNodeIf(intfID);
      break;
    }
    case PortDescriptor::PortType::SYSTEM_PORT:
      auto physPortID = getPortID(port.sysPortID(), getSw()->getState());
      intfID = getSw()->getState()->getInterfaceIDForPort(
          PortDescriptor(physPortID));
      srcIntf = state->getInterfaces()->getNodeIf(intfID);
      break;
  }

  if (!srcIntf) {
    // srcIntf must/can never be nullptr
    XLOG(DBG2) << "No interface found " << intfID << ". Skip sending probe";
    return;
  }

  folly::MacAddress srcMac = srcIntf->getMac();
  folly::IPAddressV6 srcIP(folly::IPAddressV6::LINK_LOCAL, srcMac);
  if (srcIntf->canReachAddress(targetIP)) {
    srcIP = srcIntf->getAddressToReach(targetIP)->first.asV6();
  }
  // unicast solicitation
  IPv6Handler::sendUnicastNeighborSolicitation(
      getSw(),
      targetIP,
      targetMac,
      srcIP,
      srcMac,
      getSw()->getVlanIDForTx(srcIntf),
      port);
}

inline void NdpCache::probeFor(folly::IPAddressV6 ip) const {
  // multicast solicitation
  IPv6Handler::sendMulticastNeighborSolicitation(getSw(), ip);
}

std::list<NdpEntryThrift> NdpCache::getNdpCacheData() {
  return getCacheData<NdpEntryThrift>();
}

std::optional<NdpEntryThrift> NdpCache::getNdpCacheData(
    folly::IPAddressV6 ip) const {
  return getCacheData<NdpEntryThrift>(ip);
}

} // namespace facebook::fboss
