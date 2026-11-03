// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/packet/EthFrame.h"

#include <gtest/gtest.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include "fboss/agent/packet/PktUtil.h"

using namespace ::testing;
using namespace facebook::fboss;
using namespace facebook::fboss::utility;

TEST(EthFrameTest, ParseIpv6Frame) {
  // Deserialize
  auto buf = PktUtil::parseHexData(
      // Dest mac, source mac
      "00 02 00 00 00 01  02 00 02 01 02 03"
      // 802.1q, VLAN 1
      "81 00 00 01"
      // IPv6
      "86 dd"
      // IPv6 version, traffic class, flow label
      "6f ff ff ff"
      // Payload Length
      "00 00"
      // Next Header: No Next Header
      "3B"
      // Hop Limit: 1
      "01"
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 04");
  folly::io::Cursor cursor(&buf);
  EthFrame ethFrame(cursor);
  EXPECT_EQ(ethFrame.length(), buf.length());
  EXPECT_TRUE(ethFrame.v6PayLoad().has_value());
  EXPECT_EQ(ethFrame.v6PayLoad()->length(), 40);

  // Serialize
  auto buf2 = ethFrame.toIOBuf();
  EXPECT_TRUE(folly::IOBufEqualTo()(*buf2, buf));

  // Parse again and check equality via == operator
  folly::io::Cursor cursor2(buf2.get());
  EthFrame ethFrame2(cursor2);
  EXPECT_EQ(ethFrame2, ethFrame);
}

TEST(EthFrameTest, ParseMacControlFrame) {
  // Deserialize
  auto buf = PktUtil::parseHexData(
      // Dest mac, source mac
      "00 02 00 00 00 01  02 00 02 01 02 03"
      // EPON (Mac Control)
      "88 08"
      // PFC opcode
      "01 01"
      // Always 00
      "00"
      // Class Vector
      "ff"
      // Quanta for each class
      "00 f0 00 f0 00 f0 00 f0 00 f0 00 f0 00 f0 00 f0"
      // 26-byte padding
      "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
      "00 00 00 00 00 00 00 00 00 00");
  folly::io::Cursor cursor(&buf);
  EthFrame ethFrame(cursor);
  EXPECT_EQ(ethFrame.length(), buf.length());
  EXPECT_TRUE(ethFrame.macControlPayload().has_value());
  EXPECT_EQ(ethFrame.macControlPayload()->size(), 46);

  // Serialize
  auto buf2 = ethFrame.toIOBuf();
  EXPECT_TRUE(folly::IOBufEqualTo()(*buf2, buf));

  // Parse again and check equality via == operator
  folly::io::Cursor cursor2(buf2.get());
  EthFrame ethFrame2(cursor2);
  EXPECT_EQ(ethFrame2, ethFrame);
}

TEST(EthFrameTest, ParseCsigIpv6Frame) {
  // Deserialize
  auto buf = PktUtil::parseHexData(
      // Dest mac, source mac
      "00 02 00 00 00 01  02 00 02 01 02 03"
      // 802.1q, VLAN 1
      "81 00 00 01"
      // CSIG: signalType 5, signal 0x17, link locator 0x42
      "99 00 ab c2"
      // IPv6
      "86 dd"
      // IPv6 version, traffic class, flow label
      "6f ff ff ff"
      // Payload Length
      "00 00"
      // Next Header: No Next Header
      "3B"
      // Hop Limit: 1
      "01"
      // IPv6 Source Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 03"
      // IPv6 Destination Address
      "26 20 00 00 1c fe fa ce"
      "b0 0c 00 00 00 00 00 04");
  folly::io::Cursor cursor(&buf);
  EthFrame ethFrame(cursor);
  EXPECT_EQ(ethFrame.length(), buf.length());
  ASSERT_TRUE(ethFrame.header().getCsigTag().has_value());
  EXPECT_EQ(*ethFrame.header().getCsigTag(), CsigTag(0x5, 0x17, 0x42));
  EXPECT_TRUE(ethFrame.v6PayLoad().has_value());
  EXPECT_EQ(ethFrame.v6PayLoad()->length(), 40);

  // Serialize
  auto buf2 = ethFrame.toIOBuf();
  EXPECT_TRUE(folly::IOBufEqualTo()(*buf2, buf));

  // Parse again and check equality via == operator
  folly::io::Cursor cursor2(buf2.get());
  EthFrame ethFrame2(cursor2);
  EXPECT_EQ(ethFrame2, ethFrame);

  // Stripping VLANs keeps the CSIG tag, and the untagged frame round-trips
  ethFrame.stripVlans();
  EXPECT_TRUE(ethFrame.header().getVlanTags().empty());
  EXPECT_EQ(*ethFrame.header().getCsigTag(), CsigTag(0x5, 0x17, 0x42));
  EXPECT_EQ(ethFrame.length(), buf.length() - 4);
  auto untaggedBuf = ethFrame.toIOBuf();
  folly::io::Cursor untaggedCursor(untaggedBuf.get());
  EXPECT_EQ(EthFrame(untaggedCursor), ethFrame);
}
