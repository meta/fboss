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

struct FakeSamplePacket {
 public:
  FakeSamplePacket(
      sai_uint32_t sampleRate,
      sai_samplepacket_type_t type,
      sai_samplepacket_mode_t mode)
      : sampleRate(sampleRate), type(type), mode(mode) {}
  sai_object_id_t id;
  uint32_t sampleRate;
  sai_samplepacket_type_t type;
  sai_samplepacket_mode_t mode;
};

// Start at 1 so a port's sample packet OID is never SAI_NULL_OBJECT_ID
using FakeSamplePacketManager =
    FakeManager<sai_object_id_t, FakeSamplePacket, 1>;

void populate_samplepacket_api(sai_samplepacket_api_t** samplepacket_api);

} // namespace facebook::fboss
