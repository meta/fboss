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

#include <optional>

#include "fboss/agent/hw/sai/fake/FakeManager.h"

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

class FakeNextHopGroupMember {
 public:
  FakeNextHopGroupMember(
      sai_object_id_t nextHopGroupId,
      sai_object_id_t nextHopId,
      std::optional<sai_uint32_t> weight,
      sai_int32_t configuredRole = 0 /* PRIMARY */,
      sai_object_id_t monitoredObject = SAI_NULL_OBJECT_ID)
      : nextHopGroupId(nextHopGroupId),
        nextHopId(nextHopId),
        weight(weight),
        configuredRole(configuredRole),
        monitoredObject(monitoredObject) {}
  sai_object_id_t nextHopGroupId;
  sai_object_id_t nextHopId;
  sai_object_id_t id;
  std::optional<sai_uint32_t> weight;
  sai_int32_t configuredRole;
  sai_object_id_t monitoredObject;
};

class FakeNextHopGroup {
 public:
  FakeNextHopGroup(int32_t type) : type(type) {}
  FakeNextHopGroup(
      int32_t type,
      sai_object_id_t ars_id,
      sai_int32_t hash_algorithm,
      bool hierarchical_nexthop,
      std::optional<bool> split_horizon_enable = std::nullopt)
      : type(type),
        ars_id(ars_id),
        hash_algorithm(hash_algorithm),
        hierarchical_nexthop(hierarchical_nexthop),
        split_horizon_enable(split_horizon_enable) {}
  int32_t type;
  sai_object_id_t ars_id;
  sai_int32_t hash_algorithm;
  bool hierarchical_nexthop;
  std::optional<bool> split_horizon_enable;
  sai_uint64_t ars_fail_pkt_count{0};
  sai_uint64_t ars_port_reassign_count{0};
  sai_object_id_t id;
  FakeManager<sai_object_id_t, FakeNextHopGroupMember>& fm() {
    return fm_;
  }
  const FakeManager<sai_object_id_t, FakeNextHopGroupMember>& fm() const {
    return fm_;
  }

 private:
  FakeManager<sai_object_id_t, FakeNextHopGroupMember> fm_;
};

using FakeNextHopGroupManager =
    FakeManagerWithMembers<FakeNextHopGroup, FakeNextHopGroupMember>;

void populate_next_hop_group_api(sai_next_hop_group_api_t** next_hop_group_api);

} // namespace facebook::fboss
