/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiBuffer.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeQueue;
using facebook::fboss::FakeSai;

sai_status_t create_buffer_pool_fn(
    sai_object_id_t* buffer_pool_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_buffer_pool_type_t> poolType;
  std::optional<sai_uint64_t> poolSize;
  std::optional<sai_buffer_pool_threshold_mode_t> threshMode;
  std::optional<sai_uint64_t> xoffSize;
  std::optional<sai_uint64_t> reservedBytes;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_BUFFER_POOL_ATTR_TYPE:
        poolType = static_cast<sai_buffer_pool_type_t>(attr_list[i].value.s32);
        break;
      case SAI_BUFFER_POOL_ATTR_SIZE:
        poolSize = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE:
        threshMode = static_cast<sai_buffer_pool_threshold_mode_t>(
            attr_list[i].value.s32);
        break;
      case SAI_BUFFER_POOL_ATTR_XOFF_SIZE:
        xoffSize = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_POOL_ATTR_RESERVED_BUFFER_SIZE:
        reservedBytes = attr_list[i].value.u64;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!poolType || !poolSize || !threshMode) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *buffer_pool_id = fs->bufferPoolManager.create(
      poolType.value(),
      poolSize.value(),
      threshMode.value(),
      xoffSize,
      reservedBytes);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_buffer_pool_fn(sai_object_id_t pool_id) {
  auto fs = FakeSai::getInstance();
  fs->bufferPoolManager.remove(pool_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_buffer_pool_attribute_fn(
    sai_object_id_t buffer_pool_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& pool = fs->bufferPoolManager.get(buffer_pool_id);
  // Mirrors create_buffer_pool_fn / get_buffer_pool_attribute_fn: settable for
  // everything FakeBufferPool models. SaiBufferManager reprograms an existing
  // pool via setObject() -> setAttributes(), so a stub here turns a pool resize
  // (e.g. XOFF_SIZE once a headroom is configured) into a SaiApiError that
  // terminates the agent from the oper-delta thread. TYPE is create-only.
  switch (attr->id) {
    case SAI_BUFFER_POOL_ATTR_SIZE:
      pool.size = attr->value.u64;
      break;
    case SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE:
      pool.threshMode =
          static_cast<sai_buffer_pool_threshold_mode_t>(attr->value.s32);
      break;
    case SAI_BUFFER_POOL_ATTR_XOFF_SIZE:
      pool.xoffSize = attr->value.u64;
      break;
    case SAI_BUFFER_POOL_ATTR_RESERVED_BUFFER_SIZE:
      pool.reservedBytes = attr->value.u64;
      break;
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_buffer_pool_attribute_fn(
    sai_object_id_t pool_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto pool = fs->bufferPoolManager.get(pool_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_BUFFER_POOL_ATTR_TYPE:
        attr[i].value.s32 = pool.poolType;
        break;
      case SAI_BUFFER_POOL_ATTR_SIZE:
        attr[i].value.u64 = pool.size;
        break;
      case SAI_BUFFER_POOL_ATTR_THRESHOLD_MODE:
        attr[i].value.s32 = pool.threshMode;
        break;
      case SAI_BUFFER_POOL_ATTR_XOFF_SIZE:
        attr[i].value.u64 = pool.xoffSize ? pool.xoffSize.value() : 0;
        break;
      case SAI_BUFFER_POOL_ATTR_RESERVED_BUFFER_SIZE:
        attr[i].value.u64 = pool.reservedBytes ? pool.reservedBytes.value() : 0;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_buffer_pool_stats_fn(
    sai_object_id_t /*queeue*/,
    uint32_t num_of_counters,
    const sai_stat_id_t* /*counter_ids*/,
    uint64_t* counters) {
  for (auto i = 0; i < num_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}
/*
 * In fake sai there isn't a dataplane, so all stats
 * stay at 0. Leverage the corresponding non _ext
 * stats fn to get the stats. If stats are always 0,
 * modes (READ, READ_AND_CLEAR) don't matter
 */
sai_status_t get_buffer_pool_stats_ext_fn(
    sai_object_id_t buffer_pool,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_buffer_pool_stats_fn(
      buffer_pool, num_of_counters, counter_ids, counters);
}
/*
 *  noop clear stats API. Since fake doesnt have a
 *  dataplane stats are always set to 0, so
 *  no need to clear them
 */
sai_status_t clear_buffer_pool_stats_fn(
    sai_object_id_t /* buffer_pool_id */,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids) {
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_buffer_profile_fn(
    sai_object_id_t* buffer_profile_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_object_id_t> poolId;
  std::optional<sai_uint64_t> reservedBytes;
  std::optional<sai_buffer_profile_threshold_mode_t> threshMode;
  std::optional<sai_int8_t> dynamicThreshold;
  std::optional<sai_uint64_t> staticThreshold;
  std::optional<sai_uint64_t> xoffTh;
  std::optional<sai_uint64_t> xonTh;
  std::optional<sai_uint64_t> xonOffsetTh;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_BUFFER_PROFILE_ATTR_POOL_ID:
        poolId = attr_list[i].value.oid;
        break;
      case SAI_BUFFER_PROFILE_ATTR_RESERVED_BUFFER_SIZE:
        reservedBytes = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE:
        threshMode = static_cast<sai_buffer_profile_threshold_mode_t>(
            attr_list[i].value.s32);
        break;
      case SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
        dynamicThreshold = attr_list[i].value.s8;
        break;
      case SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH:
        staticThreshold = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XOFF_TH:
        xoffTh = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XON_TH:
        xonTh = attr_list[i].value.u64;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH:
        xonOffsetTh = attr_list[i].value.u64;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!poolId) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *buffer_profile_id = fs->bufferProfileManager.create(
      poolId.value(),
      reservedBytes,
      threshMode,
      dynamicThreshold,
      staticThreshold,
      xoffTh,
      xonTh,
      xonOffsetTh);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_buffer_profile_attribute_fn(
    sai_object_id_t profile_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& profile = fs->bufferProfileManager.get(profile_id);
  switch (attr->id) {
    case SAI_BUFFER_PROFILE_ATTR_RESERVED_BUFFER_SIZE:
      profile.reservedBytes = attr->value.u64;
      break;
    case SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE:
      profile.threshMode =
          static_cast<sai_buffer_profile_threshold_mode_t>(attr->value.s32);
      break;
    case SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
      profile.dynamicThreshold = attr->value.s8;
      break;
    case SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH:
      profile.staticThreshold = attr->value.u64;
      break;
    case SAI_BUFFER_PROFILE_ATTR_XOFF_TH:
      profile.xoffTh = attr->value.u64;
      break;
    case SAI_BUFFER_PROFILE_ATTR_XON_TH:
      profile.xonTh = attr->value.u64;
      break;
    case SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH:
      profile.xonOffsetTh = attr->value.u64;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_buffer_profile_fn(sai_object_id_t profile_id) {
  auto fs = FakeSai::getInstance();
  fs->bufferProfileManager.remove(profile_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_buffer_profile_attribute_fn(
    sai_object_id_t profile_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto profile = fs->bufferProfileManager.get(profile_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_BUFFER_PROFILE_ATTR_POOL_ID:
        attr[i].value.oid = profile.poolId;
        break;
      case SAI_BUFFER_PROFILE_ATTR_RESERVED_BUFFER_SIZE:
        attr[i].value.u64 =
            profile.reservedBytes ? profile.reservedBytes.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_THRESHOLD_MODE:
        attr[i].value.s32 = profile.threshMode ? profile.threshMode.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_SHARED_DYNAMIC_TH:
        attr[i].value.s8 =
            profile.dynamicThreshold ? profile.dynamicThreshold.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_SHARED_STATIC_TH:
        attr[i].value.u64 =
            profile.staticThreshold ? profile.staticThreshold.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XOFF_TH:
        attr[i].value.u64 = profile.xoffTh ? profile.xoffTh.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XON_TH:
        attr[i].value.u64 = profile.xonTh ? profile.xonTh.value() : 0;
        break;
      case SAI_BUFFER_PROFILE_ATTR_XON_OFFSET_TH:
        attr[i].value.u64 =
            profile.xonOffsetTh ? profile.xonOffsetTh.value() : 0;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_ingress_priority_group_fn(
    sai_object_id_t* ingress_priority_group_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  std::optional<sai_object_id_t> port;
  std::optional<sai_uint8_t> index;
  std::optional<sai_object_id_t> bufferProfile;
  std::optional<bool> losslessEnable;
  auto fs = FakeSai::getInstance();
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE:
        bufferProfile = attr_list[i].value.oid;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_PORT:
        port = attr_list[i].value.oid;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_INDEX:
        index = attr_list[i].value.u8;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_LOSSLESS_ENABLE:
        losslessEnable = attr_list[i].value.booldata;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!port || !index) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *ingress_priority_group_id = fs->ingressPriorityGroupManager.create(
      port.value(), index.value(), bufferProfile, losslessEnable);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_ingress_priority_group_fn(
    sai_object_id_t ingress_priority_group_id) {
  auto fs = FakeSai::getInstance();
  fs->ingressPriorityGroupManager.remove(ingress_priority_group_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_ingress_priority_group_attribute_fn(
    sai_object_id_t ingress_priority_group_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& ingressPriorityGroup =
      fs->ingressPriorityGroupManager.get(ingress_priority_group_id);
  switch (attr->id) {
    case SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE:
      ingressPriorityGroup.bufferProfile = attr->value.oid;
      break;
    case SAI_INGRESS_PRIORITY_GROUP_ATTR_PORT:
      ingressPriorityGroup.port = attr->value.oid;
      break;
    case SAI_INGRESS_PRIORITY_GROUP_ATTR_INDEX:
      ingressPriorityGroup.index = attr->value.u8;
      break;
    case SAI_INGRESS_PRIORITY_GROUP_ATTR_LOSSLESS_ENABLE:
      ingressPriorityGroup.losslessEnable = attr->value.booldata;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_ingress_priority_group_attribute_fn(
    sai_object_id_t ingress_priority_group_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& ingressPriorityGroup =
      fs->ingressPriorityGroupManager.get(ingress_priority_group_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_BUFFER_PROFILE:
        attr[i].value.oid = ingressPriorityGroup.bufferProfile
            ? ingressPriorityGroup.bufferProfile.value()
            : 0;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_PORT:
        attr[i].value.oid = ingressPriorityGroup.port;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_INDEX:
        attr[i].value.u8 = ingressPriorityGroup.index;
        break;
      case SAI_INGRESS_PRIORITY_GROUP_ATTR_LOSSLESS_ENABLE:
        attr[i].value.booldata = ingressPriorityGroup.losslessEnable
            ? ingressPriorityGroup.losslessEnable.value()
            : false;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

/*
 * In fake sai there isn't a dataplane, so all ingress priority group stats
 * stay at 0. Mirrors the buffer pool stats handling above.
 */
sai_status_t get_ingress_priority_group_stats_fn(
    sai_object_id_t /*ingress_priority_group_id*/,
    uint32_t number_of_counters,
    const sai_stat_id_t* /*counter_ids*/,
    uint64_t* counters) {
  for (auto i = 0; i < number_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_ingress_priority_group_stats_ext_fn(
    sai_object_id_t ingress_priority_group_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_ingress_priority_group_stats_fn(
      ingress_priority_group_id, number_of_counters, counter_ids, counters);
}

/*
 * noop clear stats API. Since fake doesn't have a dataplane stats are always
 * set to 0, so no need to clear them.
 */
sai_status_t clear_ingress_priority_group_stats_fn(
    sai_object_id_t /*ingress_priority_group_id*/,
    uint32_t /*number_of_counters*/,
    const sai_stat_id_t* /*counter_ids*/) {
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_buffer_api_t _buffer_api;

void populate_buffer_api(sai_buffer_api_t** buffer_api) {
  _buffer_api.create_buffer_pool = &create_buffer_pool_fn;
  _buffer_api.remove_buffer_pool = &remove_buffer_pool_fn;
  _buffer_api.set_buffer_pool_attribute = &set_buffer_pool_attribute_fn;
  _buffer_api.get_buffer_pool_attribute = &get_buffer_pool_attribute_fn;
  _buffer_api.get_buffer_pool_stats = &get_buffer_pool_stats_fn;
  _buffer_api.get_buffer_pool_stats_ext = &get_buffer_pool_stats_ext_fn;
  _buffer_api.clear_buffer_pool_stats = &clear_buffer_pool_stats_fn;
  _buffer_api.create_buffer_profile = &create_buffer_profile_fn;
  _buffer_api.remove_buffer_profile = &remove_buffer_profile_fn;
  _buffer_api.set_buffer_profile_attribute = &set_buffer_profile_attribute_fn;
  _buffer_api.get_buffer_profile_attribute = &get_buffer_profile_attribute_fn;
  _buffer_api.create_ingress_priority_group = &create_ingress_priority_group_fn;
  _buffer_api.remove_ingress_priority_group = &remove_ingress_priority_group_fn;
  _buffer_api.get_ingress_priority_group_attribute =
      &get_ingress_priority_group_attribute_fn;
  _buffer_api.set_ingress_priority_group_attribute =
      &set_ingress_priority_group_attribute_fn;
  _buffer_api.get_ingress_priority_group_stats =
      &get_ingress_priority_group_stats_fn;
  _buffer_api.get_ingress_priority_group_stats_ext =
      &get_ingress_priority_group_stats_ext_fn;
  _buffer_api.clear_ingress_priority_group_stats =
      &clear_ingress_priority_group_stats_fn;
  *buffer_api = &_buffer_api;
}

} // namespace facebook::fboss
