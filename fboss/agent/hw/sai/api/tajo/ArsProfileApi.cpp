// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/ArsProfileApi.h"

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
std::optional<sai_attr_id_t> SaiArsProfileTraits::Attributes::
    AttributeExtensionSamplingIntervalNanosec::operator()() {
  return std::nullopt;
}
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
std::optional<sai_attr_id_t>
SaiArsProfileTraits::Attributes::AttributeArsMaxGroups::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsProfileTraits::Attributes::AttributeArsBaseIndex::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiArsProfileTraits::Attributes::AttributeEcmpMemberCount::operator()() {
  return std::nullopt;
}
#endif

} // namespace facebook::fboss
