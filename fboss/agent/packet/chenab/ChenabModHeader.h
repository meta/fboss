// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>
#include <folly/io/Cursor.h>

namespace facebook::fboss {

struct ChenabModOuterHeader {
  folly::MacAddress srcMac;
  folly::MacAddress dstMac;
  folly::IPAddress srcIp;
  folly::IPAddress dstIp;
  size_t ipPayloadLength{0};
  uint8_t dscp{0};
  uint8_t ttl{0};
};

struct ChenabTacMetadata {
  uint32_t uid{0};
  std::array<uint8_t, 8> signatureHash{};
  std::array<uint8_t, 10> timestamp{};
  uint16_t recordTime{0};
  uint16_t egressLocalPort{0};
  uint16_t trapId{0};
  uint8_t version{0};
  uint8_t mirrorReason{0};
};

struct ChenabTacNewEntry {
  ChenabTacMetadata metadata;
  std::vector<uint8_t> truncatedPacket;
  uint16_t ingressLocalPort{0};
  uint16_t packetLength{0};
  bool elephant{false};
};

struct ChenabTacAggregate {
  ChenabTacMetadata metadata;
  uint64_t byteCount{0};
  uint32_t packetCount{0};
  bool evicted{false};
};

using ChenabTacEvent = std::variant<ChenabTacNewEntry, ChenabTacAggregate>;

struct ChenabTacPortMap {
  std::vector<std::pair<uint16_t, uint16_t>> localToLabel;
};

using ChenabModPacket =
    std::variant<ChenabTacNewEntry, ChenabTacAggregate, ChenabTacPortMap>;

ChenabTacEvent parseChenabTacEvent(folly::io::Cursor& cursor);
ChenabModOuterHeader parseChenabModOuterHeader(folly::io::Cursor cursor);
ChenabModPacket parseChenabModPacket(folly::io::Cursor& cursor);
ChenabModPacket parseChenabModPacket(
    folly::io::Cursor& cursor,
    ChenabModOuterHeader& outer);

} // namespace facebook::fboss
