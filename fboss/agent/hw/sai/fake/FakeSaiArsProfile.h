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

#include <optional>

extern "C" {
#include <sai.h>
}

namespace facebook::fboss {

struct FakeArsProfile {
 public:
  FakeArsProfile(
      sai_ars_profile_algo_t algo,
      sai_uint32_t sampling_interval,
      sai_uint32_t random_seed,
      bool enable_ipv4,
      bool enable_ipv6,
      bool port_load_past,
      sai_uint8_t port_load_past_weight,
      sai_uint32_t load_past_min_val,
      sai_uint32_t load_past_max_val,
      std::optional<bool> port_load_future,
      std::optional<sai_uint8_t> port_load_future_weight,
      sai_uint32_t load_future_min_val,
      sai_uint32_t load_future_max_val,
      bool port_load_current,
      sai_uint8_t port_load_exponent,
      sai_uint32_t load_current_min_val,
      sai_uint32_t load_current_max_val,
      sai_uint32_t quant_band_0_min_threshold,
      sai_uint32_t quant_band_1_min_threshold,
      sai_uint32_t quant_band_2_min_threshold,
      sai_uint32_t ars_max_groups,
      sai_uint32_t ars_base_index,
      sai_uint32_t ecmp_member_count)
      : algo(algo),
        sampling_interval(sampling_interval),
        random_seed(random_seed),
        enable_ipv4(enable_ipv4),
        enable_ipv6(enable_ipv6),
        port_load_past(port_load_past),
        port_load_past_weight(port_load_past_weight),
        load_past_min_val(load_past_min_val),
        load_past_max_val(load_past_max_val),
        port_load_future(port_load_future),
        port_load_future_weight(port_load_future_weight),
        load_future_min_val(load_future_min_val),
        load_future_max_val(load_future_max_val),
        port_load_current(port_load_current),
        port_load_exponent(port_load_exponent),
        load_current_min_val(load_current_min_val),
        load_current_max_val(load_current_max_val),
        quant_band_0_min_threshold(quant_band_0_min_threshold),
        quant_band_1_min_threshold(quant_band_1_min_threshold),
        quant_band_2_min_threshold(quant_band_2_min_threshold),
        ars_max_groups(ars_max_groups),
        ars_base_index(ars_base_index),
        ecmp_member_count(ecmp_member_count) {}
  sai_ars_profile_algo_t algo;
  sai_uint32_t sampling_interval;
  sai_uint32_t random_seed;
  bool enable_ipv4;
  bool enable_ipv6;
  bool port_load_past;
  sai_uint8_t port_load_past_weight;
  sai_uint32_t load_past_min_val;
  sai_uint32_t load_past_max_val;
  std::optional<bool> port_load_future;
  std::optional<sai_uint8_t> port_load_future_weight;
  sai_uint32_t load_future_min_val;
  sai_uint32_t load_future_max_val;
  bool port_load_current;
  sai_uint8_t port_load_exponent;
  sai_uint32_t load_current_min_val;
  sai_uint32_t load_current_max_val;
  sai_uint32_t max_flows = 32768;
  sai_uint32_t quant_band_0_min_threshold;
  sai_uint32_t quant_band_1_min_threshold;
  sai_uint32_t quant_band_2_min_threshold;
  sai_uint32_t ars_max_groups;
  sai_uint32_t ars_base_index;
  sai_uint32_t ecmp_member_count;
  sai_object_id_t id;
};

// first next hop begins with 1
using FakeArsProfileManager = FakeManager<sai_object_id_t, FakeArsProfile, 1>;

void populate_ars_profile_api(sai_ars_profile_api_t** ars_profile_api);

} // namespace facebook::fboss
