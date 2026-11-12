/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/packet/EthHdr.h"

/*
 * References:
 *   https://code.google.com/p/googletest/wiki/Primer
 *   https://code.google.com/p/googletest/wiki/AdvancedGuide
 */
#include <gtest/gtest.h>

#include <folly/MacAddress.h>
#include <folly/io/Cursor.h>

#include "fboss/agent/hw/mock/MockRxPacket.h"

using namespace facebook::fboss;
using folly::MacAddress;
using folly::io::Cursor;
using std::vector;

TEST(EthHdrTest, default_constructor) {
  EthHdr ethHdr;
  EXPECT_EQ(MacAddress(), ethHdr.dstAddr);
  EXPECT_EQ(MacAddress(), ethHdr.srcAddr);
  EXPECT_EQ(vector<VlanTag>(), ethHdr.vlanTags);
  EXPECT_EQ(0, ethHdr.etherType);
}

TEST(EthHdrTest, copy_constructor) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4);
  EthHdr lhs(dstAddr, srcAddr, vlanTags, etherType);
  EthHdr rhs(lhs);
  EXPECT_EQ(lhs, rhs);
}

TEST(EthHdrTest, parameterized_data_constructor) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr ethHdr(dstAddr, srcAddr, vlanTags, etherType);
  EXPECT_EQ(dstAddr, ethHdr.dstAddr);
  EXPECT_EQ(srcAddr, ethHdr.srcAddr);
  EXPECT_EQ(vlanTags, ethHdr.vlanTags);
  EXPECT_EQ(etherType, ethHdr.etherType);
}

TEST(EthHdrTest, cursor_data_constructor) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "81 00 00 01" // VLAN: 1
      "88 A8 00 02" // VLAN: 2
      "88 A8 00 03" // VLAN: 3
      "86 DD" // EtherType: IPv6
  );
  Cursor cursor(pkt->buf());
  EthHdr ethHdr(cursor);
  EXPECT_EQ(dstAddr, ethHdr.dstAddr);
  EXPECT_EQ(srcAddr, ethHdr.srcAddr);
  EXPECT_EQ(vlanTags, ethHdr.vlanTags);
  EXPECT_EQ(etherType, ethHdr.etherType);
}

TEST(EthHdrTest, cursor_data_constructor_too_small_0) {
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "86   " // OOPS! One octet too small!
  );
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ EthHdr ethHdr(cursor); }, HdrParseError);
}

TEST(EthHdrTest, cursor_data_constructor_too_small_1) {
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "81 00 00 01" // VLAN: 1
      "88 A8 00 02" // VLAN: 2
      "88 A8 00 03" // VLAN: 3
      "86   " // OOPS! One octet too small!
  );
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ EthHdr ethHdr(cursor); }, HdrParseError);
}

TEST(EthHdrTest, assignment_operator) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4);
  EthHdr lhs(dstAddr, srcAddr, vlanTags, etherType);
  EthHdr rhs = lhs;
  EXPECT_EQ(lhs, rhs);
}

TEST(EthHdrTest, equality_operator) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4);
  EthHdr lhs(dstAddr, srcAddr, vlanTags, etherType);
  EthHdr rhs(dstAddr, srcAddr, vlanTags, etherType);
  EXPECT_EQ(lhs, rhs);
}

TEST(EthHdrTest, inequality_operator) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType1 = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4);
  uint16_t etherType2 = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr lhs(dstAddr, srcAddr, vlanTags, etherType1);
  EthHdr rhs(dstAddr, srcAddr, vlanTags, etherType2);
  EXPECT_NE(lhs, rhs);
}

TEST(EthHdrTest, vlan_tag) {
  // Vlan id has bits in both octets set.
  // Drop eligibility is 0
  VlanTag vlan1(2050, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN), 0, 4);
  EXPECT_EQ(vlan1.vid(), 2050);
  EXPECT_EQ(vlan1.tpid(), static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN));
  EXPECT_EQ(vlan1.dei(), 0);
  EXPECT_EQ(vlan1.pcp(), 4);

  // Vlan id has set bits in only one octets
  // Drop eligibility is 1
  VlanTag vlan2(50, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_QINQ), 1, 0);
  EXPECT_EQ(vlan2.vid(), 50);
  EXPECT_EQ(vlan2.tpid(), static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_QINQ));
  EXPECT_EQ(vlan2.dei(), 1);
  EXPECT_EQ(vlan2.pcp(), 0);
}

TEST(EthHdrTest, setDstMac) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr hdr(dstAddr, srcAddr, vlanTags, etherType);
  EXPECT_EQ(hdr.getDstMac(), dstAddr);
  hdr.setDstMac(srcAddr);
  EXPECT_EQ(hdr.getDstMac(), srcAddr);
}

TEST(EthHdrTest, toString) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr hdr(dstAddr, srcAddr, vlanTags, etherType);
  std::cout << hdr;
}

TEST(EthHdrTest, addVlan) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr hdr(dstAddr, srcAddr, {}, etherType);
  hdr.addVlan(VlanID(1), ETHERTYPE::ETHERTYPE_VLAN);
  EXPECT_EQ(hdr.getVlanTags(), std::vector<VlanTag>({VlanTag(0x81000001)}));
}

TEST(EthHdrTest, addVlans) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  EthHdr hdr(dstAddr, srcAddr, {}, etherType);
  hdr.addVlans(
      std::vector<VlanID>{VlanID(1), VlanID(2)}, ETHERTYPE::ETHERTYPE_VLAN);
  EXPECT_EQ(
      hdr.getVlanTags(),
      std::vector<VlanTag>({VlanTag(0x81000001), VlanTag(0x81000002)}));
}

TEST(EthHdrTest, setVlan) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  EthHdr hdr(dstAddr, srcAddr, vlanTags, etherType);
  hdr.setVlan(VlanID(1), ETHERTYPE::ETHERTYPE_VLAN);
  EXPECT_EQ(hdr.getVlanTags(), std::vector<VlanTag>({VlanTag(0x81000001)}));
}

TEST(EthHdrTest, setVlans) {
  MacAddress dstAddr("ff:ff:ff:ff:ff:ff");
  MacAddress srcAddr("10:dd:b1:bb:5a:ef");
  uint16_t etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  vector<VlanTag> vlanTags;
  vlanTags.emplace_back(0x81000001);
  vlanTags.emplace_back(0x88A80002);
  vlanTags.emplace_back(0x88A80003);
  EthHdr hdr(dstAddr, srcAddr, vlanTags, etherType);
  hdr.setVlans(
      std::vector<VlanID>{VlanID(1), VlanID(2)}, ETHERTYPE::ETHERTYPE_VLAN);
  EXPECT_EQ(
      hdr.getVlanTags(),
      std::vector<VlanTag>({VlanTag(0x81000001), VlanTag(0x81000002)}));
}

TEST(EthHdrTest, csig_tag_fields) {
  CsigTag tag(/*signalType*/ 0x5, /*signal*/ 0x17, /*linkLocator*/ 0x42);
  EXPECT_EQ(0x5, tag.signalType());
  EXPECT_EQ(0x17, tag.signal());
  EXPECT_EQ(0x42, tag.linkLocator());
  EXPECT_EQ(CsigTag(0xABC2), tag);
}

TEST(EthHdrTest, cursor_data_constructor_csig) {
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "99 00 AB C2" // CSIG: signalType 5, signal 0x17, link locator 0x42
      "86 DD" // EtherType: IPv6
  );
  Cursor cursor(pkt->buf());
  EthHdr ethHdr(cursor);
  EXPECT_EQ(vector<VlanTag>(), ethHdr.vlanTags);
  ASSERT_TRUE(ethHdr.getCsigTag().has_value());
  EXPECT_EQ(CsigTag(0x5, 0x17, 0x42), *ethHdr.getCsigTag());
  EXPECT_EQ(static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6), ethHdr.etherType);
  EXPECT_EQ(EthHdr::UNTAGGED_PKT_SIZE + CsigTag::SIZE, ethHdr.size());
  EXPECT_EQ(ethHdr.size(), cursor - Cursor(pkt->buf()));
}

TEST(EthHdrTest, cursor_data_constructor_vlan_csig) {
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "81 00 00 01" // VLAN: 1
      "99 00 AB C2" // CSIG: signalType 5, signal 0x17, link locator 0x42
      "86 DD" // EtherType: IPv6
  );
  Cursor cursor(pkt->buf());
  EthHdr ethHdr(cursor);
  EXPECT_EQ(vector<VlanTag>{VlanTag(0x81000001)}, ethHdr.vlanTags);
  ASSERT_TRUE(ethHdr.getCsigTag().has_value());
  EXPECT_EQ(CsigTag(0x5, 0x17, 0x42), *ethHdr.getCsigTag());
  EXPECT_EQ(static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6), ethHdr.etherType);
  EXPECT_EQ(EthHdr::SIZE + CsigTag::SIZE, ethHdr.size());
  EXPECT_EQ(ethHdr.size(), cursor - Cursor(pkt->buf()));
}

TEST(EthHdrTest, cursor_data_constructor_csig_too_small) {
  auto pkt = MockRxPacket::fromHex(
      // Ethernet Header
      "ff ff ff ff ff ff" // Destination MAC Address
      "10 dd b1 bb 5a ef" // Source MAC Address
      "99 00 AB C2" // CSIG
      "86   " // OOPS! One octet too small!
  );
  Cursor cursor(pkt->buf());
  EXPECT_THROW({ EthHdr ethHdr(cursor); }, HdrParseError);
}

TEST(EthHdrTest, csig_tag_equality) {
  EthHdr lhs(
      MacAddress("ff:ff:ff:ff:ff:ff"),
      MacAddress("10:dd:b1:bb:5a:ef"),
      {},
      static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
  EthHdr rhs(lhs);
  rhs.setCsigTag(CsigTag(0x5, 0x17, 0x42));
  EXPECT_NE(lhs, rhs);
  lhs.setCsigTag(CsigTag(0x5, 0x17, 0x42));
  EXPECT_EQ(lhs, rhs);
}
