// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/ArsApi.h"

#if defined(BRCM_SAI_SDK_GTE_13_0)
#include <experimental/saiarsextensions.h>
#endif

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeNextHopGroupType::operator()() {
#if defined(BRCM_SAI_SDK_GTE_14_0) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_EXTENSION_NEXT_HOP_GROUP_TYPE;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeSourcePortPrune::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_EXTENSION_SOURCE_PORT_PRUNE;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeEcmpMemberCount::operator()() {
#if defined(BRCM_SAI_SDK_GTE_15_4) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_EXTENSION_ECMP_MEMBER_COUNT;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeMaxAltMembersPerGroup::operator()() {
#if defined(BRCM_SAI_SDK_GTE_15_4) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_MAX_ALT_MEMEBERS_PER_GROUP;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeMaxPrimaryMembersPerGroup::operator()() {
#if defined(BRCM_SAI_SDK_GTE_15_4) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_MAX_PRIMARY_MEMEBERS_PER_GROUP;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeCommonMembersThresholdCount::operator()() {
#if defined(BRCM_SAI_SDK_GTE_15_4) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ARS_ATTR_EXTENSION_COMMON_MEMBERS_THRESHOLD_COUNT;
#else
  return std::nullopt;
#endif
}
#endif

} // namespace facebook::fboss
