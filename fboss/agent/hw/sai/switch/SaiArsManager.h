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

#include "fboss/agent/hw/sai/api/ArsApi.h"
#include "fboss/agent/hw/sai/api/Types.h"
#include "fboss/agent/hw/sai/store/SaiObject.h"
#include "fboss/agent/state/FlowletSwitchingConfig.h"
#include "fboss/agent/types.h"

#include <memory>

namespace facebook::fboss {

class SaiManagerTable;
class SaiPlatform;
struct SaiNextHopGroupHandle;
class SaiStore;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
using SaiArs = SaiObject<SaiArsTraits>;
#endif

struct SaiArsHandle {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::shared_ptr<SaiArs> ars;
#endif
};

class SaiArsManager {
 public:
  SaiArsManager(
      SaiStore* saiStore,
      SaiManagerTable* managerTable,
      const SaiPlatform* platform);
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  // splitHorizonEnabled is the resolved ecmpGroupSettings entry for
  // EcmpGroupType::ARS, threaded in from the state being applied; the
  // programmed state would still hold the pre-delta value here.
  void addArs(
      const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchingConfig,
      std::optional<bool> splitHorizonEnabled = std::nullopt);
  void removeArs(
      const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchingConfig);
  void changeArs(
      const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitchingConfig,
      const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitchingConfig,
      std::optional<bool> splitHorizonEnabled = std::nullopt);
  SaiArsHandle* getArsHandle() const;
  SaiArsHandle* getVirtualArsGroupHandle() const;
  SaiArsHandle* getStandbyArsHandle() const;
  sai_int32_t cfgSwitchingModeToSai(cfg::SwitchingMode switchingMode) const;

  SaiArsTraits::CreateAttributes makeArsAttributes(
      cfg::SwitchingMode switchingMode,
      std::optional<sai_uint32_t> idleTime = std::nullopt,
      std::optional<sai_uint32_t> maxFlows = std::nullopt,
      std::optional<SaiArsTraits::Attributes::PrimaryPathQualityThreshold>
          primaryPathQualityThreshold = std::nullopt,
      std::optional<SaiArsTraits::Attributes::AlternatePathCost>
          alternatePathCost = std::nullopt,
      std::optional<SaiArsTraits::Attributes::AlternatePathBias>
          alternatePathBias = std::nullopt,
      std::optional<SaiArsTraits::Attributes::NextHopGroupType>
          nextHopGroupType = std::nullopt,
      std::optional<SaiArsTraits::Attributes::SourcePortPrune> sourcePortPrune =
          std::nullopt,
      const std::optional<SaiArsTraits::Attributes::EcmpMemberCount>&
          ecmpMemberCount = std::nullopt,
      const std::optional<SaiArsTraits::Attributes::MaxAltMembersPerGroup>&
          maxAltMembersPerGroup = std::nullopt,
      const std::optional<SaiArsTraits::Attributes::MaxPrimaryMembersPerGroup>&
          maxPrimaryMembersPerGroup = std::nullopt,
      const std::optional<
          SaiArsTraits::Attributes::CommonMembersThresholdCount>&
          commonMembersThresholdCount = std::nullopt) const;

  void setArsObject(
      SaiArsHandle* handle,
      const SaiArsTraits::CreateAttributes& attributes);

  // An unset setting leaves the create-only attribute alone.
  static std::optional<SaiArsTraits::Attributes::SourcePortPrune>
  toSourcePortPruneAttribute(std::optional<bool> splitHorizonEnabled);
#endif

 private:
  SaiStore* saiStore_;
  SaiManagerTable* managerTable_;
  const SaiPlatform* platform_;

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  std::unique_ptr<SaiArsHandle> arsHandle_;
  std::unique_ptr<SaiArsHandle> virtualArsGroupHandle_;
  std::unique_ptr<SaiArsHandle> standbyArsHandle_;
#endif
};

} // namespace facebook::fboss
