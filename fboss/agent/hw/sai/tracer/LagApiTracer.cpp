/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/LagApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/LagApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _LagMap{
    SAI_ATTR_MAP(Lag, PortList),
    SAI_ATTR_MAP(Lag, Label),
    SAI_ATTR_MAP(Lag, PortVlanId),
};

std::map<int32_t, std::pair<std::string, std::size_t>> _LagMemberMap{
    SAI_ATTR_MAP(LagMember, LagId),
    SAI_ATTR_MAP(LagMember, PortId),
    SAI_ATTR_MAP(LagMember, EgressDisable),
};
} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(lag, SAI_OBJECT_TYPE_LAG, lag);
WRAP_REMOVE_FUNC(lag, SAI_OBJECT_TYPE_LAG, lag);
WRAP_SET_ATTR_FUNC(lag, SAI_OBJECT_TYPE_LAG, lag);
WRAP_GET_ATTR_FUNC(lag, SAI_OBJECT_TYPE_LAG, lag);

WRAP_CREATE_FUNC(lag_member, SAI_OBJECT_TYPE_LAG_MEMBER, lag);
WRAP_REMOVE_FUNC(lag_member, SAI_OBJECT_TYPE_LAG_MEMBER, lag);
WRAP_SET_ATTR_FUNC(lag_member, SAI_OBJECT_TYPE_LAG_MEMBER, lag);
WRAP_GET_ATTR_FUNC(lag_member, SAI_OBJECT_TYPE_LAG_MEMBER, lag);

sai_lag_api_t* wrappedLagApi() {
  SAI_EXT_ATTR_MAP(Lag, DisableTtlDecrement)
  static sai_lag_api_t lagWrappers;

  lagWrappers.create_lag = &wrap_create_lag;
  lagWrappers.remove_lag = &wrap_remove_lag;
  lagWrappers.set_lag_attribute = &wrap_set_lag_attribute;
  lagWrappers.get_lag_attribute = &wrap_get_lag_attribute;
  lagWrappers.create_lag_member = &wrap_create_lag_member;
  lagWrappers.remove_lag_member = &wrap_remove_lag_member;
  lagWrappers.set_lag_member_attribute = &wrap_set_lag_member_attribute;
  lagWrappers.get_lag_member_attribute = &wrap_get_lag_member_attribute;

  return &lagWrappers;
}

SET_SAI_ATTRIBUTES(Lag)
SET_SAI_ATTRIBUTES(LagMember)

} // namespace facebook::fboss
