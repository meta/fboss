// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/VrfResourceAccountant.h"

#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"
#include "fboss/agent/state/VrfMap.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

VrfResourceAccountant::VrfResourceAccountant(const HwAsicTable* asicTable)
    : asicTable_(asicTable) {}

void VrfResourceAccountant::stateChanged(const StateDelta& delta) {
  DeltaFunctions::forEachChanged(
      delta.getVrfsDelta(),
      [&](const auto& /*oldVrf*/, const auto& /*newVrf*/) {},
      [&](const auto& /*newVrf*/) { vrfUsage_++; },
      [&](const auto& /*deletedVrf*/) { vrfUsage_--; });
}

bool VrfResourceAccountant::isValidUpdate(const StateDelta& delta) {
  stateChanged(delta);
  return checkVrfResource();
}

bool VrfResourceAccountant::checkVrfResource() const {
  // +1 for the default VRF
  const auto numVrfs = vrfUsage_ + 1;
  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    const auto maxVrfs = hwAsic->getMaxVrfs();
    if (maxVrfs.has_value() && numVrfs > maxVrfs.value()) {
      XLOG(ERR) << "VRF resource limit exceeded. VRFs: " << numVrfs
                << " ASIC limit: " << maxVrfs.value();
      return false;
    }
  }
  return true;
}

} // namespace facebook::fboss
