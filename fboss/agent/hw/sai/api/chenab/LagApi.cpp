// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/api/LagApi.h"

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiLagTraits::Attributes::AttributeDisableTtlDecrement::operator()() {
  return std::nullopt;
}

} // namespace facebook::fboss
