// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree.

#include "fboss/agent/hw/sai/tracer/PolicerApiTracer.h"
#include "fboss/agent/hw/sai/api/PolicerApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
const std::map<int32_t, std::pair<std::string, std::size_t>> _PolicerMap{
    SAI_ATTR_MAP(Policer, Type),
    SAI_ATTR_MAP(Policer, Mode),
    SAI_ATTR_MAP(Policer, ColorSource),
    SAI_ATTR_MAP(Policer, Cir),
    SAI_ATTR_MAP(Policer, Cbs),
    SAI_ATTR_MAP(Policer, Pbs),
    SAI_ATTR_MAP(Policer, GreenPacketAction),
    SAI_ATTR_MAP(Policer, YellowPacketAction),
    SAI_ATTR_MAP(Policer, RedPacketAction),
};
} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(policer, SAI_OBJECT_TYPE_POLICER, policer);
WRAP_REMOVE_FUNC(policer, SAI_OBJECT_TYPE_POLICER, policer);
WRAP_SET_ATTR_FUNC(policer, SAI_OBJECT_TYPE_POLICER, policer);
WRAP_GET_ATTR_FUNC(policer, SAI_OBJECT_TYPE_POLICER, policer);

sai_policer_api_t* wrappedPolicerApi() {
  static sai_policer_api_t wrappers{};
  wrappers.create_policer = &wrap_create_policer;
  wrappers.remove_policer = &wrap_remove_policer;
  wrappers.set_policer_attribute = &wrap_set_policer_attribute;
  wrappers.get_policer_attribute = &wrap_get_policer_attribute;
  return &wrappers;
}

void setPolicerAttributes(
    const sai_attribute_t* attr_list,
    uint32_t attr_count,
    std::vector<std::string>& attrLines,
    sai_status_t /* rv */) {
  for (uint32_t i = 0; i < attr_count; ++i) {
    auto iter = _PolicerMap.find(attr_list[i].id);
    if (iter == _PolicerMap.end()) {
      XLOG(WARN) << "Unsupported policer attribute " << attr_list[i].id;
      continue;
    }
    attrLines.push_back(to<std::string>("// ", iter->second.first));
    switch (attr_list[i].id) {
      case SAI_POLICER_ATTR_CIR:
      case SAI_POLICER_ATTR_CBS:
      case SAI_POLICER_ATTR_PBS:
        attrLines.push_back(u64Attr(attr_list, i));
        break;
      default:
        attrLines.push_back(s32Attr(attr_list, i));
        break;
    }
  }
}

} // namespace facebook::fboss
