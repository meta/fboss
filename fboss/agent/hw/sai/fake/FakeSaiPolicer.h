// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#pragma once

#include "fboss/agent/hw/sai/fake/FakeManager.h"

namespace facebook::fboss {

struct FakePolicer {
  sai_object_id_t id{};
  sai_int32_t type{};
  sai_int32_t mode{};
  sai_int32_t colorSource{SAI_POLICER_COLOR_SOURCE_BLIND};
  sai_uint64_t cir{};
  sai_uint64_t cbs{};
  sai_uint64_t pbs{};
  sai_int32_t greenPacketAction{SAI_PACKET_ACTION_FORWARD};
  sai_int32_t yellowPacketAction{SAI_PACKET_ACTION_FORWARD};
  sai_int32_t redPacketAction{SAI_PACKET_ACTION_DROP};
};

using FakePolicerManager = FakeManager<sai_object_id_t, FakePolicer, 1>;

void populate_policer_api(sai_policer_api_t** policer_api);

} // namespace facebook::fboss
