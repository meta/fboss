// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/packet/TCPHeader.h"

#include <gtest/gtest.h>

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/packet/IPv4Hdr.h"
#include "fboss/agent/packet/IPv6Hdr.h"

using namespace facebook::fboss;
using folly::IOBuf;
using folly::io::Cursor;
using folly::io::RWPrivateCursor;

namespace {
TCPHeader makeFullHeader() {
  return TCPHeader(
      /*srcPort=*/8080,
      /*dstPort=*/443,
      /*seqNumber=*/1000,
      /*ackNumber=*/2000,
      /*flags=*/0x12, // SYN+ACK
      /*windowSize=*/65535,
      /*urgentPointer=*/0);
}
} // namespace

// --- Default constructor ---

TEST(TCPHeaderExtTest, defaultConstructor) {
  TCPHeader hdr;
  EXPECT_EQ(hdr.srcPort, 0);
  EXPECT_EQ(hdr.dstPort, 0);
  EXPECT_EQ(hdr.sequenceNumber, 0);
  EXPECT_EQ(hdr.ackNumber, 0);
  EXPECT_EQ(hdr.dataOffsetAndReserved, 5 << 4); // default data offset
  EXPECT_EQ(hdr.flags, 0);
  EXPECT_EQ(hdr.windowSize, 0);
  EXPECT_EQ(hdr.csum, 0);
  EXPECT_EQ(hdr.urgentPointer, 0);
}

// --- Two-arg constructor ---

TEST(TCPHeaderExtTest, twoArgConstructor) {
  TCPHeader hdr(1234, 5678);
  EXPECT_EQ(hdr.srcPort, 1234);
  EXPECT_EQ(hdr.dstPort, 5678);
  EXPECT_EQ(hdr.sequenceNumber, 0);
  EXPECT_EQ(hdr.ackNumber, 0);
}

// --- Full constructor ---

TEST(TCPHeaderExtTest, fullConstructor) {
  auto hdr = makeFullHeader();
  EXPECT_EQ(hdr.srcPort, 8080);
  EXPECT_EQ(hdr.dstPort, 443);
  EXPECT_EQ(hdr.sequenceNumber, 1000);
  EXPECT_EQ(hdr.ackNumber, 2000);
  EXPECT_EQ(hdr.flags, 0x12);
  EXPECT_EQ(hdr.windowSize, 65535);
  EXPECT_EQ(hdr.urgentPointer, 0);
}

// --- size() ---

TEST(TCPHeaderExtTest, sizeIs20) {
  EXPECT_EQ(TCPHeader::size(), 20);
}

// --- write and parse round-trip with full fields ---

TEST(TCPHeaderExtTest, writeAndParseRoundTrip) {
  auto hdr = makeFullHeader();
  hdr.csum = 0x1234;

  uint8_t arr[TCPHeader::size()];
  IOBuf buf(IOBuf::WRAP_BUFFER, arr, sizeof(arr));
  RWPrivateCursor cursor(&buf);
  hdr.write(&cursor);

  Cursor readCursor(&buf);
  TCPHeader hdr2;
  hdr2.parse(&readCursor);

  EXPECT_EQ(hdr, hdr2);
  EXPECT_EQ(hdr2.srcPort, 8080);
  EXPECT_EQ(hdr2.dstPort, 443);
  EXPECT_EQ(hdr2.sequenceNumber, 1000);
  EXPECT_EQ(hdr2.ackNumber, 2000);
  EXPECT_EQ(hdr2.flags, 0x12);
  EXPECT_EQ(hdr2.csum, 0x1234);
}

// --- parse error on too-small buffer ---

TEST(TCPHeaderExtTest, parseTooSmallThrows) {
  uint8_t arr[TCPHeader::size() - 1];
  IOBuf buf(IOBuf::WRAP_BUFFER, arr, sizeof(arr));
  Cursor readCursor(&buf);
  TCPHeader hdr;
  EXPECT_THROW(hdr.parse(&readCursor), FbossError);
}

TEST(TCPHeaderExtTest, parseExactSizeSucceeds) {
  TCPHeader hdr(1111, 2222);
  uint8_t arr[TCPHeader::size()];
  IOBuf buf(IOBuf::WRAP_BUFFER, arr, sizeof(arr));
  RWPrivateCursor cursor(&buf);
  hdr.write(&cursor);

  Cursor readCursor(&buf);
  TCPHeader hdr2;
  hdr2.parse(&readCursor);
  EXPECT_EQ(hdr2.srcPort, 1111);
  EXPECT_EQ(hdr2.dstPort, 2222);
}

// --- equality tests ---

TEST(TCPHeaderExtTest, equalHeaders) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  EXPECT_EQ(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalSrcPort) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.srcPort = 9999;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalDstPort) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.dstPort = 80;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalSeqNumber) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.sequenceNumber = 5000;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalAckNumber) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.ackNumber = 9999;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalChecksum) {
  // Note: TCPHeader::operator== INCLUDES csum in comparison
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr1.csum = 0x1111;
  hdr2.csum = 0x2222;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, unequalWindowSize) {
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.windowSize = 100;
  EXPECT_NE(hdr1, hdr2);
}

TEST(TCPHeaderExtTest, dataOffsetNotComparedInEquality) {
  // Note: TCPHeader::operator== has a known bug where it compares
  // this->dataOffsetAndReserved with itself instead of r.dataOffsetAndReserved.
  // So headers with different dataOffsetAndReserved are still considered equal.
  auto hdr1 = makeFullHeader();
  auto hdr2 = makeFullHeader();
  hdr2.dataOffsetAndReserved = 6 << 4;
  EXPECT_EQ(hdr1, hdr2);
}

// --- toString tests ---

TEST(TCPHeaderExtTest, toStringContainsPorts) {
  auto hdr = makeFullHeader();
  auto str = hdr.toString();
  EXPECT_NE(str.find("8080"), std::string::npos);
  EXPECT_NE(str.find("443"), std::string::npos);
}

TEST(TCPHeaderExtTest, toStringContainsSeqAndAck) {
  auto hdr = makeFullHeader();
  auto str = hdr.toString();
  EXPECT_NE(str.find("1000"), std::string::npos);
  EXPECT_NE(str.find("2000"), std::string::npos);
}

TEST(TCPHeaderExtTest, toStringContainsFlags) {
  auto hdr = makeFullHeader();
  auto str = hdr.toString();
  // flags field is printed as int(18) = "18"
  EXPECT_NE(str.find("18"), std::string::npos);
}

// --- folly::to<string> and ostream ---

TEST(TCPHeaderExtTest, toAppendWorks) {
  auto hdr = makeFullHeader();
  std::string result;
  toAppend(hdr, &result);
  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("8080"), std::string::npos);
}

TEST(TCPHeaderExtTest, ostreamOperator) {
  auto hdr = makeFullHeader();
  std::ostringstream oss;
  oss << hdr;
  EXPECT_FALSE(oss.str().empty());
  EXPECT_NE(oss.str().find("443"), std::string::npos);
}

// --- computeChecksum with IPv4 ---

TEST(TCPHeaderExtTest, computeChecksumIPv4) {
  auto hdr = makeFullHeader();
  // Create a minimal IPv4 header with TCP protocol
  IPv4Hdr ipHdr(
      folly::IPAddressV4("10.0.0.1"),
      folly::IPAddressV4("10.0.0.2"),
      static_cast<uint8_t>(IP_PROTO::IP_PROTO_TCP),
      TCPHeader::size());

  // Write TCP header into a buffer
  auto buf = IOBuf::create(TCPHeader::size());
  buf->append(TCPHeader::size());
  RWPrivateCursor cursor(buf.get());
  hdr.write(&cursor);

  // Compute checksum - position cursor after TCP header for payload
  Cursor payloadCursor(buf.get());
  payloadCursor.skip(TCPHeader::size());
  auto csum = hdr.computeChecksum(ipHdr, payloadCursor);
  EXPECT_NE(csum, 0);
}

// --- computeChecksum with IPv6 ---

TEST(TCPHeaderExtTest, computeChecksumIPv6) {
  auto hdr = makeFullHeader();
  IPv6Hdr ipHdr;
  ipHdr.srcAddr = folly::IPAddressV6("2001:db8::1");
  ipHdr.dstAddr = folly::IPAddressV6("2001:db8::2");
  ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_TCP));
  ipHdr.payloadLength = TCPHeader::size();

  auto buf = IOBuf::create(TCPHeader::size());
  buf->append(TCPHeader::size());
  RWPrivateCursor cursor(buf.get());
  hdr.write(&cursor);

  Cursor payloadCursor(buf.get());
  payloadCursor.skip(TCPHeader::size());
  auto csum = hdr.computeChecksum(ipHdr, payloadCursor);
  EXPECT_NE(csum, 0);
}

// --- updateChecksum modifies csum field ---

TEST(TCPHeaderExtTest, updateChecksumIPv4SetsCsum) {
  auto hdr = makeFullHeader();
  EXPECT_EQ(hdr.csum, 0);

  IPv4Hdr ipHdr(
      folly::IPAddressV4("10.0.0.1"),
      folly::IPAddressV4("10.0.0.2"),
      static_cast<uint8_t>(IP_PROTO::IP_PROTO_TCP),
      TCPHeader::size());

  auto buf = IOBuf::create(TCPHeader::size());
  buf->append(TCPHeader::size());
  RWPrivateCursor cursor(buf.get());
  hdr.write(&cursor);

  Cursor payloadCursor(buf.get());
  payloadCursor.skip(TCPHeader::size());
  hdr.updateChecksum(ipHdr, payloadCursor);
  EXPECT_NE(hdr.csum, 0);
}

// --- Large values round-trip ---

TEST(TCPHeaderExtTest, maxValuesRoundTrip) {
  TCPHeader hdr;
  hdr.srcPort = 65535;
  hdr.dstPort = 65535;
  hdr.sequenceNumber = 0xFFFFFFFF;
  hdr.ackNumber = 0xFFFFFFFF;
  hdr.dataOffsetAndReserved = 0xFF;
  hdr.flags = 0xFF;
  hdr.windowSize = 65535;
  hdr.csum = 0xFFFF;
  hdr.urgentPointer = 65535;

  uint8_t arr[TCPHeader::size()];
  IOBuf buf(IOBuf::WRAP_BUFFER, arr, sizeof(arr));
  RWPrivateCursor cursor(&buf);
  hdr.write(&cursor);

  Cursor readCursor(&buf);
  TCPHeader hdr2;
  hdr2.parse(&readCursor);
  EXPECT_EQ(hdr, hdr2);
}
