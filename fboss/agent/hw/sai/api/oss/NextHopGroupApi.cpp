// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/NextHopGroupApi.h"

namespace facebook::fboss {

std::optional<sai_attr_id_t> SaiNextHopGroupTraits::Attributes::
    AttributeArsNextHopGroupMetaData::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeSplitHorizonEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeArsFailPktCount::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiNextHopGroupTraits::Attributes::AttributeArsPortReassignCount::operator()() {
  return std::nullopt;
}

} // namespace facebook::fboss
