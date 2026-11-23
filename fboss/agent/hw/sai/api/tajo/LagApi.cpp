// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/sai/api/LagApi.h"

#if defined(TAJO_SDK_GTE_26_2)
extern "C" {
#include <experimental/sailagcustom.h>
}
#endif

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiLagTraits::Attributes::AttributeDisableTtlDecrement::operator()() {
#if defined(TAJO_SDK_GTE_26_2)
  return SAI_LAG_ATTR_EXT_DISABLE_DECREMENT_TTL;
#else
  return std::nullopt;
#endif
}

} // namespace facebook::fboss
