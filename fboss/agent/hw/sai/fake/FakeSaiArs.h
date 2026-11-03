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

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct FakeArs {
 public:
  FakeArs(
      sai_ars_mode_t mode,
      sai_uint32_t idle_time,
      sai_uint32_t max_flows,
      std::optional<sai_uint32_t> primary_path_quality_threshold = std::nullopt,
      std::optional<sai_uint32_t> alternate_path_cost = std::nullopt,
      std::optional<sai_uint32_t> alternate_path_bias = std::nullopt,
      std::optional<sai_int32_t> next_hop_group_type = std::nullopt,
      std::optional<bool> source_port_prune = std::nullopt,
      std::optional<sai_uint32_t> ecmp_member_count = std::nullopt,
      std::optional<sai_uint32_t> max_alt_members_per_group = std::nullopt,
      std::optional<sai_uint32_t> max_primary_members_per_group = std::nullopt,
      std::optional<sai_uint32_t> common_members_threshold_count = std::nullopt)
      : mode(mode),
        idle_time(idle_time),
        max_flows(max_flows),
        primary_path_quality_threshold(primary_path_quality_threshold),
        alternate_path_cost(alternate_path_cost),
        alternate_path_bias(alternate_path_bias),
        next_hop_group_type(next_hop_group_type),
        source_port_prune(source_port_prune),
        ecmp_member_count(ecmp_member_count),
        max_alt_members_per_group(max_alt_members_per_group),
        max_primary_members_per_group(max_primary_members_per_group),
        common_members_threshold_count(common_members_threshold_count) {}
  sai_ars_mode_t mode;
  sai_uint32_t idle_time;
  sai_uint32_t max_flows;
  std::optional<sai_uint32_t> primary_path_quality_threshold;
  std::optional<sai_uint32_t> alternate_path_cost;
  std::optional<sai_uint32_t> alternate_path_bias;
  std::optional<sai_int32_t> next_hop_group_type;
  std::optional<bool> source_port_prune;
  std::optional<sai_uint32_t> ecmp_member_count;
  std::optional<sai_uint32_t> max_alt_members_per_group;
  std::optional<sai_uint32_t> max_primary_members_per_group;
  std::optional<sai_uint32_t> common_members_threshold_count;
  sai_object_id_t id;
};

// first next hop begins with 1
using FakeArsManager = FakeManager<sai_object_id_t, FakeArs, 1>;

void populate_ars_api(sai_ars_api_t** ars_api);

} // namespace facebook::fboss
