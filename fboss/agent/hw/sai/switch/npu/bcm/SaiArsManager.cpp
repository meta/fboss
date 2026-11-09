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

#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/platforms/sai/SaiPlatform.h"

#if defined(BRCM_SAI_SDK_GTE_14_0)
#include <experimental/saiarsextensions.h>
#endif

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
void SaiArsManager::addArs(
    const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchConfig,
    std::optional<bool> splitHorizonEnabled) {
  auto switchingMode = flowletSwitchConfig->getSwitchingMode();
  auto idleTime = flowletSwitchConfig->getInactivityIntervalUsecs();
  auto maxFlows = flowletSwitchConfig->getFlowletTableSize();

  std::optional<SaiArsTraits::Attributes::AlternatePathCost>
      alternatePathCostForArs = std::nullopt;
  std::optional<SaiArsTraits::Attributes::AlternatePathBias>
      alternatePathBiasForArs = std::nullopt;
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
#if defined(BRCM_SAI_SDK_GTE_15_4)
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ARS_ALTERNATE_MEMBERS) ||
      platform_->getAsic()->isSupported(HwAsic::Feature::VIRTUAL_ARS_GROUP)) {
#else
  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ARS_ALTERNATE_MEMBERS)) {
#endif
    // Need to set default values as these attributes are part of adapter key
    alternatePathCostForArs = SaiArsTraits::Attributes::AlternatePathCost{0};
    alternatePathBiasForArs = SaiArsTraits::Attributes::AlternatePathBias{0};
  }
#endif
  std::optional<SaiArsTraits::Attributes::EcmpMemberCount>
      ecmpMemberCountForArs = std::nullopt;
#if defined(BRCM_SAI_SDK_GTE_16_0)
  // On TH5, past the ASIC's native ARS width the adapter splits the group into
  // primary and alternate member sets.
  if (auto arsGroupWidth = flowletSwitchConfig->getArsGroupWidth()) {
    ecmpMemberCountForArs = SaiArsTraits::Attributes::EcmpMemberCount{
        static_cast<sai_uint32_t>(*arsGroupWidth)};
  }
#endif
#if defined(BRCM_SAI_SDK_GTE_15_4)
  if (!ecmpMemberCountForArs &&
      platform_->getAsic()->isSupported(HwAsic::Feature::VIRTUAL_ARS_GROUP)) {
    if (auto arsWidth = platform_->getAsic()->getMaxArsWidth()) {
      ecmpMemberCountForArs =
          SaiArsTraits::Attributes::EcmpMemberCount{*arsWidth};
    }
  }
#endif
  std::optional<SaiArsTraits::Attributes::NextHopGroupType> nextHopGroupType =
      std::nullopt;
#if defined(BRCM_SAI_SDK_GTE_14_0) && defined(BRCM_SAI_SDK_XGS)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::VIRTUAL_ARS_GROUP)) {
    nextHopGroupType = SaiArsTraits::Attributes::NextHopGroupType{
        SAI_ARS_NEXT_HOP_GROUP_TYPE_REGULAR};
  }
#endif

  setArsObject(
      arsHandle_.get(),
      makeArsAttributes(
          switchingMode,
          idleTime,
          maxFlows,
          std::nullopt,
          alternatePathCostForArs,
          alternatePathBiasForArs,
          nextHopGroupType,
          toSourcePortPruneAttribute(splitHorizonEnabled),
          ecmpMemberCountForArs));

#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0)
  auto standbySwitchingMode = flowletSwitchConfig->getStandbySwitchingMode();
  if (standbySwitchingMode.has_value()) {
    // ThriftConfigApplier guarantees the standby idle time and table size are
    // set alongside the mode, and that the mode differs from the primary's.
    setArsObject(
        standbyArsHandle_.get(),
        makeArsAttributes(
            *standbySwitchingMode,
            *flowletSwitchConfig->getStandbyInactivityIntervalUsecs(),
            *flowletSwitchConfig->getStandbyFlowletTableSize(),
            std::nullopt,
            alternatePathCostForArs,
            alternatePathBiasForArs,
            nextHopGroupType,
            std::nullopt,
            ecmpMemberCountForArs));
  }
#endif

#if defined(BRCM_SAI_SDK_GTE_14_0) && defined(BRCM_SAI_SDK_XGS)
  if (platform_->getAsic()->isSupported(HwAsic::Feature::VIRTUAL_ARS_GROUP) &&
      flowletSwitchConfig->getMinWidthForArsVirtualGroup().has_value()) {
    std::optional<SaiArsTraits::Attributes::PrimaryPathQualityThreshold>
        virtualArsQualityThreshold = std::nullopt;
    std::optional<SaiArsTraits::Attributes::EcmpMemberCount> ecmpMemberCount =
        std::nullopt;
    std::optional<SaiArsTraits::Attributes::MaxAltMembersPerGroup>
        maxAltMembers = std::nullopt;
    std::optional<SaiArsTraits::Attributes::MaxPrimaryMembersPerGroup>
        maxPrimaryMembers = std::nullopt;
    std::optional<SaiArsTraits::Attributes::CommonMembersThresholdCount>
        commonMembersThreshold = std::nullopt;
    auto virtualAlternatePathCost = alternatePathCostForArs;
    auto virtualAlternatePathBias = alternatePathBiasForArs;
#if defined(BRCM_SAI_SDK_GTE_15_4)
    auto cost = flowletSwitchConfig->getAlternatePathCost();
    auto bias = flowletSwitchConfig->getAlternatePathBias();
    virtualArsQualityThreshold =
        SaiArsTraits::Attributes::PrimaryPathQualityThreshold{0};
    if (auto threshold =
            flowletSwitchConfig->getPrimaryPathQualityThreshold()) {
      virtualArsQualityThreshold =
          SaiArsTraits::Attributes::PrimaryPathQualityThreshold{
              static_cast<sai_uint32_t>(*threshold)};
    }
    if (auto width = flowletSwitchConfig->getMaxArsVirtualGroupWidth();
        width && *width > 0) {
      auto total = static_cast<sai_uint32_t>(*width);
      ecmpMemberCount = SaiArsTraits::Attributes::EcmpMemberCount{total};
      if (auto alternateMembers =
              flowletSwitchConfig->getArsVirtualGroupAlternateMembers()) {
        // ThriftConfigApplier bounds this to (0, maxArsVirtualGroupWidth), so
        // the primary count below cannot underflow or reach zero.
        auto alternates = static_cast<sai_uint32_t>(*alternateMembers);
        maxAltMembers =
            SaiArsTraits::Attributes::MaxAltMembersPerGroup{alternates};
        maxPrimaryMembers = SaiArsTraits::Attributes::MaxPrimaryMembersPerGroup{
            total - alternates};
        if (auto threshold = flowletSwitchConfig
                                 ->getArsVirtualGroupCommonMembersThreshold()) {
          commonMembersThreshold =
              SaiArsTraits::Attributes::CommonMembersThresholdCount{
                  static_cast<sai_uint32_t>(*threshold)};
        }
      }
    }
    // The adapter only programs the DGM parameters from a virtual group, so
    // this is the one object where the alternate path cost and bias reach
    // hardware. Everywhere else they stay 0 to keep the adapter host key
    // matching what the adapter reports back. Config sets the two fields
    // independently, so apply them only as a pair: cost without bias, or the
    // reverse, is half a DGM policy rather than a weaker one.
    if (cost.has_value() && bias.has_value()) {
      virtualAlternatePathCost = SaiArsTraits::Attributes::AlternatePathCost{
          static_cast<sai_uint32_t>(*cost)};
      virtualAlternatePathBias = SaiArsTraits::Attributes::AlternatePathBias{
          static_cast<sai_uint32_t>(*bias)};
    }
#endif
    setArsObject(
        virtualArsGroupHandle_.get(),
        makeArsAttributes(
            switchingMode,
            idleTime,
            maxFlows,
            virtualArsQualityThreshold,
            virtualAlternatePathCost,
            virtualAlternatePathBias,
            SaiArsTraits::Attributes::NextHopGroupType{
                SAI_ARS_NEXT_HOP_GROUP_TYPE_VIRTUAL},
            std::nullopt,
            ecmpMemberCount,
            maxAltMembers,
            maxPrimaryMembers,
            commonMembersThreshold));
  } else if (virtualArsGroupHandle_->ars) {
    // Config no longer asks for virtual groups, so drop the one we created.
    virtualArsGroupHandle_->ars.reset();
  }
#endif
}
#endif

} // namespace facebook::fboss
