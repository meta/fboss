// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/AclApi.h"

#include <experimental/saiaclextensions.h>

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeActionL3SwitchCancel::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  return SAI_ACL_ENTRY_ATTR_ACTION_L3_SWITCH_CANCEL;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiAclEntryTraits::Attributes::AttributeFieldRouteDestination::operator()() {
  // TODO(zecheng): return the real BCM SAI extension id for the PBR
  // route-destination ACL match field once the vendor SDK exposes it (phase 2).
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiAclTableTraits::Attributes::AttributeFieldRouteDestination::operator()() {
  return std::nullopt;
}

} // namespace facebook::fboss
