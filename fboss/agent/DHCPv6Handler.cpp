/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/DHCPv6Handler.h"
#include <folly/IPAddressV6.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>
#include <functional>
#include <string>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/packet/DHCPv6Packet.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"

using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;
using std::string;
using std::unique_ptr;
using namespace facebook::fboss;

using VlanTags_t = EthHdr::VlanTags_t;

namespace {

template <typename DHCPBodyFn>
void sendDHCPv6Packet(
    SwSwitch* sw,
    MacAddress dstMac,
    MacAddress srcMac,
    std::optional<VlanID> vlanID,
    IPAddressV6 dstIp,
    IPAddressV6 srcIp,
    uint16_t udpDstPort,
    uint16_t udpSrcPort,
    uint32_t dhcpLength,
    DHCPBodyFn serializeDhcp) {
  // construct EthHdr,
  VlanTags_t vlanTags;
  if (vlanID.has_value()) {
    vlanTags.emplace_back(
        vlanID.value(), static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN));
  }

  EthHdr ethHdr(
      dstMac,
      srcMac,
      vlanTags,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));

  // IPv6Hdr
  IPv6Hdr ipHdr(srcIp, dstIp);
  ipHdr.nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP);
  ipHdr.trafficClass = kGetNetworkControlTrafficClass();
  ipHdr.payloadLength = UDPHeader::size() + dhcpLength;
  ipHdr.hopLimit = 127;

  // UDPHeader
  UDPHeader udpHdr(udpSrcPort, udpDstPort, UDPHeader::size() + dhcpLength);

  // Allocate packet
  auto txPacket = sw->allocatePacket(
      18 + // ethernet header
      IPv6Hdr::SIZE + udpHdr.size() + dhcpLength);

  RWPrivateCursor rwCursor(txPacket->buf());
  // Write EthHdr
  txPacket->writeEthHeader(
      &rwCursor,
      ethHdr.getDstMac(),
      ethHdr.getSrcMac(),
      vlanID,
      ethHdr.getEtherType());
  ipHdr.serialize(&rwCursor);

  // write UDP header, DHCP packet and compute checksum
  rwCursor.writeBE<uint16_t>(udpHdr.srcPort);
  rwCursor.writeBE<uint16_t>(udpHdr.dstPort);
  rwCursor.writeBE<uint16_t>(udpHdr.length);
  folly::io::RWPrivateCursor csumCursor(rwCursor);
  rwCursor.skip(2);
  folly::io::Cursor payloadStart(rwCursor);
  serializeDhcp(&rwCursor);
  udpHdr.updateChecksum(ipHdr, payloadStart);
  csumCursor.writeBE<uint16_t>(udpHdr.csum);

  XLOG(DBG4) << " Send dhcp packet:" << " Eth header: " << ethHdr.toString()
             << " IP header: " << ipHdr.toString()
             << " UDP Header: " << udpHdr.toString()
             << " dhcpLength: " << dhcpLength;
  // Send packet
  sw->sendPacketSwitchedAsync(std::move(txPacket));
}

} // namespace

namespace facebook::fboss {

bool DHCPv6Handler::isForDHCPv6RelayOrServer(const UDPHeader& udpHdr) {
  // according to RFC 3315 section 5.2, packets to server or agent are
  // all on port 547
  return (udpHdr.dstPort == DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT);
}

template <typename VlanOrIntfT>
void DHCPv6Handler::handlePacket(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress dstMac,
    const IPv6Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  sw->portStats(pkt->getSrcPort())->dhcpV6Pkt();
  // Parse dhcp packet
  DHCPv6Packet dhcp6Pkt;
  try {
    dhcp6Pkt.parse(&cursor);
  } catch (const FbossError& ex) {
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    XLOG(ERR) << "Failed to parse DHCPv6 packet: " << ex.what();
    return;
  }
  if (dhcp6Pkt.type == static_cast<uint8_t>(DHCPv6Type::DHCPv6_RELAY_FORWARD)) {
    XLOG(DBG4) << "Received DHCPv6 relay forward packet: "
               << dhcp6Pkt.toString();
    processDHCPv6RelayForward(
        sw, std::move(pkt), srcMac, dstMac, ipHdr, dhcp6Pkt);
  } else if (
      dhcp6Pkt.type == static_cast<uint8_t>(DHCPv6Type::DHCPv6_RELAY_REPLY)) {
    XLOG(DBG4) << "Received DHCPv6 relay reply packet: " << dhcp6Pkt.toString();
    processDHCPv6RelayReply(
        sw, std::move(pkt), srcMac, dstMac, ipHdr, dhcp6Pkt);
  } else {
    XLOG(DBG4) << "Received DHCPv6 packet: " << dhcp6Pkt.toString();
    processDHCPv6Packet(
        sw, std::move(pkt), srcMac, dstMac, ipHdr, dhcp6Pkt, vlanOrIntf);
  }
}

// Explicit instantiation to avoid linker errors
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
template void DHCPv6Handler::handlePacket<Vlan>(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress dstMac,
    const IPv6Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<Vlan>& vlanOrIntf);

template void DHCPv6Handler::handlePacket<Interface>(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress dstMac,
    const IPv6Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<Interface>& vlanOrIntf);

template <typename VlanOrIntfT>
void DHCPv6Handler::processDHCPv6Packet(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress /*dstMac*/,
    const IPv6Hdr& ipHdr,
    const DHCPv6Packet& dhcpPacket,
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  auto vlanId = getVlanIDFromVlanOrIntf(vlanOrIntf);
  auto vlanIdStr = vlanId.has_value()
      ? folly::to<std::string>(static_cast<int>(vlanId.value()))
      : "None";
  auto state = sw->getState();

  if (!vlanOrIntf) {
    sw->stats()->dhcpV6DropPkt();
    XLOG(DBG2) << "VLAN " << vlanIdStr << " is no longer present"
               << "DHCPv6Packet dropped on port " << pkt->getSrcPort();
    return;
  }

  auto dhcp6Server = vlanOrIntf->getDhcpV6Relay();

  // look in the override map, and use relevant destination
  XLOG(DBG4) << "srcMac: " << srcMac.toString();
  auto dhcpOverrideMap = vlanOrIntf->getDhcpV6RelayOverrides();
  for (auto o : dhcpOverrideMap) {
    if (MacAddress(o.first) == srcMac) {
      dhcp6Server = o.second;
      if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
        XLOG(DBG4) << "dhcp6Server: " << dhcp6Server;
      } else {
        XLOG(DBG4) << "dhcp6Server: " << dhcp6Server.value();
      }
      break;
    }
  }

  IPAddressV6 dhcp6ServerIP;
  if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
    if (dhcp6Server.isZero()) {
      XLOG(DBG4) << "No DHCPv6 relay configured for Vlan " << vlanIdStr
                 << " dropped DHCPv6 packet on port " << pkt->getSrcPort();
      sw->stats()->dhcpV6DropPkt();
      return;
    }
    dhcp6ServerIP = dhcp6Server;
  } else {
    if (!dhcp6Server.has_value() || dhcp6Server.value().isZero()) {
      sw->stats()->dhcpV6DropPkt();
      XLOG(DBG4) << "No DHCPv6 relay configured for Interface "
                 << vlanOrIntf->getID() << " dropped DHCPv6 packet on port "
                 << pkt->getSrcPort();
      return;
    }
    dhcp6ServerIP = dhcp6Server.value();
  }

  auto switchIp = state->getDhcpV6RelaySrc();
  if (switchIp.isZero()) {
    auto intfIDOpt = sw->getState()->getInterfaceIDForPortIf(
        PortDescriptor(pkt->getSrcPort()));
    if (!intfIDOpt) {
      sw->stats()->dhcpV6DropPkt();
      XLOG(ERR) << "No interface for port " << pkt->getSrcPort()
                << ", DHCPv6 packet dropped";
      return;
    }
    switchIp = getSwitchIntfIPv6(state, intfIDOpt.value());
  }

  // link address set to unspecified
  IPAddressV6 la("::");
  // ip src -> peer-address
  IPAddressV6 pa = ipHdr.srcAddr;
  DHCPv6Packet relayFwdPkt(
      static_cast<uint8_t>(DHCPv6Type::DHCPv6_RELAY_FORWARD), 0, la, pa);

  // use the client src mac address as the interface id
  relayFwdPkt.addInterfaceIDOption(srcMac);
  // add relay message option
  relayFwdPkt.addRelayMessageOption(dhcpPacket);

  if (relayFwdPkt.computePacketLength() > DHCPv6Packet::MAX_DHCPV6_MSG_LENGTH) {
    XLOG(DBG2) << "DHCPv6 relay forward message exceeds max length, drop it.";
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    return;
  }

  // create the dhcpv6 packet
  // vlanIp -> ip src, ipHdr.dst -> ip dst, srcMac -> mac src, dstMac -> mac dst
  SwitchID switchID;
  if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
    switchID = sw->getScopeResolver()->scope(vlanOrIntf).switchId();
  } else {
    switchID =
        sw->getScopeResolver()->scope(vlanOrIntf, sw->getState()).switchId();
  }

  MacAddress cpuMac = sw->getHwAsicTable()->getHwAsicIf(switchID)->getAsicMac();
  auto serializeBody = [&](RWPrivateCursor* sendCursor) {
    relayFwdPkt.write(sendCursor);
  };

  sendDHCPv6Packet(
      sw,
      cpuMac,
      cpuMac,
      sw->getVlanIDForTx(vlanOrIntf),
      dhcp6ServerIP,
      switchIp,
      DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
      DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
      relayFwdPkt.computePacketLength(),
      serializeBody);
}

void DHCPv6Handler::processDHCPv6RelayForward(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress dstMac,
    const IPv6Hdr& ipHdr,
    DHCPv6Packet& dhcpPacket) {
  /**
   * NOTE: relay forward packet handling is not tested thoroughly since we
   * don't have other relay agents running in the cluster;
   */
  // relay forward from other agent
  if (dhcpPacket.hopCount >= MAX_RELAY_HOPCOUNT) {
    XLOG(DBG2) << "Received DHCPv6 relay foward packet with max relay hopcount";
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    return;
  }
  // increment the hopcount and forward it
  dhcpPacket.hopCount++;
  auto vlan = pkt->getSrcVlanIf();
  auto serializeBody = [&](RWPrivateCursor* sendCursor) {
    dhcpPacket.write(sendCursor);
  };
  auto switchId = sw->getScopeResolver()->scope(pkt->getSrcPort()).switchId();
  MacAddress cpuMac = sw->getHwAsicTable()->getHwAsicIf(switchId)->getAsicMac();
  sendDHCPv6Packet(
      sw,
      dstMac,
      cpuMac,
      vlan,
      ipHdr.dstAddr,
      ipHdr.srcAddr,
      DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
      DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
      dhcpPacket.computePacketLength(),
      serializeBody);
}

void DHCPv6Handler::processDHCPv6RelayReply(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress /*srcMac*/,
    MacAddress /*dstMac*/,
    const IPv6Hdr& ipHdr,
    DHCPv6Packet& dhcpPacket) {
  auto state = sw->getState();

  auto switchIp = state->getDhcpV6ReplySrc();
  if (switchIp.isZero()) {
    switchIp = ipHdr.dstAddr;
  }
  auto intf = state->getInterface(RouterID(0), switchIp);
  if (!intf) {
    sw->portStats(pkt->getSrcPort())->dhcpV6DropPkt();
    XLOG(DBG2) << "Could not look up interface for " << switchIp
               << "DHCPv6 packet dropped";
    return;
  }

  // relay reply from the server
  MacAddress destMac;
  const uint8_t* relayData = nullptr;
  uint16_t relayLen = 0;
  std::unordered_set<uint16_t> selector = {
      static_cast<uint16_t>(DHCPv6OptionType::DHCPv6_OPTION_INTERFACE_ID),
      static_cast<uint16_t>(DHCPv6OptionType::DHCPv6_OPTION_RELAY_MSG)};
  std::vector<DHCPv6Option> dhcpOptions;
  try {
    dhcpOptions = dhcpPacket.extractOptions(selector);
  } catch (const FbossError& ex) {
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    XLOG(DBG2) << "Bad dhcp relay reply message: " << ex.what();
    return;
  }
  for (int i = 0; i < dhcpOptions.size(); i++) {
    if (dhcpOptions[i].op ==
        static_cast<uint16_t>(DHCPv6OptionType::DHCPv6_OPTION_INTERFACE_ID)) {
      // The INTERFACE_ID option supplies the outbound destination MAC. We read
      // a fixed MacAddress::SIZE (6) bytes from the option data, so the option
      // must actually carry that many bytes. A shorter attacker-supplied option
      // would otherwise over-read past the end of the parsed options buffer
      // (the DCHECK previously here is a no-op in opt builds). Drop the packet
      // on a length mismatch, mirroring the runtime validation on the
      // RELAY_MSG path below.
      if (dhcpOptions[i].len != MacAddress::SIZE) {
        sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
        XLOG(DBG2)
            << "Bad dhcp relay reply message: INTERFACE_ID option length "
            << dhcpOptions[i].len << " != " << MacAddress::SIZE;
        return;
      }
      destMac = MacAddress::fromBinary(
          folly::ByteRange(dhcpOptions[i].data, MacAddress::SIZE));
    } else if (
        dhcpOptions[i].op ==
        static_cast<uint16_t>(DHCPv6OptionType::DHCPv6_OPTION_RELAY_MSG)) {
      relayData = dhcpOptions[i].data;
      relayLen = dhcpOptions[i].len;
    }
  }
  if (destMac == MacAddress::ZERO || relayLen == 0) {
    sw->portStats(pkt->getSrcPort())->dhcpV6DropPkt();
    XLOG(DBG2) << "Bad dhcp relay reply message: malformed options";
    return;
  }
  // Defensively verify that relayData/relayLen point at a region fully
  // contained within the parsed options buffer before issuing the outbound
  // copy. extractOptions() already rejects out-of-bounds options, but this
  // guards against relayData being detached from the source buffer. Use
  // std::less/std::greater so the comparison is well-defined even if relayData
  // points into a different allocation than optionsBuf (raw relational pointer
  // comparison across allocations is UB).
  const auto& optionsBuf = dhcpPacket.options;
  const uint8_t* bufBegin = optionsBuf.data();
  const uint8_t* bufEnd = bufBegin + optionsBuf.size();
  // relayLen != 0 (checked above) implies the RELAY_MSG branch set relayData,
  // but guard explicitly so the pointer arithmetic below never runs on nullptr.
  if (relayData == nullptr ||
      std::less<const uint8_t*>{}(relayData, bufBegin) ||
      std::greater<const uint8_t*>{}(relayData + relayLen, bufEnd)) {
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    XLOG(DBG2) << "Bad dhcp relay reply message: relay option length "
               << relayLen << " exceeds source buffer";
    return;
  }
  // Mirror the length guard used in processDHCPv6Packet: the relayed message
  // becomes the body of the outbound DHCPv6 packet, so it must fit.
  if (relayLen > DHCPv6Packet::MAX_DHCPV6_MSG_LENGTH) {
    sw->portStats(pkt->getSrcPort())->dhcpV6BadPkt();
    XLOG(DBG2) << "DHCPv6 relay reply message exceeds max length, drop it.";
    return;
  }
  /**
   * srcMac -> cpu mac, intf id -> dst mac
   * switch ip -> ip src, peerAddr -> ip dst,
   * relay message option -> send dhcp packet,
   */
  auto switchIds = sw->getScopeResolver()->scope(pkt->getSrcPort()).switchIds();
  CHECK_EQ(switchIds.size(), 1);
  MacAddress cpuMac =
      sw->getHwAsicTable()->getHwAsicIf(*switchIds.begin())->getAsicMac();
  // send dhcp packet
  auto serializeBody = [&](RWPrivateCursor* sendCursor) {
    sendCursor->push(relayData, relayLen);
  };
  sendDHCPv6Packet(
      sw,
      destMac,
      cpuMac,
      sw->getVlanIDForTx(intf),
      dhcpPacket.peerAddr,
      switchIp,
      DHCPv6Packet::DHCP6_CLIENT_UDPPORT,
      DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
      relayLen,
      serializeBody);
}

} // namespace facebook::fboss
