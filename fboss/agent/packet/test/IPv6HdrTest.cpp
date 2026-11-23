/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/packet/IPv6Hdr.h"

/*
 * References:
 *   https://code.google.com/p/googletest/wiki/Primer
 *   https://code.google.com/p/googletest/wiki/AdvancedGuide
 */
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cctype>
#include <optional>

#include <folly/IPAddress.h>
#include <folly/IPAddressV6.h>
#include <folly/io/Cursor.h>

#include "fboss/agent/hw/mock/MockRxPacket.h"

using namespace facebook::fboss;
using folly::IPAddress;
using folly::IPAddressV6;
using folly::io::Cursor;
using std::string;

TEST(IPv6HdrTest, default_constructor) {
  IPv6Hdr ipv6Hdr;
  EXPECT_EQ(6, ipv6Hdr.version);
  EXPECT_EQ(0, ipv6Hdr.trafficClass);
  EXPECT_EQ(0, ipv6Hdr.flowLabel);
  EXPECT_EQ(0, ipv6Hdr.payloadLength);
  EXPECT_EQ(0, ipv6Hdr.nextHeader);
  EXPECT_EQ(255, ipv6Hdr.hopLimit);
  EXPECT_EQ(IPAddressV6("::0"), ipv6Hdr.srcAddr);
  EXPECT_EQ(IPAddressV6("::0"), ipv6Hdr.dstAddr);
}

TEST(IPv6HdrTest, copy_constructor) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr lhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  IPv6Hdr rhs(lhs);
  EXPECT_EQ(lhs, rhs);
}

TEST(IPv6HdrTest, parameterized_data_constructor) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr ipv6Hdr(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  EXPECT_EQ(version, ipv6Hdr.version);
  EXPECT_EQ(trafficClass, ipv6Hdr.trafficClass);
  EXPECT_EQ(flowLabel, ipv6Hdr.flowLabel);
  EXPECT_EQ(payloadLength, ipv6Hdr.payloadLength);
  EXPECT_EQ(nextHeader, ipv6Hdr.nextHeader);
  EXPECT_EQ(hopLimit, ipv6Hdr.hopLimit);
  EXPECT_EQ(srcAddr, ipv6Hdr.srcAddr);
  EXPECT_EQ(dstAddr, ipv6Hdr.dstAddr);
}

TEST(IPv6HdrTest, cursor_data_constructor) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0xff;
  uint32_t flowLabel = 0xfffff;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  auto pkt = MockRxPacket::fromHex(
      // IPv6 Header
      "6" // VERSION: 6
      "ff" // Traffic Class
      "fffff" // Flow Label
      "00 00" // Payload Length
      "3B" // Next Header: No Next Header
      "01" // Hop Limit: 1
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 04");
  Cursor cursor(pkt->buf());
  IPv6Hdr ipv6Hdr(cursor);
  EXPECT_EQ(version, ipv6Hdr.version);
  EXPECT_EQ(trafficClass, ipv6Hdr.trafficClass);
  EXPECT_EQ(flowLabel, ipv6Hdr.flowLabel);
  EXPECT_EQ(payloadLength, ipv6Hdr.payloadLength);
  EXPECT_EQ(nextHeader, ipv6Hdr.nextHeader);
  EXPECT_EQ(hopLimit, ipv6Hdr.hopLimit);
  EXPECT_EQ(srcAddr, ipv6Hdr.srcAddr);
  EXPECT_EQ(dstAddr, ipv6Hdr.dstAddr);
}

TEST(IPv6HdrTest, parseBroadcast) {
  // Test a broadcast packet from a node with no IP to all nodes
  auto pkt = MockRxPacket::fromHex(
      // IPv6 Header
      "6" // VERSION: 6
      "00" // Traffic Class
      "00000" // Flow Label
      "00 00" // Payload Length
      "3B" // Next Header: No Next Header
      "ff" // Hop Limit
      // IPv6 Source Address
      "00 00 00 00 00 00 00 00"
      "00 00 00 00 00 00 00 00"
      // IPv6 Destination Address
      "ff 02 00 00 00 00 00 00"
      "00 00 00 00 00 00 00 01");
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  EXPECT_EQ(IPV6_VERSION, hdr.version);
  EXPECT_EQ(0, hdr.trafficClass);
  EXPECT_EQ(0, hdr.flowLabel);
  EXPECT_EQ(0, hdr.payloadLength);
  EXPECT_EQ(
      static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT), hdr.nextHeader);
  EXPECT_EQ(0xff, hdr.hopLimit);
  EXPECT_EQ(IPAddressV6("::0"), hdr.srcAddr);
  EXPECT_EQ(IPAddressV6("ff02::1"), hdr.dstAddr);
}

TEST(IPv6HdrTest, cursor_data_constructor_too_small) {
  auto pkt = MockRxPacket::fromHex(
      // IPv6 Header
      "6" // VERSION: 6
      "00" // Traffic Class
      "00000" // Flow Label
      "00 00" // Payload Length
      "3B" // Next Header: No Next Header
      "01" // Hop Limit: 1
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00   " // OOPS! One octet too small!
  );
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ IPv6Hdr ipv6Hdr(cursor); }, HdrParseError);
}

TEST(IPv6HdrTest, cursor_data_constructor_bad_version) {
  auto pkt = MockRxPacket::fromHex(
      // IPv6 Header
      "4" // VERSION: 4
      "00" // Traffic Class
      "00000" // Flow Label
      "00 00" // Payload Length
      "3B" // Next Header: No Next Header
      "01" // Hop Limit: 1
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 04");
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ IPv6Hdr ipv6Hdr(cursor); }, HdrParseError);
}

TEST(IPv6HdrTest, cursor_data_constructor_allow_zero_hop_limit) {
  /*
   * Packets with NH limit as 0 is valid for TAJO platform
   */
  auto pkt = MockRxPacket::fromHex(
      // IPv6 Header
      "6" // VERSION: 6
      "00" // Traffic Class
      "00000" // Flow Label
      "00 00" // Payload Length
      "3B" // Next Header: No Next Header
      "00" // Hop Limit: 0
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 04");
  Cursor cursor(pkt->buf());
  IPv6Hdr ipv6Hdr(cursor);
  EXPECT_EQ(ipv6Hdr.hopLimit, 0);
}

TEST(IPv6HdrTest, assignment_operator) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr lhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  IPv6Hdr rhs = lhs;
  EXPECT_EQ(lhs, rhs);
}

TEST(IPv6HdrTest, equality_operator) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr lhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  IPv6Hdr rhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  EXPECT_EQ(lhs, rhs);
}

TEST(IPv6HdrTest, inequality_operator) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 1;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr1("2620:0:1cfe:face:b00c::4");
  IPAddressV6 dstAddr2("2620:0:1cfe:face:b00c::5");
  IPv6Hdr lhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr1);
  IPv6Hdr rhs(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr2);
  EXPECT_NE(lhs, rhs);
}

TEST(IPv6HdrTest, decrementTTL) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 64;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr hdr(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  auto hdr2 = hdr;
  hdr2.decrementTTL();
  EXPECT_NE(hdr2, hdr);
  EXPECT_EQ(hdr2.version, hdr.version);
  EXPECT_EQ(hdr2.trafficClass, hdr.trafficClass);
  EXPECT_EQ(hdr2.flowLabel, hdr.flowLabel);
  EXPECT_EQ(hdr2.payloadLength, hdr.payloadLength);
  EXPECT_EQ(hdr2.nextHeader, hdr.nextHeader);
  EXPECT_EQ(hdr2.hopLimit, hdr.hopLimit - 1);
  EXPECT_EQ(hdr2.srcAddr, hdr.srcAddr);
  EXPECT_EQ(hdr2.dstAddr, hdr.dstAddr);
}

TEST(IPv6HdrTest, decrementTTL0) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0;
  uint32_t flowLabel = 0;
  uint16_t payloadLength = 0;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 0;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr hdr(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  auto hdr2 = hdr;
  hdr2.decrementTTL();
  EXPECT_EQ(hdr2, hdr);
}

TEST(IPv6HdrTest, toString) {
  uint8_t version = IPV6_VERSION;
  uint8_t trafficClass = 0xAB;
  uint32_t flowLabel = 12345;
  uint16_t payloadLength = 100;
  uint8_t nextHeader = static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_NONXT);
  uint8_t hopLimit = 64;
  IPAddressV6 srcAddr("2620:0:1cfe:face:b00c::3");
  IPAddressV6 dstAddr("2620:0:1cfe:face:b00c::4");
  IPv6Hdr hdr(
      version,
      trafficClass,
      flowLabel,
      payloadLength,
      nextHeader,
      hopLimit,
      srcAddr,
      dstAddr);
  auto str = hdr.toString();
  EXPECT_FALSE(str.empty());
  EXPECT_NE(str.find("version: 6"), std::string::npos);
  EXPECT_NE(str.find("trafficClass: 171"), std::string::npos);
  EXPECT_NE(str.find("flowLabel: 12345"), std::string::npos);
  EXPECT_NE(str.find("payloadLength: 100"), std::string::npos);
  EXPECT_NE(str.find("hopLimit: 64"), std::string::npos);
  EXPECT_NE(str.find("srcAddr: 2620:0:1cfe:face:b00c::3"), std::string::npos);
  EXPECT_NE(str.find("dstAddr: 2620:0:1cfe:face:b00c::4"), std::string::npos);
}

namespace {

size_t hexLength(const std::string& hex) {
  return std::count_if(hex.begin(), hex.end(), ::isxdigit) / 2;
}

// IPv6 packet from fe80::1 to ff02::1 with the given next header and payload.
// The Payload Length field is the length of payload unless payloadLength is
// given.
std::string ipv6Hex(
    const std::string& nextHeader,
    const std::string& payload,
    std::optional<size_t> payloadLength = std::nullopt) {
  return "60 00 00 00" // Version, Traffic Class, Flow Label
      + fmt::format("{:04x}", payloadLength.value_or(hexLength(payload))) +
      nextHeader + // Next Header
      "ff" // Hop Limit
      "fe 80 00 00 00 00 00 00 00 00 00 00 00 00 00 01"
      "ff 02 00 00 00 00 00 00 00 00 00 00 00 00 00 01" +
      payload;
}

// An options extension header (Hop-by-Hop or Destination Options) of
// totalLen octets, padded with PadN.
std::string optionsHdrHex(const std::string& nextHeader, size_t totalLen) {
  std::string hex = nextHeader + fmt::format("{:02x}", totalLen / 8 - 1);
  hex += fmt::format("01 {:02x}", totalLen - 4); // PadN
  for (size_t i = 4; i < totalLen; ++i) {
    hex += " 00";
  }
  return hex;
}

const std::string kRouterAdvertisementHex = "86 00 00 00 40 00 00 00";

uint8_t proto(IP_PROTO p) {
  return static_cast<uint8_t>(p);
}

} // namespace

TEST(IPv6HdrTest, noExtensionHeaders) {
  auto pkt = MockRxPacket::fromHex(ipv6Hex("3a", kRouterAdvertisementHex));
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  EXPECT_FALSE(hdr.hasExtensionHeaders());
  EXPECT_TRUE(hdr.nextHeaderChain.empty());
  EXPECT_EQ(proto(IP_PROTO::IP_PROTO_IPV6_ICMP), hdr.lastNextHeader());
  EXPECT_EQ(0, hdr.extensionHeadersLength);
}

TEST(IPv6HdrTest, extensionHeaderChain) {
  auto pkt = MockRxPacket::fromHex(ipv6Hex(
      "00",
      optionsHdrHex("3c", 8) + optionsHdrHex("3a", 16) +
          kRouterAdvertisementHex));
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  const std::vector<uint8_t> expected{
      proto(IP_PROTO::IP_PROTO_IPV6_HOPOPT),
      proto(IP_PROTO::IP_PROTO_IPV6_OPTS),
      proto(IP_PROTO::IP_PROTO_IPV6_ICMP)};
  EXPECT_TRUE(hdr.hasExtensionHeaders());
  EXPECT_EQ(expected, hdr.nextHeaderChain);
  EXPECT_EQ(proto(IP_PROTO::IP_PROTO_IPV6_ICMP), hdr.lastNextHeader());
  EXPECT_EQ(24, hdr.extensionHeadersLength);
  // The cursor stops after the fixed header, at the first extension header.
  EXPECT_EQ(IPv6Hdr::size(), cursor - Cursor(pkt->buf()));
}

// A Router Advertisement behind a 248 octet Hop-by-Hop header (T296568668).
TEST(IPv6HdrTest, longHopByHopHeader) {
  auto pkt = MockRxPacket::fromHex(
      ipv6Hex("00", optionsHdrHex("3a", 248) + kRouterAdvertisementHex));
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  EXPECT_TRUE(hdr.hasExtensionHeaders());
  EXPECT_EQ(proto(IP_PROTO::IP_PROTO_IPV6_ICMP), hdr.lastNextHeader());
  EXPECT_EQ(248, hdr.extensionHeadersLength);
}

TEST(IPv6HdrTest, fragmentAndAuthenticationHeaders) {
  auto pkt = MockRxPacket::fromHex(ipv6Hex(
      "2c",
      // Fragment: Next Header AH, reserved, offset 0, id
      "33 00 00 00 00 00 00 01"
      // AH: Next Header TCP, Payload Len 4 -> (4 + 2) * 4 = 24 octets
      "06 04 00 00 00 00 00 01 00 00 00 01"
      "00 00 00 00 00 00 00 00 00 00 00 00"));
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  const std::vector<uint8_t> expected{
      proto(IP_PROTO::IP_PROTO_IPV6_FRAG),
      proto(IP_PROTO::IP_PROTO_AH),
      proto(IP_PROTO::IP_PROTO_TCP)};
  EXPECT_EQ(expected, hdr.nextHeaderChain);
  EXPECT_EQ(proto(IP_PROTO::IP_PROTO_TCP), hdr.lastNextHeader());
  EXPECT_EQ(32, hdr.extensionHeadersLength);
}

// A non-first fragment carries no upper-layer header, so its chain ends at
// the Fragment header.
TEST(IPv6HdrTest, nonFirstFragment) {
  auto pkt = MockRxPacket::fromHex(ipv6Hex(
      "2c",
      // Fragment: Next Header ICMPv6, reserved, offset 8 octets, id
      "3a 00 00 08 00 00 00 01"
      // Fragment data
      "00 00 00 00 00 00 00 00"));
  Cursor cursor(pkt->buf());
  IPv6Hdr hdr(cursor);
  const std::vector<uint8_t> expected{proto(IP_PROTO::IP_PROTO_IPV6_FRAG)};
  EXPECT_TRUE(hdr.hasExtensionHeaders());
  EXPECT_EQ(expected, hdr.nextHeaderChain);
  EXPECT_EQ(proto(IP_PROTO::IP_PROTO_IPV6_FRAG), hdr.lastNextHeader());
  EXPECT_EQ(8, hdr.extensionHeadersLength);
}

TEST(IPv6HdrTest, truncatedExtensionHeader) {
  // Hop-by-Hop header claims 16 octets, as does the Payload Length, but only
  // 8 are present.
  auto pkt =
      MockRxPacket::fromHex(ipv6Hex("00", "3a 01 01 04 00 00 00 00", 16));
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ IPv6Hdr hdr(cursor); }, HdrParseError);
}

// The chain must fit in the declared payload even when the buffer holds more,
// e.g. Ethernet padding.
TEST(IPv6HdrTest, extensionHeaderPastPayloadLength) {
  auto pkt = MockRxPacket::fromHex(
      ipv6Hex("00", optionsHdrHex("3a", 16) + kRouterAdvertisementHex, 8));
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ IPv6Hdr hdr(cursor); }, HdrParseError);
}

// Without extension headers Payload Length isn't checked against the cursor,
// even when the cursor is bounded short of it.
TEST(IPv6HdrTest, noExtensionHeadersPayloadLengthNotChecked) {
  auto pkt = MockRxPacket::fromHex(ipv6Hex("3a", kRouterAdvertisementHex, 64));
  Cursor cursor(pkt->buf(), pkt->buf()->computeChainDataLength());
  IPv6Hdr hdr(cursor);
  EXPECT_FALSE(hdr.hasExtensionHeaders());
  EXPECT_EQ(64, hdr.payloadLength);
}
