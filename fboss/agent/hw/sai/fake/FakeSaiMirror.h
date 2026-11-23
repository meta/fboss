/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/hw/sai/fake/FakeManager.h"

#include <folly/IPAddress.h>
#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct FakeMirror {
 public:
  FakeMirror(sai_mirror_session_type_t type, sai_object_id_t monitorPort)
      : type(type), monitorPort(monitorPort) {}
  FakeMirror(
      sai_mirror_session_type_t type,
      sai_object_id_t monitorPort,
      sai_erspan_encapsulation_type_t erspanEncapType,
      uint8_t tos,
      const folly::IPAddress& srcIp,
      const folly::IPAddress& dstIp,
      const folly::MacAddress& srcMac,
      const folly::MacAddress& dstMac,
      uint8_t ipHeaderVersion,
      sai_uint16_t greProtocolType,
      uint8_t ttl,
      sai_uint16_t truncateSize,
      sai_uint32_t sampleRate)
      : type(type),
        monitorPort(monitorPort),
        erspanEncapType(erspanEncapType),
        tos(tos),
        srcIp(srcIp),
        dstIp(dstIp),
        srcMac(srcMac),
        dstMac(dstMac),
        ipHeaderVersion(ipHeaderVersion),
        greProtocolType(greProtocolType),
        ttl(ttl),
        truncateSize(truncateSize),
        sampleRate(sampleRate) {}
  FakeMirror(
      sai_mirror_session_type_t type,
      sai_object_id_t monitorPort,
      sai_uint16_t tos,
      const folly::IPAddress& srcIp,
      const folly::IPAddress& dstIp,
      const folly::MacAddress& srcMac,
      const folly::MacAddress& dstMac,
      sai_uint8_t ipHeaderVersion,
      sai_uint16_t udpSrcPort,
      sai_uint16_t udpDstPort,
      uint8_t ttl,
      sai_uint16_t truncateSize,
      sai_uint32_t sampleRate)
      : type(type),
        monitorPort(monitorPort),
        tos(tos),
        srcIp(srcIp),
        dstIp(dstIp),
        srcMac(srcMac),
        dstMac(dstMac),
        ipHeaderVersion(ipHeaderVersion),
        udpSrcPort(udpSrcPort),
        udpDstPort(udpDstPort),
        ttl(ttl),
        truncateSize(truncateSize),
        sampleRate(sampleRate) {}
  sai_object_id_t id;
  sai_int32_t type;
  sai_object_id_t monitorPort;
  sai_object_id_t policer{SAI_NULL_OBJECT_ID};
  sai_int32_t erspanEncapType;
  uint8_t tos;
  folly::IPAddress srcIp;
  folly::IPAddress dstIp;
  folly::MacAddress srcMac;
  folly::MacAddress dstMac;
  sai_uint8_t ipHeaderVersion;
  sai_uint16_t greProtocolType;
  sai_uint16_t udpSrcPort;
  sai_uint16_t udpDstPort;
  uint8_t ttl;
  sai_uint16_t truncateSize;
  sai_uint32_t sampleRate;
};

using FakeMirrorManager = FakeManager<sai_object_id_t, FakeMirror>;

void populate_mirror_api(sai_mirror_api_t** mirror_api);

} // namespace facebook::fboss
