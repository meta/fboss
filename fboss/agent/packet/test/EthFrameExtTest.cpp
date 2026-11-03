// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/packet/EthFrame.h"

#include <gtest/gtest.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/IPPacket.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"
#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/packet/MPLSPacket.h"
#include "fboss/agent/packet/PktUtil.h"
#include "fboss/agent/packet/UDPDatagram.h"
#include "fboss/agent/packet/UDPHeader.h"

using namespace facebook::fboss;
using namespace facebook::fboss::utility;

namespace {
const auto kSrcMac = folly::MacAddress("02:00:00:00:00:01");
const auto kDstMac = folly::MacAddress("02:00:00:00:00:02");
const auto kSrcIpV4 = folly::IPAddressV4("10.0.0.1");
const auto kDstIpV4 = folly::IPAddressV4("10.0.0.2");
const auto kSrcIpV6 = folly::IPAddressV6("2001:db8::1");
const auto kDstIpV6 = folly::IPAddressV6("2001:db8::2");
} // namespace

// --- Parse IPv4 frame ---

TEST(EthFrameExtTest, parseIpv4Frame) {
  auto buf = PktUtil::parseHexData(
      // Dest mac, source mac
      "02 00 00 00 00 02  02 00 00 00 00 01"
      // 802.1q, VLAN 10
      "81 00 00 0a"
      // IPv4
      "08 00"
      // IPv4 header: version/IHL, DSCP/ECN, total length
      "45 00 00 14"
      // ID, flags/frag
      "00 00 40 00"
      // TTL, protocol (no next), checksum (0 - not validated during parsing)
      "40 00 00 00"
      // Src IP: 10.0.0.1
      "0a 00 00 01"
      // Dst IP: 10.0.0.2
      "0a 00 00 02");
  folly::io::Cursor cursor(&buf);
  EthFrame frame(cursor);

  EXPECT_TRUE(frame.v4PayLoad().has_value());
  EXPECT_FALSE(frame.v6PayLoad().has_value());
  EXPECT_FALSE(frame.mplsPayLoad().has_value());
  EXPECT_FALSE(frame.arpHdr().has_value());
  EXPECT_EQ(frame.v4PayLoad()->header().srcAddr, kSrcIpV4);
  EXPECT_EQ(frame.v4PayLoad()->header().dstAddr, kDstIpV4);

  // Serialize round-trip
  auto buf2 = frame.toIOBuf();
  EXPECT_TRUE(folly::IOBufEqualTo()(*buf2, buf));
}

// --- Parse ARP frame ---

TEST(EthFrameExtTest, parseArpFrame) {
  auto buf = PktUtil::parseHexData(
      // Dest mac (broadcast), source mac
      "ff ff ff ff ff ff  02 00 00 00 00 01"
      // ARP ethertype
      "08 06"
      // ARP: htype=1 ptype=0x0800 hlen=6 plen=4 oper=1 (request)
      "00 01 08 00 06 04 00 01"
      // SHA: 02:00:00:00:00:01
      "02 00 00 00 00 01"
      // SPA: 10.0.0.1
      "0a 00 00 01"
      // THA: 00:00:00:00:00:00
      "00 00 00 00 00 00"
      // TPA: 10.0.0.2
      "0a 00 00 02");
  folly::io::Cursor cursor(&buf);
  EthFrame frame(cursor);

  EXPECT_TRUE(frame.arpHdr().has_value());
  EXPECT_FALSE(frame.v4PayLoad().has_value());
  EXPECT_FALSE(frame.v6PayLoad().has_value());
  EXPECT_EQ(frame.arpHdr()->spa, kSrcIpV4);
  EXPECT_EQ(frame.arpHdr()->tpa, kDstIpV4);

  // Serialize round-trip
  auto buf2 = frame.toIOBuf();
  folly::io::Cursor cursor2(buf2.get());
  EthFrame frame2(cursor2);
  EXPECT_EQ(frame, frame2);
}

// --- Unhandled ethertype throws ---

TEST(EthFrameExtTest, unhandledEthertypeThrows) {
  auto buf = PktUtil::parseHexData(
      // Dest mac, source mac
      "02 00 00 00 00 02  02 00 00 00 00 01"
      // Unknown ethertype 0x9999
      "99 99"
      // Some payload
      "00 00 00 00");
  folly::io::Cursor cursor(&buf);
  EXPECT_THROW({ EthFrame frame(cursor); }, FbossError);
}

// --- Construct with V4 payload ---

TEST(EthFrameExtTest, constructWithV4Payload) {
  auto tags = EthHdr::VlanTags_t{VlanTag(VlanID(1), 0x8100)};
  EthHdr hdr{kSrcMac, kDstMac, {tags}, 0};

  IPv4Hdr ipHdr(
      kSrcIpV4, kDstIpV4, static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP), 0);
  UDPHeader udpHdr(1000, 2000, 0);
  IPPacket<folly::IPAddressV4> ipPkt(
      ipHdr, UDPDatagram(udpHdr, std::vector<uint8_t>(4, 0xff)));

  EthFrame frame(hdr, ipPkt);
  EXPECT_TRUE(frame.v4PayLoad().has_value());
  EXPECT_EQ(
      frame.header().etherType,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4));
}

// --- Construct with V6 payload ---

TEST(EthFrameExtTest, constructWithV6Payload) {
  EthHdr hdr{kSrcMac, kDstMac, {}, 0};

  IPv6Hdr ipHdr;
  ipHdr.srcAddr = kSrcIpV6;
  ipHdr.dstAddr = kDstIpV6;
  ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));
  UDPHeader udpHdr(3000, 4000, 0);
  IPPacket<folly::IPAddressV6> ipPkt(
      ipHdr, UDPDatagram(udpHdr, std::vector<uint8_t>(4, 0xee)));

  EthFrame frame(hdr, ipPkt);
  EXPECT_TRUE(frame.v6PayLoad().has_value());
  EXPECT_EQ(
      frame.header().etherType,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
}

// --- Construct with MPLS payload ---

TEST(EthFrameExtTest, constructWithMPLSPayload) {
  EthHdr hdr{kSrcMac, kDstMac, {}, 0};
  MPLSHdr mplsHdr(MPLSHdr::Label{100, 0, true, 64});

  IPv4Hdr ipHdr(
      kSrcIpV4, kDstIpV4, static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP), 0);
  UDPHeader udpHdr(5000, 5001, 0);
  IPPacket<folly::IPAddressV4> ipPkt(
      ipHdr, UDPDatagram(udpHdr, std::vector<uint8_t>(4, 0xaa)));
  MPLSPacket mplsPkt(mplsHdr, ipPkt);

  EthFrame frame(hdr, mplsPkt);
  EXPECT_TRUE(frame.mplsPayLoad().has_value());
  EXPECT_EQ(
      frame.header().etherType,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS));
}

// --- getEthFrame helper V4 ---

TEST(EthFrameExtTest, getEthFrameV4) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/8000,
      /*dPort=*/8001,
      VlanID(1));

  EXPECT_TRUE(frame.v4PayLoad().has_value());
  EXPECT_EQ(frame.v4PayLoad()->header().srcAddr, kSrcIpV4);
  EXPECT_EQ(frame.v4PayLoad()->header().dstAddr, kDstIpV4);
  auto udp = frame.v4PayLoad()->udpPayload();
  ASSERT_TRUE(udp.has_value());
  EXPECT_EQ(udp->header().srcPort, 8000);
  EXPECT_EQ(udp->header().dstPort, 8001);
  // Default payload is 256 bytes
  EXPECT_EQ(udp->payload().size(), 256);
  ASSERT_EQ(frame.header().vlanTags.size(), 1);
  EXPECT_EQ(frame.header().vlanTags[0].vid(), 1);
}

TEST(EthFrameExtTest, getEthFrameV4NoVlan) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/8000,
      /*dPort=*/8001,
      std::nullopt);

  EXPECT_TRUE(frame.header().vlanTags.empty());
  EXPECT_TRUE(frame.v4PayLoad().has_value());
  EXPECT_EQ(frame.v4PayLoad()->header().srcAddr, kSrcIpV4);
  EXPECT_EQ(frame.v4PayLoad()->header().dstAddr, kDstIpV4);
}

// --- getEthFrame helper V6 ---

TEST(EthFrameExtTest, getEthFrameV6) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV6,
      kDstIpV6,
      /*sPort=*/9000,
      /*dPort=*/9001,
      VlanID(2));

  EXPECT_TRUE(frame.v6PayLoad().has_value());
  EXPECT_EQ(frame.v6PayLoad()->header().srcAddr, kSrcIpV6);
  EXPECT_EQ(frame.v6PayLoad()->header().dstAddr, kDstIpV6);
  auto udp = frame.v6PayLoad()->udpPayload();
  ASSERT_TRUE(udp.has_value());
  EXPECT_EQ(udp->header().srcPort, 9000);
  EXPECT_EQ(udp->header().dstPort, 9001);
}

// --- getEthFrame with custom payload size ---

TEST(EthFrameExtTest, getEthFrameCustomPayloadSize) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1),
      /*payloadSize=*/64);

  auto udp = frame.v4PayLoad()->udpPayload();
  ASSERT_TRUE(udp.has_value());
  EXPECT_EQ(udp->payload().size(), 64);
}

// --- getEthFrame with MPLS labels ---

TEST(EthFrameExtTest, getEthFrameWithMPLSLabels) {
  std::vector<MPLSHdr::Label> labels = {
      {100, 0, false, 64},
      {200, 0, true, 64},
  };
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      labels,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/7000,
      /*dPort=*/7001,
      VlanID(1));

  EXPECT_TRUE(frame.mplsPayLoad().has_value());
  EXPECT_EQ(
      frame.header().etherType,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS));
  EXPECT_TRUE(frame.mplsPayLoad()->v4PayLoad().has_value());
  ASSERT_EQ(frame.header().vlanTags.size(), 1);
  EXPECT_EQ(frame.header().vlanTags[0].vid(), 1);
}

TEST(EthFrameExtTest, getEthFrameWithMPLSLabelsNoVlan) {
  std::vector<MPLSHdr::Label> labels = {
      {100, 0, true, 64},
  };
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      labels,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/7000,
      /*dPort=*/7001,
      std::nullopt);

  EXPECT_TRUE(frame.header().vlanTags.empty());
  EXPECT_TRUE(frame.mplsPayLoad().has_value());
  EXPECT_EQ(
      frame.header().etherType,
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS));
}

// --- setDstMac ---

TEST(EthFrameExtTest, setDstMac) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto newDst = folly::MacAddress("02:00:00:00:00:ff");
  frame.setDstMac(newDst);
  EXPECT_EQ(frame.header().getDstMac(), newDst);
}

// --- setVlan ---

TEST(EthFrameExtTest, setVlan) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  frame.setVlan(VlanID(100));
  EXPECT_EQ(VlanID(frame.header().getVlanTags()[0].vid()), VlanID(100));
}

// --- stripVlans ---

TEST(EthFrameExtTest, stripVlans) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  EXPECT_FALSE(frame.header().getVlanTags().empty());
  frame.stripVlans();
  EXPECT_TRUE(frame.header().getVlanTags().empty());
}

// --- decrementTTL ---

TEST(EthFrameExtTest, decrementTTLV4) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto origTtl = frame.v4PayLoad()->header().ttl;
  frame.decrementTTL();
  EXPECT_EQ(frame.v4PayLoad()->header().ttl, origTtl - 1);
}

TEST(EthFrameExtTest, decrementTTLV6) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV6,
      kDstIpV6,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto origHopLimit = frame.v6PayLoad()->header().hopLimit;
  frame.decrementTTL();
  EXPECT_EQ(frame.v6PayLoad()->header().hopLimit, origHopLimit - 1);
}

// --- equality ---

TEST(EthFrameExtTest, equalFrames) {
  auto frame1 = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto frame2 = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  EXPECT_EQ(frame1, frame2);
}

TEST(EthFrameExtTest, unequalFrames) {
  auto frame1 = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto frame2 = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/3000,
      /*dPort=*/4000,
      VlanID(1));
  EXPECT_NE(frame1, frame2);
}

// --- getTxPacket and makeEthFrame round-trip ---

TEST(EthFrameExtTest, getTxPacketRoundTrip) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/5000,
      /*dPort=*/5001,
      VlanID(1));

  auto txPkt = frame.getTxPacket(&TxPacket::allocateTxPacket);
  ASSERT_NE(txPkt, nullptr);

  auto frame2 = makeEthFrame(*txPkt, /*skipTtlDecrement=*/true);
  EXPECT_EQ(frame, frame2);
}

// --- makeEthFrame with dst MAC override ---

TEST(EthFrameExtTest, makeEthFrameWithDstMac) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV6,
      kDstIpV6,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto txPkt = frame.getTxPacket(&TxPacket::allocateTxPacket);

  auto newDst = folly::MacAddress("02:00:00:00:00:ff");
  auto frame2 = makeEthFrame(*txPkt, newDst);
  EXPECT_EQ(frame2.header().getDstMac(), newDst);
}

// --- makeEthFrame with dst MAC and VLAN override ---

TEST(EthFrameExtTest, makeEthFrameWithDstMacAndVlan) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto txPkt = frame.getTxPacket(&TxPacket::allocateTxPacket);

  auto newDst = folly::MacAddress("02:00:00:00:00:ff");
  auto frame2 = makeEthFrame(*txPkt, newDst, VlanID(42));
  EXPECT_EQ(frame2.header().getDstMac(), newDst);
  EXPECT_EQ(VlanID(frame2.header().getVlanTags()[0].vid()), VlanID(42));
}

// --- toString ---

TEST(EthFrameExtTest, toStringContainsInfo) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  auto str = frame.toString();
  EXPECT_NE(str.find("Eth"), std::string::npos);
  // Verify MAC addresses appear in the output
  EXPECT_NE(str.find("02:00:00:00:00:01"), std::string::npos);
  EXPECT_NE(str.find("02:00:00:00:00:02"), std::string::npos);
}

// --- length calculations ---

TEST(EthFrameExtTest, lengthWithV4) {
  auto frame = getEthFrame(
      kSrcMac,
      kDstMac,
      kSrcIpV4,
      kDstIpV4,
      /*sPort=*/1000,
      /*dPort=*/2000,
      VlanID(1));
  // EthHdr with VLAN + IPv4 header + UDP header + 256 payload
  EXPECT_GT(frame.length(), 0);
  auto buf = frame.toIOBuf();
  EXPECT_EQ(buf->computeChainDataLength(), frame.length());
}

TEST(EthFrameExtTest, lengthHeaderOnly) {
  EthHdr hdr{kSrcMac, kDstMac, {}, 0};
  EthFrame frame(hdr);
  EXPECT_EQ(frame.length(), hdr.size());
}

TEST(EthFrameExtTest, lengthWithArp) {
  auto buf = PktUtil::parseHexData(
      // Dest mac (broadcast), source mac
      "ff ff ff ff ff ff  02 00 00 00 00 01"
      // ARP ethertype
      "08 06"
      // ARP: htype=1 ptype=0x0800 hlen=6 plen=4 oper=1 (request)
      "00 01 08 00 06 04 00 01"
      // SHA: 02:00:00:00:00:01
      "02 00 00 00 00 01"
      // SPA: 10.0.0.1
      "0a 00 00 01"
      // THA: 00:00:00:00:00:00
      "00 00 00 00 00 00"
      // TPA: 10.0.0.2
      "0a 00 00 02");
  folly::io::Cursor cursor(&buf);
  EthFrame frame(cursor);

  auto buf2 = frame.toIOBuf();
  EXPECT_EQ(buf2->computeChainDataLength(), frame.length());
}
