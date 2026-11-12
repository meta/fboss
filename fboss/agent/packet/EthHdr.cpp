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

#include <sstream>
#include <stdexcept>

namespace facebook::fboss {

using folly::MacAddress;
using folly::io::Cursor;
using std::string;
using std::stringstream;

EthHdr::EthHdr(Cursor& cursor) {
  try {
    uint8_t buf[14];
    cursor.pull(buf, sizeof(buf));
    dstAddr =
        MacAddress::fromBinary(folly::ByteRange(&buf[0], MacAddress::SIZE));
    srcAddr =
        MacAddress::fromBinary(folly::ByteRange(&buf[6], MacAddress::SIZE));
    etherType =
        (static_cast<uint16_t>(buf[12]) << 8) | static_cast<uint16_t>(buf[13]);
    // Look for VLAN tags.
    while (etherType == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_VLAN) ||
           etherType == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_QINQ)) {
      cursor.pull(buf, 4);
      uint32_t tag = (static_cast<uint32_t>(etherType) << 16) |
          (static_cast<uint32_t>(buf[0]) << 8) | static_cast<uint32_t>(buf[1]);
      vlanTags.emplace_back(tag);
      etherType =
          (static_cast<uint16_t>(buf[2]) << 8) | static_cast<uint16_t>(buf[3]);
    }
    // Look for a CSIG tag, after any VLAN tags.
    if (etherType == static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_CSIG)) {
      cursor.pull(buf, 4);
      csigTag = CsigTag(
          static_cast<uint16_t>(
              (static_cast<uint16_t>(buf[0]) << 8) |
              static_cast<uint16_t>(buf[1])));
      etherType =
          (static_cast<uint16_t>(buf[2]) << 8) | static_cast<uint16_t>(buf[3]);
    }
  } catch (const std::out_of_range&) {
    throw HdrParseError("Ethernet header too small");
  }
}

void EthHdr::addVlans(const std::vector<VlanID>& vlans, ETHERTYPE ether) {
  for (auto vlan : vlans) {
    vlanTags.emplace_back(
        static_cast<uint16_t>(vlan), static_cast<uint16_t>(ether));
  }
}

string VlanTag::toString() const {
  stringstream ss;
  ss << " Protocol: " << std::hex << "0x" << tpid()
     << " priority : " << std::dec << static_cast<int>(pcp())
     << " drop eligibility: " << static_cast<int>(dei()) << " vlan : " << vid();
  return ss.str();
}

string CsigTag::toString() const {
  stringstream ss;
  ss << " signal type: " << static_cast<int>(signalType())
     << " signal: " << std::hex << "0x" << static_cast<int>(signal())
     << " link locator: 0x" << static_cast<int>(linkLocator());
  return ss.str();
}

string EthHdr::toString() const {
  stringstream ss;
  ss << " Source : " << getSrcMac().toString()
     << " Destination: " << getDstMac().toString()
     << " Ether type: " << std::hex << "0x" << getEtherType();
  ss << " VlanTags: {";
  for (auto vlanTag : vlanTags) {
    ss << folly::to<std::string>("{", vlanTag, "} ");
  }
  ss << "}";
  if (csigTag) {
    ss << " CsigTag: {" << csigTag->toString() << "}";
  }
  return ss.str();
}

std::string EthHdr::printVlanTags() {
  std::stringstream ss;
  ss << "[";
  for (auto vlanTag : vlanTags) {
    ss << std::hex << vlanTag.toString() << " ";
  }
  ss << "]";
  return ss.str();
}

} // namespace facebook::fboss
