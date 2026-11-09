/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/fake/FakeSaiNextHopGroup.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeNextHopGroup;
using facebook::fboss::FakeNextHopGroupMember;
using facebook::fboss::FakeSai;

sai_status_t create_next_hop_group_fn(
    sai_object_id_t* next_hop_group_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<int32_t> type;
  sai_object_id_t ars_id = SAI_NULL_OBJECT_ID;
  sai_int32_t hash_algorithm = SAI_HASH_ALGORITHM_NONE;
  bool hierarchical_nexthop = true;
  std::optional<bool> split_horizon_enable;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_NEXT_HOP_GROUP_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_ARS_OBJECT_ID:
        ars_id = attr_list[i].value.oid;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_HASH_ALGORITHM:
        hash_algorithm = attr_list[i].value.s32;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_HIERARCHICAL_NEXTHOP:
        hierarchical_nexthop = attr_list[i].value.booldata;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE:
        split_horizon_enable = attr_list[i].value.booldata;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  if (!type) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (type.value()) {
    case SAI_NEXT_HOP_GROUP_TYPE_ECMP:
    case SAI_NEXT_HOP_GROUP_TYPE_PROTECTION:
    case SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION:
      break;
    default:
      return SAI_STATUS_INVALID_PARAMETER;
  }
  *next_hop_group_id = fs->nextHopGroupManager.create(
      type.value(),
      ars_id,
      hash_algorithm,
      hierarchical_nexthop,
      split_horizon_enable);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_next_hop_group_fn(sai_object_id_t next_hop_group_id) {
  auto fs = FakeSai::getInstance();
  fs->nextHopGroupManager.remove(next_hop_group_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_next_hop_group_attribute_fn(
    sai_object_id_t next_hop_group_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  auto fs = FakeSai::getInstance();
  const auto& nextHopGroup = fs->nextHopGroupManager.get(next_hop_group_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_NEXT_HOP_GROUP_ATTR_TYPE:
        attr[i].value.s32 = nextHopGroup.type;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_ARS_OBJECT_ID:
        attr[i].value.oid = nextHopGroup.ars_id;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_HASH_ALGORITHM:
        attr[i].value.s32 = nextHopGroup.hash_algorithm;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_HIERARCHICAL_NEXTHOP:
        attr[i].value.booldata = nextHopGroup.hierarchical_nexthop;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE:
        attr[i].value.booldata =
            nextHopGroup.split_horizon_enable.value_or(false);
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_ARS_FAIL_PKT_COUNT:
        attr[i].value.u64 = nextHopGroup.ars_fail_pkt_count;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_ARS_PORT_REASSIGN_COUNT:
        attr[i].value.u64 = nextHopGroup.ars_port_reassign_count;
        break;
      case SAI_NEXT_HOP_GROUP_ATTR_NEXT_HOP_MEMBER_LIST: {
        const auto& nextHopGroupMemberMap =
            fs->nextHopGroupManager.get(next_hop_group_id).fm().map();
        if (nextHopGroupMemberMap.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = nextHopGroupMemberMap.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.objlist.count = nextHopGroupMemberMap.size();
        int j = 0;
        for (const auto& m : nextHopGroupMemberMap) {
          attr[i].value.objlist.list[j++] = m.first;
        }
      } break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_next_hop_group_attribute_fn(
    sai_object_id_t next_hop_group_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& nextHopGroup = fs->nextHopGroupManager.get(next_hop_group_id);
  switch (attr->id) {
    case SAI_NEXT_HOP_GROUP_ATTR_ARS_OBJECT_ID:
      nextHopGroup.ars_id = attr->value.oid;
      break;
    case SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE:
      nextHopGroup.split_horizon_enable = attr->value.booldata;
      break;
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_next_hop_group_member_fn(
    sai_object_id_t* next_hop_group_member_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_object_id_t> nextHopGroupId;
  std::optional<sai_object_id_t> nextHopId;
  std::optional<sai_uint32_t> weight = std::nullopt;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  sai_int32_t configuredRole =
      SAI_NEXT_HOP_GROUP_MEMBER_CONFIGURED_ROLE_PRIMARY;
#else
  // PRIMARY. The symbolic constant is unavailable before SAI 1.16, and the
  // CONFIGURED_ROLE attribute is only read/stored under the 1.16 guard below.
  sai_int32_t configuredRole = 0;
#endif
  sai_object_id_t monitoredObject = SAI_NULL_OBJECT_ID;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID:
        nextHopGroupId = attr_list[i].value.oid;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID:
        nextHopId = attr_list[i].value.oid;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT:
        // WEIGHT is a u32 attribute (see the get path and the member's u32
        // weight field); read it as u32 to match.
        weight = attr_list[i].value.u32;
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_CONFIGURED_ROLE:
        configuredRole = attr_list[i].value.s32;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_MONITORED_OBJECT:
        monitoredObject = attr_list[i].value.oid;
        break;
#endif
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  if (!nextHopGroupId || !nextHopId) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *next_hop_group_member_id = fs->nextHopGroupManager.createMember(
      nextHopGroupId.value(),
      nextHopGroupId.value(),
      nextHopId.value(),
      weight,
      configuredRole,
      monitoredObject);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_next_hop_group_member_fn(
    sai_object_id_t next_hop_group_member_id) {
  auto fs = FakeSai::getInstance();
  fs->nextHopGroupManager.removeMember(next_hop_group_member_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_next_hop_group_member_attribute_fn(
    sai_object_id_t next_hop_group_member_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& nextHopGroupMember =
      fs->nextHopGroupManager.getMember(next_hop_group_member_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_GROUP_ID:
        attr[i].value.oid = nextHopGroupMember.nextHopGroupId;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_NEXT_HOP_ID:
        attr[i].value.oid = nextHopGroupMember.nextHopId;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT:
        if (auto weight = nextHopGroupMember.weight) {
          attr[i].value.u32 = *weight;
        }
        break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_CONFIGURED_ROLE:
        attr[i].value.s32 = nextHopGroupMember.configuredRole;
        break;
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_MONITORED_OBJECT:
        attr[i].value.oid = nextHopGroupMember.monitoredObject;
        break;
#endif
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_next_hop_group_member_attribute_fn(
    sai_object_id_t next_hop_group_member_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  auto& nextHopGroupMember =
      fs->nextHopGroupManager.getMember(next_hop_group_member_id);
  switch (attr->id) {
    case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT:
      nextHopGroupMember.weight = attr->value.u32;
      break;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
    case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_CONFIGURED_ROLE:
      nextHopGroupMember.configuredRole = attr->value.s32;
      break;
    case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_MONITORED_OBJECT:
      nextHopGroupMember.monitoredObject = attr->value.oid;
      break;
#endif
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_next_hop_group_members_attribute_fn(
    uint32_t object_count,
    const sai_object_id_t* object_id,
    const sai_attribute_t* attr_list,
    sai_bulk_op_error_mode_t /* mode */,
    sai_status_t* object_statuses) {
  auto fs = FakeSai::getInstance();
  auto setMemberAttribute = [](auto& attr, auto& member) {
    switch (attr.id) {
      case SAI_NEXT_HOP_GROUP_MEMBER_ATTR_WEIGHT:
        member.weight = attr.value.u32;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
    return SAI_STATUS_SUCCESS;
  };
  for (int count = 0; count < object_count; count++) {
    auto& nextHopGroupMember =
        fs->nextHopGroupManager.getMember(object_id[count]);
    object_statuses[count] =
        setMemberAttribute(attr_list[count], nextHopGroupMember);
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {

static sai_next_hop_group_api_t _next_hop_group_api;

void populate_next_hop_group_api(
    sai_next_hop_group_api_t** next_hop_group_api) {
  _next_hop_group_api.create_next_hop_group = &create_next_hop_group_fn;
  _next_hop_group_api.remove_next_hop_group = &remove_next_hop_group_fn;
  _next_hop_group_api.set_next_hop_group_attribute =
      &set_next_hop_group_attribute_fn;
  _next_hop_group_api.get_next_hop_group_attribute =
      &get_next_hop_group_attribute_fn;
  _next_hop_group_api.create_next_hop_group_member =
      &create_next_hop_group_member_fn;
  _next_hop_group_api.remove_next_hop_group_member =
      &remove_next_hop_group_member_fn;
  _next_hop_group_api.set_next_hop_group_member_attribute =
      &set_next_hop_group_member_attribute_fn;
  _next_hop_group_api.get_next_hop_group_member_attribute =
      &get_next_hop_group_member_attribute_fn;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  _next_hop_group_api.set_next_hop_group_members_attribute =
      &set_next_hop_group_members_attribute_fn;
#endif
  *next_hop_group_api = &_next_hop_group_api;
}

} // namespace facebook::fboss
