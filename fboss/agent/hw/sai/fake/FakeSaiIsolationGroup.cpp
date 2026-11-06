/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/fake/FakeSaiIsolationGroup.h"
#include "fboss/agent/hw/sai/fake/FakeSai.h"

#include <optional>

using facebook::fboss::FakeIsolationGroup;
using facebook::fboss::FakeIsolationGroupMember;
using facebook::fboss::FakeSai;

sai_status_t create_isolation_group_fn(
    sai_object_id_t* isolation_group_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_int32_t> type;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ISOLATION_GROUP_ATTR_TYPE:
        type = attr_list[i].value.s32;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  // TYPE is MANDATORY_ON_CREATE.
  if (!type) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *isolation_group_id = fs->isolationGroupManager.create(type.value());
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_isolation_group_fn(sai_object_id_t isolation_group_id) {
  auto fs = FakeSai::getInstance();
  fs->isolationGroupManager.remove(isolation_group_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_isolation_group_attribute_fn(
    sai_object_id_t isolation_group_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  const auto& isolationGroup =
      fs->isolationGroupManager.get(isolation_group_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_ISOLATION_GROUP_ATTR_TYPE:
        attr[i].value.s32 = isolationGroup.type;
        break;
      case SAI_ISOLATION_GROUP_ATTR_ISOLATION_MEMBER_LIST: {
        const auto& memberMap = isolationGroup.fm().map();
        if (memberMap.size() > attr[i].value.objlist.count) {
          attr[i].value.objlist.count = memberMap.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr[i].value.objlist.count = memberMap.size();
        int j = 0;
        for (const auto& m : memberMap) {
          attr[i].value.objlist.list[j++] = m.first;
        }
      } break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_isolation_group_attribute_fn(
    sai_object_id_t /* isolation_group_id */,
    const sai_attribute_t* attr) {
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    // TYPE is CREATE_ONLY and the member list is READ_ONLY, so nothing on an
    // isolation group is settable after create.
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
}

sai_status_t create_isolation_group_member_fn(
    sai_object_id_t* isolation_group_member_id,
    sai_object_id_t /* switch_id */,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  std::optional<sai_object_id_t> isolationGroupId;
  std::optional<sai_object_id_t> isolationObject;
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_GROUP_ID:
        isolationGroupId = attr_list[i].value.oid;
        break;
      case SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_OBJECT:
        isolationObject = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_INVALID_PARAMETER;
    }
  }
  // Both attributes are MANDATORY_ON_CREATE.
  if (!isolationGroupId || !isolationObject) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  *isolation_group_member_id = fs->isolationGroupManager.createMember(
      isolationGroupId.value(), isolationGroupId.value());
  auto& member =
      fs->isolationGroupManager.getMember(*isolation_group_member_id);
  member.isolationObject = isolationObject.value();
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_isolation_group_member_fn(
    sai_object_id_t isolation_group_member_id) {
  auto fs = FakeSai::getInstance();
  fs->isolationGroupManager.removeMember(isolation_group_member_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_isolation_group_member_attribute_fn(
    sai_object_id_t isolation_group_member_id,
    uint32_t attr_count,
    sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  const auto& member =
      fs->isolationGroupManager.getMember(isolation_group_member_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr[i].id) {
      case SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_GROUP_ID:
        attr[i].value.oid = member.isolationGroupId;
        break;
      case SAI_ISOLATION_GROUP_MEMBER_ATTR_ISOLATION_OBJECT:
        attr[i].value.oid = member.isolationObject;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_isolation_group_member_attribute_fn(
    sai_object_id_t /* isolation_group_member_id */,
    const sai_attribute_t* attr) {
  if (!attr) {
    return SAI_STATUS_INVALID_PARAMETER;
  }
  switch (attr->id) {
    // Both member attributes are CREATE_ONLY.
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
}

namespace facebook::fboss {

static sai_isolation_group_api_t _isolation_group_api;

void populate_isolation_group_api(
    sai_isolation_group_api_t** isolation_group_api) {
  _isolation_group_api.create_isolation_group = &create_isolation_group_fn;
  _isolation_group_api.remove_isolation_group = &remove_isolation_group_fn;
  _isolation_group_api.set_isolation_group_attribute =
      &set_isolation_group_attribute_fn;
  _isolation_group_api.get_isolation_group_attribute =
      &get_isolation_group_attribute_fn;
  _isolation_group_api.create_isolation_group_member =
      &create_isolation_group_member_fn;
  _isolation_group_api.remove_isolation_group_member =
      &remove_isolation_group_member_fn;
  _isolation_group_api.get_isolation_group_member_attribute =
      &get_isolation_group_member_attribute_fn;
  _isolation_group_api.set_isolation_group_member_attribute =
      &set_isolation_group_member_attribute_fn;
  *isolation_group_api = &_isolation_group_api;
}

} // namespace facebook::fboss
