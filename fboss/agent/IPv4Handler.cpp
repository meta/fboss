/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/IPv4Handler.h"

#include <folly/IPAddress.h>
#include <folly/IPAddressV4.h>
#include <folly/MacAddress.h>
#include <folly/io/Cursor.h>
#include <folly/logging/xlog.h>
#include <cstdint>
#include <memory>
#include "fboss/agent/ArpHandler.h"
#include "fboss/agent/DHCPv4Handler.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/IPHeaderV4.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/PortStats.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/ICMPExtHdr.h"
#include "fboss/agent/packet/ICMPHdr.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/rib/RoutingInformationBase.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/Route.h"

#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"

using folly::IPAddress;
using folly::IPAddressV4;
using folly::MacAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;
using std::unique_ptr;

DEFINE_bool(
    ipv4_ext_headers_enabled,
    false,
    "Flag to turn on IPv4 extension headers");

namespace facebook::fboss {

template <typename BodyFn>
std::unique_ptr<TxPacket> createICMPv4Pkt(
    SwSwitch* sw,
    folly::MacAddress dstMac,
    folly::MacAddress srcMac,
    std::optional<VlanID> vlan,
    folly::IPAddressV4& dstIP,
    folly::IPAddressV4& srcIP,
    ICMPv4Type icmpType,
    ICMPv4Code icmpCode,
    uint32_t bodyLength,
    BodyFn serializeBody) {
  IPv4Hdr ipv4(
      srcIP,
      dstIP,
      static_cast<uint8_t>(IP_PROTO::IP_PROTO_ICMP),
      ICMPHdr::SIZE + bodyLength);
  ipv4.computeChecksum();

  ICMPHdr icmp4(
      static_cast<uint8_t>(icmpType), static_cast<uint8_t>(icmpCode), 0);
  uint32_t pktLen = icmp4.computeTotalLengthV4(bodyLength, vlan.has_value());

  auto pkt = sw->allocatePacket(pktLen);
  RWPrivateCursor cursor(pkt->buf());
  icmp4.serializeFullPacket(
      &cursor, dstMac, srcMac, vlan, ipv4, bodyLength, serializeBody);
  return pkt;
}

IPv4Handler::IPv4Handler(SwSwitch* sw) : sw_(sw) {}

void IPv4Handler::sendICMPTimeExceeded(
    PortID port,
    std::optional<VlanID> srcVlan,
    MacAddress dst,
    MacAddress src,
    IPv4Hdr& v4Hdr,
    Cursor cursor) {
  auto state = sw_->getState();
  // Send an ICMP Time Exceeded message with extension headers to show
  // the IP address used on the interface.

  // Build the interface name sub object
  auto srcPort = sw_->getState()->getPort(port);
  std::string srcPortName =
      sw_->getState()->getHostname() + ":" + srcPort->getName();
  auto nameObj =
      std::make_unique<ICMPExtIfaceNameSubObject>(srcPortName.c_str());

  // Build the IP sub object and find the Src IP
  std::unique_ptr<ICMPExtIPSubObject> ipObj = nullptr;
  IPAddressV4 srcIp;
  try {
    auto intfId = sw_->getState()->getInterfaceIDForPort(PortDescriptor(port));
    srcIp = getSwitchIntfIP(state, intfId);
    ipObj = std::make_unique<ICMPExtIpSubObjectV4>(ICMPExtIpSubObjectV4(srcIp));

  } catch (const std::exception&) {
    // Fall back to using a src IP of any interface on the switch, where if
    // none is available, the preconfigured src address
    try {
      srcIp = getAnyIntfIP(state);
    } catch (const FbossError&) {
      srcIp = state->getIcmpV4UnavailableSrcAddress();
    }
  }

  // Build the extension header objects
  ICMPExtHdr icmpExtHdr;

  ICMPExtInterfaceInformationObject icmpIfaceInformation(
      std::move(ipObj), std::move(nameObj));

  // Add the interface identification to the ICMP extension headers
  icmpExtHdr.objects.push_back(&icmpIfaceInformation);

  uint32_t const extHdrLength =
      FLAGS_ipv4_ext_headers_enabled ? icmpExtHdr.length() : 0;

  // RFC1812 extends the "original datagram" field to contain as many
  // octets as possible without causing the ICMP message to exceed the minimum
  // IPv4 reassembly buffer size (576 octets)
  // ...In order to achieve backwards compatibility, when the ICMP Extension
  //  Structure is appended to an ICMP message and that ICMP message
  //  contains an "original datagram" field, the "original datagram" field
  //  MUST contain at least 128 octets.  If the original datagram did not
  //  contain 128 octets, the "original datagram" field MUST be zero padded
  //  to 128 octets.  (See Section 5.1 for rationale.)

  const auto originalDatagramLengthBytes = cursor.length() + v4Hdr.size();

  // Unless we're over 128 bytes, zero pad the payload
  uint8_t lengthIn32BitWords = 32;
  uint8_t paddingBytesRequired = 128 - originalDatagramLengthBytes;

  if (originalDatagramLengthBytes >= 128) {
    // Find out the length of the payload we can add.
    uint16_t maxDatagramLength = 576 // Maximum size of IPv4 payload
        - v4Hdr.size() // Length of the IPv4 header
        - ICMPHdr::SIZE // UCMP header For fields {type, code, checksum}
        - ICMPHdr::ICMPV4_UNUSED_LEN // ICMP fields {unused, length, unused}
        - extHdrLength; // Length of any extension headers

    // The "original datagram" field MUST be zero padded to the nearest
    // 32-bit boundary.

    if (originalDatagramLengthBytes > maxDatagramLength) {
      lengthIn32BitWords = maxDatagramLength / 4;
      paddingBytesRequired = 0;
    } else {
      // Add 3, then divide by 4 to get the number of words in each datagram.
      // adding 3 to round up and account for partial words.
      lengthIn32BitWords = (originalDatagramLengthBytes + 3) / 4;

      // Figure out how much padding is required here.
      paddingBytesRequired =
          lengthIn32BitWords * 4 - originalDatagramLengthBytes;
    }
  }

  // Calculate the size of the payload, excluding first 32 bits of of the header
  // which is landed by the createICMPv4Pkt.
  uint32_t bodyLength =
      ICMPHdr::ICMPV4_UNUSED_LEN // For {unused, length, unused}
      + (lengthIn32BitWords *
         4) // Bytes of the payload (which includes original v4 hdr)
      + extHdrLength; // Size of all the extension headers to include

  // Lambda function to serialize the body with extension headers
  auto serializeBody = [&](RWPrivateCursor* sendCursor) {
    // RFC4884, section 4.2.  Write the length surrounded by unused
    sendCursor->writeBE<uint8_t>(static_cast<uint8_t>(0)); // unused bytes
    sendCursor->writeBE<uint8_t>(lengthIn32BitWords); // Length
    sendCursor->writeBE<uint16_t>(static_cast<uint16_t>(0)); // unused bytes

    // Write the IP header, and as much of the "original datagram"
    // as possible.
    v4Hdr.serialize(sendCursor);
    sendCursor->push(
        cursor.data(),
        lengthIn32BitWords * 4 - v4Hdr.size() - paddingBytesRequired);

    // Pad the payload to the nearest 32-bit boundary
    for (auto i = 0; i < paddingBytesRequired; i++) {
      sendCursor->writeBE<uint8_t>(static_cast<uint8_t>(0));
    }

    // Write the extension header
    if (FLAGS_ipv4_ext_headers_enabled) {
      icmpExtHdr.serialize(sendCursor);
    }
  };

  auto icmpPkt = createICMPv4Pkt(
      sw_,
      dst,
      src,
      srcVlan,
      v4Hdr.srcAddr,
      srcIp,
      ICMPv4Type::ICMPV4_TYPE_TIME_EXCEEDED,
      ICMPv4Code::ICMPV4_CODE_TIME_EXCEEDED_TTL_EXCEEDED,
      bodyLength,
      serializeBody);

  auto srcVlanStr = srcVlan.has_value()
      ? folly::to<std::string>(static_cast<int>(srcVlan.value()))
      : "None";
  XLOG(DBG4) << "sending ICMP Time Exceeded with srcMac " << src
             << " dstMac: " << dst << " vlan: " << srcVlanStr
             << " dstIp: " << v4Hdr.srcAddr.str() << " srcIp: " << srcIp.str()
             << " bodyLength: " << bodyLength;
  sw_->sendPacketSwitchedAsync(std::move(icmpPkt));
}

template <typename VlanOrIntfT>
void IPv4Handler::handlePacket(
    unique_ptr<RxPacket> pkt,
    MacAddress dst,
    MacAddress src,
    Cursor cursor,
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  SwitchStats* stats = sw_->stats();
  PortID port = pkt->getSrcPort();
  auto vlanID = pkt->getSrcVlanIf();

  const uint32_t l3Len = pkt->getLength() - (cursor - Cursor(pkt->buf()));
  stats->port(port)->ipv4Rx();
  IPv4Hdr v4Hdr(cursor);
  XLOG(DBG4) << "Rx IPv4 packet (" << l3Len << " bytes) " << v4Hdr.srcAddr.str()
             << " --> " << v4Hdr.dstAddr.str() << " proto: 0x" << std::hex
             << static_cast<int>(v4Hdr.protocol);

  auto payloadLength = pkt->getLength() - (cursor - Cursor(pkt->buf()));
  if (v4Hdr.payloadSize() > payloadLength) {
    sw_->portStats(pkt->getSrcPort())->pktBogus();
    XLOG(DBG3) << "Discarding pkt with invalid length field. length: " << " ("
               << v4Hdr.payloadSize() << ")" << " payload length: " << " ("
               << payloadLength << ")" << " src: " << v4Hdr.srcAddr.str()
               << " (" << src << ")" << " dst: " << v4Hdr.dstAddr.str() << " ("
               << dst << ")";
    return;
  }

  // Additional data (such as FCS) may be appended after the IP payload, trim to
  // expected size of ipv4 payload
  cursor = Cursor(cursor, v4Hdr.payloadSize());

  // retrieve the current switch state
  auto state = sw_->getState();
  if (v4Hdr.protocol == static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP)) {
    Cursor udpCursor(cursor);
    UDPHeader udpHdr;
    if (!udpHdr.tryParse(&udpCursor, sw_->portStats(port))) {
      XLOG_EVERY_MS(ERR, 1000) << "failed to parse udp header";
      return;
    }
    XLOG(DBG4) << "UDP packet, Source port :" << udpHdr.srcPort
               << " destination port: " << udpHdr.dstPort;
    if (DHCPv4Handler::isDHCPv4Packet(udpHdr)) {
      auto switchType = sw_->getSwitchInfoTable().l3SwitchType();
      if (switchType == cfg::SwitchType::VOQ) {
        // TODO(skhare)
        // Support DHCP packets for VOQ switches
        XLOG_EVERY_MS(DBG2, 5000)
            << "Dropping DHCPv4 pkt for VOQ switches, Not Supported yet";
        return;
      } else {
        DHCPv4Handler::handlePacket(
            sw_,
            std::move(pkt),
            src,
            dst,
            v4Hdr,
            udpHdr,
            udpCursor,
            vlanOrIntf);
        return;
      }
    }
  }

  // Handle packets destined for us
  // Get the Interface to which this packet should be forwarded in host
  // TODO: assume vrf 0 now
  std::shared_ptr<Interface> intf{nullptr};
  auto interfaceMap = state->getInterfaces();
  if (v4Hdr.dstAddr.isMulticast()) {
    // If packet is received on a lag member port, ensure that
    // LAG is in forwarding state
    if (!AggregatePort::isIngressValid(state, pkt)) {
      XLOG_EVERY_MS(DBG2, 5000)
          << "Dropping multicast ipv4 pkt ingressing on disabled agg member port "
          << pkt->getSrcPort();
      return;
    }
    // Forward multicast packet directly to corresponding host interface
    auto intfIDOpt = state->getInterfaceIDForPortIf(PortDescriptor(port));
    if (intfIDOpt) {
      intf = state->getInterfaces()->getNodeIf(intfIDOpt.value());
    }
  } else if (v4Hdr.dstAddr.isLinkLocal()) {
    // XXX: Ideally we should scope the limit to Link only. However we are
    // using v4 link locals in a special way on Galaxy/6pack which needs
    // because of which we do not limit the scope.
    //
    // Forward link-local packet directly to corresponding host interface
    // provided desAddr is assigned to that interface.
    // auto intfID =
    // sw_->getState()->getInterfaceIDForPort(PortDescriptor(port)); intf =
    // state->getInterfaces()->getNodeIf(intfID); if (not
    // intf->hasAddress(v4Hdr.dstAddr)) {
    //   intf = nullptr;
    // }
    intf = interfaceMap->getInterfaceIf(RouterID(0), v4Hdr.dstAddr);
  } else {
    // Else loopup host interface based on destAddr
    const auto iter = sw_->getAddrToLocalIntfMap().find(
        std::make_pair(RouterID(0), v4Hdr.dstAddr));
    if (iter != sw_->getAddrToLocalIntfMap().end()) {
      intf = interfaceMap->getNodeIf(iter->second);
    }
  }

  if (intf) {
    // TODO: Also check to see if this is the broadcast address for one of the
    // interfaces on this VLAN.  We should probably build up a more efficient
    // data structure to look up this information.
    stats->port(port)->ipv4Mine();
    if (v4Hdr.ttl == 1) {
      stats->ipv4Ttl1Mine();
    }
    // Anything not handled by the controller, we will forward it to the host,
    // i.e. ping, ssh, bgp...
    // FixME: will do another diff to set length in RxPacket, so that it
    // can be reused here.
    if (sw_->sendPacketToHost(intf->getID(), std::move(pkt))) {
      stats->port(port)->pktToHost(l3Len);
    } else {
      stats->port(port)->pktDropped();
    }
    return;
  }

  // if packet is not for us, check the ttl exceed
  if (FLAGS_send_icmp_time_exceeded && v4Hdr.ttl <= 1) {
    XLOG(DBG4) << "Rx IPv4 Packet with TTL expired";
    stats->port(port)->pktDropped();
    stats->port(port)->ipv4TtlExceeded();
    // Look up cpu mac from switchInfo
    auto switchId = sw_->getScopeResolver()->scope(port).switchId();
    MacAddress cpuMac =
        sw_->getHwAsicTable()->getHwAsicIf(switchId)->getAsicMac();
    sendICMPTimeExceeded(port, vlanID, cpuMac, cpuMac, v4Hdr, cursor);
    return;
  }

  // Handle broadcast packets.
  // TODO: Also check to see if this is the broadcast address for one of the
  // interfaces on this VLAN. We should probably build up a more efficient
  // data structure to look up this information.
  if (v4Hdr.dstAddr.isLinkLocalBroadcast()) {
    stats->port(port)->pktDropped();
    return;
  }

  // TODO: check the reason of punt, for now, assume it is for
  // resolving the address
  // We will need to manage the rate somehow. Either from HW
  // or a SW control here
  stats->port(port)->ipv4Nexthop();
  if (!resolveMac(state, port, v4Hdr.dstAddr)) {
    stats->port(port)->ipv4NoArp();
    XLOG(DBG4) << "Cannot find the interface to send out ARP request for "
               << v4Hdr.dstAddr.str();
  }
  // TODO: ideally, we need to store this packet until the ARP is done and
  // then send this pkt out. For now, just drop it.
  stats->port(port)->pktDropped();
}

// Explicit instantiation to avoid linker errors
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
template void IPv4Handler::handlePacket(
    unique_ptr<RxPacket> pkt,
    MacAddress dst,
    MacAddress src,
    Cursor cursor,
    const std::shared_ptr<Vlan>& vlanOrIntf);

template void IPv4Handler::handlePacket(
    unique_ptr<RxPacket> pkt,
    MacAddress dst,
    MacAddress src,
    Cursor cursor,
    const std::shared_ptr<Interface>& vlanOrIntf);

// Return true if we successfully sent an ARP request, false otherwise
bool IPv4Handler::resolveMac(
    std::shared_ptr<SwitchState> state,
    PortID ingressPort,
    IPAddressV4 dest) {
  // need to find out our own IP and MAC addresses so that we can send the
  // ARP request out. Since the request will be broadcast, there is no need to
  // worry about which port to send the packet out.
  // The RIB can lead the published state, so ids must resolve from the RIB.
  auto routeAndNextHops = sw_->getRib()->getRouteAndNextHops(
      dest, RouterID(0), false /* normalized */);

  if (!routeAndNextHops || !routeAndNextHops->first->isResolved()) {
    sw_->portStats(ingressPort)->ipv4DstLookupFailure();
    // No way to reach dest
    return false;
  }
  const auto& [route, nhs] = *routeAndNextHops;

  auto intfs = state->getInterfaces();
  auto sent = false;
  for (auto nh : nhs) {
    auto intf = intfs->getNodeIf(nh.intf());
    if (intf) {
      if (nh.addr().isV4()) {
        auto source = intf->getAddressToReach(nh.addr())->first.asV4();
        auto target = route->isConnected() ? dest : nh.addr().asV4();
        if (source == target) {
          // This packet is for us.  Don't send ARP requess for our own IP.
          continue;
        }

        sent = sw_->sendArpRequestHelper(intf, state, source, target);

      } else if (nh.addr().isV6() && !route->isConnected()) {
        auto source = intf->getAddressToReach(nh.addr())->first.asV6();
        auto target = nh.addr().asV6();
        if (source == target) {
          // This packet is for us.  Don't send NDP requests to ourself.
          continue;
        }

        sent = sw_->sendNdpSolicitationHelper(intf, state, target);
      }
    }
  }

  return sent;
}

} // namespace facebook::fboss
