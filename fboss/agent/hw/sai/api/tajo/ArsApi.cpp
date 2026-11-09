// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/ArsApi.h"

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeNextHopGroupType::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeSourcePortPrune::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeEcmpMemberCount::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeMaxAltMembersPerGroup::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeMaxPrimaryMembersPerGroup::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsTraits::Attributes::AttributeCommonMembersThresholdCount::operator()() {
  return std::nullopt;
}
#endif

} // namespace facebook::fboss
