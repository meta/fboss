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

SaiArsManager::SaiArsManager(
    SaiStore* saiStore,
    SaiManagerTable* managerTable,
    const SaiPlatform* platform)
    : saiStore_(saiStore), managerTable_(managerTable), platform_(platform) {
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
  arsHandle_ = std::make_unique<SaiArsHandle>();
  virtualArsGroupHandle_ = std::make_unique<SaiArsHandle>();
  standbyArsHandle_ = std::make_unique<SaiArsHandle>();
#endif
}

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
sai_int32_t SaiArsManager::cfgSwitchingModeToSai(
    cfg::SwitchingMode switchingMode) const {
  switch (switchingMode) {
    case cfg::SwitchingMode::FLOWLET_QUALITY:
      return SAI_ARS_MODE_FLOWLET_QUALITY;
    case cfg::SwitchingMode::PER_PACKET_QUALITY:
      return SAI_ARS_MODE_PER_PACKET_QUALITY;
    case cfg::SwitchingMode::FIXED_ASSIGNMENT:
      return SAI_ARS_MODE_FIXED;
    case cfg::SwitchingMode::PER_PACKET_RANDOM:
      return SAI_ARS_MODE_PER_PACKET_RANDOM;
  }

  // should return in one of the cases
  throw FbossError("Unsupported flowlet switching mode");
}

SaiArsTraits::CreateAttributes SaiArsManager::makeArsAttributes(
    cfg::SwitchingMode switchingMode,
    std::optional<sai_uint32_t> idleTime,
    std::optional<sai_uint32_t> maxFlows,
    std::optional<SaiArsTraits::Attributes::PrimaryPathQualityThreshold>
        primaryPathQualityThreshold,
    std::optional<SaiArsTraits::Attributes::AlternatePathCost>
        alternatePathCost,
    std::optional<SaiArsTraits::Attributes::AlternatePathBias>
        alternatePathBias,
    std::optional<SaiArsTraits::Attributes::NextHopGroupType> nextHopGroupType,
    std::optional<SaiArsTraits::Attributes::SourcePortPrune> sourcePortPrune,
    const std::optional<SaiArsTraits::Attributes::EcmpMemberCount>&
        ecmpMemberCount,
    const std::optional<SaiArsTraits::Attributes::MaxAltMembersPerGroup>&
        maxAltMembersPerGroup,
    const std::optional<SaiArsTraits::Attributes::MaxPrimaryMembersPerGroup>&
        maxPrimaryMembersPerGroup,
    const std::optional<SaiArsTraits::Attributes::CommonMembersThresholdCount>&
        commonMembersThresholdCount) const {
  std::optional<SaiArsTraits::Attributes::IdleTime> idleTimeAttr = std::nullopt;
  if (idleTime) {
    idleTimeAttr = SaiArsTraits::Attributes::IdleTime{*idleTime};
  }
  std::optional<SaiArsTraits::Attributes::MaxFlows> maxFlowsAttr = std::nullopt;
  if (maxFlows) {
    maxFlowsAttr = SaiArsTraits::Attributes::MaxFlows{*maxFlows};
  }
  return SaiArsTraits::CreateAttributes{
      SaiArsTraits::Attributes::Mode{cfgSwitchingModeToSai(switchingMode)},
      idleTimeAttr,
      maxFlowsAttr,
      primaryPathQualityThreshold,
      alternatePathCost,
      alternatePathBias,
      nextHopGroupType,
      sourcePortPrune,
      ecmpMemberCount,
      maxAltMembersPerGroup,
      maxPrimaryMembersPerGroup,
      commonMembersThresholdCount};
}

void SaiArsManager::setArsObject(
    SaiArsHandle* handle,
    const SaiArsTraits::CreateAttributes& attributes) {
  handle->ars = saiStore_->get<SaiArsTraits>().setObject(
      getAdapterHostKey(attributes), attributes);
}

std::optional<SaiArsTraits::Attributes::SourcePortPrune>
SaiArsManager::toSourcePortPruneAttribute(
    std::optional<bool> splitHorizonEnabled) {
  if (splitHorizonEnabled.has_value()) {
    return SaiArsTraits::Attributes::SourcePortPrune{*splitHorizonEnabled};
  }
  return std::nullopt;
}

void SaiArsManager::removeArs(
    const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchConfig) {
  if (arsHandle_->ars) {
    arsHandle_->ars.reset();
  }
  if (virtualArsGroupHandle_->ars) {
    virtualArsGroupHandle_->ars.reset();
  }
  if (standbyArsHandle_->ars) {
    standbyArsHandle_->ars.reset();
  }
}

void SaiArsManager::changeArs(
    const std::shared_ptr<FlowletSwitchingConfig>& oldFlowletSwitchConfig,
    const std::shared_ptr<FlowletSwitchingConfig>& newFlowletSwitchConfig,
    std::optional<bool> splitHorizonEnabled) {
  addArs(newFlowletSwitchConfig, splitHorizonEnabled);
}

SaiArsHandle* SaiArsManager::getArsHandle() const {
  return arsHandle_.get();
}

SaiArsHandle* SaiArsManager::getVirtualArsGroupHandle() const {
  return virtualArsGroupHandle_.get();
}

SaiArsHandle* SaiArsManager::getStandbyArsHandle() const {
  return standbyArsHandle_.get();
}

#endif

} // namespace facebook::fboss
