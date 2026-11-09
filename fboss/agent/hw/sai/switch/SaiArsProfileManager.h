/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include "fboss/agent/hw/sai/api/ArsProfileApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/types.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
class SaiStore;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
using SaiArsProfile = SaiObject<SaiArsProfileTraits>;
#endif

struct SaiArsProfileHandle {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::shared_ptr<SaiArsProfile> arsProfile;
  bool arsVirtualGroupsEnabled{false};
#endif
};

class SaiArsProfileManager {
 public:
  SaiArsProfileManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
  static auto constexpr kArsRandomSeed = 0x5555;
  static auto constexpr kArsMinSamplingRateNs = 512;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  void addArsProfile(
      const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchingConfig);
  void removeArsProfile();
  void changeArsProfile(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitchingConfig,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitchingConfig);
  SaiArsProfileHandle* getArsProfileHandle();
#endif

 private:
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  SaiArsProfileTraits::CreateAttributes createAttributes(
      const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchingConfig);
  std::unique_ptr<SaiArsProfileHandle> arsProfileHandle_;
#endif
};

} // namespace facebook::fboss
