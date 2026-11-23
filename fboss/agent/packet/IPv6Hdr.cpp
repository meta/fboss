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

#include <folly/IPAddress.h>
#include <folly/IPAddressV6.h>
#include <folly/lang/Bits.h>
#include <folly/logging/xlog.h>
#include <sstream>
#include <stdexcept>
#include "fboss/agent/packet/PktUtil.h"

namespace facebook::fboss {

using folly::Endian;
using folly::IOBuf;
using folly::IPAddress;
using folly::IPAddressV6;
using folly::io::Cursor;
using std::string;
using std::stringstream;

namespace {

bool isExtensionHeader(uint8_t nextHeader) {
  switch (static_cast<IP_PROTO>(nextHeader)) {
    case IP_PROTO::IP_PROTO_IPV6_HOPOPT:
    case IP_PROTO::IP_PROTO_IPV6_ROUTE:
    case IP_PROTO::IP_PROTO_IPV6_FRAG:
    case IP_PROTO::IP_PROTO_AH:
    case IP_PROTO::IP_PROTO_IPV6_OPTS:
    case IP_PROTO::IP_PROTO_MOBILITY_HEADER:
    case IP_PROTO::IP_PROTO_HIP:
    case IP_PROTO::IP_PROTO_SHIM6:
    case IP_PROTO::IP_PROTO_253:
    case IP_PROTO::IP_PROTO_254:
      return true;
    default:
      return false;
  }
}

// Total length in octets of an extension header, from its Hdr Ext Len field.
size_t extensionHeaderLength(uint8_t nextHeader, uint8_t hdrExtLen) {
  switch (static_cast<IP_PROTO>(nextHeader)) {
    case IP_PROTO::IP_PROTO_IPV6_FRAG:
      return 8;
    case IP_PROTO::IP_PROTO_AH:
      // RFC 4302: in 4-octet units, minus 2
      return (static_cast<size_t>(hdrExtLen) + 2) * 4;
    default:
      // RFC 8200: in 8-octet units, not including the first 8 octets
      return (static_cast<size_t>(hdrExtLen) + 1) * 8;
  }
}

} // namespace

IPv6Hdr::IPv6Hdr(Cursor& cursor) {
  try {
    uint8_t buf[16];
    cursor.pull(buf, 4);
    version = buf[0] >> 4;
    if (version != IPV6_VERSION) {
      throw HdrParseError("IPv6: version != 6");
    }
    trafficClass = ((buf[0] & 0x0F) << 4) | ((buf[1] & 0xF0) >> 4);
    flowLabel = (static_cast<uint32_t>(buf[1] & 0x0F) << 16) |
        (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
    payloadLength = cursor.readBE<uint16_t>();
    nextHeader = cursor.read<uint8_t>();
    hopLimit = cursor.read<uint8_t>();
    srcAddr = PktUtil::readIPv6(&cursor);
    dstAddr = PktUtil::readIPv6(&cursor);
  } catch (const std::out_of_range&) {
    throw HdrParseError("IPv6 header too small");
  }
  parseNextHeaderChain(cursor);
}

void IPv6Hdr::serialize(folly::io::RWPrivateCursor* cursor) const {
  cursor->write<uint8_t>(
      static_cast<uint8_t>((version << 4) | (trafficClass >> 4)));
  cursor->write<uint8_t>(static_cast<uint8_t>(
      ((trafficClass & 0xf) << 4) | ((flowLabel >> 16) & 0xf)));
  cursor->writeBE<uint16_t>(static_cast<uint16_t>(flowLabel & 0xffff));
  cursor->writeBE<uint16_t>(payloadLength);
  cursor->write<uint8_t>(nextHeader);
  cursor->write<uint8_t>(hopLimit);
  cursor->push(srcAddr.bytes(), 16);
  cursor->push(dstAddr.bytes(), 16);
}

uint32_t IPv6Hdr::pseudoHdrPartialCsum() const {
  return pseudoHdrPartialCsum(payloadLength);
}

uint32_t IPv6Hdr::pseudoHdrPartialCsum(uint32_t length) const {
  uint32_t sum = 0;
  sum += addrPartialCsum(srcAddr);
  sum += addrPartialCsum(dstAddr);
  sum += (length >> 16);
  sum += (length & 0xffff);
  sum += nextHeader;
  return sum;
}

uint32_t IPv6Hdr::addrPartialCsum(const IPAddressV6& addr) {
  IOBuf buf(IOBuf::WRAP_BUFFER, addr.bytes(), 16);
  Cursor c(&buf);
  uint32_t sum = 0;
  for (uint8_t n = 0; n < 8; ++n) {
    sum += c.readBE<uint16_t>();
  }
  return sum;
}

string IPv6Hdr::toString() const {
  stringstream ss;
  ss << "IPv6Hdr {"
     << " version: " << static_cast<int>(version)
     << " trafficClass: " << static_cast<int>(trafficClass)
     << " flowLabel: " << flowLabel << " payloadLength: " << payloadLength
     << " nextHeader: " << static_cast<int>(nextHeader)
     << " hopLimit: " << static_cast<int>(hopLimit)
     << " srcAddr: " << srcAddr.str() << " dstAddr: " << dstAddr.str();
  if (hasExtensionHeaders()) {
    ss << " nextHeaderChain:";
    for (auto header : nextHeaderChain) {
      ss << " " << static_cast<int>(header);
    }
  }
  ss << " }";
  return ss.str();
}

void IPv6Hdr::parseNextHeaderChain(Cursor cursor) {
  auto header = nextHeader;
  if (!isExtensionHeader(header)) {
    return;
  }
  nextHeaderChain.push_back(header);
  try {
    // Bytes past the declared payload, e.g. Ethernet padding, are not part of
    // the chain.
    //
    // Jumbograms (RFC 2675) declare a payload length of 0 and so fail to
    // parse. That is intended: a jumbogram's payload exceeds 65,535 octets and
    // it may not be fragmented, so it needs a link MTU above 65,575 octets
    // (65,535 + the 40 octet IPv6 header). Ethernet MTUs, jumbo frames
    // included, are around 9K at most, so a jumbogram never arrives on a
    // switch port; a packet claiming to be one is malformed.
    cursor = Cursor(cursor, payloadLength);
    while (isExtensionHeader(header)) {
      auto next = cursor.read<uint8_t>();
      auto hdrExtLen = cursor.read<uint8_t>();
      auto length = extensionHeaderLength(header, hdrExtLen);
      bool nonFirstFragment = false;
      if (header == static_cast<uint8_t>(IP_PROTO::IP_PROTO_IPV6_FRAG)) {
        nonFirstFragment = (cursor.readBE<uint16_t>() & 0xfff8) != 0;
        cursor.skip(length - 4);
      } else {
        cursor.skip(length - 2);
      }
      extensionHeadersLength += length;
      if (nonFirstFragment) {
        break;
      }
      header = next;
      nextHeaderChain.push_back(header);
    }
  } catch (const std::out_of_range&) {
    throw HdrParseError("IPv6 extension header chain truncated");
  }
}

} // namespace facebook::fboss
