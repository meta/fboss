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

#include <fmt/core.h>
#include <folly/logging/xlog.h>

#include "fboss/agent/packet/HdrParseError.h"

using namespace folly::io;

namespace facebook::fboss::psamp {

namespace {

// Drop reason wire layout, per chip.
//
//   TH5  byte 0 ingress code, byte 1 MMU code, no egress slot. Zero means that
//        pipeline was not involved, so a real code 0 cannot be told from one.
//
//   TH6  byte 0 holds pipeline and code, byte 1 is metadata. The range names
//        the pipeline, so a zero code is still a decoded value:
//          0x00-0x78 ingress, 0x90-0x95 MMU, 0x98-0xFF egress
//        Egress has no documented upper bound; the ranges are SDK-programmed.
//        A byte between ranges is not a code any pipeline can raise.

enum class DropPipeline { Ingress, Mmu, Egress };

void setDropReason(XgsPsampData& data, DropPipeline pipeline, uint8_t code) {
  switch (pipeline) {
    case DropPipeline::Ingress:
      data.dropReasonIngress = code;
      return;
    case DropPipeline::Mmu:
      data.dropReasonMmu = code;
      return;
    case DropPipeline::Egress:
      data.dropReasonEgress = code;
      return;
  }
}

constexpr uint8_t kTh6IngressDropReasonMin = 0x00;
constexpr uint8_t kTh6IngressDropReasonMax = 0x78;
constexpr uint8_t kTh6MmuDropReasonMin = 0x90;
constexpr uint8_t kTh6MmuDropReasonMax = 0x95;
constexpr uint8_t kTh6EgressDropReasonMin = 0x98;
constexpr uint8_t kTh6EgressDropReasonMax = 0xFF;

// A function, so comparing against 0xFF is not a tautology the compiler warns
// on.
bool inDropReasonRange(uint8_t wireByte, uint8_t min, uint8_t max) {
  return wireByte >= min && wireByte <= max;
}

void deserializeDropReasonTh6(Cursor& cursor, XgsPsampData& data) {
  const uint8_t wireByte = cursor.read<uint8_t>();
  cursor.skip(1);
  if (inDropReasonRange(
          wireByte, kTh6EgressDropReasonMin, kTh6EgressDropReasonMax)) {
    setDropReason(
        data, DropPipeline::Egress, wireByte - kTh6EgressDropReasonMin);
  } else if (inDropReasonRange(
                 wireByte, kTh6MmuDropReasonMin, kTh6MmuDropReasonMax)) {
    setDropReason(data, DropPipeline::Mmu, wireByte - kTh6MmuDropReasonMin);
  } else if (inDropReasonRange(
                 wireByte,
                 kTh6IngressDropReasonMin,
                 kTh6IngressDropReasonMax)) {
    setDropReason(
        data, DropPipeline::Ingress, wireByte - kTh6IngressDropReasonMin);
  } else {
    XLOG_EVERY_N(WARNING, 1000) << fmt::format(
        "TH6 PSAMP: drop reason byte 0x{:02X} matches no pipeline range",
        wireByte);
  }
}

void deserializeDropReason(
    Cursor& cursor,
    XgsPsampData& data,
    cfg::AsicType asicType) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5: {
      const uint8_t ingress = cursor.read<uint8_t>();
      const uint8_t mmu = cursor.read<uint8_t>();
      if (ingress != 0) {
        setDropReason(data, DropPipeline::Ingress, ingress);
      }
      if (mmu != 0) {
        setDropReason(data, DropPipeline::Mmu, mmu);
      }
      return;
    }
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      deserializeDropReasonTh6(cursor, data);
      return;
    default:
      throw HdrParseError(
          fmt::format(
              "Unsupported ASIC type for XGS PSAMP deserialization: {}",
              static_cast<int>(asicType)));
  }
}

} // namespace

uint16_t xgsPsampTemplateIdForAsic(cfg::AsicType asicType) {
  // NOLINTNEXTLINE(clang-diagnostic-switch-enum)
  switch (asicType) {
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK5:
      return XGS_PSAMP_TEMPLATE_ID_TH5;
    case cfg::AsicType::ASIC_TYPE_TOMAHAWK6:
      return XGS_PSAMP_TEMPLATE_ID_TH6;
    default:
      throw HdrParseError(
          fmt::format(
              "No XGS PSAMP template ID for ASIC type {}",
              static_cast<int>(asicType)));
  }
}

uint32_t XgsPsampTemplateHeader::size() const {
  return 4;
}

XgsPsampTemplateHeader XgsPsampTemplateHeader::deserialize(
    Cursor& cursor,
    cfg::AsicType asicType) {
  if (cursor.totalLength() < 4) {
    throw HdrParseError(
        "PSAMP template header too small: need 4 bytes, have " +
        std::to_string(cursor.totalLength()));
  }

  XgsPsampTemplateHeader hdr;
  hdr.templateId = cursor.readBE<uint16_t>();
  const uint16_t expectedId = xgsPsampTemplateIdForAsic(asicType);
  if (hdr.templateId != expectedId) {
    throw HdrParseError(
        fmt::format(
            "Unexpected PSAMP template ID: expected 0x{:04X}, got 0x{:04X}",
            expectedId,
            hdr.templateId));
  }
  hdr.psampLength = cursor.readBE<uint16_t>();
  return hdr;
}

uint32_t XgsPsampData::size() const {
  return static_cast<uint32_t>(24 + sampledPacketData.size());
}

XgsPsampData XgsPsampData::deserialize(Cursor& cursor, cfg::AsicType asicType) {
  if (cursor.totalLength() < 24) {
    throw HdrParseError(
        "PSAMP data too small: need 24 bytes, have " +
        std::to_string(cursor.totalLength()));
  }

  XgsPsampData data;
  data.observationTimeNs = cursor.readBE<uint64_t>();
  data.switchId = cursor.readBE<uint32_t>();
  data.egressModPortId = cursor.readBE<uint16_t>();
  data.ingressPort = cursor.readBE<uint16_t>();
  deserializeDropReason(cursor, data, asicType);
  data.userMetaField = cursor.readBE<uint16_t>();
  data.cosColorProb = cursor.read<uint8_t>();
  data.varLenIndicator = cursor.read<uint8_t>();
  if (data.varLenIndicator != XGS_PSAMP_VAR_LEN_INDICATOR) {
    throw HdrParseError(
        "Invalid PSAMP variable length indicator: expected 0xFF, got 0x" +
        fmt::format("{:02X}", data.varLenIndicator));
  }
  data.packetSampledLength = cursor.readBE<uint16_t>();
  if (cursor.totalLength() < data.packetSampledLength) {
    throw HdrParseError(
        "PSAMP sampled packet data too small: need " +
        std::to_string(data.packetSampledLength) + " bytes, have " +
        std::to_string(cursor.totalLength()));
  }
  data.sampledPacketData.resize(data.packetSampledLength);
  cursor.pull(data.sampledPacketData.data(), data.packetSampledLength);
  return data;
}

uint32_t XgsPsampModPacket::size() const {
  return ipfixHeader.size() + templateHeader.size() + data.size();
}

XgsPsampModPacket XgsPsampModPacket::deserialize(
    Cursor& cursor,
    cfg::AsicType asicType) {
  XgsPsampModPacket pkt;
  pkt.ipfixHeader = IpfixHeader::deserialize(cursor);
  pkt.templateHeader = XgsPsampTemplateHeader::deserialize(cursor, asicType);
  pkt.data = XgsPsampData::deserialize(cursor, asicType);
  if (pkt.ipfixHeader.length != pkt.size()) {
    throw HdrParseError(
        "IPFIX length mismatch: header says " +
        std::to_string(pkt.ipfixHeader.length) + " but actual size is " +
        std::to_string(pkt.size()));
  }
  return pkt;
}

} // namespace facebook::fboss::psamp
