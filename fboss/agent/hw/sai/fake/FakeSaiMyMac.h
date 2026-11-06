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

#include <folly/MacAddress.h>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct FakeMyMac {
 public:
  FakeMyMac(
      const folly::MacAddress& macAddress,
      const folly::MacAddress& macAddressMask,
      sai_uint16_t vlanId)
      : macAddress(macAddress),
        macAddressMask(macAddressMask),
        vlanId(vlanId) {}
  sai_object_id_t id{SAI_NULL_OBJECT_ID};
  folly::MacAddress macAddress;
  folly::MacAddress macAddressMask;
  sai_uint16_t vlanId;
};

using FakeMyMacManager = FakeManager<sai_object_id_t, FakeMyMac>;

void populate_my_mac_api(sai_my_mac_api_t** my_mac_api);

} // namespace facebook::fboss
