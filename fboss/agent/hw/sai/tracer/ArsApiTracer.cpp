/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/ArsApiTracer.h"

#include "fboss/agent/hw/sai/api/ArsApi.h"

using folly::to;

namespace {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
std::map<int32_t, std::pair<std::string, std::size_t>> _ArsMap{
    SAI_ATTR_MAP(Ars, Mode),
    SAI_ATTR_MAP(Ars, IdleTime),
    SAI_ATTR_MAP(Ars, MaxFlows),
    SAI_ATTR_MAP(Ars, PrimaryPathQualityThreshold),
    SAI_ATTR_MAP(Ars, AlternatePathCost),
    SAI_ATTR_MAP(Ars, AlternatePathBias),
};

void handleExtensionAttributes() {
#if defined(BRCM_SAI_SDK_GTE_14_0) && defined(BRCM_SAI_SDK_XGS)
  SAI_EXT_ATTR_MAP(Ars, NextHopGroupType);
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  SAI_EXT_ATTR_MAP(Ars, SourcePortPrune);
#endif
#if defined(BRCM_SAI_SDK_GTE_15_4) && defined(BRCM_SAI_SDK_XGS)
  SAI_EXT_ATTR_MAP(Ars, EcmpMemberCount);
  SAI_EXT_ATTR_MAP(Ars, MaxAltMembersPerGroup);
  SAI_EXT_ATTR_MAP(Ars, MaxPrimaryMembersPerGroup);
  SAI_EXT_ATTR_MAP(Ars, CommonMembersThresholdCount);
#endif
}
#endif

} // namespace

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
WRAP_CREATE_FUNC(ars, SAI_OBJECT_TYPE_ARS, ars);
WRAP_REMOVE_FUNC(ars, SAI_OBJECT_TYPE_ARS, ars);
WRAP_SET_ATTR_FUNC(ars, SAI_OBJECT_TYPE_ARS, ars);
WRAP_GET_ATTR_FUNC(ars, SAI_OBJECT_TYPE_ARS, ars);

sai_ars_api_t* wrappedArsApi() {
  handleExtensionAttributes();
  static sai_ars_api_t arsWrappers;

  arsWrappers.create_ars = &wrap_create_ars;
  arsWrappers.remove_ars = &wrap_remove_ars;
  arsWrappers.set_ars_attribute = &wrap_set_ars_attribute;
  arsWrappers.get_ars_attribute = &wrap_get_ars_attribute;

  return &arsWrappers;
}

SET_SAI_ATTRIBUTES(Ars)
#endif

} // namespace facebook::fboss
