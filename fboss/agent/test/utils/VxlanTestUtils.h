// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/packet/PktFactory.h"
#include "fboss/agent/packet/UDPDatagram.h"
#include "fboss/agent/types.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace facebook::fboss {
class TxPacket;
} // namespace facebook::fboss

namespace facebook::fboss::utility {

inline constexpr uint16_t kVxlanUdpDstPort = 4789;
inline constexpr size_t kVxlanHeaderSize = 8;
inline constexpr uint32_t kVxlanMaxVni = 0xFFFFFF;
inline constexpr uint8_t kVxlanFlagI = 0x08;

struct VxlanPacket {
  EthFrame outerFrame;
  UDPDatagram udp;
  uint8_t flags{0};
  uint32_t vni{0};
  EthFrame innerFrame;
};

cfg::VxLanTunnel makeVxlanTunnelConfig(
    const std::string& name,
    InterfaceID underlayIntf,
    const folly::IPAddressV6& srcIp,
    const folly::IPAddressV6& dstIp,
    cfg::TunnelMode ttlMode = cfg::TunnelMode::UNIFORM,
    cfg::TunnelMode dscpMode = cfg::TunnelMode::UNIFORM,
    cfg::TunnelMode ecnMode = cfg::TunnelMode::UNIFORM);

cfg::VxLanTunnel makeVxlanTunnelConfig(
    const std::string& name,
    InterfaceID underlayIntf,
    const folly::IPAddress& srcIp,
    const folly::IPAddress& dstIp,
    cfg::TunnelMode ttlMode = cfg::TunnelMode::UNIFORM,
    cfg::TunnelMode dscpMode = cfg::TunnelMode::UNIFORM,
    cfg::TunnelMode ecnMode = cfg::TunnelMode::UNIFORM);

std::array<uint8_t, kVxlanHeaderSize> makeVxlanHeader(uint32_t vni);

std::optional<VxlanPacket> parseVxlanPacket(const EthFrame& frame);

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
    uint8_t outerTrafficClass = 0,
    uint8_t innerTrafficClass = 0,
    uint8_t outerHopLimit = 255,
    std::optional<std::vector<uint8_t>> payload = std::nullopt);

struct VxlanEncapExpected {
  std::optional<folly::MacAddress> outerDstMac;
  std::optional<folly::MacAddress> outerSrcMac;
  std::optional<folly::IPAddress> outerSrcIp;
  std::optional<folly::IPAddress> outerDstIp;
  std::optional<uint8_t> outerTrafficClass;
  std::optional<uint8_t> outerHopLimit;
  std::optional<uint32_t> outerFlowLabel;
  std::optional<uint32_t> vni;
  std::optional<folly::MacAddress> innerDstMac;
  std::optional<folly::MacAddress> innerSrcMac;
  std::optional<folly::IPAddress> innerSrcIp;
  std::optional<folly::IPAddress> innerDstIp;
  std::optional<uint16_t> innerSrcPort;
  std::optional<uint16_t> innerDstPort;
  std::optional<std::vector<uint8_t>> innerPayload;
};

struct VxlanDecapExpected {
  std::optional<folly::MacAddress> innerDstMac;
  std::optional<folly::MacAddress> innerSrcMac;
  std::optional<folly::IPAddress> innerSrcIp;
  std::optional<folly::IPAddress> innerDstIp;
  std::optional<uint8_t> innerTrafficClass;
  std::optional<uint8_t> innerHopLimit;
  std::optional<uint16_t> innerSrcPort;
  std::optional<uint16_t> innerDstPort;
  std::optional<std::vector<uint8_t>> innerPayload;
};

void verifyVxlanEncap(
    const EthFrame& rxFrame,
    const VxlanEncapExpected& expected);

void verifyVxlanDecap(
    const EthFrame& rxFrame,
    const VxlanDecapExpected& expected);

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
    uint8_t outerTrafficClass = 0,
    uint8_t outerHopLimit = 255);

} // namespace facebook::fboss::utility
