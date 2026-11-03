// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/utils/VxlanTestUtils.h"

#include "fboss/agent/FbossError.h"

#include <folly/io/IOBuf.h>

namespace facebook::fboss::utility {

std::array<uint8_t, kVxlanHeaderSize> makeVxlanHeader(uint32_t vni) {
  if (vni > kVxlanMaxVni) {
    throw FbossError("VNI out of range: ", vni);
  }
  std::array<uint8_t, kVxlanHeaderSize> header{};
  header[0] = kVxlanFlagI;
  header[4] = static_cast<uint8_t>((vni >> 16) & 0xFF);
  header[5] = static_cast<uint8_t>((vni >> 8) & 0xFF);
  header[6] = static_cast<uint8_t>(vni & 0xFF);
  return header;
}

namespace {
std::optional<VxlanPacket> parseVxlanPayload(
    const EthFrame& frame,
    const UDPDatagram& udp) {
  if (udp.header().dstPort != kVxlanUdpDstPort) {
    return std::nullopt;
  }
  const auto& payload = udp.payload();
  if (payload.size() < kVxlanHeaderSize + EthHdr::UNTAGGED_PKT_SIZE) {
    return std::nullopt;
  }
  auto innerBuf = folly::IOBuf::copyBuffer(
      payload.data() + kVxlanHeaderSize, payload.size() - kVxlanHeaderSize);
  folly::io::Cursor cursor(innerBuf.get());
  VxlanPacket packet{
      frame,
      udp,
      payload[0],
      (static_cast<uint32_t>(payload[4]) << 16) |
          (static_cast<uint32_t>(payload[5]) << 8) |
          static_cast<uint32_t>(payload[6]),
      EthFrame(cursor)};
  return packet;
}
} // namespace

namespace {
std::vector<uint8_t> vxlanPayloadBytes(uint32_t vni, const EthFrame& inner) {
  const auto innerBuf = inner.toIOBuf();
  std::vector<uint8_t> payload(kVxlanHeaderSize + inner.length());
  const auto header = makeVxlanHeader(vni);
  std::copy(header.begin(), header.end(), payload.begin());
  folly::io::Cursor cursor(innerBuf.get());
  cursor.pull(
      payload.data() + kVxlanHeaderSize, payload.size() - kVxlanHeaderSize);
  return payload;
}

std::vector<uint8_t> vxlanPayload(
    uint32_t vni,
    std::unique_ptr<TxPacket> innerTx) {
  return vxlanPayloadBytes(
      vni, makeEthFrame(*innerTx, true /* skipTtlDecrement */));
}
} // namespace

std::unique_ptr<TxPacket> makeVxlanTxPacket(
    const AllocatePktFn& allocatePkt,
    std::optional<VlanID> vlan,
    folly::MacAddress outerSrcMac,
    folly::MacAddress outerDstMac,
    const folly::IPAddressV6& outerSrcIp,
    const folly::IPAddressV6& outerDstIp,
    uint32_t vni,
    folly::MacAddress innerSrcMac,
    folly::MacAddress innerDstMac,
    const folly::IPAddressV6& innerSrcIp,
    const folly::IPAddressV6& innerDstIp,
    uint16_t innerSrcPort,
    uint16_t innerDstPort,
    uint8_t outerTrafficClass,
    uint8_t innerTrafficClass,
    uint8_t outerHopLimit,
    std::optional<std::vector<uint8_t>> payload) {
  auto innerTx = makeUDPTxPacket(
      allocatePkt,
      std::nullopt,
      innerSrcMac,
      innerDstMac,
      innerSrcIp,
      innerDstIp,
      innerSrcPort,
      innerDstPort,
      innerTrafficClass,
      255 /* hopLimit */,
      payload);
  return makeUDPTxPacket(
      allocatePkt,
      vlan,
      outerSrcMac,
      outerDstMac,
      outerSrcIp,
      outerDstIp,
      innerSrcPort,
      kVxlanUdpDstPort,
      outerTrafficClass,
      outerHopLimit,
      vxlanPayload(vni, std::move(innerTx)));
}

std::unique_ptr<TxPacket> makeVxlanPacket(
    const AllocatePktFn& allocatePkt,
    std::optional<VlanID> vlan,
    folly::MacAddress outerSrcMac,
    folly::MacAddress outerDstMac,
    const folly::IPAddressV6& outerSrcIp,
    const folly::IPAddressV6& outerDstIp,
    uint32_t vni,
    uint16_t outerSrcPort,
    const EthFrame& innerFrame,
    uint8_t outerTrafficClass,
    uint8_t outerHopLimit) {
  return makeUDPTxPacket(
      allocatePkt,
      vlan,
      outerSrcMac,
      outerDstMac,
      outerSrcIp,
      outerDstIp,
      outerSrcPort,
      kVxlanUdpDstPort,
      outerTrafficClass,
      outerHopLimit,
      vxlanPayloadBytes(vni, innerFrame));
}

std::optional<VxlanPacket> parseVxlanPacket(const EthFrame& frame) {
  if (const auto& v6 = frame.v6PayLoad()) {
    if (const auto& udp = v6->udpPayload()) {
      return parseVxlanPayload(frame, *udp);
    }
  } else if (const auto& v4 = frame.v4PayLoad()) {
    if (const auto& udp = v4->udpPayload()) {
      return parseVxlanPayload(frame, *udp);
    }
  }
  return std::nullopt;
}

} // namespace facebook::fboss::utility
