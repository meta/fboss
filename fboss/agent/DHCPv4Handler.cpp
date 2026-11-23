/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/DHCPv4Handler.h"
#include <folly/IPAddress.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/logging/xlog.h>
#include <string>

#include "fboss/agent/DHCPv4OptionsOfInterest.h"
#include "fboss/agent/FbossError.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/RxPacket.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/DHCPv4Packet.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPProto.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/InterfaceMap.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"

using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV4;
using folly::MacAddress;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;
using std::string;
using std::unique_ptr;
using namespace facebook::fboss;

using VlanTags_t = EthHdr::VlanTags_t;

namespace {
IPv4Hdr makeIpv4Header(
    IPAddressV4 srcIp,
    IPAddressV4 dstIp,
    uint8_t ttl,
    uint16_t length) {
  // Prepare IPv4 header
  IPv4Hdr ipHdr(
      4, // version
      IPv4Hdr::minSize() / 4, // ihl in 32 bit chunks
      0, // dscp
      0, // ecn
      length, // length
      0, // id
      false, // Don't fragment
      false, // More fragments
      0, // Fragment offset
      ttl, // TTL
      static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP), // Protocol
      0, // Checksum
      srcIp, // Source
      dstIp // Destination IP
  );
  ipHdr.computeChecksum();
  return ipHdr;
}

EthHdr
makeEthHdr(MacAddress srcMac, MacAddress dstMac, std::optional<VlanID> vlan) {
  VlanTags_t vlanTags;
  if (vlan.has_value()) {
    vlanTags.emplace_back(
        vlan.value(), static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN));
  }

  return EthHdr(
      dstMac,
      srcMac,
      vlanTags,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4));
}

void sendDHCPPacket(
    SwSwitch* sw,
    const EthHdr& ethHdr,
    const IPv4Hdr& ipHdr,
    const UDPHeader& udpHdr,
    const DHCPv4Packet& dhcpPacket) {
  // Allocate packet
  auto txPacket = sw->allocatePacket(
      18 + // ethernet header
      ipHdr.size() + udpHdr.size() + dhcpPacket.size());

  std::optional<VlanID> vlanID = std::nullopt;
  const auto& vlanTags = ethHdr.getVlanTags();
  if (!vlanTags.empty()) {
    vlanID = VlanID(vlanTags[0].vid());
  }

  RWPrivateCursor rwCursor(txPacket->buf());
  // Write data to packet buffer
  txPacket->writeEthHeader(
      &rwCursor,
      ethHdr.getDstMac(),
      ethHdr.getSrcMac(),
      vlanID,
      ethHdr.getEtherType());
  ipHdr.write(&rwCursor);
  rwCursor.writeBE<uint16_t>(udpHdr.srcPort);
  rwCursor.writeBE<uint16_t>(udpHdr.dstPort);
  rwCursor.writeBE<uint16_t>(udpHdr.length);
  folly::io::RWPrivateCursor csumCursor(rwCursor);
  rwCursor.skip(2);
  folly::io::Cursor payloadStart(rwCursor);

  dhcpPacket.write(&rwCursor);
  uint16_t csum = udpHdr.computeChecksum(ipHdr, payloadStart);
  csumCursor.writeBE<uint16_t>(csum);

  XLOG(DBG4) << " Sent dhcp packet :" << " Eth header : " << ethHdr
             << " IPv4 Header : " << ipHdr << " UDP Header : " << udpHdr;
  // Send packet
  sw->sendPacketSwitchedAsync(std::move(txPacket));
}

// Returns true iff the option starting at optIndex fits entirely within
// optionsIn: its code byte, and (for options that carry a length) the length
// byte plus all declared data bytes. Mirrors the option-length validation in
// DHCPv6Packet::extractOptions; a malformed option whose declared length runs
// past the buffer would otherwise cause an out-of-bounds read in processOption.
bool isOptionInBounds(const DHCPv4Packet::Options& optionsIn, int optIndex) {
  if (static_cast<size_t>(optIndex) >= optionsIn.size()) {
    return false;
  }
  if (DHCPv4Packet::isOptionWithoutLength(optionsIn[optIndex])) {
    return true;
  }
  if (static_cast<size_t>(optIndex) + 1 >= optionsIn.size()) {
    return false;
  }
  const size_t optLen = optionsIn[optIndex + 1];
  const size_t optDataIndex = static_cast<size_t>(optIndex) + 2;
  return optDataIndex + optLen <= optionsIn.size();
}

int processOption(
    const DHCPv4Packet::Options& optionsIn,
    int optIndex,
    DHCPv4Packet& dhcpPacketOut,
    bool toAppend) {
  uint8_t op = optionsIn[optIndex];
  uint8_t optLen =
      DHCPv4Packet::isOptionWithoutLength(op) ? 0 : optionsIn[optIndex + 1];
  const uint8_t* optBytes = optLen == 0 ? nullptr : &optionsIn[optIndex + 2];
  if (toAppend) {
    dhcpPacketOut.appendOption(op, optLen, optBytes);
  }
  return optLen == 0 ? 1 : // option byte
      2 + optLen; // Option byte + len + option data
}

} // namespace

namespace facebook::fboss {

bool DHCPv4Handler::isDHCPv4Packet(const UDPHeader& udpHdr) {
  auto srcPort = udpHdr.srcPort;
  auto dstPort = udpHdr.dstPort;
  return (srcPort == kBootPCPort || srcPort == kBootPSPort) ||
      (dstPort == kBootPCPort || dstPort == kBootPSPort);
}

template <typename VlanOrIntfT>
void DHCPv4Handler::handlePacket(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress /*dstMac*/,
    const IPv4Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  sw->portStats(pkt->getSrcPort())->dhcpV4Pkt();
  if (ipHdr.ttl <= 1) {
    sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
    XLOG(DBG4) << "Dropped DHCP packet with TTL of " << ipHdr.ttl;
    return;
  }

  // Parse dhcp packet
  DHCPv4Packet dhcpPkt;
  try {
    dhcpPkt.parse(&cursor);
  } catch (const FbossError& ex) {
    sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
    XLOG(ERR) << "Failed to parse DHCPv4 packet: " << ex.what();
    return;
  }
  if (dhcpPkt.hasDhcpCookie()) {
    switch (dhcpPkt.op) {
      case BOOTREQUEST:
        XLOG(DBG4) << " Got boot request ";
        processRequest(sw, std::move(pkt), srcMac, ipHdr, dhcpPkt, vlanOrIntf);
        break;
      case BOOTREPLY:
        XLOG(DBG4) << " Got boot reply";
        processReply(sw, std::move(pkt), ipHdr, dhcpPkt);
        break;
      default:
        XLOG(DBG4) << " Unknown DHCP Packet type " << (uint)dhcpPkt.op;
        sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
        break;
    }
  } else {
    // Got a bootp packet do nothing. Should never
    // really happen since DHCP obsoleted BOOT protocol
    // and we run only DHCP. Drop the packet
    XLOG(DBG4) << " Dropped bootp packet ";
  }
}

// Explicit instantiation to avoid linker errors
// https://isocpp.org/wiki/faq/templates#separate-template-fn-defn-from-decl
template void DHCPv4Handler::handlePacket(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress /*dstMac*/,
    const IPv4Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<Vlan>& vlanOrIntf);

template void DHCPv4Handler::handlePacket(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    MacAddress /*dstMac*/,
    const IPv4Hdr& ipHdr,
    const UDPHeader& /*udpHdr*/,
    Cursor cursor,
    const std::shared_ptr<Interface>& vlanOrIntf);

template <typename VlanOrIntfT>
void DHCPv4Handler::processRequest(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    MacAddress srcMac,
    const IPv4Hdr& origIPHdr,
    const DHCPv4Packet& dhcpPacket,
    const std::shared_ptr<VlanOrIntfT>& vlanOrIntf) {
  auto dhcpPacketOut(dhcpPacket);
  auto state = sw->getState();
  auto vlanID = getVlanIDFromVlanOrIntf(vlanOrIntf);
  auto vlanIDStr = vlanID.has_value()
      ? folly::to<std::string>(static_cast<int>(vlanID.value()))
      : "None";

  if (!vlanOrIntf) {
    sw->stats()->dhcpV4DropPkt();
    XLOG(DBG4) << " VLAN  " << vlanIDStr << " is no longer present "
               << " DHCPv4Packet dropped on port " << pkt->getSrcPort();
    return;
  }
  auto dhcpServer = vlanOrIntf->getDhcpV4Relay();

  XLOG(DBG4) << "srcMac: " << srcMac.toString();
  // look in the override map, and use relevant destination
  auto dhcpOverrideMap = vlanOrIntf->getDhcpV4RelayOverrides();
  if (dhcpOverrideMap.find(srcMac) != dhcpOverrideMap.end()) {
    dhcpServer = dhcpOverrideMap[srcMac];
    if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
      XLOG(DBG4) << "dhcpServer: " << dhcpServer;
    } else {
      XLOG(DBG4) << "dhcpServer: " << dhcpServer.value();
    }
  }

  IPAddressV4 dhcpServerIP;
  if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
    if (dhcpServer.isZero()) {
      sw->stats()->dhcpV4DropPkt();
      XLOG(DBG4) << " No relay configured for VLAN : " << vlanIDStr
                 << " dropped dhcp packet ";
      return;
    }
    dhcpServerIP = dhcpServer;
  } else {
    if (!dhcpServer.has_value() || dhcpServer.value().isZero()) {
      sw->stats()->dhcpV4DropPkt();
      XLOG(DBG4) << " No relay configured for Interface: "
                 << vlanOrIntf->getID() << " dropped dhcp packet ";
      return;
    }
    dhcpServerIP = dhcpServer.value();
  }

  auto switchIp = state->getDhcpV4RelaySrc();
  if (switchIp.isZero()) {
    auto interfaceIDOpt = sw->getState()->getInterfaceIDForPortIf(
        PortDescriptor(pkt->getSrcPort()));
    if (!interfaceIDOpt) {
      sw->stats()->dhcpV4DropPkt();
      XLOG(ERR) << "No interface for port " << pkt->getSrcPort()
                << ", DHCP packet dropped";
      return;
    }
    auto interface = state->getInterfaces()->getNodeIf(interfaceIDOpt.value());

    for (auto iter : std::as_const(*interface->getAddresses())) {
      auto address =
          std::make_pair(folly::IPAddress(iter.first), iter.second->cref());
      if (address.first.isV4()) {
        switchIp = address.first.asV4();
        break;
      }
    }
  }

  if (switchIp.isZero()) {
    sw->stats()->dhcpV4DropPkt();
    XLOG(ERR) << "Could not find a SVI interface on vlan : " << vlanIDStr
              << "DHCP packet dropped ";
    return;
  }

  XLOG(DBG4) << " Got switch ip : " << switchIp;
  // Prepare DHCP packet to relay
  if (!addAgentOptions(
          sw, pkt->getSrcPort(), switchIp, dhcpPacket, dhcpPacketOut)) {
    sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
    XLOG(DBG4) << "Bad DHCP packet, error adding agent options."
               << " DHCP packet dropped";
    return;
  }
  // Incrementing hops is optional for relay agent forwarding,
  // however it seems safer in case of loops. Also seen cases
  // where not incrementing this on the DHCP request causes
  // the server to drop our request.
  const int kMaxHops = 255;
  if (dhcpPacketOut.hops < kMaxHops) {
    dhcpPacketOut.hops++;
  } else {
    XLOG(DBG4) << "Max hops exceeded for dhcp packet";
    sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
    return;
  }
  dhcpPacketOut.giaddr = switchIp;
  // Look up cpu mac from HwAsicTable
  SwitchID switchID;
  if constexpr (std::is_same_v<VlanOrIntfT, Vlan>) {
    switchID = sw->getScopeResolver()->scope(vlanOrIntf).switchId();
  } else {
    switchID =
        sw->getScopeResolver()->scope(vlanOrIntf, sw->getState()).switchId();
  }

  MacAddress cpuMac = sw->getHwAsicTable()->getHwAsicIf(switchID)->getAsicMac();

  // Prepare the packet to be sent out
  EthHdr ethHdr = makeEthHdr(cpuMac, cpuMac, sw->getVlanIDForTx(vlanOrIntf));
  auto ipHdr = makeIpv4Header(
      switchIp,
      dhcpServerIP,
      origIPHdr.ttl - 1,
      IPv4Hdr::minSize() + UDPHeader::size() + dhcpPacketOut.size());
  UDPHeader udpHdr(
      kBootPSPort, kBootPSPort, UDPHeader::size() + dhcpPacketOut.size());
  // Send packet
  sendDHCPPacket(sw, ethHdr, ipHdr, udpHdr, dhcpPacketOut);
}

void DHCPv4Handler::processReply(
    SwSwitch* sw,
    std::unique_ptr<RxPacket> pkt,
    const IPv4Hdr& origIPHdr,
    const DHCPv4Packet& dhcpPacket) {
  auto dhcpPacketOut(dhcpPacket);
  auto state = sw->getState();
  if (!stripAgentOptions(sw, pkt->getSrcPort(), dhcpPacket, dhcpPacketOut)) {
    sw->portStats(pkt->getSrcPort())->dhcpV4BadPkt();
    XLOG(DBG4) << "Bad DHCP packet, error stripping agent options."
               << " DHCP packet dropped";
    return;
  }
  IPAddressV4 clientIP = IPAddressV4::fromLong(INADDR_BROADCAST);
  if (!(dhcpPacket.flags & DHCPv4Packet::kFlagBroadcast)) {
    clientIP = dhcpPacket.yiaddr;
  }
  auto switchIp = state->getDhcpV4ReplySrc();
  if (switchIp.isZero()) {
    switchIp = origIPHdr.dstAddr;
  }
  auto switchId = sw->getScopeResolver()->scope(pkt->getSrcPort()).switchId();
  MacAddress cpuMac = sw->getHwAsicTable()->getHwAsicIf(switchId)->getAsicMac();
  // Extract client MAC address from dhcp reply
  uint8_t chaddr[MacAddress::SIZE];
  memcpy(chaddr, dhcpPacketOut.chaddr.data(), MacAddress::SIZE);
  MacAddress dstMac =
      MacAddress::fromBinary(folly::ByteRange(chaddr, MacAddress::SIZE));

  // Clear out the relay address field
  dhcpPacketOut.giaddr = IPAddressV4();

  // TODO we should add router id information to the packet
  // to get the VRF of the interface that this packet came
  // in on. Assuming 0 for now since we have only one VRF
  auto intf = state->getInterface(RouterID(0), IPAddress(switchIp));
  if (!intf) {
    sw->portStats(pkt->getSrcPort())->dhcpV4DropPkt();
    LOG(INFO) << "Could not lookup interface for : " << switchIp
              << "DHCP packet dropped ";
    return;
  }

  // Prepare the packet to be sent out
  EthHdr ethHdr = makeEthHdr(cpuMac, dstMac, intf->getVlanID());
  auto ipHdr = makeIpv4Header(
      switchIp,
      clientIP,
      origIPHdr.ttl - 1,
      IPv4Hdr::minSize() + UDPHeader::size() + dhcpPacketOut.size());
  UDPHeader udpHdr(
      kBootPSPort, kBootPCPort, UDPHeader::size() + dhcpPacketOut.size());

  sendDHCPPacket(sw, ethHdr, ipHdr, udpHdr, dhcpPacketOut);
}

bool DHCPv4Handler::addAgentOptions(
    SwSwitch* /*sw*/,
    PortID /*port*/,
    IPAddressV4 relayAddr,
    const DHCPv4Packet& dhcpPacketIn,
    DHCPv4Packet& dhcpPacketOut) {
  dhcpPacketOut.clearOptions();
  const auto& optionsIn = dhcpPacketIn.options;
  auto optIndex = 0;
  bool isDHCP = false;
  bool done = false;
  uint16_t maxMsgSize = 0;
  while (optIndex < optionsIn.size() && !done) {
    if (!isOptionInBounds(optionsIn, optIndex)) {
      XLOG(DBG2) << "Dropping DHCP request: malformed option at offset "
                 << optIndex << " (code "
                 << (static_cast<size_t>(optIndex) < optionsIn.size()
                         ? static_cast<int>(optionsIn[optIndex])
                         : -1)
                 << ", options size " << optionsIn.size() << ")";
      return false; // truncated/malformed option -> drop packet
    }
    bool appendOption = true;
    switch (optionsIn[optIndex]) {
      case DHCP_MESSAGE_TYPE:
        isDHCP = true;
        break;
      case DHCP_MAX_MESSAGE_SIZE: {
        // Only read the value when the option actually declares data.
        // isOptionInBounds() already guarantees optIndex + 2 + optLen <= size,
        // so with optLen > 0 the read at optIndex + 2 is in bounds; a length-0
        // option (which still passes isOptionInBounds) is skipped to avoid an
        // out-of-bounds read.
        const size_t optLen = optionsIn[optIndex + 1];
        if (optLen > 0) {
          maxMsgSize = ntohs(optionsIn[optIndex + 2]);
        }
        break;
      }
      case DHCP_AGENT_OPTIONS:
        if (isDHCP) {
          // FIXME We should really forward this along unchanged.
          // see t3862629 for details.
          LOG(INFO) << " Agent options already present dropping DHCP packet";
          return false; // Options already present discard packet
        }
        break;
      case END:
        done = true;
        appendOption = false;
        break;
    }
    optIndex += processOption(optionsIn, optIndex, dhcpPacketOut, appendOption);
  }
  if (isDHCP) {
    // Append agent option
    uint8_t optlen = 2 + relayAddr.byteCount();
    uint8_t subOptArray[optlen];
    subOptArray[0] = AGENT_CIRCUIT_ID;
    subOptArray[1] = relayAddr.byteCount();
    memcpy(subOptArray + 2, relayAddr.bytes(), relayAddr.byteCount());
    dhcpPacketOut.appendOption(DHCP_AGENT_OPTIONS, optlen, subOptArray);
  }
  dhcpPacketOut.appendOption(END, 0, nullptr);
  dhcpPacketOut.padToMinLength();
  if (isDHCP && maxMsgSize && dhcpPacketOut.size() > maxMsgSize) {
    return false;
  }

  return isDHCP;
}

bool DHCPv4Handler::stripAgentOptions(
    SwSwitch* /*sw*/,
    PortID /*port*/,
    const DHCPv4Packet& dhcpPacketIn,
    DHCPv4Packet& dhcpPacketOut) {
  dhcpPacketOut.clearOptions();
  const auto& optionsIn = dhcpPacketIn.options;
  auto optIndex = 0;
  bool isDHCP = false;
  bool done = false;
  while (optIndex < optionsIn.size() && !done) {
    if (!isOptionInBounds(optionsIn, optIndex)) {
      XLOG(DBG2) << "Dropping DHCP reply: malformed option at offset "
                 << optIndex << " (code "
                 << (static_cast<size_t>(optIndex) < optionsIn.size()
                         ? static_cast<int>(optionsIn[optIndex])
                         : -1)
                 << ", options size " << optionsIn.size() << ")";
      return false; // truncated/malformed option -> drop packet
    }
    bool appendOption = true;
    switch (optionsIn[optIndex]) {
      case DHCP_MESSAGE_TYPE:
        isDHCP = true;
        break;
      case DHCP_AGENT_OPTIONS:
        if (isDHCP) {
          appendOption = false;
        }
        break;
      case END:
        done = true;
        appendOption = true;
        break;
    }
    optIndex += processOption(optionsIn, optIndex, dhcpPacketOut, appendOption);
  }
  dhcpPacketOut.padToMinLength();
  return isDHCP;
}

} // namespace facebook::fboss
