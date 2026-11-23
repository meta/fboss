// Copyright 2004-present Facebook. All Rights Reserved.

#include <gtest/gtest.h>

#include "fboss/agent/hw/mock/MockRxPacket.h"
#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/packet/MPLSHdr.h"
#include "fboss/agent/packet/PktUtil.h"

namespace facebook::fboss {

TEST(MPLSHdrTest, parameterized_data_constructor_label) {
  MPLSHdr hdr{MPLSHdr::Label{103, 4, true, 79}};
  EXPECT_EQ(MPLSHdr::Label::kSizeBytes, hdr.size());
  const auto& label = hdr.stack()[0];
  EXPECT_EQ(103, label.label);
  EXPECT_EQ(4, label.trafficClass);
  EXPECT_EQ(1, label.bottomOfStack);
  EXPECT_EQ(79, label.timeToLive);
}

TEST(MPLSHdrTest, parameterized_data_constructor_stack) {
  std::vector<MPLSHdr::Label> stack{
      MPLSHdr::Label{101, 4, false, 79},
      MPLSHdr::Label{102, 4, false, 79},
      MPLSHdr::Label{103, 4, true, 79}};
  MPLSHdr hdr{stack};
  const auto& hdrStack = hdr.stack();
  EXPECT_EQ(hdr.size(), 3 * MPLSHdr::Label::kSizeBytes);
  for (auto i = 0; i < hdrStack.size(); i++) {
    EXPECT_EQ(stack[i], hdrStack[i]);
  }
}

TEST(MPLSHdrTest, copy_constructor) {
  std::vector<MPLSHdr::Label> stack{
      MPLSHdr::Label{101, 4, false, 79},
      MPLSHdr::Label{102, 4, false, 79},
      MPLSHdr::Label{103, 4, true, 79}};
  MPLSHdr hdr0{stack};
  MPLSHdr hdr1{hdr0};
  const auto& hdr0Stack = hdr0.stack();
  const auto& hdr1Stack = hdr1.stack();

  EXPECT_EQ(hdr1.size(), hdr0.size());
  EXPECT_EQ(hdr1.size(), 3 * MPLSHdr::Label::kSizeBytes);
  EXPECT_EQ(hdr0Stack, hdr1Stack);
}

TEST(MPLSHdrTest, cursor_data_constructor_1_label) {
  auto rxPkt = MockRxPacket::fromHex(
      "00067" // 103 label
      "d" // 110,1 -> exp 6 bottomOfStack 1
      "3f" // timeToLive 63
  );
  folly::io::Cursor cursor(rxPkt->buf());
  MPLSHdr hdr(&cursor);

  const auto& stack = hdr.stack();

  EXPECT_EQ(MPLSHdr::Label::kSizeBytes, hdr.size());
  EXPECT_EQ(stack[0].label, 103);
  EXPECT_EQ(stack[0].trafficClass, 6);
  EXPECT_EQ(stack[0].bottomOfStack, 1);
  EXPECT_EQ(stack[0].timeToLive, 63);
}

TEST(MPLSHdrTest, cursor_data_constructor_3_labels) {
  auto rxPkt = MockRxPacket::fromHex(
      "054c4" // 21700 label
      "8" // 010,0 -> exp 4 bottomOfStack 0
      "7d" // timeToLive 125

      "00068" // 104 label
      "c" // 110,0 -> exp 6 bottomOfStack 0
      "7e" // timeToLive 126

      "00067" // 103 label
      "d" // 110,1 -> exp 6 bottomOfStack 1
      "3f" // timeToLive 63

      // ip header
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
      "b0 0c 00 00 00 00 00 04"

      // junk
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef"
      "deadbeef");

  std::vector<MPLSHdr::Label> stack{
      MPLSHdr::Label{21700, 4, false, 125},
      MPLSHdr::Label{104, 6, false, 126},
      MPLSHdr::Label{103, 6, true, 63},
  };
  folly::io::Cursor cursor(rxPkt->buf());
  MPLSHdr hdr(&cursor);
  EXPECT_EQ(hdr.size(), 3 * MPLSHdr::Label::kSizeBytes);
  const auto& hdrStack = hdr.stack();
  EXPECT_EQ(hdrStack.size(), stack.size());
  for (auto i = 0; i < hdrStack.size(); i++) {
    EXPECT_EQ(stack[i], hdrStack[i]);
  }
}

TEST(MPLSHdrTest, decapsulateV6MplsPacket) {
  auto ioBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "44 4c a8 e4 19 e3 02 90 fb 43 a2 ec 81 00"
      // vlan tag and mpls
      "0f a1 88 47"
      // label on top
      "00 12 c2 7e"
      // label in middle
      "00 0c 82 7e"
      // label at the bottom
      "00 06 43 7e"
      // ip header
      "60 00 00 00 00 40 3a 7e 24 01 db 00 00 30 c0 0d"
      "fa ce 00 00 00 95 00 00 25 01 00 01 00 00 00 00"
      "00 00 00 00 00 00 01 01"
      // icmp6
      "80 00 e0 54 14 91 00 01 66 27 85 5d 00 00 00 00"
      "fd 9f 02 00 00 00 00 00 10 11 12 13 14 15 16 17"
      "18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27"
      "28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37");

  auto ethType = utility::decapsulateMplsPacket(&ioBuf);
  EXPECT_EQ(ethType.value(), ETHERTYPE::ETHERTYPE_IPV6);

  // labels popped, ether type fixed to v6
  auto expectedIOBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "44 4c a8 e4 19 e3 02 90 fb 43 a2 ec 81 00"
      // vlan tag and v6
      "0f a1 86 dd"
      // ip header
      "60 00 00 00 00 40 3a 7e 24 01 db 00 00 30 c0 0d"
      "fa ce 00 00 00 95 00 00 25 01 00 01 00 00 00 00"
      "00 00 00 00 00 00 01 01"
      // icmp6
      "80 00 e0 54 14 91 00 01 66 27 85 5d 00 00 00 00"
      "fd 9f 02 00 00 00 00 00 10 11 12 13 14 15 16 17"
      "18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27"
      "28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37");

  EXPECT_TRUE(folly::IOBufEqualTo()(ioBuf, expectedIOBuf));
}

TEST(MPLSHdrTest, decapsulateV4MplsPacket) {
  auto ioBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "a4 4c 11 b7 c4 01 02 90 fb 55 a8 71 81 00"
      // vlan tag and mpls
      "0f a7 88 47"
      // label on top
      "00 06 90 5f"
      // label at the bottom
      "00 06 71 5f"
      // ip header
      "45 00 00 54 20 a2 40 00 5f 01 b6 a2 0a 2e 18 05"
      "0a 2e 18 04"
      // icmp4
      "08 00 0f 64 9f 0f 00 01 d9 92 43 5c 00 00 00 00"
      "68 c9 05 00 00 00 00 00 10 11 12 13 14 15 16 17"
      "18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27"
      "28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37");

  auto ethType = utility::decapsulateMplsPacket(&ioBuf);
  EXPECT_EQ(ethType.value(), ETHERTYPE::ETHERTYPE_IPV4);

  // labels popped, ether type fixed to v4
  auto expectedIOBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "a4 4c 11 b7 c4 01 02 90 fb 55 a8 71 81 00"
      // vlan tag and mpls
      "0f a7 08 00"
      // ip header
      "45 00 00 54 20 a2 40 00 5f 01 b6 a2 0a 2e 18 05"
      "0a 2e 18 04"
      // icmpv4
      "08 00 0f 64 9f 0f 00 01 d9 92 43 5c 00 00 00 00"
      "68 c9 05 00 00 00 00 00 10 11 12 13 14 15 16 17"
      "18 19 1a 1b 1c 1d 1e 1f 20 21 22 23 24 25 26 27"
      "28 29 2a 2b 2c 2d 2e 2f 30 31 32 33 34 35 36 37");

  EXPECT_TRUE(folly::IOBufEqualTo()(ioBuf, expectedIOBuf));
}

TEST(MPLSHdrTest, decapsulateCsigMplsPacket) {
  auto ioBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "a4 4c 11 b7 c4 01 02 90 fb 55 a8 71 81 00"
      // vlan tag, csig tag and mpls
      "0f a7 99 00 ab c2 88 47"
      // label
      "00 06 71 5f"
      // ip header
      "45 00 00 54 20 a2 40 00 5f 01 b6 a2 0a 2e 18 05"
      "0a 2e 18 04");

  auto ethType = utility::decapsulateMplsPacket(&ioBuf);
  EXPECT_EQ(ethType.value(), ETHERTYPE::ETHERTYPE_IPV4);

  // label popped, csig tag kept, ether type fixed to v4
  auto expectedIOBuf = PktUtil::parseHexData(
      // dst mac, src mac vlan-type
      "a4 4c 11 b7 c4 01 02 90 fb 55 a8 71 81 00"
      // vlan tag, csig tag and v4
      "0f a7 99 00 ab c2 08 00"
      // ip header
      "45 00 00 54 20 a2 40 00 5f 01 b6 a2 0a 2e 18 05"
      "0a 2e 18 04");

  EXPECT_TRUE(folly::IOBufEqualTo()(ioBuf, expectedIOBuf));
}

TEST(MPLSHdrTest, decrementTTL) {
  MPLSHdr hdr{MPLSHdr::Label{103, 4, true, 79}};
  EXPECT_EQ(MPLSHdr::Label::kSizeBytes, hdr.size());
  auto hdr2 = hdr;
  hdr2.decrementTTL();
  EXPECT_NE(hdr2, hdr);
  const auto& label = hdr2.stack()[0];
  EXPECT_EQ(103, label.label);
  EXPECT_EQ(4, label.trafficClass);
  EXPECT_EQ(1, label.bottomOfStack);
  EXPECT_EQ(78, label.timeToLive);
}

TEST(MPLSHdrTest, decrementTTL0) {
  MPLSHdr hdr{MPLSHdr::Label{103, 4, true, 0}};
  EXPECT_EQ(MPLSHdr::Label::kSizeBytes, hdr.size());
  auto hdr2 = hdr;
  hdr2.decrementTTL();
  EXPECT_EQ(hdr2, hdr);
  const auto& label = hdr2.stack()[0];
  EXPECT_EQ(103, label.label);
  EXPECT_EQ(4, label.trafficClass);
  EXPECT_EQ(1, label.bottomOfStack);
  EXPECT_EQ(0, label.timeToLive);
}

TEST(MPLSHdrTest, toString) {
  MPLSHdr hdr{MPLSHdr::Label{103, 4, true, 79}};
  std::cout << hdr;
}
} // namespace facebook::fboss
