/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/api/AddressUtil.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"
#include "fboss/agent/hw/sai/fake/FakeSaiPort.h"

#include <optional>

using facebook::fboss::FakePort;
using facebook::fboss::FakeSai;

sai_status_t create_mirror_session_fn(
    sai_object_id_t* mirror_session_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_mirror_session_type_t> type;
  std::optional<sai_object_id_t> monitorPort;
  std::optional<folly::IPAddress> srcIp;
  std::optional<folly::IPAddress> dstIp;
  std::optional<folly::MacAddress> srcMac;
  std::optional<folly::MacAddress> dstMac;
  std::optional<sai_uint16_t> udpSrcPort;
  std::optional<sai_uint16_t> udpDstPort;
  std::optional<uint8_t> ttl;
  std::optional<uint8_t> tos;
  std::optional<sai_erspan_encapsulation_type_t> erspanEncapType;
  std::optional<sai_uint16_t> greProtocolType;
  std::optional<sai_uint16_t> truncateSize;
  std::optional<sai_uint8_t> ipHeaderVersion;
  std::optional<sai_uint32_t> sampleRate;
  sai_object_id_t policer{SAI_NULL_OBJECT_ID};

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_MIRROR_SESSION_ATTR_POLICER:
        policer = attr_list[i].value.oid;
        if (policer != SAI_NULL_OBJECT_ID &&
            !fs->policerManager.exists(policer)) {
          return SAI_STATUS_INVALID_PARAMETER;
        }
        break;
      case SAI_MIRROR_SESSION_ATTR_TYPE:
        type = static_cast<sai_mirror_session_type_t>(attr_list[i].value.s32);
        break;
      case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
        monitorPort = attr_list[i].value.oid;
        break;
      case SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS:
        srcIp = facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
        break;
      case SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS:
        dstIp = facebook::fboss::fromSaiIpAddress(attr_list[i].value.ipaddr);
        break;
      case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
        srcMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
        dstMac = facebook::fboss::fromSaiMacAddress(attr_list[i].value.mac);
        break;
      case SAI_MIRROR_SESSION_ATTR_UDP_SRC_PORT:
        udpSrcPort = attr_list[i].value.u16;
        break;
      case SAI_MIRROR_SESSION_ATTR_UDP_DST_PORT:
        udpDstPort = attr_list[i].value.u16;
        break;
      case SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE:
        erspanEncapType = static_cast<sai_erspan_encapsulation_type_t>(
            attr_list[i].value.s32);
        break;
      case SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE:
        truncateSize = attr_list[i].value.u16;
        break;
      case SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION:
        ipHeaderVersion = attr_list[i].value.u8;
        break;
      case SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE:
        greProtocolType = attr_list[i].value.u16;
        break;
      case SAI_MIRROR_SESSION_ATTR_TOS:
        tos = attr_list[i].value.u8;
        break;
      case SAI_MIRROR_SESSION_ATTR_TTL:
        ttl = attr_list[i].value.u8;
        break;
      case SAI_MIRROR_SESSION_ATTR_SAMPLE_RATE:
        sampleRate = attr_list[i].value.u32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }

  if (!type || !monitorPort) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  if (type == SAI_MIRROR_SESSION_TYPE_LOCAL) {
    *mirror_session_id =
        fs->mirrorManager.create(type.value(), monitorPort.value());
  } else if (type == SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE) {
    if (!srcIp || !dstIp || !srcMac || !dstMac || !tos || !erspanEncapType ||
        !greProtocolType || !ipHeaderVersion) {
      return SAI_STATUS_INVALID_PARAMETER;
    }
    *mirror_session_id = fs->mirrorManager.create(
        type.value(),
        monitorPort.value(),
        erspanEncapType.value(),
        tos.value(),
        srcIp.value(),
        dstIp.value(),
        srcMac.value(),
        dstMac.value(),
        ipHeaderVersion.value(),
        greProtocolType.value(),
        ttl.has_value() ? ttl.value() : (uint8_t)0,
        truncateSize.has_value() ? truncateSize.value() : (uint16_t)0,
        sampleRate.has_value() ? sampleRate.value() : (uint32_t)0);
  } else if (type == SAI_MIRROR_SESSION_TYPE_SFLOW) {
    if (!srcIp || !dstIp || !srcMac || !dstMac || !tos || !udpSrcPort ||
        !udpDstPort) {
      return SAI_STATUS_INVALID_PARAMETER;
    }
    *mirror_session_id = fs->mirrorManager.create(
        type.value(),
        monitorPort.value(),
        tos.value(),
        srcIp.value(),
        dstIp.value(),
        srcMac.value(),
        dstMac.value(),
        ipHeaderVersion.value(),
        udpSrcPort.value(),
        udpDstPort.value(),
        ttl.has_value() ? ttl.value() : (uint8_t)0,
        truncateSize.has_value() ? truncateSize.value() : (uint16_t)0,
        sampleRate.has_value() ? sampleRate.value() : (uint32_t)0);
  } else {
    return SAI_STATUS_INVALID_PARAMETER;
  }

  fs->mirrorManager.get(*mirror_session_id).policer = policer;
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_mirror_session_fn(sai_object_id_t mirror_session_id) {
  auto fs = FakeSai::getInstance();
  fs->mirrorManager.remove(mirror_session_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_mirror_session_attribute_fn(
    sai_object_id_t mirror_session_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& mirrorSession = fs->mirrorManager.get(mirror_session_id);
  switch (attr->id) {
    case SAI_MIRROR_SESSION_ATTR_POLICER:
      if (attr->value.oid != SAI_NULL_OBJECT_ID &&
          !fs->policerManager.exists(attr->value.oid)) {
        return SAI_STATUS_INVALID_PARAMETER;
      }
      mirrorSession.policer = attr->value.oid;
      break;
    case SAI_MIRROR_SESSION_ATTR_TYPE:
      return SAI_STATUS_INVALID_PARAMETER;
    case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
      mirrorSession.monitorPort = attr->value.oid;
      break;
    case SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS:
      mirrorSession.srcIp =
          facebook::fboss::fromSaiIpAddress(attr->value.ipaddr);
      break;
    case SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS:
      mirrorSession.dstIp =
          facebook::fboss::fromSaiIpAddress(attr->value.ipaddr);
      break;
    case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
      mirrorSession.srcMac =
          facebook::fboss::fromSaiMacAddress(attr->value.mac);
      break;
    case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
      mirrorSession.dstMac =
          facebook::fboss::fromSaiMacAddress(attr->value.mac);
      break;
    case SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION:
      mirrorSession.ipHeaderVersion = attr->value.u8;
      break;
    case SAI_MIRROR_SESSION_ATTR_UDP_SRC_PORT:
      mirrorSession.udpSrcPort = attr->value.u16;
      break;
    case SAI_MIRROR_SESSION_ATTR_UDP_DST_PORT:
      mirrorSession.udpDstPort = attr->value.u16;
      break;
    case SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE:
      return SAI_STATUS_INVALID_PARAMETER;
    case SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE:
      mirrorSession.truncateSize = attr->value.u16;
      break;
    case SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE:
      mirrorSession.greProtocolType = attr->value.u16;
      break;
    case SAI_MIRROR_SESSION_ATTR_TOS:
      mirrorSession.tos = attr->value.u8;
      break;
    case SAI_MIRROR_SESSION_ATTR_TTL:
      mirrorSession.ttl = attr->value.u8;
      break;
    case SAI_MIRROR_SESSION_ATTR_SAMPLE_RATE:
      mirrorSession.sampleRate = attr->value.u32;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_mirror_session_attribute_fn(
    sai_object_id_t mirror_session_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  const auto& mirrorSession = fs->mirrorManager.get(mirror_session_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_MIRROR_SESSION_ATTR_POLICER:
        attr_list[i].value.oid = mirrorSession.policer;
        break;
      case SAI_MIRROR_SESSION_ATTR_TYPE:
        attr_list[i].value.s32 = static_cast<int32_t>(mirrorSession.type);
        break;
      case SAI_MIRROR_SESSION_ATTR_MONITOR_PORT:
        attr_list[i].value.oid = mirrorSession.monitorPort;
        break;
      case SAI_MIRROR_SESSION_ATTR_SRC_IP_ADDRESS:
        attr_list[i].value.ipaddr =
            facebook::fboss::toSaiIpAddress(mirrorSession.srcIp);
        break;
      case SAI_MIRROR_SESSION_ATTR_DST_IP_ADDRESS:
        attr_list[i].value.ipaddr =
            facebook::fboss::toSaiIpAddress(mirrorSession.dstIp);
        break;
      case SAI_MIRROR_SESSION_ATTR_SRC_MAC_ADDRESS:
        facebook::fboss::toSaiMacAddress(
            mirrorSession.srcMac, attr_list[i].value.mac);
        break;
      case SAI_MIRROR_SESSION_ATTR_DST_MAC_ADDRESS:
        facebook::fboss::toSaiMacAddress(
            mirrorSession.dstMac, attr_list[i].value.mac);
        break;
      case SAI_MIRROR_SESSION_ATTR_IPHDR_VERSION:
        attr_list[i].value.u8 = mirrorSession.ipHeaderVersion;
        break;
      case SAI_MIRROR_SESSION_ATTR_UDP_SRC_PORT:
        attr_list[i].value.u16 = mirrorSession.udpSrcPort;
        break;
      case SAI_MIRROR_SESSION_ATTR_UDP_DST_PORT:
        attr_list[i].value.u16 = mirrorSession.udpDstPort;
        break;
      case SAI_MIRROR_SESSION_ATTR_ERSPAN_ENCAPSULATION_TYPE:
        attr_list[i].value.s32 =
            static_cast<int32_t>(mirrorSession.erspanEncapType);
        break;
      case SAI_MIRROR_SESSION_ATTR_TRUNCATE_SIZE:
        attr_list[i].value.u16 = mirrorSession.truncateSize;
        break;
      case SAI_MIRROR_SESSION_ATTR_GRE_PROTOCOL_TYPE:
        attr_list[i].value.u16 = mirrorSession.greProtocolType;
        break;
      case SAI_MIRROR_SESSION_ATTR_TOS:
        attr_list[i].value.u8 = mirrorSession.tos;
        break;
      case SAI_MIRROR_SESSION_ATTR_TTL:
        attr_list[i].value.u8 = mirrorSession.ttl;
        break;
      case SAI_MIRROR_SESSION_ATTR_SAMPLE_RATE:
        attr_list[i].value.u32 = mirrorSession.sampleRate;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_mirror_api_t _mirror_api;

void populate_mirror_api(sai_mirror_api_t** mirror_api) {
  _mirror_api.create_mirror_session = &create_mirror_session_fn;
  _mirror_api.remove_mirror_session = &remove_mirror_session_fn;
  _mirror_api.set_mirror_session_attribute = &set_mirror_session_attribute_fn;
  _mirror_api.get_mirror_session_attribute = &get_mirror_session_attribute_fn;
  *mirror_api = &_mirror_api;
}

} // namespace facebook::fboss
