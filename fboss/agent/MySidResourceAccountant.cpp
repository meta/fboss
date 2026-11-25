// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/MySidResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

namespace {
constexpr auto kHundredPercentage = 100;
} // namespace

MySidResourceAccountant::MySidResourceAccountant(const HwAsicTable* asicTable)
    : asicTable_(asicTable) {}

void MySidResourceAccountant::stateChanged(const StateDelta& delta) {
  DeltaFunctions::forEachChanged(
      delta.getMySidsDelta(),
      [&](const auto& /*oldEntry*/, const auto& /*newEntry*/) {
        // Changed entries don't affect count
      },
      [&](const auto& /*newEntry*/) { mySidUsage_++; },
      [&](const auto& /*deletedEntry*/) { mySidUsage_--; });
}

bool MySidResourceAccountant::isValidUpdate(const StateDelta& delta) {
  stateChanged(delta);
  return checkMySidResource(false /* intermediateState */);
}

bool MySidResourceAccountant::checkMySidResource(bool intermediateState) const {
  uint32_t resourcePercentage =
      intermediateState ? kHundredPercentage : FLAGS_mysid_resource_percentage;

  for (const auto& [_, hwAsic] : asicTable_->getHwAsics()) {
    const auto mySidLimit = hwAsic->getMaxMySidEntries();
    if (mySidLimit.has_value()) {
      uint32_t enforcedLimit =
          (mySidLimit.value() * resourcePercentage) / kHundredPercentage;
      if (mySidUsage_ > enforcedLimit) {
        XLOG(DBG2) << "MySID resource limit exceeded. MySID usage: "
                   << mySidUsage_ << " ASIC limit: " << enforcedLimit;
        return false;
      }
    }
  }
  return true;
}

} // namespace facebook::fboss
