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

#include <optional>

extern "C" {
#include <sai.h>
}

#include "fboss/agent/hw/sai/api/SaiVersion.h"

namespace facebook::fboss {

struct FakeNextHop {
 public:
  FakeNextHop(
      sai_next_hop_type_t type,
      const folly::IPAddress& ip,
      sai_object_id_t routerInterfaceId,
      std::vector<sai_uint32_t> labels,
      bool disableTtlDecrement)
      : type(type),
        ip(ip),
        routerInterfaceId(routerInterfaceId),
        labelStack(std::move(labels)),
        disableTtlDecrement(disableTtlDecrement) {}
  sai_next_hop_type_t type;
  folly::IPAddress ip;
  sai_object_id_t routerInterfaceId;
  sai_object_id_t id;
  std::vector<sai_uint32_t> labelStack;
  sai_int32_t outsegType{SAI_OUTSEG_TYPE_SWAP};
  bool disableTtlDecrement{false};
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  sai_object_id_t tunnelId{SAI_NULL_OBJECT_ID};
  sai_object_id_t srv6SidlistId{SAI_NULL_OBJECT_ID};
  std::optional<folly::MacAddress> overlayDstMac;
#endif
};

// first next hop begins with 1
using FakeNextHopManager = FakeManager<sai_object_id_t, FakeNextHop, 1>;

void populate_next_hop_api(sai_next_hop_api_t** next_hop_api);

} // namespace facebook::fboss
