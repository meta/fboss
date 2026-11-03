// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/EthFrame.h"
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

std::array<uint8_t, kVxlanHeaderSize> makeVxlanHeader(uint32_t vni);

std::optional<VxlanPacket> parseVxlanPacket(const EthFrame& frame);

} // namespace facebook::fboss::utility
