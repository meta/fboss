// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/agent/packet/Ethertype.h"
#include "fboss/agent/types.h"

#include "fboss/agent/packet/ArpHdr.h"
#include "fboss/agent/packet/EthHdr.h"
#include "fboss/agent/packet/IPPacket.h"
#include "fboss/agent/packet/MPLSPacket.h"

#include <optional>

namespace facebook::fboss {

class TxPacket;

namespace utility {

class EthFrame {
 public:
  static constexpr size_t FCS_SIZE = 4;
  // 2 bits for hyper port packet type, 14 bits for sequence number
  static constexpr size_t HYPER_PORT_HEADER_SIZE = 2;
  // read entire ethernet frame, and populate payloads, useful to parse RxPacket
  explicit EthFrame(folly::io::Cursor& cursor);

  // set header fields, useful to construct TxPacket
  explicit EthFrame(EthHdr hdr) : hdr_(std::move(hdr)) {}

  EthFrame(EthHdr hdr, MPLSPacket payload) : hdr_(std::move(hdr)) {
    mplsPayLoad_ = std::move(payload);
    hdr_.etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_MPLS);
  }

  EthFrame(EthHdr hdr, IPPacket<folly::IPAddressV4> payload)
      : hdr_(std::move(hdr)), v4PayLoad_(payload) {
    hdr_.etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV4);
  }

  EthFrame(EthHdr hdr, IPPacket<folly::IPAddressV6> payload)
      : hdr_(std::move(hdr)), v6PayLoad_(payload) {
    hdr_.etherType = static_cast<uint16_t>(ETHERTYPE::ETHERTYPE_IPV6);
  }

  EthHdr header() const {
    return hdr_;
  }

  size_t length() const {
    auto len = 0;
    if (v4PayLoad_) {
      len += v4PayLoad_->length();
    } else if (v6PayLoad_) {
      len += v6PayLoad_->length();
    } else if (mplsPayLoad_) {
      len += mplsPayLoad_->length();
    } else if (arpHdr_) {
      len += arpHdr_->size();
    } else if (macControlPayload_) {
      len += macControlPayload_->size();
    }
    len += hdr_.size();
    return len;
  }
  // construct TxPacket by encapsulating payload
  std::unique_ptr<facebook::fboss::TxPacket> getTxPacket(
      std::function<std::unique_ptr<facebook::fboss::TxPacket>(uint32_t)>
          allocatePacket) const;

  std::optional<IPPacket<folly::IPAddressV4>> v4PayLoad() const {
    return v4PayLoad_;
  }

  std::optional<IPPacket<folly::IPAddressV6>> v6PayLoad() const {
    return v6PayLoad_;
  }

  std::optional<MPLSPacket> mplsPayLoad() const {
    return mplsPayLoad_;
  }

  std::optional<ArpHdr> arpHdr() const {
    return arpHdr_;
  }

  std::optional<std::vector<uint8_t>> macControlPayload() const {
    return macControlPayload_;
  }

  void serialize(folly::io::RWPrivateCursor& cursor) const;

  bool operator==(const EthFrame& that) const {
    return std::tie(
               hdr_,
               v4PayLoad_,
               v6PayLoad_,
               mplsPayLoad_,
               arpHdr_,
               macControlPayload_) ==
        std::tie(
               that.hdr_,
               that.v4PayLoad_,
               that.v6PayLoad_,
               that.mplsPayLoad_,
               that.arpHdr_,
               that.macControlPayload_);
  }
  bool operator!=(const EthFrame& that) const {
    return !(*this == that);
  }
  void decrementTTL() {
    if (mplsPayLoad_.has_value()) {
      mplsPayLoad_->decrementTTL();
    } else if (v6PayLoad_.has_value()) {
      v6PayLoad_->decrementTTL();
    } else if (v4PayLoad_.has_value()) {
      v4PayLoad_->decrementTTL();
    }
  }
  void setDstMac(const folly::MacAddress& dstMac) {
    hdr_.setDstMac(dstMac);
  }
  void setVlan(VlanID vlan) {
    hdr_.setVlan(vlan);
  }

  void stripVlans();
  std::string toString() const;
  std::unique_ptr<folly::IOBuf> toIOBuf() const;

 private:
  EthHdr hdr_;
  std::optional<IPPacket<folly::IPAddressV4>> v4PayLoad_;
  std::optional<IPPacket<folly::IPAddressV6>> v6PayLoad_;
  std::optional<MPLSPacket> mplsPayLoad_;
  std::optional<ArpHdr> arpHdr_;
  std::optional<std::vector<uint8_t>> macControlPayload_;
};

inline std::ostream& operator<<(std::ostream& os, const EthFrame& ethFrame) {
  os << ethFrame.toString();
  return os;
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
    size_t payloadSize = 256);

template <typename AddrT>
EthFrame getEthFrame(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    std::vector<MPLSHdr::Label> labels,
    AddrT srcIp,
    AddrT dstIp,
    uint16_t sPort,
    uint16_t dPort,
    std::optional<VlanID> vlanId);

EthFrame makeEthFrame(const TxPacket& txPkt, bool skipTtlDecrement = false);

EthFrame makeEthFrame(const TxPacket& txPkt, folly::MacAddress dstMac);

EthFrame
makeEthFrame(const TxPacket& txPkt, folly::MacAddress dstMac, VlanID vlan);
EthHdr makeEthHdr(
    folly::MacAddress srcMac,
    folly::MacAddress dstMac,
    std::optional<VlanID> vlan,
    ETHERTYPE etherType,
    // 802.1p priority (PCP); valid range is 0-7 and it is only emitted when a
    // VLAN tag is present.
    uint8_t pcp = 0);

} // namespace utility
} // namespace facebook::fboss
