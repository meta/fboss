// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/AclApi.h"

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeActionL3SwitchCancel::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeFieldRouteDestination::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiAclTableTraits::Attributes::AttributeFieldRouteDestination::operator()() {
  return std::nullopt;
}

#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
std::optional<sai_attr_id_t>
SaiAclCounterTraits::Attributes::AttributeLabelExtendedWrapper::operator()() {
  return std::nullopt;
}
#endif

} // namespace facebook::fboss
