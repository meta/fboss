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
    [[maybe_unused]] std::optional<bool> splitHorizonEnabled) {
  // Chenab resolves the Broadcom extension attributes through
  // api/oss/ArsApi.cpp, which has no id for any of them, and
  // SaiExtensionAttribute CHECKs on a missing id - so constructing one here
  // would abort. Leave them unset.
  SaiArsTraits::CreateAttributes attributes{
      SaiArsTraits::Attributes::Mode{
          cfgSwitchingModeToSai(flowletSwitchConfig->getSwitchingMode())},
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt,
      std::nullopt};

  auto hostKey = getAdapterHostKey(attributes);

  auto& store = saiStore_->get<SaiArsTraits>();
  arsHandle_->ars = store.setObject(hostKey, attributes);
}

#endif

} // namespace facebook::fboss
