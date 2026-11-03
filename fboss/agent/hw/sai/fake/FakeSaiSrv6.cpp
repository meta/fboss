// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/hw/sai/fake/FakeSaiSrv6.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <cstring>

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
namespace {

using facebook::fboss::FakeSai;

sai_status_t create_srv6_sidlist_fn(
    sai_object_id_t* srv6_sidlist_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  sai_int32_t type{};
  std::vector<std::array<uint8_t, 16>> segmentList;
  sai_object_id_t nextHopId{SAI_NULL_OBJECT_ID};
  for (uint32_t i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_SRV6_SIDLIST_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      case SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST: {
        const auto& list = attr_list[i].value.segmentlist;
        segmentList.resize(list.count);
        for (uint32_t j = 0; j < list.count; j++) {
          // NOLINTNEXTLINE(facebook-hte-LocalUncheckedArrayBounds)
          std::memcpy(segmentList[j].data(), &list.list[j], 16);
        }
        break;
      }
      case SAI_SRV6_SIDLIST_ATTR_NEXT_HOP_ID:
        nextHopId = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  *srv6_sidlist_id =
      fs->srv6SidListManager.create(type, std::move(segmentList), nextHopId);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_srv6_sidlist_fn(sai_object_id_t srv6_sidlist_id) {
  auto fs = FakeSai::getInstance();
  fs->srv6SidListManager.remove(srv6_sidlist_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_srv6_sidlist_attribute_fn(
    sai_object_id_t srv6_sidlist_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& sidList = fs->srv6SidListManager.get(srv6_sidlist_id);
  for (uint32_t i = 0; i < attr_count; i++) {
    switch (attr[i].id) {
      case SAI_SRV6_SIDLIST_ATTR_TYPE:
        attr[i].value.s32 = sidList.type;
        break;
      case SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST: {
        if (attr[i].value.segmentlist.count <
            static_cast<uint32_t>(sidList.segmentList.size())) {
          attr[i].value.segmentlist.count =
              static_cast<uint32_t>(sidList.segmentList.size());
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.segmentlist.count =
            static_cast<uint32_t>(sidList.segmentList.size());
        for (size_t j = 0; j < sidList.segmentList.size(); j++) {
          std::memcpy(
              &attr[i].value.segmentlist.list[j],
              sidList.segmentList[j].data(),
              16);
        }
        break;
      }
      case SAI_SRV6_SIDLIST_ATTR_NEXT_HOP_ID:
        attr[i].value.oid = sidList.nextHopId;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_srv6_sidlist_attribute_fn(
    sai_object_id_t srv6_sidlist_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& sidList = fs->srv6SidListManager.get(srv6_sidlist_id);
  switch (attr->id) {
    case SAI_SRV6_SIDLIST_ATTR_TYPE:
      sidList.type = attr->value.s32;
      break;
    case SAI_SRV6_SIDLIST_ATTR_SEGMENT_LIST: {
      const auto& list = attr->value.segmentlist;
      sidList.segmentList.resize(list.count);
      for (uint32_t j = 0; j < list.count; j++) {
        std::memcpy(sidList.segmentList[j].data(), &list.list[j], 16);
      }
      break;
    }
    case SAI_SRV6_SIDLIST_ATTR_NEXT_HOP_ID:
      sidList.nextHopId = attr->value.oid;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_srv6_sidlist_stats_fn(
    sai_object_id_t /*srv6_sidlist*/,
    uint32_t num_of_counters,
    const sai_stat_id_t* /*counter_ids*/,
    uint64_t* counters) {
  for (uint32_t i = 0; i < num_of_counters; ++i) {
    counters[i] = 0;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_srv6_sidlist_stats_ext_fn(
    sai_object_id_t srv6_sidlist,
    uint32_t num_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t /*mode*/,
    uint64_t* counters) {
  return get_srv6_sidlist_stats_fn(
      srv6_sidlist, num_of_counters, counter_ids, counters);
}

sai_status_t clear_srv6_sidlist_stats_fn(
    sai_object_id_t /*srv6_sidlist*/,
    uint32_t /*number_of_counters*/,
    const sai_stat_id_t* /*counter_ids*/) {
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_my_sid_entry_attribute_fn(
    const sai_my_sid_entry_t* my_sid_entry,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& entry = fs->mySidEntryManager.get(
      facebook::fboss::FakeSaiMySidEntry(*my_sid_entry));
  switch (attr->id) {
    case SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR:
      entry.endpointBehavior = attr->value.s32;
      break;
    case SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR:
      entry.endpointBehaviorFlavor = attr->value.s32;
      break;
    case SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID:
      entry.nextHopId = attr->value.oid;
      break;
    case SAI_MY_SID_ENTRY_ATTR_VRF:
      entry.vrf = attr->value.oid;
      break;
    case SAI_MY_SID_ENTRY_ATTR_PACKET_ACTION:
      entry.packetAction = attr->value.s32;
      break;
    case SAI_MY_SID_ENTRY_ATTR_TUNNEL_ID:
      entry.tunnelId = attr->value.oid;
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_my_sid_entry_attribute_fn(
    const sai_my_sid_entry_t* my_sid_entry,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& entry = fs->mySidEntryManager.get(
      facebook::fboss::FakeSaiMySidEntry(*my_sid_entry));
  for (uint32_t i = 0; i < attr_count; i++) {
    switch (attr_list[i].id) {
      case SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR:
        attr_list[i].value.s32 = entry.endpointBehavior;
        break;
      case SAI_MY_SID_ENTRY_ATTR_ENDPOINT_BEHAVIOR_FLAVOR:
        attr_list[i].value.s32 = entry.endpointBehaviorFlavor;
        break;
      case SAI_MY_SID_ENTRY_ATTR_NEXT_HOP_ID:
        attr_list[i].value.oid = entry.nextHopId;
        break;
      case SAI_MY_SID_ENTRY_ATTR_VRF:
        attr_list[i].value.oid = entry.vrf;
        break;
      case SAI_MY_SID_ENTRY_ATTR_PACKET_ACTION:
        attr_list[i].value.s32 = entry.packetAction;
        break;
      case SAI_MY_SID_ENTRY_ATTR_TUNNEL_ID:
        if (!entry.tunnelId.has_value()) {
          return SAI_STATUS_ITEM_NOT_FOUND;
        }
        attr_list[i].value.oid = entry.tunnelId.value();
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_my_sid_entry_fn(
    const sai_my_sid_entry_t* my_sid_entry,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  fs->mySidEntryManager.create(
      facebook::fboss::FakeSaiMySidEntry(*my_sid_entry));
  for (uint32_t i = 0; i < attr_count; i++) {
    sai_status_t status =
        set_my_sid_entry_attribute_fn(my_sid_entry, &attr_list[i]);
    if (status != SAI_STATUS_SUCCESS) {
      return status;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_my_sid_entry_fn(const sai_my_sid_entry_t* my_sid_entry) {
  auto fs = FakeSai::getInstance();
  if (fs->mySidEntryManager.remove(
          facebook::fboss::FakeSaiMySidEntry(*my_sid_entry)) == 0) {
    return SAI_STATUS_FAILURE;
  }
  return SAI_STATUS_SUCCESS;
}

} // namespace

namespace facebook::fboss {
// NOLINTNEXTLINE(facebook-avoid-non-const-global-variables)
static sai_srv6_api_t _srv6_api;

void populate_srv6_api(sai_srv6_api_t** srv6_api) {
  _srv6_api.create_srv6_sidlist = &create_srv6_sidlist_fn;
  _srv6_api.remove_srv6_sidlist = &remove_srv6_sidlist_fn;
  _srv6_api.get_srv6_sidlist_attribute = &get_srv6_sidlist_attribute_fn;
  _srv6_api.set_srv6_sidlist_attribute = &set_srv6_sidlist_attribute_fn;

  _srv6_api.get_srv6_sidlist_stats = &get_srv6_sidlist_stats_fn;
  _srv6_api.get_srv6_sidlist_stats_ext = &get_srv6_sidlist_stats_ext_fn;
  _srv6_api.clear_srv6_sidlist_stats = &clear_srv6_sidlist_stats_fn;

  _srv6_api.create_my_sid_entry = &create_my_sid_entry_fn;
  _srv6_api.remove_my_sid_entry = &remove_my_sid_entry_fn;
  _srv6_api.set_my_sid_entry_attribute = &set_my_sid_entry_attribute_fn;
  _srv6_api.get_my_sid_entry_attribute = &get_my_sid_entry_attribute_fn;

  *srv6_api = &_srv6_api;
}

} // namespace facebook::fboss
#endif
