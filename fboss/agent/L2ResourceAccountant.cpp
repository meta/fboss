// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/L2ResourceAccountant.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/StateDelta.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {

void L2ResourceAccountant::stateChanged(const StateDelta& delta) {
  isValidUpdate(delta);
}

bool L2ResourceAccountant::isValidUpdate(const StateDelta& delta) {
  auto processDelta = [&](const auto& deltaMac) {
    DeltaFunctions::forEachChanged(
        deltaMac,
        [&](const auto& /*oldMac*/, const auto& /*newMac*/) {
          return LoopAction::CONTINUE;
        },
        [&](const auto& /*newMac*/) {
          l2Entries_++;
          return LoopAction::CONTINUE;
        },
        [&](const auto& /*deletedMac*/) {
          l2Entries_--;
          return LoopAction::CONTINUE;
        });
  };

  for (auto& deltaVlan : delta.getVlansDelta()) {
    processDelta(deltaVlan.getMacDelta());
  }
  if (l2Entries_ > FLAGS_max_l2_entries) {
    XLOG(ERR) << "Total l2 entries in new switchState: " << l2Entries_
              << " exceeds the limit: " << FLAGS_max_l2_entries;
    return false;
  }
  return true;
}

uint32_t L2ResourceAccountant::getUsage() const {
  return l2Entries_;
}

} // namespace facebook::fboss
