// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/utils/VxlanTestUtils.h"

#include "fboss/agent/FbossError.h"

#include <folly/io/IOBuf.h>
#include <gtest/gtest.h>

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

namespace {
void verifyInnerUdp(
    const EthFrame& innerFrame,
    const std::optional<uint16_t>& srcPort,
    const std::optional<uint16_t>& dstPort,
    const std::optional<std::vector<uint8_t>>& payload) {
  std::optional<UDPDatagram> udp;
  if (const auto& v6 = innerFrame.v6PayLoad()) {
    udp = v6->udpPayload();
  } else if (const auto& v4 = innerFrame.v4PayLoad()) {
    udp = v4->udpPayload();
  }
  ASSERT_TRUE(udp.has_value());
  if (srcPort.has_value()) {
    EXPECT_EQ(udp->header().srcPort, srcPort.value());
  }
  if (dstPort.has_value()) {
    EXPECT_EQ(udp->header().dstPort, dstPort.value());
  }
  if (payload.has_value()) {
    EXPECT_EQ(udp->payload(), payload.value());
  }
}

void verifyInnerIps(
    const EthFrame& innerFrame,
    const std::optional<folly::IPAddress>& srcIp,
    const std::optional<folly::IPAddress>& dstIp) {
  if (const auto& v6 = innerFrame.v6PayLoad()) {
    if (srcIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v6->header().srcAddr), srcIp.value());
    }
    if (dstIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v6->header().dstAddr), dstIp.value());
    }
  } else if (const auto& v4 = innerFrame.v4PayLoad()) {
    if (srcIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v4->header().srcAddr), srcIp.value());
    }
    if (dstIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v4->header().dstAddr), dstIp.value());
    }
  } else {
    FAIL() << "Inner frame has no IP payload";
  }
}
} // namespace

void verifyVxlanEncap(
    const EthFrame& rxFrame,
    const VxlanEncapExpected& expected) {
  const auto packet = parseVxlanPacket(rxFrame);
  ASSERT_TRUE(packet.has_value()) << "Rx frame is not a VXLAN packet";
  const auto outerHdr = rxFrame.header();
  if (expected.outerDstMac.has_value()) {
    EXPECT_EQ(outerHdr.dstAddr, expected.outerDstMac.value());
  }
  if (expected.outerSrcMac.has_value()) {
    EXPECT_EQ(outerHdr.srcAddr, expected.outerSrcMac.value());
  }
  if (const auto& outerV6 = rxFrame.v6PayLoad()) {
    const auto v6Hdr = outerV6->header();
    if (expected.outerSrcIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v6Hdr.srcAddr), expected.outerSrcIp.value());
    }
    if (expected.outerDstIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v6Hdr.dstAddr), expected.outerDstIp.value());
    }
    if (expected.outerTrafficClass.has_value()) {
      EXPECT_EQ(v6Hdr.trafficClass, expected.outerTrafficClass.value());
    }
    if (expected.outerHopLimit.has_value()) {
      EXPECT_EQ(v6Hdr.hopLimit, expected.outerHopLimit.value());
    }
    if (expected.outerFlowLabel.has_value()) {
      EXPECT_EQ(v6Hdr.flowLabel, expected.outerFlowLabel.value());
    }
  } else if (const auto& outerV4 = rxFrame.v4PayLoad()) {
    const auto v4Hdr = outerV4->header();
    if (expected.outerSrcIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v4Hdr.srcAddr), expected.outerSrcIp.value());
    }
    if (expected.outerDstIp.has_value()) {
      EXPECT_EQ(folly::IPAddress(v4Hdr.dstAddr), expected.outerDstIp.value());
    }
    if (expected.outerHopLimit.has_value()) {
      EXPECT_EQ(v4Hdr.ttl, expected.outerHopLimit.value());
    }
  } else {
    FAIL() << "VXLAN outer header has no IP payload";
  }
  EXPECT_EQ(packet->udp.header().dstPort, kVxlanUdpDstPort);
  if (expected.vni.has_value()) {
    EXPECT_EQ(packet->vni, expected.vni.value());
  }
  const auto innerHdr = packet->innerFrame.header();
  if (expected.innerDstMac.has_value()) {
    EXPECT_EQ(innerHdr.dstAddr, expected.innerDstMac.value());
  }
  if (expected.innerSrcMac.has_value()) {
    EXPECT_EQ(innerHdr.srcAddr, expected.innerSrcMac.value());
  }
  verifyInnerIps(packet->innerFrame, expected.innerSrcIp, expected.innerDstIp);
  verifyInnerUdp(
      packet->innerFrame,
      expected.innerSrcPort,
      expected.innerDstPort,
      expected.innerPayload);
}

void verifyVxlanDecap(
    const EthFrame& rxFrame,
    const VxlanDecapExpected& expected) {
  EXPECT_FALSE(parseVxlanPacket(rxFrame).has_value())
      << "Decap egress frame still carries a VXLAN header";
  const auto hdr = rxFrame.header();
  if (expected.innerDstMac.has_value()) {
    EXPECT_EQ(hdr.dstAddr, expected.innerDstMac.value());
  }
  if (expected.innerSrcMac.has_value()) {
    EXPECT_EQ(hdr.srcAddr, expected.innerSrcMac.value());
  }
  verifyInnerIps(rxFrame, expected.innerSrcIp, expected.innerDstIp);
  if (const auto& v6 = rxFrame.v6PayLoad()) {
    const auto v6Hdr = v6->header();
    if (expected.innerTrafficClass.has_value()) {
      EXPECT_EQ(v6Hdr.trafficClass, expected.innerTrafficClass.value());
    }
    if (expected.innerHopLimit.has_value()) {
      EXPECT_EQ(v6Hdr.hopLimit, expected.innerHopLimit.value());
    }
  } else if (const auto& v4 = rxFrame.v4PayLoad()) {
    const auto v4Hdr = v4->header();
    if (expected.innerHopLimit.has_value()) {
      EXPECT_EQ(v4Hdr.ttl, expected.innerHopLimit.value());
    }
  }
  verifyInnerUdp(
      rxFrame,
      expected.innerSrcPort,
      expected.innerDstPort,
      expected.innerPayload);
}

} // namespace facebook::fboss::utility
