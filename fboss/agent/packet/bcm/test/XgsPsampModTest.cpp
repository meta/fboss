/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/packet/bcm/XgsPsampMod.h"
#include "fboss/agent/packet/IpfixHeader.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <fmt/core.h>

#include <gtest/gtest.h>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/packet/HdrParseError.h"

namespace facebook::fboss::psamp {

namespace {
std::string xgsAsicName(cfg::AsicType asicType) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      return "TH5";
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      return "TH6";
    default:
      return fmt::format("Asic{}", static_cast<int>(asicType));
  }
}

// A bare EXPECT_THROW would also be satisfied by the unsupported-ASIC throw,
// so each case names the failure it is testing for.
void expectDataParseError(
    const std::vector<uint8_t>& bytes,
    cfg::AsicType asicType,
    const std::string& expected) {
  auto buf = folly::IOBuf::wrapBuffer(bytes.data(), bytes.size());
  folly::io::Cursor cursor(buf.get());
  try {
    XgsPsampData::deserialize(cursor, asicType);
    ADD_FAILURE() << "expected HdrParseError: " << expected;
  } catch (const HdrParseError& e) {
    EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
        << e.what();
  }
}

// The pipelines a drop can be attributed to. Empty means that pipeline
// reported nothing.
struct DropReasonCase {
  std::optional<uint8_t> ingress;
  std::optional<uint8_t> mmu;
  std::optional<uint8_t> egress;
};

// The two drop reason bytes as they sit on the wire.
using WireBytes = std::array<uint8_t, 2>;

std::vector<std::pair<WireBytes, DropReasonCase>> dropReasonCases(
    cfg::AsicType asicType) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      // A byte per pipeline, so both pass straight through.
      return {
          {{0x00, 0x00}, {}},
          {{0x1A, 0x00}, {.ingress = 0x1A}},
          {{0x00, 0x03}, {.mmu = 3}},
          {{0x91, 0x00}, {.ingress = 0x91}},
          {{0xFF, 0x00}, {.ingress = 0xFF}},
      };
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      // One unified byte, the second is metadata. Which pipeline a code
      // belongs to is the base offset it sits above.
      return {
          {{0x00, 0x00}, {.ingress = 0}}, // ingress base, code 0
          {{0x1A, 0x00}, {.ingress = 0x1A}},
          {{0x78, 0x00}, {.ingress = 0x78}}, // last ingress code
          {{0x90, 0x00}, {.mmu = 0}}, // MMU base, code 0
          {{0x91, 0x00}, {.mmu = 1}}, // MMU base + 1, seen on hardware
          {{0x95, 0x00}, {.mmu = 5}}, // last MMU code
          {{0x98, 0x00}, {.egress = 0}}, // egress base, code 0
          {{0x99, 0x00}, {.egress = 1}}, // first egress code
          {{0xFF, 0x00}, {.egress = 0x67}}, // last code the byte can carry
          {{0x79, 0x00}, {}}, // between the ingress and MMU ranges
      };
    default:
      throw HdrParseError(
          fmt::format(
              "No drop reason cases for ASIC type {}",
              static_cast<int>(asicType)));
  }
}

XgsPsampData decodeDropReason(WireBytes wire, cfg::AsicType asicType) {
  std::vector<uint8_t> bytes(24, 0x00);
  // Drop reason follows observationTimeNs(8) switchId(4) port(2) port(2).
  bytes[16] = wire[0];
  bytes[17] = wire[1];
  bytes[21] = XGS_PSAMP_VAR_LEN_INDICATOR;
  auto buf = folly::IOBuf::wrapBuffer(bytes.data(), bytes.size());
  folly::io::Cursor cursor(buf.get());
  return XgsPsampData::deserialize(cursor, asicType);
}

// A mirror-on-drop export captured off real hardware. Outer headers are
// stripped and the sampled payload truncated to 20 bytes with the lengths
// adjusted; every other byte is verbatim.
struct CapturedPacket {
  std::vector<uint8_t> ipfixBytes;
  uint32_t exportTime;
  uint64_t observationTimeNs;
  uint16_t egressModPortId;
  std::optional<uint8_t> dropReasonIngress;
  std::optional<uint8_t> dropReasonMmu;
  uint8_t cosColorProb;
};

CapturedPacket capturedPacketForAsic(cfg::AsicType asicType) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      // clang-format off
      return {{
          0x00, 0x0A, 0x00, 0x40,             // IPFIX version 10, length 64
          0x69, 0x2F, 0x80, 0x0E,             // export time
          0x00, 0x00, 0x00, 0x01,             // sequence number
          0x00, 0x00, 0x00, 0x01,             // observation domain ID
          0x12, 0x34,                         // template ID (TH5)
          0x00, 0x30,                         // psamp length = 48
          0x69, 0x2F, 0x80, 0x0B, 0x0B, 0x47, 0x69, 0xCE, // observationTimeNs
          0x00, 0x00, 0x00, 0x00,             // switchId
          0x00, 0x00,                         // egressModPortId
          0x00, 0x01,                         // ingressPort
          0x1A,                               // dropReasonIngress
          0x00,                               // dropReasonMmu
          0x12, 0x34,                         // userMetaField
          0x00,                               // cosColorProb
          0xFF,                               // varLenIndicator
          0x00, 0x14,                         // packetSampledLength = 20
          0x02, 0x00, 0x00, 0x00, 0x00, 0x01, // inner dst MAC
          0x02, 0x00, 0x00, 0x00, 0x00, 0x01, // inner src MAC
          0x86, 0xDD,                         // inner EtherType IPv6
          0x60, 0x00, 0x00, 0x00, 0x02, 0x08, // inner IPv6 prefix
      }, 0x692F800E, 0x692F800B0B4769CE, 0, 0x1A, {}, 0};
      // clang-format on
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      // tahansb800bc (BCM78914_B0, brcm-14.2.0.0_odp) running
      // AgentMirrorOnDropStatelessTest.MmuDrop.
      // clang-format off
      return {{
          0x00, 0x0A, 0x00, 0x40,             // IPFIX version 10, length 64
          0x6A, 0x98, 0x9B, 0x3B,             // export time
          0x00, 0x00, 0x00, 0x01,             // sequence number
          0x00, 0x00, 0x00, 0x01,             // observation domain ID
          0x12, 0x39,                         // template ID (TH6)
          0x00, 0x30,                         // psamp length = 48
          0x6A, 0x98, 0x9B, 0x3B, 0x00, 0xBB, 0xBD, 0xE8, // observationTimeNs
          0x00, 0x00, 0x00, 0x00,             // switchId
          0x00, 0x90,                         // egressModPortId = 144
          0x00, 0x01,                         // ingressPort
          0x91,                               // MMU base 0x90 + code 1
          0x00,
          0x12, 0x34,                         // userMetaField
          0x20,                               // cosColorProb
          0xFF,                               // varLenIndicator
          0x00, 0x14,                         // packetSampledLength = 20
          0x02, 0x00, 0x00, 0x00, 0x00, 0x01, // inner dst MAC
          0x02, 0x00, 0x00, 0x00, 0x00, 0x01, // inner src MAC
          0x86, 0xDD,                         // inner EtherType IPv6
          0x64, 0x00, 0x00, 0x00, 0x02, 0x08, // inner IPv6 prefix
      }, 0x6A989B3B, 0x6A989B3B00BBBDE8, 144, {}, 1, 0x20};
      // clang-format on
    default:
      throw HdrParseError(
          fmt::format(
              "No captured packet for ASIC type {}",
              static_cast<int>(asicType)));
  }
}

} // namespace

// Tests whose behaviour depends on which XGS chip produced the packet. Adding
// an ASIC here runs every one of them against it.
class XgsPsampAsicTest : public testing::TestWithParam<cfg::AsicType> {};

INSTANTIATE_TEST_SUITE_P(
    PerAsic,
    XgsPsampAsicTest,
    ::testing::Values(
        cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
        cfg::AsicType::ASIC_TYPE_TOMAHAWK6),
    [](const testing::TestParamInfo<cfg::AsicType>& info) {
      return xgsAsicName(info.param);
    });

TEST(XgsPsampModTest, IpfixHeaderSerializeDeserialize) {
  IpfixHeader header;
  header.version = IPFIX_VERSION;
  header.length = 618; // arbitrary value to verify round-trip serialization
  header.exportTime = 0x692F800E;
  header.sequenceNumber = 1;
  header.observationDomainId = 42;

  EXPECT_EQ(header.size(), 16);

  constexpr int bufSize = 1024;
  std::vector<uint8_t> buffer(bufSize);
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), bufSize);
  auto cursor = std::make_shared<folly::io::RWPrivateCursor>(buf.get());

  header.serialize(cursor.get());
  size_t serializedSize = bufSize - cursor->length();
  EXPECT_EQ(serializedSize, 16);

  // clang-format off
  std::vector<uint8_t> expected = {
      0x00, 0x0A,                         // version = 10
      0x02, 0x6A,                         // length = 618
      0x69, 0x2F, 0x80, 0x0E,             // exportTime
      0x00, 0x00, 0x00, 0x01,             // sequenceNumber = 1
      0x00, 0x00, 0x00, 0x2A,             // observationDomainId = 42
  };
  // clang-format on
  std::vector<uint8_t> actual(buffer.begin(), buffer.begin() + serializedSize);
  EXPECT_EQ(actual, expected);

  auto deserializeBuf = folly::IOBuf::wrapBuffer(buffer.data(), serializedSize);
  folly::io::Cursor deserializeCursor(deserializeBuf.get());
  auto deserialized = IpfixHeader::deserialize(deserializeCursor);

  EXPECT_EQ(deserialized.version, header.version);
  EXPECT_EQ(deserialized.length, header.length);
  EXPECT_EQ(deserialized.exportTime, header.exportTime);
  EXPECT_EQ(deserialized.sequenceNumber, header.sequenceNumber);
  EXPECT_EQ(deserialized.observationDomainId, header.observationDomainId);
}

TEST(XgsPsampModTest, IpfixHeaderTruncatedBuffer) {
  std::vector<uint8_t> smallBuf(10); // < 16 bytes
  auto buf = folly::IOBuf::wrapBuffer(smallBuf.data(), smallBuf.size());
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(IpfixHeader::deserialize(cursor), HdrParseError);
}

TEST(XgsPsampModTest, IpfixHeaderWrongVersion) {
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x00, 0x05,                         // version = 5 (wrong)
      0x00, 0x10,                         // length = 16
      0x00, 0x00, 0x00, 0x00,             // exportTime
      0x00, 0x00, 0x00, 0x00,             // sequenceNumber
      0x00, 0x00, 0x00, 0x00,             // observationDomainId
  };
  // clang-format on
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), buffer.size());
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(IpfixHeader::deserialize(cursor), HdrParseError);
}

TEST(XgsPsampModTest, XgsPsampTemplateIdForAsic) {
  EXPECT_EQ(
      xgsPsampTemplateIdForAsic(cfg::AsicType::ASIC_TYPE_TOMAHAWK5),
      XGS_PSAMP_TEMPLATE_ID_TH5);
  EXPECT_EQ(
      xgsPsampTemplateIdForAsic(cfg::AsicType::ASIC_TYPE_TOMAHAWK6),
      XGS_PSAMP_TEMPLATE_ID_TH6);
  EXPECT_THROW(
      xgsPsampTemplateIdForAsic(cfg::AsicType::ASIC_TYPE_JERICHO3),
      HdrParseError);
}

TEST(XgsPsampModTest, XgsPsampTemplateHeaderMismatchedAsic) {
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x12, 0x34,                         // template ID = 0x1234 (TH5)
      0x00, 0x30,                         // psampLength = 48
  };
  // clang-format on
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), buffer.size());
  folly::io::Cursor th5Cursor(buf.get());
  EXPECT_NO_THROW(
      XgsPsampTemplateHeader::deserialize(
          th5Cursor, cfg::AsicType::ASIC_TYPE_TOMAHAWK5));
  folly::io::Cursor th6Cursor(buf.get());
  EXPECT_THROW(
      XgsPsampTemplateHeader::deserialize(
          th6Cursor, cfg::AsicType::ASIC_TYPE_TOMAHAWK6),
      HdrParseError);
}

TEST(XgsPsampModTest, XgsPsampTemplateHeaderTruncatedBuffer) {
  std::vector<uint8_t> smallBuf(3); // < 4 bytes
  auto buf = folly::IOBuf::wrapBuffer(smallBuf.data(), smallBuf.size());
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(
      XgsPsampTemplateHeader::deserialize(
          cursor, cfg::AsicType::ASIC_TYPE_TOMAHAWK5),
      HdrParseError);
}

TEST(XgsPsampModTest, XgsPsampTemplateHeaderWrongTemplateId) {
  // clang-format off
  std::vector<uint8_t> buffer = {
      0xAB, 0xCD,                         // templateId = 0xABCD (wrong)
      0x00, 0x30,                         // psampLength = 48
  };
  // clang-format on
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), buffer.size());
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(
      XgsPsampTemplateHeader::deserialize(
          cursor, cfg::AsicType::ASIC_TYPE_TOMAHAWK5),
      HdrParseError);
}

// XgsPsampData has 24 bytes of fixed fields (observationTimeNs(8) +
// switchId(4) + egressModPortId(2) + ingressPort(2) + dropReasonIngress(1) +
// dropReasonMmu(1) + userMetaField(2) + cosColorProb(1) + varLenIndicator(1) +
// packetSampledLength(2)) before the variable-length sampled packet payload.
// Deserialize must throw when the buffer is shorter than this fixed prefix.
TEST_P(XgsPsampAsicTest, DataTruncatedBuffer) {
  std::vector<uint8_t> smallBuf(20); // 20 < 24 fixed-field bytes
  expectDataParseError(smallBuf, GetParam(), "PSAMP data too small");
}

// The fixed fields parse successfully but packetSampledLength claims 100 bytes
// of sampled packet data while only 2 bytes remain in the buffer. Deserialize
// must throw because the cursor cannot satisfy the advertised payload length.
TEST_P(XgsPsampAsicTest, DataInsufficientSampledData) {
  // packetSampledLength=100 but only 2 data bytes follow
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // observationTimeNs
      0x00, 0x00, 0x00, 0x00,             // switchId
      0x00, 0x00,                         // egressModPortId
      0x00, 0x00,                         // ingressPort
      0x00,                               // dropReasonIngress
      0x00,                               // dropReasonMmu
      0x00, 0x00,                         // userMetaField
      0x00,                               // cosColorProb
      0xFF,                               // varLenIndicator
      0x00, 0x64,                         // packetSampledLength = 100
      0xAA, 0xBB,                         // only 2 bytes of data
  };
  // clang-format on
  expectDataParseError(buffer, GetParam(), "sampled packet data too small");
}

TEST_P(XgsPsampAsicTest, DataInvalidVarLenIndicator) {
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // observationTimeNs
      0x00, 0x00, 0x00, 0x00,             // switchId
      0x00, 0x00,                         // egressModPortId
      0x00, 0x00,                         // ingressPort
      0x00,                               // dropReasonIngress
      0x00,                               // dropReasonMmu
      0x00, 0x00,                         // userMetaField
      0x00,                               // cosColorProb
      0xAA,                               // varLenIndicator = 0xAA (wrong)
      0x00, 0x00,                         // packetSampledLength
  };
  // clang-format on
  expectDataParseError(buffer, GetParam(), "variable length indicator");
}

TEST(XgsPsampModTest, DataUnsupportedAsicType) {
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // observationTimeNs
      0x00, 0x00, 0x00, 0x00,             // switchId
      0x00, 0x00,                         // egressModPortId
      0x00, 0x00,                         // ingressPort
      0x00,                               // dropReasonIngress
      0x00,                               // dropReasonMmu
      0x00, 0x00,                         // userMetaField
      0x00,                               // cosColorProb
      0xFF,                               // varLenIndicator
      0x00, 0x00,                         // packetSampledLength = 0
  };
  // clang-format on
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), buffer.size());
  folly::io::Cursor supportedCursor(buf.get());
  EXPECT_NO_THROW(
      XgsPsampData::deserialize(
          supportedCursor, cfg::AsicType::ASIC_TYPE_TOMAHAWK5));
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(
      XgsPsampData::deserialize(cursor, cfg::AsicType::ASIC_TYPE_JERICHO3),
      HdrParseError);
}

// Every sub-header parses, and only the final length cross-check fails.
TEST_P(XgsPsampAsicTest, ModPacketLengthMismatch) {
  const uint16_t templateId = xgsPsampTemplateIdForAsic(GetParam());
  // clang-format off
  std::vector<uint8_t> buffer = {
      0x00, 0x0A,                         // IPFIX version 10
      0x27, 0x0F,                         // length = 9999, does not match
      0x00, 0x00, 0x00, 0x00,             // export time
      0x00, 0x00, 0x00, 0x00,             // sequence number
      0x00, 0x00, 0x00, 0x00,             // observation domain ID
      0x00, 0x00,                         // template ID, filled in below
      0x00, 0x1E,                         // psamp length = 30
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // observationTimeNs
      0x00, 0x00, 0x00, 0x00,             // switchId
      0x00, 0x00,                         // egressModPortId
      0x00, 0x00,                         // ingressPort
      0x00,                               // dropReasonIngress
      0x00,                               // dropReasonMmu
      0x00, 0x00,                         // userMetaField
      0x00,                               // cosColorProb
      0xFF,                               // varLenIndicator
      0x00, 0x02,                         // packetSampledLength = 2
      0xAA, 0xBB,                         // sampled data
  };
  // clang-format on
  buffer[16] = static_cast<uint8_t>(templateId >> 8);
  buffer[17] = static_cast<uint8_t>(templateId & 0xFF);
  auto buf = folly::IOBuf::wrapBuffer(buffer.data(), buffer.size());
  folly::io::Cursor cursor(buf.get());
  EXPECT_THROW(
      XgsPsampModPacket::deserialize(cursor, GetParam()), HdrParseError);
}

TEST_P(XgsPsampAsicTest, DeserializeCapturedPacket) {
  const auto captured = capturedPacketForAsic(GetParam());
  auto buf = folly::IOBuf::wrapBuffer(
      captured.ipfixBytes.data(), captured.ipfixBytes.size());
  folly::io::Cursor cursor(buf.get());
  auto pkt = XgsPsampModPacket::deserialize(cursor, GetParam());

  EXPECT_EQ(pkt.ipfixHeader.version, IPFIX_VERSION);
  EXPECT_EQ(pkt.ipfixHeader.length, 64);
  EXPECT_EQ(pkt.ipfixHeader.exportTime, captured.exportTime);
  EXPECT_EQ(pkt.ipfixHeader.sequenceNumber, 1);
  EXPECT_EQ(pkt.ipfixHeader.observationDomainId, 1);

  EXPECT_EQ(
      pkt.templateHeader.templateId, xgsPsampTemplateIdForAsic(GetParam()));
  EXPECT_EQ(pkt.templateHeader.psampLength, 48);

  EXPECT_EQ(pkt.data.observationTimeNs, captured.observationTimeNs);
  EXPECT_EQ(pkt.data.switchId, 0);
  EXPECT_EQ(pkt.data.egressModPortId, captured.egressModPortId);
  EXPECT_EQ(pkt.data.ingressPort, 1);
  EXPECT_EQ(pkt.data.dropReasonIngress, captured.dropReasonIngress);
  EXPECT_EQ(pkt.data.dropReasonMmu, captured.dropReasonMmu);
  EXPECT_EQ(pkt.data.userMetaField, 0x1234);
  EXPECT_EQ(pkt.data.cosColorProb, captured.cosColorProb);
  EXPECT_EQ(pkt.data.varLenIndicator, XGS_PSAMP_VAR_LEN_INDICATOR);
  EXPECT_EQ(pkt.data.packetSampledLength, 20);
  EXPECT_EQ(pkt.data.sampledPacketData.size(), 20);
  // Inner sampled packet starts with an Ethernet header (IPv6 EtherType).
  EXPECT_EQ(pkt.data.sampledPacketData[12], 0x86);
  EXPECT_EQ(pkt.data.sampledPacketData[13], 0xDD);
  EXPECT_EQ(pkt.size(), captured.ipfixBytes.size());
}

TEST_P(XgsPsampAsicTest, DecodeDropReason) {
  for (const auto& [wire, expected] : dropReasonCases(GetParam())) {
    auto data = decodeDropReason(wire, GetParam());
    const auto label =
        fmt::format("wire bytes 0x{:02X} 0x{:02X}", wire[0], wire[1]);
    EXPECT_EQ(data.dropReasonIngress, expected.ingress) << label;
    EXPECT_EQ(data.dropReasonMmu, expected.mmu) << label;
    EXPECT_EQ(data.dropReasonEgress, expected.egress) << label;
  }
}

TEST(XgsPsampModTest, DecodeTh6IgnoresSecondDropReasonByte) {
  auto data =
      decodeDropReason({0x1A, 0x03}, cfg::AsicType::ASIC_TYPE_TOMAHAWK6);
  EXPECT_EQ(data.dropReasonIngress, 0x1A);
  EXPECT_FALSE(data.dropReasonMmu.has_value());
  EXPECT_FALSE(data.dropReasonEgress.has_value());
}

} // namespace facebook::fboss::psamp
