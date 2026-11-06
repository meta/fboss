/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/IsolationGroupApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/IsolationGroupApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _IsolationGroupMap{
    SAI_ATTR_MAP(IsolationGroup, Type),
    SAI_ATTR_MAP(IsolationGroup, IsolationMemberList),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _IsolationGroupMemberMap{
    SAI_ATTR_MAP(IsolationGroupMember, IsolationGroupId),
    SAI_ATTR_MAP(IsolationGroupMember, IsolationObject),
};

} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(
    isolation_group,
    SAI_OBJECT_TYPE_ISOLATION_GROUP,
    isolationGroup);
WRAP_REMOVE_FUNC(
    isolation_group,
    SAI_OBJECT_TYPE_ISOLATION_GROUP,
    isolationGroup);
WRAP_SET_ATTR_FUNC(
    isolation_group,
    SAI_OBJECT_TYPE_ISOLATION_GROUP,
    isolationGroup);
WRAP_GET_ATTR_FUNC(
    isolation_group,
    SAI_OBJECT_TYPE_ISOLATION_GROUP,
    isolationGroup);

WRAP_CREATE_FUNC(
    isolation_group_member,
    SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER,
    isolationGroup);
WRAP_REMOVE_FUNC(
    isolation_group_member,
    SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER,
    isolationGroup);
WRAP_SET_ATTR_FUNC(
    isolation_group_member,
    SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER,
    isolationGroup);
WRAP_GET_ATTR_FUNC(
    isolation_group_member,
    SAI_OBJECT_TYPE_ISOLATION_GROUP_MEMBER,
    isolationGroup);

sai_isolation_group_api_t* wrappedIsolationGroupApi() {
  static sai_isolation_group_api_t isolationGroupWrappers;

  isolationGroupWrappers.create_isolation_group = &wrap_create_isolation_group;
  isolationGroupWrappers.remove_isolation_group = &wrap_remove_isolation_group;
  isolationGroupWrappers.set_isolation_group_attribute =
      &wrap_set_isolation_group_attribute;
  isolationGroupWrappers.get_isolation_group_attribute =
      &wrap_get_isolation_group_attribute;
  isolationGroupWrappers.create_isolation_group_member =
      &wrap_create_isolation_group_member;
  isolationGroupWrappers.remove_isolation_group_member =
      &wrap_remove_isolation_group_member;
  isolationGroupWrappers.set_isolation_group_member_attribute =
      &wrap_set_isolation_group_member_attribute;
  isolationGroupWrappers.get_isolation_group_member_attribute =
      &wrap_get_isolation_group_member_attribute;

  return &isolationGroupWrappers;
}

SET_SAI_ATTRIBUTES(IsolationGroup)
SET_SAI_ATTRIBUTES(IsolationGroupMember)

} // namespace facebook::fboss
