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
#include "fboss/agent/hw/mock/MockHwSwitch.h"
#include "fboss/agent/hw/mock/MockPlatform.h"
#include "fboss/agent/packet/DHCPv4Packet.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/UDPHeader.h"
#include "fboss/agent/state/ArpResponseTable.h"
#include "fboss/agent/state/Interface.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/state/Vlan.h"
#include "fboss/agent/state/VlanMap.h"
#include "fboss/agent/test/CounterCache.h"
#include "fboss/agent/test/HwTestHandle.h"
#include "fboss/agent/test/TestUtils.h"

#include <gtest/gtest.h>

using namespace facebook::fboss;
using folly::IPAddressV4;
using folly::MacAddress;
using folly::io::Cursor;
using std::back_inserter;
using std::make_shared;
using std::make_unique;
using std::replace;
using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;

using ::testing::_;
using testing::Return;

namespace {
const IPAddressV4 kVlanInterfaceIP("10.0.0.1");
const IPAddressV4 kDhcpV4Relay("20.20.20.20");
const MacAddress kClientMac("02:00:00:00:00:02");
const IPAddressV4 kClientIP("10.0.0.10");

const MacAddress kClientMacOverride("03:00:00:00:00:03");
const IPAddressV4 kDhcpOverride("30.30.30.30");

const IPAddressV4 kDhcpV4RelaySrc("88.0.0.1");
// have to match an interface (fboss55) IP address
const IPAddressV4 kDhcpV4ReplySrc("10.0.55.1");

template <typename VlansOrIntfsT, typename NodeIDT>
shared_ptr<SwitchState> testStateHelper(
    std::shared_ptr<SwitchState> state,
    VlansOrIntfsT vlansOrIntfs,
    NodeIDT nodeId) {
  // Set up an arp response entry for VLAN 1, 10.0.0.1,
  // so that we can detect the packet to 10.0.0.1 is for myself
  auto respTable1 = make_shared<ArpResponseTable>();
  respTable1->setEntry(
      kVlanInterfaceIP, MockPlatform::getMockLocalMac(), InterfaceID(1));

  vlansOrIntfs->getNode(nodeId)->setArpResponseTable(respTable1);
  vlansOrIntfs->getNode(nodeId)->setDhcpV4Relay(kDhcpV4Relay);
  DhcpV4OverrideMap overrides;
  overrides[kClientMacOverride] = kDhcpOverride;
  vlansOrIntfs->getNode(nodeId)->setDhcpV4RelayOverrides(overrides);
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
  switchSettings->setDhcpV4RelaySrc(kDhcpV4RelaySrc);
  switchSettings->setDhcpV4ReplySrc(kDhcpV4ReplySrc);
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

void sendDHCPPacket(
    HwTestHandle* handle,
    string srcMac,
    string dstMac,
    string vlan,
    string srcIp,
    string dstIp,
    string srcPort,
    string dstPort,
    string bootpOp,
    string dhcpMsgTypeOpt,
    string appendOptions = "",
    string yiaddr = "00 00 00 00") {
  string chaddr = kClientMac.toString();
  replace(chaddr.begin(), chaddr.end(), ':', ' ');
  // Pad 10 zero bytes to fill in 16 byte chaddr
  chaddr += "00 00 00 00 00 00 00 00 00 00";

  constexpr auto ethHdrSize = 16;
  constexpr auto ipHdrSize = 20;
  constexpr auto udpHdrSize = 8;
  auto payloadSize = 248 + PktUtil::parseHexData(appendOptions).length();
  std::string ipHdrLengthStr =
      fmt::format("{0:04x}", ipHdrSize + udpHdrSize + payloadSize);
  std::string udpHdrLengthStr =
      fmt::format("{0:04x}", +udpHdrSize + payloadSize);

  auto buf = make_unique<folly::IOBuf>(PktUtil::parseHexData(
      // Ethernet header
      dstMac + srcMac + "81 00" + vlan +
      // Ether type
      "08 00" +
      // IPv4 Header
      // Version, IHL, DSCP, ECN, Length (272)
      "45  00" + ipHdrLengthStr +
      // Id, Flags, frag offset,
      "00  00  00  00"
      // TTL, Protocol (UDP), checksum
      "ff  11  00  00" +
      // UDP Header
      srcIp + dstIp + srcPort + dstPort +
      // Length (252), checksum
      udpHdrLengthStr + "00  00" +
      // DHCPv4 Packet
      // op(1),htype(1), hlen(6), hops(1)
      bootpOp +
      "01  06  01"
      // xid (10.10.10.1)
      "0a  0a  0a  01"
      // secs(10) flags(0x0000)
      "00  0a  00  00"
      // ciaddr (10.10.10.2)
      "0a  0a  0a  02"
      // yiaddr
      + yiaddr +
      // siaddr (10.10.10.4)
      "0a  0a  0a  04"
      // giaddr (10.10.10.5)
      "0a  0a  0a  05" +
      chaddr +
      // Sname (abcd)
      "61  62  63  64"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      // File (defg)
      "65  66  67  68"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      "00  00  00  00"
      // DHCP Cookie {99, 130, 83, 99};
      "63  82  53 63"
      // DHCP msg type option + 1 byte pad
      + dhcpMsgTypeOpt
      // Other options to append
      + appendOptions +
      // 4 X Pad, 1 X end
      "00  00  00 00 ff"));
  ASSERT_EQ(
      payloadSize + udpHdrSize + ipHdrSize + ethHdrSize + /*'\0\0'*/ 2,
      buf->length())
      << "Don't forget to adjust the headers' length";
  handle->rxPacket(std::move(buf), PortDescriptor(PortID(1)), VlanID(1));
}

struct Option {
  uint8_t op{0};
  uint8_t optLen{0};
  vector<uint8_t> optData;
  bool checkPresent{false};
};

TxMatchFn checkDHCPPkt(
    MacAddress dstMac,
    VlanID vlan,
    IPAddressV4 srcIp,
    IPAddressV4 dstIp,
    uint16_t srcPort,
    uint16_t dstPort,
    IPAddressV4 giaddr,
    const vector<Option>& options) {
  return [=](const TxPacket* txPacket) {
    const auto* buf = txPacket->buf();
    Cursor c(buf);
    EthHdr ethHdr(c);
    if (ethHdr.dstAddr != dstMac) {
      throw FbossError(
          "expected dest MAC to be ", dstMac, "; got ", ethHdr.dstAddr);
    }
    if (VlanID(ethHdr.vlanTags[0].vid()) != vlan) {
      throw FbossError(
          "expected vlan to be ",
          vlan,
          "; got ",
          VlanID(ethHdr.vlanTags[0].vid()));
    }
    if (ethHdr.etherType != static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4)) {
      throw FbossError(
          "expected ether type to be ",
          static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4),
          "; got ",
          ethHdr.etherType);
    }
    IPv4Hdr ipHdr(c);
    if (ipHdr.srcAddr != srcIp) {
      throw FbossError(
          "expected source ip to be ", srcIp, "; got ", ipHdr.srcAddr);
    }
    if (ipHdr.dstAddr != dstIp) {
      throw FbossError(
          "expected destination ip to be ", dstIp, "; got ", ipHdr.dstAddr);
    }
    if (ipHdr.protocol != IPPROTO_UDP) {
      throw FbossError(
          "expected protocol to be ", IPPROTO_UDP, "; got ", ipHdr.protocol);
    }

    SwitchStats switchStats(1 /*numSwitches*/);
    PortStats portStats(PortID(0), "foo", &switchStats);
    UDPHeader udpHdr;
    udpHdr.parse(&c, &portStats);
    if (udpHdr.srcPort != srcPort) {
      throw FbossError(
          "expected source port to be ", srcPort, "; got ", udpHdr.srcPort);
    }
    if (udpHdr.dstPort != dstPort) {
      throw FbossError(
          "expected destination port to be ",
          dstPort,
          "; got ",
          udpHdr.dstPort);
    }
    DHCPv4Packet dhcpPkt;
    dhcpPkt.parse(&c);
    if (!giaddr.isZero() && dhcpPkt.giaddr != giaddr) {
      throw FbossError(
          "expected giaddr to be ", kVlanInterfaceIP, "; got ", dhcpPkt.giaddr);
    }
    for (auto& option : options) {
      vector<uint8_t> optData;
      if (option.checkPresent) {
        if (!DHCPv4Packet::getOptionSlow(option.op, dhcpPkt.options, optData)) {
          throw FbossError(
              "expected option ",
              static_cast<int>(option.op),
              " to be "
              "present, but not found");
        }
        if (!equal(
                option.optData.begin(),
                option.optData.end(),
                optData.begin())) {
          throw FbossError(
              "unmatched data for option ", static_cast<int>(option.op));
        }
      } else {
        if (DHCPv4Packet::getOptionSlow(option.op, dhcpPkt.options, optData)) {
          throw FbossError(
              "unexpected option ", static_cast<int>(option.op), " found ");
        }
      }
    }
  };
}

TxMatchFn checkDHCPReq(
    IPAddressV4 dhcpRelay = kDhcpV4Relay,
    IPAddressV4 dhcpRelaySrc = kVlanInterfaceIP) {
  MacAddress dstMac = MockPlatform::getMockLocalMac();
  VlanID vlan(1);
  IPAddressV4 srcIp = dhcpRelaySrc;
  IPAddressV4 dstIp = dhcpRelay;
  uint16_t srcPort = DHCPv4Handler::kBootPSPort;
  uint16_t dstPort = DHCPv4Handler::kBootPSPort;
  IPAddressV4 giaddr = dhcpRelaySrc;
  Option dhcpAgentOption;
  dhcpAgentOption.op = 82;
  dhcpAgentOption.optLen = 2 + dhcpRelaySrc.byteCount();
  dhcpAgentOption.optData.push_back(DHCPv4Handler::AGENT_CIRCUIT_ID);
  dhcpAgentOption.optData.push_back(dhcpRelaySrc.byteCount());
  copy(
      dhcpRelaySrc.bytes(),
      dhcpRelaySrc.bytes() + kVlanInterfaceIP.byteCount(),
      back_inserter(dhcpAgentOption.optData));
  dhcpAgentOption.checkPresent = true;
  vector<Option> optionsToCheck;
  optionsToCheck.push_back(dhcpAgentOption);
  return checkDHCPPkt(
      dstMac, vlan, srcIp, dstIp, srcPort, dstPort, giaddr, optionsToCheck);
}

TxMatchFn checkDHCPReply(
    IPAddressV4 replySrc = kVlanInterfaceIP,
    VlanID vlan = VlanID(1)) {
  MacAddress dstMac = kClientMac;
  IPAddressV4 srcIp = replySrc;
  IPAddressV4 dstIp = kClientIP;
  uint16_t srcPort = DHCPv4Handler::kBootPSPort;
  uint16_t dstPort = DHCPv4Handler::kBootPCPort;
  IPAddressV4 giaddr("0.0.0.0");
  Option dhcpAgentOption;
  dhcpAgentOption.op = 82;
  dhcpAgentOption.checkPresent = false;
  vector<Option> optionsToCheck;
  optionsToCheck.push_back(dhcpAgentOption);
  return checkDHCPPkt(
      dstMac, vlan, srcIp, dstIp, srcPort, dstPort, giaddr, optionsToCheck);
}

} // unnamed namespace

/*
 * DHCPv4HandlerTest tests validate DHCP relay with VLANs and Interfaces for
 * NPU switches.
 *
 * TODO(skhare) Validate for VOQ switches as well. Since VOQ switches don't
 * support VLAns. That will involve modifying the pkts in these tests to not
 * carry VLANs.
 */

class DHCPv4HandlerTest : public ::testing::Test {
  void SetUp() override {}
};

TEST_F(DHCPv4HandlerTest, DHCPRequest) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();

  VlanID vlanID(1);
  const char* senderIP = "00 00 00 00";
  // Client mac
  auto senderMac = kClientMac.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  const string targetMac = "ff ff ff ff ff ff";
  const string targetIP = "ff ff ff ff";
  const string bootpOp = "01";
  const string vlan = "00 01";
  const string srcPort = "00 43";
  const string dstPort = "00 44";
  // DHCP Message type (option = 53, len = 1, message type = DHCP discover
  const string dhcpMsgTypeOpt = "35  01  01";
  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  EXPECT_SWITCHED_PKT(sw, "DHCP request", checkDHCPReq());

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

TEST_F(DHCPv4HandlerTest, DHCPRequestMaxMessageSize) {
  constexpr uint16_t kMaxMsgSize = 576;
  constexpr size_t kFixedPartAndCookieSize = 240;
  constexpr size_t kMsgTypeOptSize = 3;
  constexpr size_t kTrailingPadSize = 4;
  constexpr size_t kAgentOptSize = 8;
  constexpr size_t kEndOptSize = 1;
  constexpr size_t kMaxOptDataLen = 255;
  constexpr size_t kOptHeaderSize = 2;
  const string maxMsgSizeOpt =
      fmt::format("39 02 {:02x} {:02x}", kMaxMsgSize >> 8, kMaxMsgSize & 0xff);
  const size_t relayedSizeWithoutFiller = kFixedPartAndCookieSize +
      kMsgTypeOptSize + PktUtil::parseHexData(maxMsgSizeOpt).length() +
      kTrailingPadSize + kAgentOptSize + kEndOptSize;

  auto vendorSpecificOpt = [](size_t dataLen) {
    string opt = fmt::format(" 2b {:02x}", dataLen);
    for (size_t i = 0; i < dataLen; ++i) {
      opt += " 41";
    }
    return opt;
  };
  auto optionsForRelayedSize = [&](size_t relayedSize) {
    string options = maxMsgSizeOpt;
    for (size_t fillerSize = relayedSize - relayedSizeWithoutFiller;
         fillerSize > 0;) {
      const size_t dataLen =
          std::min(fillerSize - kOptHeaderSize, kMaxOptDataLen);
      options += vendorSpecificOpt(dataLen);
      fillerSize -= kOptHeaderSize + dataLen;
    }
    return options;
  };

  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  auto clientMac = kClientMac.toString();
  std::replace(clientMac.begin(), clientMac.end(), ':', ' ');
  auto sendRequest = [&](const string& options) {
    sendDHCPPacket(
        handle.get(),
        clientMac,
        "ff ff ff ff ff ff",
        "00 01",
        "00 00 00 00",
        "ff ff ff ff",
        "00 43",
        "00 44",
        "01",
        "35 01 01",
        options);
  };

  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);
  {
    CounterCache counters(sw);
    EXPECT_SWITCHED_PKT(sw, "DHCP request", checkDHCPReq());
    sendRequest(optionsForRelayedSize(kMaxMsgSize));
    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.bad_pkt.sum", 0);
  }
  {
    CounterCache counters(sw);
    EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(0);
    sendRequest(optionsForRelayedSize(kMaxMsgSize + 1));
    counters.update();
    counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
    counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.bad_pkt.sum", 1);
  }
}

TEST_F(DHCPv4HandlerTest, RelayOverrideDHCPRequest) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  VlanID vlanID(1);
  const char* senderIP = "00 00 00 00";
  // Client mac
  auto senderMac = kClientMacOverride.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  const string targetMac = "ff ff ff ff ff ff";
  const string targetIP = "ff ff ff ff";
  const string bootpOp = "01";
  const string vlan = "00 01";
  const string srcPort = "00 43";
  const string dstPort = "00 44";
  // DHCP Message type (option = 53, len = 1, message type = DHCP discover
  const string dhcpMsgTypeOpt = "35  01  01";

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  EXPECT_SWITCHED_PKT(sw, "DHCP request", checkDHCPReq(kDhcpOverride));

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt);
}

TEST_F(DHCPv4HandlerTest, RelaySrcDHCPRequest) {
  auto handle = setupTestHandleNAT();
  auto sw = handle->getSw();
  VlanID vlanID(1);
  const char* senderIP = "00 00 00 00";
  // Client mac
  auto senderMac = kClientMacOverride.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  const string targetMac = "ff ff ff ff ff ff";
  const string targetIP = "ff ff ff ff";
  const string bootpOp = "01";
  const string vlan = "00 01";
  const string srcPort = "00 43";
  const string dstPort = "00 44";
  // DHCP Message type (option = 53, len = 1, message type = DHCP discover
  const string dhcpMsgTypeOpt = "35  01  01";

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  EXPECT_SWITCHED_PKT(
      sw, "DHCP request", checkDHCPReq(kDhcpOverride, kDhcpV4RelaySrc));

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt);
}

TEST_F(DHCPv4HandlerTest, DHCPReply) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  VlanID vlanID(55);
  // Client mac
  auto senderMac = MockPlatform::getMockLocalMac().toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  auto targetMac = kClientMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // DHCP server IP
  const char* senderIP = "14 14 14 14";
  // Cpu IP
  const string targetIP = "0a 00 00 01";
  const string bootpOp = "02";
  const string vlan = "00 01";
  const string srcPort = "00 44";
  const string dstPort = "00 43";
  // DHCP Message type (option = 53, len = 1, message type = DHCP offer
  const string dhcpMsgTypeOpt = "35  01  02";
  // Client IP addr, should be used as destination IP for DHCP replies
  const string yiaddr = "0a 00 00 0a";
  // Agent option should get stripped from reply
  const string agentOption = "52 02 00 00";

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  EXPECT_SWITCHED_PKT(sw, "DHCP reply", checkDHCPReply());

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt,
      agentOption,
      yiaddr);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

TEST_F(DHCPv4HandlerTest, RelaySrcDHCPReply) {
  auto handle = setupTestHandleNAT();
  auto sw = handle->getSw();
  VlanID vlanID(55);
  // Client mac
  auto senderMac = MockPlatform::getMockLocalMac().toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  auto targetMac = kClientMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // DHCP server IP
  const char* senderIP = "14 14 14 14";
  // Cpu IP
  const string targetIP = "0a 00 00 01";
  const string bootpOp = "02";
  const string vlan = "00 01";
  const string srcPort = "00 44";
  const string dstPort = "00 43";
  // DHCP Message type (option = 53, len = 1, message type = DHCP offer
  const string dhcpMsgTypeOpt = "35  01  02";
  // Client IP addr, should be used as destination IP for DHCP replies
  const string yiaddr = "0a 00 00 0a";
  // Agent option should get stripped from reply
  const string agentOption = "52 02 00 00";

  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);

  // DHCP reply source is override to fboss55's address
  EXPECT_SWITCHED_PKT(
      sw, "DHCP reply", checkDHCPReply(kDhcpV4ReplySrc, VlanID(55)));

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt,
      agentOption,
      yiaddr);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

TEST_F(DHCPv4HandlerTest, DHCPBadRequest) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  VlanID vlanID(1);
  const string senderIP = "00 00 00 00";
  auto senderMac = "00 00 00 00 00 01";
  const string targetMac = "ff ff ff ff ff ff";
  const string targetIP = "ff ff ff ff";
  const string bootpOp = "03"; // Bad bootp message type
  const string vlan = "00 01";
  const string srcPort = "00 43";
  const string dstPort = "00 44";
  // DHCP Message type (option = 53, len = 1, message type = DHCP discover
  const string dhcpMsgTypeOpt = "35  01  01";
  // Cache the current stats
  CounterCache counters(sw);

  // Sending an DHCP request should not trigger state update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);
  EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(0);

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.drop_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

TEST_F(DHCPv4HandlerTest, DHCPMalformedOptionOOB) {
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  VlanID vlanID(1);
  const char* senderIP = "00 00 00 00";
  auto senderMac = kClientMac.toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  const string targetMac = "ff ff ff ff ff ff";
  const string targetIP = "ff ff ff ff";
  const string bootpOp = "01";
  const string vlan = "00 01";
  const string srcPort = "00 43";
  const string dstPort = "00 44";
  // DHCP Message type (option = 53, len = 1, message type = DHCP discover)
  const string dhcpMsgTypeOpt = "35  01  01";
  // Craft a malformed option whose declared length runs past the options
  // buffer: option code 0x03 (Router), length 0xFF, as the final option. The
  // code must be one that is NOT special-cased in addAgentOptions() so it falls
  // through to processOption() (the OOB-read site); a handled code like
  // DHCP_AGENT_OPTIONS (0x52) would short-circuit with return-false before
  // processOption, so the test would pass even without the isOptionInBounds()
  // guard. With code 0x03 the guard is what drops the packet, and removing it
  // would fire the OOB read under ASan.
  const string malformedOption = "03  ff";
  // Cache the current stats
  CounterCache counters(sw);

  // Sending a DHCP request with a malformed option should not trigger state
  // update
  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);
  // Should NOT relay the packet (drops it due to bounds check failure)
  EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(0);

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt,
      malformedOption);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);

  // Now send a well-formed packet and assert it relays successfully
  CounterCache counters2(sw);
  EXPECT_SWITCHED_PKT(sw, "DHCP request", checkDHCPReq());

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt);

  counters2.update();
  counters2.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters2.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}

TEST_F(DHCPv4HandlerTest, DHCPMalformedOptionOOBReply) {
  // Same malformed-option OOB check as DHCPMalformedOptionOOB, but on the
  // BOOTREPLY path so the reply walker `stripAgentOptions` (not
  // `addAgentOptions`) is exercised. Mirrors the DHCPReply setup.
  auto handle = setupTestHandle();
  auto sw = handle->getSw();
  // Client mac
  auto senderMac = MockPlatform::getMockLocalMac().toString();
  std::replace(senderMac.begin(), senderMac.end(), ':', ' ');
  auto targetMac = kClientMac.toString();
  std::replace(targetMac.begin(), targetMac.end(), ':', ' ');
  // DHCP server IP
  const char* senderIP = "14 14 14 14";
  // Cpu IP
  const string targetIP = "0a 00 00 01";
  const string bootpOp = "02";
  const string vlan = "00 01";
  const string srcPort = "00 44";
  const string dstPort = "00 43";
  // DHCP Message type (option = 53, len = 1, message type = DHCP offer)
  const string dhcpMsgTypeOpt = "35  01  02";
  // Client IP addr, used as destination IP for DHCP replies
  const string yiaddr = "0a 00 00 0a";
  // Option code 0x03 (Router), length 0xFF as the final option: declared
  // length runs past the options buffer. It is not special-cased in
  // stripAgentOptions(), so it falls through to processOption() (the OOB-read
  // site); the isOptionInBounds() guard is what drops the packet.
  const string malformedOption = "03  ff";

  CounterCache counters(sw);

  EXPECT_HW_CALL(sw, stateChangedImpl(_, _)).Times(0);
  // Malformed reply must be dropped, not relayed.
  EXPECT_HW_CALL(sw, sendPacketSwitchedAsync_(_)).Times(0);

  sendDHCPPacket(
      handle.get(),
      senderMac,
      targetMac,
      vlan,
      senderIP,
      targetIP,
      srcPort,
      dstPort,
      bootpOp,
      dhcpMsgTypeOpt,
      malformedOption,
      yiaddr);

  counters.update();
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "dhcpV4.bad_pkt.sum", 1);
  counters.checkDelta(SwitchStats::kCounterPrefix + "trapped.pkts.sum", 1);
}
