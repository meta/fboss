/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakeSai;

sai_status_t create_ars_profile_fn(
    sai_object_id_t* ars_profile_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  sai_ars_profile_algo_t algo = SAI_ARS_PROFILE_ALGO_EWMA;
  sai_uint32_t sampling_interval = 16;
  sai_uint32_t random_seed = 0;
  bool enable_ipv4 = false;
  bool enable_ipv6 = false;
  bool port_load_past = true;
  sai_uint8_t port_load_past_weight = 16;
  sai_uint32_t load_past_min_val = 1000;
  sai_uint32_t load_past_max_val = 10000;
  std::optional<bool> port_load_future = std::nullopt;
  std::optional<sai_uint8_t> port_load_future_weight = std::nullopt;
  sai_uint32_t load_future_min_val = 60000;
  sai_uint32_t load_future_max_val = 3000000;
  bool port_load_current = true;
  sai_uint8_t port_load_exponent = 2;
  sai_uint32_t load_current_min_val = 500;
  sai_uint32_t load_current_max_val = 5000;
  sai_uint32_t quant_band_0_min_threshold = 0;
  sai_uint32_t quant_band_1_min_threshold = 0;
  sai_uint32_t quant_band_2_min_threshold = 0;
  sai_uint32_t ars_max_groups = 0;
  sai_uint32_t ars_base_index = 0;
  sai_uint32_t ecmp_member_count = 0;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ARS_PROFILE_ATTR_ALGO:
        algo = static_cast<sai_ars_profile_algo_t>(attr_list[i].value.s32);
        break;
      case SAI_ARS_PROFILE_ATTR_SAMPLING_INTERVAL:
        sampling_interval = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_ARS_RANDOM_SEED:
        random_seed = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_ENABLE_IPV4:
        enable_ipv4 = attr_list[i].value.booldata;
        break;
      case SAI_ARS_PROFILE_ATTR_ENABLE_IPV6:
        enable_ipv6 = attr_list[i].value.booldata;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST:
        port_load_past = attr_list[i].value.booldata;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST_WEIGHT:
        port_load_past_weight = attr_list[i].value.u8;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MIN_VAL:
        load_past_min_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MAX_VAL:
        load_past_max_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE:
        port_load_future = attr_list[i].value.booldata;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE_WEIGHT:
        port_load_future_weight = attr_list[i].value.u8;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MIN_VAL:
        load_future_min_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MAX_VAL:
        load_future_max_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_CURRENT:
        port_load_current = attr_list[i].value.booldata;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_EXPONENT:
        port_load_exponent = attr_list[i].value.u8;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MIN_VAL:
        load_current_min_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MAX_VAL:
        load_current_max_val = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_0_MIN_THRESHOLD:
        quant_band_0_min_threshold = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_1_MIN_THRESHOLD:
        quant_band_1_min_threshold = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_2_MIN_THRESHOLD:
        quant_band_2_min_threshold = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_MAX_GROUPS:
        ars_max_groups = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_BASE_INDEX:
        ars_base_index = attr_list[i].value.u32;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_MEMBER_COUNT:
        ecmp_member_count = attr_list[i].value.u32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  *ars_profile_id = fs->arsProfileManager.create(
      algo,
      sampling_interval,
      random_seed,
      enable_ipv4,
      enable_ipv6,
      port_load_past,
      port_load_past_weight,
      load_past_min_val,
      load_past_max_val,
      port_load_future,
      port_load_future_weight,
      load_future_min_val,
      load_future_max_val,
      port_load_current,
      port_load_exponent,
      load_current_min_val,
      load_current_max_val,
      quant_band_0_min_threshold,
      quant_band_1_min_threshold,
      quant_band_2_min_threshold,
      ars_max_groups,
      ars_base_index,
      ecmp_member_count);

  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_ars_profile_fn(sai_object_id_t ars_profile_id) {
  auto fs = FakeSai::getInstance();
  fs->arsProfileManager.remove(ars_profile_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_ars_profile_attribute_fn(
    sai_object_id_t ars_profile_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& arsProfile = fs->arsProfileManager.get(ars_profile_id);
  switch (attr->id) {
    case SAI_ARS_PROFILE_ATTR_ALGO:
      arsProfile.algo = static_cast<sai_ars_profile_algo_t>(attr->value.s32);
      break;
    case SAI_ARS_PROFILE_ATTR_SAMPLING_INTERVAL:
      arsProfile.sampling_interval = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_ARS_RANDOM_SEED:
      arsProfile.random_seed = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_ENABLE_IPV4:
      arsProfile.enable_ipv4 = attr->value.booldata;
      break;
    case SAI_ARS_PROFILE_ATTR_ENABLE_IPV6:
      arsProfile.enable_ipv6 = attr->value.booldata;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST:
      arsProfile.port_load_past = attr->value.booldata;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST_WEIGHT:
      arsProfile.port_load_past_weight = attr->value.u8;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MIN_VAL:
      arsProfile.load_past_min_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MAX_VAL:
      arsProfile.load_past_max_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE:
      arsProfile.port_load_future = attr->value.booldata;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE_WEIGHT:
      arsProfile.port_load_future_weight = attr->value.u8;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MIN_VAL:
      arsProfile.load_future_min_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MAX_VAL:
      arsProfile.load_future_max_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_CURRENT:
      arsProfile.port_load_current = attr->value.booldata;
      break;
    case SAI_ARS_PROFILE_ATTR_PORT_LOAD_EXPONENT:
      arsProfile.port_load_exponent = attr->value.u8;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MIN_VAL:
      arsProfile.load_current_min_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MAX_VAL:
      arsProfile.load_current_max_val = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_QUANT_BAND_0_MIN_THRESHOLD:
      arsProfile.quant_band_0_min_threshold = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_QUANT_BAND_1_MIN_THRESHOLD:
      arsProfile.quant_band_1_min_threshold = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_QUANT_BAND_2_MIN_THRESHOLD:
      arsProfile.quant_band_2_min_threshold = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_MAX_GROUPS:
      arsProfile.ars_max_groups = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_BASE_INDEX:
      arsProfile.ars_base_index = attr->value.u32;
      break;
    case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_MEMBER_COUNT:
      arsProfile.ecmp_member_count = attr->value.u32;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_ars_profile_attribute_fn(
    sai_object_id_t ars_profile_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  const auto& arsProfile = fs->arsProfileManager.get(ars_profile_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_ARS_PROFILE_ATTR_ALGO:
        attr[i].value.u32 = arsProfile.algo;
        break;
      case SAI_ARS_PROFILE_ATTR_SAMPLING_INTERVAL:
        attr[i].value.u32 = arsProfile.sampling_interval;
        break;
      case SAI_ARS_PROFILE_ATTR_ARS_RANDOM_SEED:
        attr[i].value.u32 = arsProfile.random_seed;
        break;
      case SAI_ARS_PROFILE_ATTR_ENABLE_IPV4:
        attr[i].value.booldata = arsProfile.enable_ipv4;
        break;
      case SAI_ARS_PROFILE_ATTR_ENABLE_IPV6:
        attr[i].value.booldata = arsProfile.enable_ipv6;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST:
        attr[i].value.booldata = arsProfile.port_load_past;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_PAST_WEIGHT:
        attr[i].value.u8 = arsProfile.port_load_past_weight;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MIN_VAL:
        attr[i].value.u32 = arsProfile.load_past_min_val;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_PAST_MAX_VAL:
        attr[i].value.u32 = arsProfile.load_past_max_val;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE:
        attr[i].value.booldata = arsProfile.port_load_future.value_or(true);
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_FUTURE_WEIGHT:
        attr[i].value.u8 = arsProfile.port_load_future_weight.value_or(16);
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MIN_VAL:
        attr[i].value.u32 = arsProfile.load_future_min_val;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_FUTURE_MAX_VAL:
        attr[i].value.u32 = arsProfile.load_future_max_val;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_CURRENT:
        attr[i].value.booldata = arsProfile.port_load_current;
        break;
      case SAI_ARS_PROFILE_ATTR_PORT_LOAD_EXPONENT:
        attr[i].value.u8 = arsProfile.port_load_exponent;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MIN_VAL:
        attr[i].value.u32 = arsProfile.load_current_min_val;
        break;
      case SAI_ARS_PROFILE_ATTR_LOAD_CURRENT_MAX_VAL:
        attr[i].value.u32 = arsProfile.load_current_max_val;
        break;
      case SAI_ARS_PROFILE_ATTR_MAX_FLOWS:
        attr[i].value.u32 = arsProfile.max_flows;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_0_MIN_THRESHOLD:
        attr[i].value.u32 = arsProfile.quant_band_0_min_threshold;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_1_MIN_THRESHOLD:
        attr[i].value.u32 = arsProfile.quant_band_1_min_threshold;
        break;
      case SAI_ARS_PROFILE_ATTR_QUANT_BAND_2_MIN_THRESHOLD:
        attr[i].value.u32 = arsProfile.quant_band_2_min_threshold;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_MAX_GROUPS:
        attr[i].value.u32 = arsProfile.ars_max_groups;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_ARS_BASE_INDEX:
        attr[i].value.u32 = arsProfile.ars_base_index;
        break;
      case SAI_ARS_PROFILE_ATTR_EXTENSION_ECMP_MEMBER_COUNT:
        attr[i].value.u32 = arsProfile.ecmp_member_count;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_ars_profile_api_t _ars_profile_api;

void populate_ars_profile_api(sai_ars_profile_api_t** ars_profile_api) {
  _ars_profile_api.create_ars_profile = &create_ars_profile_fn;
  _ars_profile_api.remove_ars_profile = &remove_ars_profile_fn;
  _ars_profile_api.set_ars_profile_attribute = &set_ars_profile_attribute_fn;
  _ars_profile_api.get_ars_profile_attribute = &get_ars_profile_attribute_fn;
  *ars_profile_api = &_ars_profile_api;
}

} // namespace facebook::fboss
