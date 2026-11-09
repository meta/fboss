/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/fake/FakeSaiQueue.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeQueue;
using facebook::fboss::FakeSai;

sai_status_t create_queue_fn(
    sai_object_id_t* queue_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_object_id_t> port;
  std::optional<sai_object_id_t> parentScheduler;
  std::optional<uint8_t> index = 0;
  std::optional<sai_queue_type_t> type;
  sai_object_id_t schedulerProfileId{SAI_NULL_OBJECT_ID};
  sai_object_id_t wredProfileId{SAI_NULL_OBJECT_ID};
  sai_object_id_t bufferProfileId{SAI_NULL_OBJECT_ID};
  bool enablePfcDldr = false;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_QUEUE_ATTR_TYPE:
        type = static_cast<sai_queue_type_t>(attr_list[i].value.s32);
        break;
      case SAI_QUEUE_ATTR_PORT:
        port = attr_list[i].value.oid;
        break;
      case SAI_QUEUE_ATTR_INDEX:
        index = attr_list[i].value.u8;
        break;
      case SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE:
        parentScheduler = attr_list[i].value.oid;
        break;
      case SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID:
        schedulerProfileId = attr_list[i].value.oid;
        break;
      case SAI_QUEUE_ATTR_WRED_PROFILE_ID:
        wredProfileId = attr_list[i].value.oid;
        break;
      case SAI_QUEUE_ATTR_BUFFER_PROFILE_ID:
        bufferProfileId = attr_list[i].value.oid;
        break;
      case SAI_QUEUE_ATTR_ENABLE_PFC_DLDR:
        enablePfcDldr = attr_list[i].value.booldata;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  if (!type || !port || !index || !parentScheduler) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *queue_id = fs->queueManager.create(
      type.value(),
      port.value(),
      index.value(),
      parentScheduler.value(),
      schedulerProfileId,
      wredProfileId,
      bufferProfileId,
      enablePfcDldr);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_queue_fn(sai_object_id_t queue_id) {
  auto fs = FakeSai::getInstance();
  fs->queueManager.remove(queue_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_queue_attribute_fn(
    sai_object_id_t queue_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& queue = fs->queueManager.get(queue_id);
  sai_status_t res;
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    case SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE:
      queue.parentScheduler = attr->value.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_QUEUE_ATTR_WRED_PROFILE_ID:
      queue.wredProfileId = attr->value.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_QUEUE_ATTR_BUFFER_PROFILE_ID:
      queue.bufferProfileId = attr->value.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID:
      queue.schedulerProfileId = attr->value.oid;
      res = SAI_STATUS_SUCCESS;
      break;
    case SAI_QUEUE_ATTR_ENABLE_PFC_DLDR:
      queue.enablePfcDldr = attr->value.booldata;
      res = SAI_STATUS_SUCCESS;
      break;
    default:
      res = SAI_STATUS_INVALID_PARAMETER;
      break;
  }
  return res;
}

sai_status_t get_queue_attribute_fn(
    sai_object_id_t queue_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto queue = fs->queueManager.get(queue_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_QUEUE_ATTR_TYPE:
        attr[i].value.s32 = queue.type;
        break;
      case SAI_QUEUE_ATTR_PORT:
        attr[i].value.oid = queue.port;
        break;
      case SAI_QUEUE_ATTR_INDEX:
        attr[i].value.u8 = queue.index;
        break;
      case SAI_QUEUE_ATTR_PARENT_SCHEDULER_NODE:
        attr[i].value.oid = queue.parentScheduler;
        break;
      case SAI_QUEUE_ATTR_WRED_PROFILE_ID:
        attr[i].value.oid = queue.wredProfileId;
        break;
      case SAI_QUEUE_ATTR_BUFFER_PROFILE_ID:
        attr[i].value.oid = queue.bufferProfileId;
        break;
      case SAI_QUEUE_ATTR_SCHEDULER_PROFILE_ID:
        attr[i].value.oid = queue.schedulerProfileId;
        break;
      case SAI_QUEUE_ATTR_ENABLE_PFC_DLDR:
        attr[i].value.booldata = queue.enablePfcDldr;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_queue_stats_fn(
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
sai_status_t get_queue_stats_ext_fn(
    sai_object_id_t queue,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_queue_stats_fn(queue, num_of_counters, counter_ids, counters);
}
/*
 *  noop clear stats API. Since fake doesnt have a
 *  dataplane stats are always set to 0, so
 *  no need to clear them
 */
sai_status_t clear_queue_stats_fn(
    sai_object_id_t /* queue_id */,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids) {
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_queue_api_t _queue_api;

void populate_queue_api(sai_queue_api_t** queue_api) {
  _queue_api.create_queue = &create_queue_fn;
  _queue_api.remove_queue = &remove_queue_fn;
  _queue_api.set_queue_attribute = &set_queue_attribute_fn;
  _queue_api.get_queue_attribute = &get_queue_attribute_fn;
  _queue_api.get_queue_stats = &get_queue_stats_fn;
  _queue_api.get_queue_stats_ext = &get_queue_stats_ext_fn;
  _queue_api.clear_queue_stats = &clear_queue_stats_fn;
  *queue_api = &_queue_api;
}

} // namespace facebook::fboss
