// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/AclApi.h"

extern "C" {
#include <sai.h>

#if defined(TAJO_SDK_GTE_26_5)
#include <saiextensions.h>
#endif
}

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeActionL3SwitchCancel::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeFieldRouteDestination::operator()() {
#if defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)
  // Matches the route destination OID, which for a PBR entry is the
  // next-hop-group the entry matches on.
  return SAI_ACL_ENTRY_ATTR_FIELD_ROUTE_DST;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiAclTableTraits::Attributes::AttributeFieldRouteDestination::operator()() {
#if defined(TAJO_SDK_GTE_26_5) && !defined(TAJO_SDK_P200)
  return SAI_ACL_TABLE_ATTR_FIELD_ROUTE_DST;
#else
  return std::nullopt;
#endif
}

} // namespace facebook::fboss
