/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiArsManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
void SaiArsManager::addArs(
    const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchConfig,
    std::optional<bool> splitHorizonEnabled) {
  auto switchingMode = flowletSwitchConfig->getSwitchingMode();
  auto idleTime = flowletSwitchConfig->getInactivityIntervalUsecs();
  auto maxFlows = flowletSwitchConfig->getFlowletTableSize();

  setArsObject(
      arsHandle_.get(),
      makeArsAttributes(
          switchingMode,
          idleTime,
          maxFlows,
          std::nullopt,
          SaiArsTraits::Attributes::AlternatePathCost{0},
          SaiArsTraits::Attributes::AlternatePathBias{0},
          std::nullopt,
          toSourcePortPruneAttribute(splitHorizonEnabled)));
}

#endif

} // namespace facebook::fboss
