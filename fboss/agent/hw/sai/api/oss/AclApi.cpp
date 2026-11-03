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

} // namespace facebook::fboss
