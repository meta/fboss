/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/tracer/MyMacApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/MyMacApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _MyMacMap{
    SAI_ATTR_MAP(MyMac, MacAddress),
    SAI_ATTR_MAP(MyMac, MacAddressMask),
    SAI_ATTR_MAP(MyMac, VlanId),
};
} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(my_mac, SAI_OBJECT_TYPE_MY_MAC, myMac);
WRAP_REMOVE_FUNC(my_mac, SAI_OBJECT_TYPE_MY_MAC, myMac);
WRAP_SET_ATTR_FUNC(my_mac, SAI_OBJECT_TYPE_MY_MAC, myMac);
WRAP_GET_ATTR_FUNC(my_mac, SAI_OBJECT_TYPE_MY_MAC, myMac);

sai_my_mac_api_t* wrappedMyMacApi() {
  static sai_my_mac_api_t myMacWrappers;
  myMacWrappers.create_my_mac = &wrap_create_my_mac;
  myMacWrappers.remove_my_mac = &wrap_remove_my_mac;
  myMacWrappers.set_my_mac_attribute = &wrap_set_my_mac_attribute;
  myMacWrappers.get_my_mac_attribute = &wrap_get_my_mac_attribute;
  return &myMacWrappers;
}

SET_SAI_ATTRIBUTES(MyMac)

} // namespace facebook::fboss
#endif
