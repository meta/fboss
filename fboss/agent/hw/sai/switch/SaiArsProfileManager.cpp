/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiArsProfileManager.h"

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"

#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

SaiArsProfileManager::SaiArsProfileManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  arsProfileHandle_ = std::make_unique<SaiArsProfileHandle>();
#endif
}

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)

void SaiArsProfileManager::addArsProfile(
    const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchConfig) {
  auto attributes = createAttributes(flowletSwitchConfig);
  auto& store = saiStore_->get<SaiArsProfileTraits>();
  arsProfileHandle_->arsProfile = store.setObject(std::monostate{}, attributes);
  auto maxArsVirtualGroups = flowletSwitchConfig->getMaxArsVirtualGroups();
  arsProfileHandle_->arsVirtualGroupsEnabled =
      maxArsVirtualGroups.has_value() && maxArsVirtualGroups.value() != 0;
}

void SaiArsProfileManager::removeArsProfile() {
  if (arsProfileHandle_->arsProfile) {
    arsProfileHandle_->arsProfile.reset();
  }
  arsProfileHandle_->arsVirtualGroupsEnabled = false;
}

void SaiArsProfileManager::changeArsProfile(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitchConfig,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitchConfig) {
  addArsProfile(newFlowletSwitchConfig);
}

SaiArsProfileHandle* SaiArsProfileManager::getArsProfileHandle() {
  return arsProfileHandle_.get();
}
#endif

} // namespace facebook::fboss
