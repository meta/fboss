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
#include <fb303/ServiceData.h>
#include <fmt/format.h>
#include <folly/Memory.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <algorithm>
#include <string>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/DHCPv6Packet.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

using namespace facebook::fboss;
using folly::IOBuf;
using folly::IPAddressV6;
using folly::MacAddress;
using folly::io::Cursor;
using std::make_unique;
using std::replace;
using std::shared_ptr;
using std::string;
using std::unique_ptr;

using ::testing::_;
using testing::Return;

namespace {

// Address constants to be used to simulate DHCPV6 client and server

// DHCP client's MAC Address
const string kClientMacStr{"02 00 00 93 b5 1a"};
const MacAddress kClientMac("02:00:00:93:b5:1a");
// DHCP client's VLAN
const string kClientVlanStr{"00 01"};
const VlanID kClientVlan(1);
const string kVlanInterfaceIPStr(
    "24 01 db 00 21 10 30 01 00 00 00 00 00 00 00 01");
const IPAddressV6 kVlanInterfaceIP("2401:db00:2110:3001::0001");

// DHCP client's link local IPv6 address
const string kDhcpV6ClientLocalIpStr{
    "fe 80 00 00 00 00 00 00 c6 71 fe ff fe 93 b5 1a"};
const IPAddressV6 kDhcpV6ClientLocalIp(
    "fe80:0000:0000:0000:c671:feff:fe93:b51a");

// All router's link local IPv6 address and corresponding MAC address
const string kDhcpV6AllRoutersIpStr{
    "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 01 02"};
const IPAddressV6 kDhcpV6AllRoutersLLIp(
    "ff02:0000:0000:0000:0000:0000:0000:0102");
const MacAddress kDhcpV6AllRoutersMac("33:33:00:00:00:02");

// DHCP Server Address
const MacAddress kRelayNextHopMac("02:00:00:93:b5:1a");
const string kDhcpV6RelayStr{"20 01 0d b8 02 00 00 00 00 00 00 00 00 00 00 02"};
const IPAddressV6 kDhcpV6Relay("2001:0db8:0200:0000:0000:0000:0000:0002");
// DHCP Client Override MAC Address
const MacAddress kClientMacOverride("03:00:00:93:b5:1a");
// DHCP Server Override Address
const IPAddressV6 kDhcpV6RelayOverride(
    "2001:0db8:0200:0000:0000:0000:0000:0003");

// SrcIP to be used for RelayForward pkts
const string kDhcpV6RelaySrcStr{
    "20 01 0d b8 03 00 00 00 00 00 00 00 00 00 00 01"};
const IPAddressV6 kDhcpV6RelaySrc("2001:0db8:0300:0000:0000:0000:0000:0001");
// SrcIP to be used for RelayReply pkts
// Has to match an interface (fboss55) IP address
const IPAddressV6 kDhcpV6ReplySrc("2401:db00:2110:3055:0000:0000:0000:0001");

template <typename VlansOrIntfsT, typename NodeIDT>
shared_ptr<SwitchState> testStateHelper(
    std::shared_ptr<SwitchState> state,
    VlansOrIntfsT vlansOrIntfs,
    NodeIDT nodeId) {
  // Configure DHCPV6 relay settings for the test VLAN / Intf
  vlansOrIntfs->getNode(nodeId)->setDhcpV6Relay(kDhcpV6Relay);
  DhcpV6OverrideMap overrides;
  overrides[kClientMacOverride] = kDhcpV6RelayOverride;
  vlansOrIntfs->getNode(nodeId)->setDhcpV6RelayOverrides(overrides);
  addSwitchInfo(
      state,
      cfg::SwitchType::NPU,
      0, /*SwitchId*/
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0, /* switchIndex*/
      std::nullopt, /* sysPort min*/
      std::nullopt, /*sysPort max()*/
      MockPlatform::getMockLocalMac().toString());
  return state;
}

shared_ptr<SwitchState> testState() {
  auto state = testStateA();

  const auto& intfs = state->getInterfaces();
  return testStateHelper(state, intfs, InterfaceID(1));
}

unique_ptr<HwTestHandle> setupTestHandle() {
  return createTestHandle(testState());
}

shared_ptr<SwitchState> testStateNAT() {
  auto state = testState();
  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setDhcpV6RelaySrc(kDhcpV6RelaySrc);
  switchSettings->setDhcpV6ReplySrc(kDhcpV6ReplySrc);
  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(
      HwSwitchMatcher(std::unordered_set<SwitchID>{SwitchID(0)})
          .matcherString(),
      switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  addSwitchInfo(
      state,
      cfg::SwitchType::NPU,
      0, /*SwitchId*/
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0, /* switchIndex*/
      std::nullopt, /* sysPort min*/
      std::nullopt, /*sysPort max()*/
      MockPlatform::getMockLocalMac().toString());
  return state;
}

unique_ptr<HwTestHandle> setupTestHandleNAT() {
  return createTestHandle(testStateNAT());
}

// Generic function to inject a RX DHCPV6 packet to the handler
void sendDHCPV6Packet(
    HwTestHandle* handle,
    string srcMac,
    string dstMac,
    string vlan,
    string srcIp,
    string dstIp,
    string srcPort,
    string dstPort,
    uint32_t dhcpV6HdrSize,
    string dhcpV6Hdr,
    string dhcpV6RequestOptions = "") {
  constexpr auto ethHdrSize = 18; // DMAC + SMAC + VLAN + IPEthType
  constexpr auto ipV6HdrSize = 40;
  constexpr auto udpHdrSize = 8;
  auto payloadSize = dhcpV6HdrSize;
  std::string ipV6HdrPayloadLengthStr =
      fmt::format("{0:04x}", udpHdrSize + payloadSize);
  std::string udpHdrLengthStr =
      fmt::format("{0:04x}", udpHdrSize + payloadSize);

  auto buf = make_unique<folly::IOBuf>(PktUtil::parseHexData(
      // Ethernet header: dstMac, srcMac, EthType, vlanId
      dstMac + srcMac + "81 00" + vlan +
      // Ethernet Header: Ether type for IPv6
      "86 dd"
      // IPv6 Header: Version 6, traffic class, flow label
      "6e 00 00 00" +
      // IPV6 Header: IPv6 Payload length
      ipV6HdrPayloadLengthStr +
      // IPV6 Header: Next Header: 17 (UDP), Hop Limit (1)
      "11 01" +
      // IPv6 Header: srcIP, dstIP, srcPort, dstPort
      srcIp + dstIp +
      // UDP Header: srcPort, dstPort, length
      srcPort + dstPort + udpHdrLengthStr +
      // UDP Header: checksum (not valid)
      "2a 7e" +
      // DHCPv6 Header: msgType, txnId
      dhcpV6Hdr +
      // DHCPv6 Header: Options
      dhcpV6RequestOptions));

  // Validate the expected pkt length to actual pkt length
  ASSERT_EQ(payloadSize + udpHdrSize + ipV6HdrSize + ethHdrSize, buf->length())
      << "Don't forget to adjust the headers' length";
  // Inject the packet to the receive handler
  handle->rxPacket(std::move(buf), PortDescriptor(PortID(1)), VlanID(1));
}

using DHCPV6PayloadCheckFn =
    std::function<void(Cursor* cursor, uint32_t length)>;

// Generic validator function to validate expected packet from the DHCPV6
// handler
TxMatchFn checkDHCPV6Pkt(
    MacAddress dstMac,
    MacAddress srcMac,
    VlanID vlan,
    IPAddressV6 srcIp,
    IPAddressV6 dstIp,
    uint16_t srcPort,
    uint16_t dstPort,
    const DHCPV6PayloadCheckFn& dhcpV6CheckPayload) {
  return [=](const TxPacket* txPacket) {
    const auto* buf = txPacket->buf();
    Cursor c(buf);

    // Validate Ethernet header fields
    auto parsedDstMac = PktUtil::readMac(&c);
    checkField(dstMac, parsedDstMac, "dst mac");
    auto parsedSrcMac = PktUtil::readMac(&c);
    checkField(srcMac, parsedSrcMac, "src mac");
    auto vlanType = c.readBE<uint16_t>();
    checkField(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN),
        vlanType,
        "VLAN ethertype");
    auto vlanTag = c.readBE<uint16_t>();
    checkField(static_cast<uint16_t>(vlan), vlanTag, "VLAN tag");
    auto ethertype = c.readBE<uint16_t>();
    checkField(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6),
        ethertype,
        "ethertype");

    // Validate IPv6 header fields
    IPv6Hdr ipv6(c);
    checkField(
        static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP),
        ipv6.nextHeader,
        "IPv6 protocol");
    checkField(srcIp, ipv6.srcAddr, "src IP");
    checkField(dstIp, ipv6.dstAddr, "dst IP");
    if (ipv6.hopLimit > 127) {
      throw FbossError(
          "DHCP messages should not have hop limit ", ipv6.hopLimit, " > 127.");
    }

    Cursor ipv6PayloadStart(c);

    // Validate UDP header fields
    SwitchStats switchStats(1 /*numSwitches*/);
    PortStats portStats(PortID(0), "foo", &switchStats);
    UDPHeader udp;
    udp.parse(&c, &portStats);
    checkField(udp.computeChecksum(ipv6, c), udp.csum, "UDP checksum");
    checkField(srcPort, udp.srcPort, "UDP srcPort");
    checkField(dstPort, udp.dstPort, "UDP dstPort");

    // Validate DHCPv6 payload fields
    dhcpV6CheckPayload(&c, ipv6.payloadLength - UDPHeader::size());

    // Validate packet length
    if (ipv6.payloadLength != (c - ipv6PayloadStart)) {
      throw FbossError(
          "IPv6 payload length mismatch: header says ",
          ipv6.payloadLength,
          " but we used ",
          c - ipv6PayloadStart);
    }

    // This is a match
    return;
  };
}

// Function to validate response packet from the DHCPV6 handler to a client's
// DHCPV6 request packet
TxMatchFn checkDHCPV6Request(
    const DHCPv6Packet& dhcp6TxPkt,
    IPAddressV6 dhcpV6RelaySrc,
    IPAddressV6 dhcpV6Relay = kDhcpV6Relay) {
  // Initialize expected Ethernet and IPv6 addresses
  MacAddress dstMac = MockPlatform::getMockLocalMac();
  MacAddress srcMac = MockPlatform::getMockLocalMac();
  VlanID vlan(1);
  IPAddressV6 srcIp = dhcpV6RelaySrc;
  IPAddressV6 dstIp = dhcpV6Relay;

  // Expected UDP ports for DHCP Relay Message
  uint16_t srcPort = DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT;
  uint16_t dstPort = DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT;

  // Validator method for DHCPV6 packet header and options
  auto dhcpV6ReqRelayFwdCheckPayload = [=](Cursor* cursor, uint32_t length) {
    // Parse dhcp packet
    DHCPv6Packet dhcp6Pkt;
    try {
      dhcp6Pkt.parse(cursor);
      // Validate if the DHCP Message type is RelayForward
      checkField(
          DHCPv6Type::DHCPv6_RELAY_FORWARD,
          static_cast<DHCPv6Type>(dhcp6Pkt.type),
          "DHCP Msg Type");
      // Validate hopCount field of Relay Message
      EXPECT_EQ(dhcp6Pkt.hopCount, 0);
      // Validate Peer Address field of Relay Message is same as client's link
      // local address
      checkField(
          kDhcpV6ClientLocalIp, dhcp6Pkt.peerAddr, "client link-local IP");

      // Validate the presence of InterfaceID Option
      auto dhcpOptions = dhcp6Pkt.extractOptions({static_cast<uint16_t>(
          DHCPv6OptionType::DHCPv6_OPTION_INTERFACE_ID)});
      // Expect exactly one InterfaceID option and validate length
      EXPECT_EQ(dhcpOptions.size(), 1);
      EXPECT_EQ(dhcpOptions[0].len, 6);

      // Validate the presence of RelayMsg Option and it's contents
      dhcpOptions = dhcp6Pkt.extractOptions(
          {static_cast<uint16_t>(DHCPv6OptionType::DHCPv6_OPTION_RELAY_MSG)});
      // Expect exactly one relayMessage option
      EXPECT_EQ(dhcpOptions.size(), 1);
      // Validate relayMessage option data to be the same as the client's
      // original DHCP Message
      IOBuf dhcp6RelayPktBuf(
          IOBuf::WRAP_BUFFER, dhcpOptions[0].data, dhcpOptions[0].len);
      Cursor c(&dhcp6RelayPktBuf);
      DHCPv6Packet dhcp6RelayMsgPkt;
      dhcp6RelayMsgPkt.parse(&c);
      EXPECT_EQ(true, (dhcp6RelayMsgPkt == dhcp6TxPkt));
    } catch (const FbossError&) {
      throw FbossError("DHCPv6 packet parse error");
    }
  };

  // Invoke the generic DHCPV6 packet validator
  return checkDHCPV6Pkt(
      dstMac,
      srcMac,
      vlan,
      srcIp,
      dstIp,
      srcPort,
      dstPort,
      dhcpV6ReqRelayFwdCheckPayload);
}

// Function to validate response packet from the DHCPV6 handler to a DHCP
// server's DHCPV6 Relay-Reply packet
TxMatchFn checkDHCPV6RelayReply(
    const DHCPv6Packet& dhcp6RelayReplyMsg,
    IPAddressV6 replySrc = kVlanInterfaceIP,
    VlanID vlan = VlanID(1)) {
  // Initialize expected Ethernet and IPv6 addresses
  MacAddress dstMac = kClientMac;
  MacAddress srcMac = MockPlatform::getMockLocalMac();
  IPAddressV6 srcIp = replySrc;
  IPAddressV6 dstIp = kDhcpV6ClientLocalIp;

  // Expected UDP ports for DHCP Reply Message
  uint16_t srcPort = DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT;
  uint16_t dstPort = DHCPv6Packet::DHCP6_CLIENT_UDPPORT;

  // Validator method for DHCPV6 packet header and options
  auto dhcpV6RelayReplyCheckPayload = [=](Cursor* cursor, uint32_t length) {
    // Parse dhcp packet
    DHCPv6Packet dhcp6Pkt;
    try {
      dhcp6Pkt.parse(cursor);
      // Validate if the DHCP Message type is Reply
      checkField(
          DHCPv6Type::DHCPv6_REPLY,
          static_cast<DHCPv6Type>(dhcp6Pkt.type),
          "DHCP Msg Type");
      // Compare the expected DHCPReplyMsg to what was actually received
      EXPECT_EQ(true, (dhcp6Pkt == dhcp6RelayReplyMsg));
    } catch (const FbossError&) {
      throw FbossError("DHCPv6 packet parse error");
    }
  };

  // Invoke the generic DHCPV6 packet validator
  return checkDHCPV6Pkt(
      dstMac,
      srcMac,
      vlan,
      srcIp,
      dstIp,
      srcPort,
      dstPort,
      dhcpV6RelayReplyCheckPayload);
}

// Function to validate response packet from the DHCPV6 handler to an agent's
// DHCPV6 Relay-Forward packet
TxMatchFn checkDHCPV6RelayForward(
    MacAddress dstMac,
    MacAddress srcMac,
    VlanID vlan,
    IPAddressV6 srcIp,
    IPAddressV6 dstIp,
    uint16_t srcPort,
    uint16_t dstPort,
    DHCPv6Packet& dhcp6TxPkt) {
  // Validator method for DHCPV6 packet header and options
  auto dhcpV6ReqRelayFwdCheckPayload = [=](Cursor* cursor, uint32_t length) {
    // Parse dhcp packet
    DHCPv6Packet dhcp6Pkt;
    try {
      dhcp6Pkt.parse(cursor);
      // Validate that the hopcount field is incremented by 1
      EXPECT_EQ(dhcp6Pkt.hopCount, dhcp6TxPkt.hopCount + 1);
      // Validate that the rest of the DHCP packet is identical
      EXPECT_EQ(dhcp6Pkt.type, dhcp6TxPkt.type);
      EXPECT_EQ(dhcp6Pkt.linkAddr, dhcp6TxPkt.linkAddr);
      EXPECT_EQ(dhcp6Pkt.peerAddr, dhcp6TxPkt.peerAddr);
      EXPECT_EQ(dhcp6Pkt.options, dhcp6TxPkt.options);
    } catch (const FbossError&) {
      throw FbossError("DHCPv6 packet parse error");
    }
  };

  // Invoke the generic DHCPV6 packet validator
  return checkDHCPV6Pkt(
      dstMac,
      srcMac,
      vlan,
      srcIp,
      dstIp,
      srcPort,
      dstPort,
      dhcpV6ReqRelayFwdCheckPayload);
}

const IPAddressV6 kPortInterfaceIP("2401:db00:2110:3099::1");

shared_ptr<SwitchState> testStateWithPortInterfaceReplySrc() {
  auto state = testState();
  const HwSwitchMatcher matcher(std::unordered_set<SwitchID>{SwitchID(0)});

  auto portIntf = std::make_shared<Interface>(
      InterfaceID(99),
      std::nullopt,
      folly::StringPiece("fboss99"),
      MacAddress("00:02:00:00:00:99"),
      9000,
      false, /* is virtual */
      false, /* is state_sync disabled */
      cfg::InterfaceType::PORT);
  portIntf->setPortID(PortID(10));
  portIntf->setAddresses({{folly::IPAddress(kPortInterfaceIP), 64}});
  state->getInterfaces()->modify(&state)->addNode(portIntf, matcher);

  auto switchSettings = std::make_shared<SwitchSettings>();
  switchSettings->setDhcpV6ReplySrc(kPortInterfaceIP);
  switchSettings->setDefaultVlan(1);
  auto multiSwitchSwitchSettings = std::make_shared<MultiSwitchSettings>();
  multiSwitchSwitchSettings->addNode(matcher.matcherString(), switchSettings);
  state->resetSwitchSettings(multiSwitchSwitchSettings);
  addSwitchInfo(
      state,
      cfg::SwitchType::NPU,
      0, /*SwitchId*/
      cfg::AsicType::ASIC_TYPE_MOCK,
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MIN(),
      cfg::switch_config_constants::DEFAULT_PORT_ID_RANGE_MAX(),
      0, /* switchIndex*/
      std::nullopt, /* sysPort min*/
      std::nullopt, /*sysPort max()*/
      MockPlatform::getMockLocalMac().toString());
  return state;
}

/*
 * Injects a DHCPv6 Relay-Reply from the server and expects the relayed reply
 * to be sent from replySrc on replyVlan.
 */
void verifyRelayReplyIsSentFrom(
    HwTestHandle* handle,
    const IPAddressV6& replySrc,
    VlanID replyVlan) {
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 server RelayReply

  // Client VLAN
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = "08 00 27 d4 10 bb";
  // Server IP
  auto senderIP = kDhcpV6RelayStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kVlanInterfaceIPStr;
  // UDP Src and Dst ports for DHCPV6 relay reply
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  // DHCPV6 Relay-Reply Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Reply Message: type (13), Hopcount (1)
      "0d 01" +
      // DHCPV6 Relay-Reply Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Reply Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Reply Message Content
  const string dhcpV6ReplyMessage =
      // DHCPV6 Reply Message: type (7), txnId (dummy 0x571958)
      "07 57 19 58"
      // DHCPV6 Reply Message options
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      // DHCPv6 IA for Non-temporary Address: Value (dummy)
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      // DHCPv6 DNS recursive name server: Value (dummy)
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Reply Message options
  const string dhcpV6RelayReplyOptions =
      // InterfaceID: Option (18 = 0012),  Length (6 = 0006), Value (Client's
      // MAC)
      "00 12 00 06" + kClientMacStr +
      // Relay Reply: Option (9 = 0009),  Length (120 = 0078), Value (DHCP
      // Reply)
      "00 09 00 78" +
      // Relay Reply: Value (DHCP Reply Message Content)
      dhcpV6ReplyMessage;

  constexpr auto dhcpV6HdrSize = 168; // computed for hdr and options above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 reply packet structure to pass into the validator
  // routine
  auto dhcp6RawPktBuf = PktUtil::parseHexData(dhcpV6ReplyMessage);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6RelayReplyMsg;
  dhcp6RelayReplyMsg.parse(&cDhcp);

  // Setup the validator function for the response packet to expect the
  // translated ReplySrcIP on the VLAN associated with the ReplySrcIP
  EXPECT_SWITCHED_PKT(
      sw,
      "DHCPV6 Relay Reply",
      checkDHCPV6RelayReply(dhcp6RelayReplyMsg, replySrc, replyVlan));

  // Inject the test packet
  sendDHCPV6Packet(
      handle,
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayReplyOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

} // unnamed namespace

/*
 * DHCPv6HandlerTest tests validate DHCP relay with VLANs and Interfaces for
 * NPU switches.
 *
 * TODO(skhare) Validate for VOQ switches as well. Since VOQ switches don't
 * support VLAns. That will involve modifying the pkts in these tests to not
 * carry VLANs.
 */
class DHCPv6HandlerTest : public ::testing::Test {
  void SetUp() override {}
};

// Test to inject a DHCPV6 client's Request RX packet and validate RelayForward
// TX packet
TEST_F(DHCPv6HandlerTest, DHCPV6Request) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 client request

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Client MAC
  auto senderMac = kClientMac.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Client Link Local IP
  auto senderIP = kDhcpV6ClientLocalIpStr;
  // Dest Router MAC
  auto targetMac = kDhcpV6AllRoutersMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Router IP
  auto targetIP = kDhcpV6AllRoutersIpStr;
  // UDP Src and Dst ports for DHCPV6 request
  const string srcPort = "02 22";
  const string dstPort = "02 23";
  // DHCPV6 Request Message type (3), txnId (dummy 0x571958)
  const string dhcpV6Hdr = "03 57 19 58";
  // DHCPV6 Request Message options
  const string dhcpV6RequestOptions =
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 Option Request:  Option (0006), Length (0004), Value (0017 0018)
      "00 06 00 04 00 17 00 18"
      // DHCPv6 Elapsed time Option (0008), Length (0002), Value (0000)
      "00 08 00 02 00 00"
      // DHCPv6 IA for prefix delegation Option (0025), Length (00 29) Value
      // (dummy)
      "00 25 00 29 27 fe 8f 95 00 00 0e 10 00 00 15 18 00 1a 00 19 00 00 1c"
      "20 00 00 1d 4c 40 20 01 00 00 00 00 fe 00 00 00 00 00 00 00 00 00";
  constexpr auto dhcpV6HdrSize = 99; // computed for hdr and options above

  // IP Address associated with the VLAN
  auto switchIp = getSwitchVlanIPv6(sw->getState(), vlanID);

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 packet structure to pass into the validator routine
  auto dhcp6RawPktBuf = PktUtil::parseHexData(
      // DHCPv6 Header: msgType, txnId
      dhcpV6Hdr +
      // DHCPv6 Header: Options
      dhcpV6RequestOptions);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6TxPkt;
  dhcp6TxPkt.parse(&cDhcp);

  // Setup the validator function for the response packet
  EXPECT_SWITCHED_PKT(
      sw, "DHCPV6 request", checkDHCPV6Request(dhcp6TxPkt, switchIp));

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RequestOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a DHCPV6 client's RX Request packet with a override-MAC
// and validate RelayForward TX packet
TEST_F(DHCPv6HandlerTest, RelayOverrideDHCPV6Request) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 client request

  // Client VLAN
  auto vlanID = kClientVlan;
  // Client MAC
  auto senderMac = kClientMacOverride.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Client Link Local IP
  auto senderIP = kDhcpV6ClientLocalIpStr;
  // Dest Router MAC
  auto targetMac = kDhcpV6AllRoutersMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Router IP
  auto targetIP = kDhcpV6AllRoutersIpStr;
  // UDP Src and Dst ports for DHCPV6 request
  const string vlan = kClientVlanStr;
  const string srcPort = "02 22";
  const string dstPort = "02 23";
  // DHCPV6 Request Message type (3), txnId (dummy 0x571958)
  const string dhcpV6Hdr = "03 57 19 58";
  // DHCPV6 Request Message options
  const string dhcpV6RequestOptions =
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 Option Request:  Option (0006), Length (0004), Value (0017 0018)
      "00 06 00 04 00 17 00 18"
      // DHCPv6 Elapsed time Option (0008), Length (0002), Value (0000)
      "00 08 00 02 00 00"
      // DHCPv6 IA for prefix delegation Option (0025), Length (00 29) Value
      // (dummy)
      "00 25 00 29 27 fe 8f 95 00 00 0e 10 00 00 15 18 00 1a 00 19 00 00 1c"
      "20 00 00 1d 4c 40 20 01 00 00 00 00 fe 00 00 00 00 00 00 00 00 00";

  constexpr auto dhcpV6HdrSize = 99; // computed for hdr and options above

  // IP Address associated with the VLAN
  auto switchIp = getSwitchVlanIPv6(sw->getState(), vlanID);

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 packet structure to pass into the validator routine
  auto dhcp6RawPktBuf = PktUtil::parseHexData(
      // DHCPv6 Header: msgType, txnId
      dhcpV6Hdr +
      // DHCPv6 Header: Options
      dhcpV6RequestOptions);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6TxPkt;
  dhcp6TxPkt.parse(&cDhcp);

  // Setup the validator function for the response packet
  EXPECT_SWITCHED_PKT(
      sw,
      "DHCPV6 request",
      checkDHCPV6Request(dhcp6TxPkt, switchIp, kDhcpV6RelayOverride));

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RequestOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a DHCPV6 client's RX Request packet with a NAT configuration
// and validate RelayForward TX packet
TEST_F(DHCPv6HandlerTest, RelaySrcDHCPV6Request) {
  // Setup SwitchState for NAT scenario with translation addresses
  auto handle = setupTestHandleNAT();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 client request

  // Client VLAN
  auto vlanID = kClientVlan;
  // Client MAC
  auto senderMac = kClientMacOverride.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Client Link Local IP
  auto senderIP = kDhcpV6ClientLocalIpStr;
  // Dest Router MAC
  auto targetMac = kDhcpV6AllRoutersMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Router IP
  auto targetIP = kDhcpV6AllRoutersIpStr;
  // UDP Src and Dst ports for DHCPV6 request
  const string vlan = kClientVlanStr;
  const string srcPort = "02 22";
  const string dstPort = "02 23";
  // DHCPV6 Request Message type (3), txnId (dummy 0x571958)
  const string dhcpV6Hdr = "03 57 19 58";
  // DHCPV6 Request Message options
  const string dhcpV6RequestOptions =
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 Option Request:  Option (0006), Length (0004), Value (0017 0018)
      "00 06 00 04 00 17 00 18"
      // DHCPv6 Elapsed time Option (0008), Length (0002), Value (0000)
      "00 08 00 02 00 00"
      // DHCPv6 IA for prefix delegation Option (0025), Length (00 29) Value
      // (dummy)
      "00 25 00 29 27 fe 8f 95 00 00 0e 10 00 00 15 18 00 1a 00 19 00 00 1c"
      "20 00 00 1d 4c 40 20 01 00 00 00 00 fe 00 00 00 00 00 00 00 00 00";

  constexpr auto dhcpV6HdrSize = 99; // computed for hdr and options above

  // Translated IP Address to be used as srcIP for the relayed packet
  auto relaySrcIp = kDhcpV6RelaySrc;

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 packet structure to pass into the validator routine
  auto dhcp6RawPktBuf = PktUtil::parseHexData(
      // DHCPv6 Header: msgType, txnId
      dhcpV6Hdr +
      // DHCPv6 Header: Options
      dhcpV6RequestOptions);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6TxPkt;
  dhcp6TxPkt.parse(&cDhcp);

  // Setup the validator function for the response packet
  EXPECT_SWITCHED_PKT(
      sw,
      "DHCPV6 request",
      checkDHCPV6Request(dhcp6TxPkt, relaySrcIp, kDhcpV6RelayOverride));

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RequestOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a DHCPV6 server's Relay-Reply RX packet and validate the
// relayed TX Reply packet to client
TEST_F(DHCPv6HandlerTest, DHCPV6RelayReply) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 server RelayReply

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = "08 00 27 d4 10 bb";
  // Server IP
  auto senderIP = kDhcpV6RelayStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kVlanInterfaceIPStr;
  // UDP Src and Dst ports for DHCPV6 relay reply
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  // DHCPV6 Relay-Reply Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Reply Message: type (13), Hopcount (1)
      "0d 01" +
      // DHCPV6 Relay-Reply Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Reply Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Reply Message Content
  const string dhcpV6ReplyMessage =
      // DHCPV6 Reply Message: type (7), txnId (dummy 0x571958)
      "07 57 19 58"
      // DHCPV6 Reply Message options
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      // DHCPv6 IA for Non-temporary Address: Value (dummy)
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      // DHCPv6 DNS recursive name server: Value (dummy)
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Reply Message options
  const string dhcpV6RelayReplyOptions =
      // InterfaceID: Option (18 = 0012),  Length (6 = 0006), Value (Client's
      // MAC)
      "00 12 00 06" + kClientMacStr +
      // Relay Reply: Option (9 = 0009),  Length (120 = 0078), Value (DHCP
      // Reply)
      "00 09 00 78" +
      // Relay Reply: Value (DHCP Reply Message Content)
      dhcpV6ReplyMessage;

  constexpr auto dhcpV6HdrSize = 168; // computed for hdr and options above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 reply packet structure to pass into the validator
  // routine
  auto dhcp6RawPktBuf = PktUtil::parseHexData(dhcpV6ReplyMessage);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6RelayReplyMsg;
  dhcp6RelayReplyMsg.parse(&cDhcp);

  // Setup the validator function for the response packet
  EXPECT_SWITCHED_PKT(
      sw, "DHCPV6 Relay Reply", checkDHCPV6RelayReply(dhcp6RelayReplyMsg));

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayReplyOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a DHCPV6 server's Relay-Reply RX packet with a NAT
// configuration and validate the relayed TX Reply packet to client
TEST_F(DHCPv6HandlerTest, SrcDHCPV6RelayReply) {
  // Setup SwitchState for NAT scenario with translation addresses
  auto handle = setupTestHandleNAT();
  verifyRelayReplyIsSentFrom(handle.get(), kDhcpV6ReplySrc, VlanID(55));
}

TEST_F(DHCPv6HandlerTest, DHCPV6RelayReplyForPortInterface) {
  auto handle = createTestHandle(testStateWithPortInterfaceReplySrc());
  verifyRelayReplyIsSentFrom(handle.get(), kPortInterfaceIP, VlanID(1));
}

// Test to inject a DHCPV6 agent's Relay-Forward RX packet and validate the
// relayed TX packet
TEST_F(DHCPv6HandlerTest, DHCPV6RelayForward) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 server RelayForward

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = MockPlatform::getMockLocalMac().toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Server IP
  auto senderIP = kDhcpV6RelaySrcStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kDhcpV6RelayStr;
  // UDP Src and Dst ports for DHCPV6 relay forward
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  // DHCPV6 Relay-Forward Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Forward Message: type (12), Hopcount (1)
      "0c 01" +
      // DHCPV6 Relay-Forward Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Forward Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Forward-Request Message Content
  const string dhcpV6RequestMessage =
      // DHCPV6 Request Message: type (3), txnId (dummy 0x571958)
      "03 57 19 58"
      // DHCPV6 Request Message options
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      // DHCPv6 IA for Non-temporary Address: Value (dummy)
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      // DHCPv6 DNS recursive name server: Value (dummy)
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Forward Message options
  const string dhcpV6RelayMessageOptions =
      // InterfaceID: Option (18 = 0012),  Length (6 = 0006), Value (Client's
      // MAC)
      "00 12 00 06" + kClientMacStr +
      // Relay Message: Option (9 = 0009),  Length (120= 0078), Value (DHCP
      // Request)
      "00 09 00 78" +
      // Relay Message: Value (DHCP Request Message Content)
      dhcpV6RequestMessage;

  constexpr auto dhcpV6HdrSize = 168; // computed for hdr and options above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Construct the DHCPV6 reply packet structure to pass into the validator
  // routine
  auto dhcp6RawPktBuf =
      PktUtil::parseHexData(dhcpV6Hdr + dhcpV6RelayMessageOptions);
  Cursor cDhcp(&dhcp6RawPktBuf);
  DHCPv6Packet dhcp6RelayFwdMsg;
  dhcp6RelayFwdMsg.parse(&cDhcp);

  // Setup the validator function for the response packet
  EXPECT_SWITCHED_PKT(
      sw,
      "DHCPV6 Relay Forward",
      checkDHCPV6RelayForward(
          MockPlatform::getMockLocalMac(),
          MockPlatform::getMockLocalMac(),
          kClientVlan,
          kDhcpV6RelaySrc,
          kDhcpV6Relay,
          DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
          DHCPv6Packet::DHCP6_SERVERAGENT_UDPPORT,
          dhcp6RelayFwdMsg));

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayMessageOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a bad DHCPV6 request RX packet and validate that it's
// dropped and counted
TEST_F(DHCPv6HandlerTest, DHCPV6BadRequest) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 client request

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Client MAC
  auto senderMac = kClientMac.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Client Link Local IP
  auto senderIP = kDhcpV6ClientLocalIpStr;
  // Dest Router MAC
  auto targetMac = kDhcpV6AllRoutersMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Router IP
  auto targetIP = kDhcpV6AllRoutersIpStr;
  // UDP Src and Dst ports for DHCPV6 request
  const string srcPort = "02 22";
  const string dstPort = "02 23";
  // DHCPV6 Request Message type (3), txnId (dummy 0x571958)
  const string dhcpV6Hdr = "03 57 19 58";
  // 1200 bytes of padding to create a larger than expected DHCPv6 packet
  const string dhcpV6RequestTooLongPad(2400, '0');
  // DHCPV6 Request Message options
  const string dhcpV6RequestOptions =
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 Option Request:  Option (0006), Length (0004), Value (0017 0018)
      "00 06 00 04 00 17 00 18"
      // DHCPv6 Elapsed time Option (0008), Length (0002), Value (0000)
      "00 08 00 02 00 00"
      // DHCPv6 IA for prefix delegation Option (0025), Length (00 29) Value
      // (dummy)
      "00 25 00 29 27 fe 8f 95 00 00 0e 10 00 00 15 18 00 1a 00 19 00 00 1c"
      "20 00 00 1d 4c 40 20 01 00 00 00 00 fe 00 00 00 00 00 00 00 00 00" +
      dhcpV6RequestTooLongPad;

  constexpr auto dhcpV6HdrSize =
      1299; // computed for hdr, options and padding above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RequestOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.drop_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a bad DHCPV6 server's Relay-Reply RX packet and validate that
// it's dropped and counted
TEST_F(DHCPv6HandlerTest, DHCPV6DropRelayReply) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 server RelayReply

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = "08 00 27 d4 10 bb";
  // Server IP
  auto senderIP = kDhcpV6RelayStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kVlanInterfaceIPStr;
  // UDP Src and Dst ports for DHCPV6 relay reply
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  // DHCPV6 Relay-Reply Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Reply Message: type (13), Hopcount (1)
      "0d 01" +
      // DHCPV6 Relay-Reply Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Reply Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Reply Message Content
  const string dhcpV6ReplyMessage =
      // DHCPV6 Reply Message: type (7), txnId (dummy 0x571958)
      "07 57 19 58"
      // DHCPV6 Reply Message options
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      // DHCPv6 IA for Non-temporary Address: Value (dummy)
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      // DHCPv6 DNS recursive name server: Value (dummy)
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Reply Message options
  const string dhcpV6RelayReplyOptions =
      // USE INVALID MAC ADDRESS IN THE NTERFACEID OPTION TO CREATE AN
      // UNEXPECTED RELAY PKT InterfaceID: Option (18 = 0012),  Length (6 =
      // 0006), Value (MAC) "00 12 00 06" + kClientMacStr +
      "00 12 00 06"
      "00 00 00 00 00 00"
      // Relay Reply: Option (9 = 0009),  Length (120 = 0078), Value (DHCP
      // Reply)
      "00 09 00 78" +
      // Relay Reply: Value (DHCP Reply Message Content)
      dhcpV6ReplyMessage;

  constexpr auto dhcpV6HdrSize = 168; // computed for hdr and options above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayReplyOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.drop_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Test to inject a bad DHCPV6 server's Relay-Forward RX packet and validate
// that it's dropped and counted
TEST_F(DHCPv6HandlerTest, DHCPV6BadRelayForward) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Initialize the injection packet fields for a DHCPV6 server RelayForward

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = MockPlatform::getMockLocalMac().toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  // Server IP
  auto senderIP = kDhcpV6RelaySrcStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kDhcpV6RelayStr;
  // UDP Src and Dst ports for DHCPV6 relay forward
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  const std::string relayHopCount = fmt::format(
      "{0:02x}", static_cast<uint16_t>(DHCPv6Handler::MAX_RELAY_HOPCOUNT));
  // DHCPV6 Relay-Forward Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Forward Message: type (12), Hopcount (MAX_RELAY_HOPCOUNT)
      // => !! BAD !!
      "0c" + relayHopCount +
      // DHCPV6 Relay-Forward Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Forward Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Forward-Request Message Content
  const string dhcpV6RequestMessage =
      // DHCPV6 Request Message: type (3), txnId (dummy 0x571958)
      "03 57 19 58"
      // DHCPV6 Request Message options
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Client Identifier: Value (dummy: 000100011c38262d080027fe8f95)
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      // DHCPv6 Server Identifier: Value (dummy: 000100011c3825e8080027d410bb)
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      // DHCPv6 IA for Non-temporary Address: Value (dummy)
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      // DHCPv6 DNS recursive name server: Value (dummy)
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Forward Message options
  const string dhcpV6RelayMessageOptions =
      // InterfaceID: Option (18 = 0012),  Length (6 = 0006), Value (Client's
      // MAC)
      "00 12 00 06" + kClientMacStr +
      // Relay Message: Option (9 = 0009),  Length (120 = 0078), Value (DHCP
      // Request)
      "00 09 00 78" +
      // Relay Message: Value (DHCP Request Message Content)
      dhcpV6RequestMessage;

  constexpr auto dhcpV6HdrSize = 168; // computed for hdr and options above

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayMessageOptions);

  // Validate the counter cache update
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.drop_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

// Inject a DHCPV6 Relay-Reply whose INTERFACE_ID option declares a length other
// than MacAddress::SIZE (6). processDHCPv6RelayReply reads a fixed
// MacAddress::SIZE bytes from the option to form the outbound destination MAC,
// so a shorter attacker-supplied option must be dropped (and counted as a bad
// packet) rather than over-reading past the parsed options buffer.
TEST_F(DHCPv6HandlerTest, DHCPV6RelayReplyBadInterfaceIdLen) {
  // Setup SwitchState
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  // Client VLAN
  auto vlanID = kClientVlan;
  const string vlan = kClientVlanStr;
  // Router MAC (dummy)
  auto senderMac = "08 00 27 d4 10 bb";
  // Server IP
  auto senderIP = kDhcpV6RelayStr;
  // Dest Router MAC
  auto targetMac = MockPlatform::getMockLocalMac().toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // Relay VLAN IP
  auto targetIP = kVlanInterfaceIPStr;
  // UDP Src and Dst ports for DHCPV6 relay reply
  const string srcPort = "02 23";
  const string dstPort = "02 23";
  // DHCPV6 Relay-Reply Message Header
  const string dhcpV6Hdr =
      // DHCPV6 Relay-Reply Message: type (13), Hopcount (1)
      "0d 01" +
      // DHCPV6 Relay-Reply Message: LinkAddr
      kVlanInterfaceIPStr +
      // DHCPV6 Relay-Reply Message: PeerAddr
      kDhcpV6ClientLocalIpStr;

  // DHCPV6 Relay-Reply Message Content
  const string dhcpV6ReplyMessage =
      // DHCPV6 Reply Message: type (7), txnId (dummy 0x571958)
      "07 57 19 58"
      // DHCPv6 Client Identifier: Option (0001),  Length (14 = 000e)
      "00 01 00 0e"
      "00 01 00 01 1c 38 26 2d 08 00 27 fe 8f 95"
      // DHCPv6 Server Identifier: Option (0002),  Length (14 = 000e)
      "00 01 00 0e"
      "00 01 00 01 1c 38 25 e8 08 00 27 d4 10 bb"
      // DHCPv6 IA for Non-temporary Address: Option (0003), Length (40 = 0028)
      "00 03 00 28"
      "11 88 88 d3 00 00 00 00 00 00 00 00 00 05 00 18 20 01 05 04 07 12"
      "00 02 00 00 00 00 00 00 02 01 00 00 69 78 00 00 a8 c0"
      // DHCPv6 DNS recursive name server: Option (0017), Length (32 = 0020)
      "00 17 00 20"
      "fc 00 05 04 07 00 00 00 00 10 00 32 00 00 00 18 fc"
      "00 05 04 07 00 00 00 00 10 00 32 00 00 00 20";

  // DHCPV6 Relay-Reply Message options with a malformed INTERFACE_ID length.
  const string dhcpV6RelayReplyOptions =
      // InterfaceID: Option (18 = 0012), Length (4 = 0004 -> invalid, must be
      // 6)
      "00 12 00 04"
      // InterfaceID value (only 4 bytes, fewer than MacAddress::SIZE)
      "00 00 00 00"
      // Relay Reply: Option (9 = 0009), Length (120 = 0078), Value (DHCP Reply)
      "00 09 00 78" +
      dhcpV6ReplyMessage;

  // hdr(34) + options: interfaceId(4+4) + relayReply(4+120) = 166
  constexpr auto dhcpV6HdrSize = 166;

  // Cache the current stats
  CounterCache counters(sw);

  // A dropped relay reply should not trigger a state update or an outbound pkt
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // Inject the test packet
  sendDHCPV6Packet(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      dhcpV6HdrSize,
      dhcpV6Hdr,
      dhcpV6RelayReplyOptions);

  // The packet is trapped, counted as bad (bad also bumps drop), and dropped.
  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV6.drop_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}
