// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/packet/EthFrame.h"

#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/packet/PktUtil.h"

#include <sstream>

namespace facebook::fboss::utility {
namespace {
static auto kDefaultPayload = std::vector<uint8_t>(256, 0xff);

// See IEEE 802.3-2022 31.4.1 and 4.4.2. TL;DR this should be
// 64 (minFrameSize) - 12 (src/dest mac) - 2 (ethertype)
// - 4 (FCS) = 46 bytes.
static size_t kMacControlPayloadSize = 46;
} // namespace

EthFrame::EthFrame(folly::io::Cursor& cursor) {
  hdr_ = EthHdr(cursor);
  switch (hdr_.etherType) {
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4):
      v4PayLoad_ = IPPacket<folly::IPAddressV4>(cursor);
      break;
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6):
      v6PayLoad_ = IPPacket<folly::IPAddressV6>(cursor);
      break;
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS):
      mplsPayLoad_ = MPLSPacket(cursor);
      break;
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_ARP):
      arpHdr_ = ArpHdr(cursor);
      break;
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_EPON): {
      CHECK(cursor.canAdvance(kMacControlPayloadSize));
      macControlPayload_ = std::vector<uint8_t>(kMacControlPayloadSize);
      cursor.pull(macControlPayload_->data(), kMacControlPayloadSize);
      break;
    }
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_LLDP):
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_SLOW_PROTOCOLS):
    case static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_EAPOL):
      // TODO - add support for parsing respective protocol PDUs
      break;
    default:
      throw FbossError("Unhandled etherType: ", hdr_.etherType);
  }
}

std::unique_ptr<facebook::fboss::TxPacket> EthFrame::getTxPacket(
    std::function<std::unique_ptr<facebook::fboss::TxPacket>(uint32_t)>
        allocatePacket) const {
  auto txPacket = allocatePacket(length());

  folly::io::RWPrivateCursor rwCursor(txPacket->buf());

  rwCursor.push(hdr_.dstAddr.bytes(), folly::MacAddress::SIZE);
  rwCursor.push(hdr_.srcAddr.bytes(), folly::MacAddress::SIZE);
  if (!hdr_.vlanTags.empty()) {
    for (const auto& vlanTag : hdr_.vlanTags) {
      rwCursor.template writeBE<uint32_t>(vlanTag.value);
    }
  }
  if (hdr_.csigTag) {
    rwCursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_CSIG));
    rwCursor.template writeBE<uint16_t>(hdr_.csigTag->value);
  }
  if (v4PayLoad_) {
    rwCursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4));
    auto v4Packet = v4PayLoad_->getTxPacket(allocatePacket);
    folly::io::Cursor cursor(v4Packet->buf());
    rwCursor.push(cursor, v4PayLoad_->length());
  } else if (v6PayLoad_) {
    rwCursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
    auto v6Packet = v6PayLoad_->getTxPacket(allocatePacket);
    folly::io::Cursor cursor(v6Packet->buf());
    rwCursor.push(cursor, v6PayLoad_->length());
  } else if (mplsPayLoad_) {
    rwCursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS));
    auto mplsPacket = mplsPayLoad_->getTxPacket(allocatePacket);
    folly::io::Cursor cursor(mplsPacket->buf());
    rwCursor.push(cursor, mplsPayLoad_->length());
  } else if (arpHdr_) {
    arpHdr_->serialize(&rwCursor);
  } else if (macControlPayload_) {
    rwCursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_EPON));
    rwCursor.push(macControlPayload_->data(), macControlPayload_->size());
  }
  return txPacket;
}

void EthFrame::serialize(folly::io::RWPrivateCursor& cursor) const {
  CHECK_GE(cursor.totalLength(), length())
      << "Insufficient room to serialize packet";

  cursor.push(hdr_.dstAddr.bytes(), folly::MacAddress::SIZE);
  cursor.push(hdr_.srcAddr.bytes(), folly::MacAddress::SIZE);

  if (!hdr_.vlanTags.empty()) {
    for (const auto& vlanTag : hdr_.vlanTags) {
      cursor.template writeBE<uint32_t>(vlanTag.value);
    }
  }
  if (hdr_.csigTag) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_CSIG));
    cursor.template writeBE<uint16_t>(hdr_.csigTag->value);
  }

  if (v4PayLoad_) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4));
    v4PayLoad_->serialize(cursor);
  } else if (v6PayLoad_) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6));
    v6PayLoad_->serialize(cursor);
  } else if (mplsPayLoad_) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS));
    mplsPayLoad_->serialize(cursor);
  } else if (arpHdr_) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_ARP));
    arpHdr_->serialize(&cursor);
  } else if (macControlPayload_) {
    cursor.template writeBE<uint16_t>(
        static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_EPON));
    cursor.push(macControlPayload_->data(), macControlPayload_->size());
  }
}

std::unique_ptr<folly::IOBuf> EthFrame::toIOBuf() const {
  auto buf = folly::IOBuf::create(length());
  buf->append(length());
  folly::io::RWPrivateCursor cursor(buf.get());
  serialize(cursor);
  return buf;
}

template <typename AddrT>
EthFrame getEthFrame(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    AddrT srcIp,
    AddrT dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId,
    size_t payloadSize) {
  constexpr auto isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
  constexpr auto etherType =
      isV4 ? ETHERTYPE::ETHERTYPE_IPV4 : ETHERTYPE::ETHERTYPE_IPV6;
  EthHdr::VlanTags_t tags;
  if (vlanId.has_value()) {
    tags.emplace_back(*vlanId, 0x8100);
  }
  EthHdr ethHdr{srcMac, dstMac, {tags}, static_cast<uint16_t>(etherType)};
  std::conditional_t<isV4, IPv4Hdr, IPv6Hdr> ipHdr;
  ipHdr.srcAddr = srcIp;
  ipHdr.dstAddr = dstIp;
  ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));

  UDPHeader udpHdr;
  udpHdr.srcPort = sPort;
  udpHdr.dstPort = dPort;

  if (payloadSize == 256) {
    return utility::EthFrame(
        ethHdr,
        utility::IPPacket<AddrT>(
            ipHdr, utility::UDPDatagram(udpHdr, kDefaultPayload)));
  }
  return utility::EthFrame(
      ethHdr,
      utility::IPPacket<AddrT>(
          ipHdr,
          utility::UDPDatagram(
              udpHdr, std::vector<uint8_t>(payloadSize, 0xff))));
}

template <typename AddrT>
EthFrame getEthFrame(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    std::vector<MPLSHdr::Label> labels,
    AddrT srcIp,
    AddrT dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId) {
  constexpr auto isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
  EthHdr::VlanTags_t tags;
  if (vlanId.has_value()) {
    tags.emplace_back(*vlanId, 0x8100);
  }
  EthHdr ethHdr{
      srcMac, dstMac, {tags}, static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS)};

  MPLSHdr mplsHdr{std::move(labels)};

  std::conditional_t<isV4, IPv4Hdr, IPv6Hdr> ipHdr;
  ipHdr.srcAddr = srcIp;
  ipHdr.dstAddr = dstIp;
  ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));

  UDPHeader udpHdr;
  udpHdr.srcPort = sPort;
  udpHdr.dstPort = dPort;

  return utility::EthFrame(
      ethHdr,
      utility::MPLSPacket(
          mplsHdr,
          utility::IPPacket<AddrT>(
              ipHdr, utility::UDPDatagram(udpHdr, kDefaultPayload))));
}

std::string utility::EthFrame::toString() const {
  std::stringstream ss;
  ss << "Eth hdr: " << hdr_.toString()
     << " arp: " << (arpHdr_.has_value() ? arpHdr_->toString() : "")
     << " mpls: " << (mplsPayLoad_.has_value() ? mplsPayLoad_->toString() : "")
     << " v6 : " << (v6PayLoad_.has_value() ? v6PayLoad_->toString() : "")
     << " v4 : " << (v4PayLoad_.has_value() ? v4PayLoad_->toString() : "")
     << " mac control: "
     << (macControlPayload_ ? PktUtil::hexDump(
                                  folly::IOBuf(
                                      folly::IOBuf::CopyBufferOp::COPY_BUFFER,
                                      macControlPayload_->data(),
                                      macControlPayload_->size()))
                            : "");
  return ss.str();
}
void EthFrame::stripVlans() {
  auto srcMac = hdr_.getSrcMac();
  auto dstMac = hdr_.getDstMac();
  auto etherType = hdr_.getEtherType();
  auto csigTag = hdr_.getCsigTag();
  hdr_ = EthHdr(dstMac, srcMac, {}, etherType);
  hdr_.setCsigTag(csigTag);
}

EthFrame makeEthFrame(const TxPacket& txPkt, bool skipTtlDecrement) {
  folly::io::Cursor cursor{txPkt.buf()};
  utility::EthFrame frame(cursor);
  if (skipTtlDecrement) {
    return frame;
  }
  frame.decrementTTL();
  return frame;
}

EthFrame makeEthFrame(const TxPacket& txPkt, folly::MacAddress dstMac) {
  auto frame = makeEthFrame(txPkt);
  frame.setDstMac(dstMac);
  return frame;
}

EthFrame
makeEthFrame(const TxPacket& txPkt, folly::MacAddress dstMac, VlanID vlan) {
  auto frame = makeEthFrame(txPkt, dstMac);
  frame.setVlan(vlan);
  return frame;
}

template EthFrame getEthFrame<folly::IPAddressV4>(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    folly::IPAddressV4 srcIp,
    folly::IPAddressV4 dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId,
    size_t payloadSize);

template EthFrame getEthFrame<folly::IPAddressV6>(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    folly::IPAddressV6 srcIp,
    folly::IPAddressV6 dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId,
    size_t payloadSize);

template EthFrame getEthFrame<folly::IPAddressV4>(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    std::vector<MPLSHdr::Label> labels,
    folly::IPAddressV4 srcIp,
    folly::IPAddressV4 dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId);

template EthFrame getEthFrame<folly::IPAddressV6>(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    std::vector<MPLSHdr::Label> labels,
    folly::IPAddressV6 srcIp,
    folly::IPAddressV6 dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId);

} // namespace facebook::fboss::utility
