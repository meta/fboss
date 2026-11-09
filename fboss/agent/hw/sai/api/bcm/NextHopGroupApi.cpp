// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"

#if defined(BRCM_SAI_SDK_GTE_13_0)
#include <experimental/sainexthopgroupextensions.h>
#endif

namespace facebook::fboss {

std::optional<sai_attr_id_t> SaiNextHopGroupTraits::Attributes::
    AttributeArsNextHopGroupMetaData::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && !defined(BRCM_SAI_SDK_GTE_14_0) && \
    defined(BRCM_SAI_SDK_XGS)
  return SAI_NEXT_HOP_GROUP_ATTR_ARS_NEXT_HOP_GROUP_META_DATA;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeSplitHorizonEnable::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && !defined(BRCM_SAI_SDK_GTE_14_0) && \
    defined(BRCM_SAI_SDK_XGS)
  return SAI_NEXT_HOP_GROUP_ATTR_SPLIT_HORIZON_ENABLE;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeArsFailPktCount::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  // Packets ARS could not resolve to a group member. Free running, and
  // cleared only by a set, so a read does not disturb other readers.
  // Broadcom expects it to increment when the group has nothing selectable:
  //  - every member port forced down by software
  //  - the group momentarily has no members programmed
  //  - every member of the group link down at once
  //  - source port pruning on, and the only usable member left is the port
  //    the packet arrived on
  return SAI_NEXT_HOP_GROUP_ATTR_ARS_FAIL_PKT_COUNT;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeArsPortReassignCount::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && defined(BRCM_SAI_SDK_XGS)
  // Times ARS moved a packet to a different egress port than the one its flow
  // set entry last held.
  return SAI_NEXT_HOP_GROUP_ATTR_ARS_PORT_REASSIGN_COUNT;
#else
  return std::nullopt;
#endif
}

} // namespace facebook::fboss
