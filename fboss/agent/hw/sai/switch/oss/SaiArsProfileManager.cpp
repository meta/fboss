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

#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)

SaiArsProfileTraits::CreateAttributes SaiArsProfileManager::createAttributes(
    const std::shared_ptr<FlowletSwitchingConfig>& flowletSwitchConfig) {
  auto samplingInterval = flowletSwitchConfig->getDynamicSampleRate();
  sai_uint32_t randomSeed = kArsRandomSeed;
  auto portLoadPastWeight = flowletSwitchConfig->getDynamicEgressLoadExponent();
  auto portLoadExponent =
      flowletSwitchConfig->getDynamicPhysicalQueueExponent();
  auto loadPastMinVal =
      flowletSwitchConfig->getDynamicEgressMinThresholdBytes();
  auto loadPastMaxVal =
      flowletSwitchConfig->getDynamicEgressMaxThresholdBytes();
  auto loadFutureMinVal =
      flowletSwitchConfig->getDynamicQueueMinThresholdBytes();
  auto loadFutureMaxVal =
      flowletSwitchConfig->getDynamicQueueMaxThresholdBytes();
  // this is per ITM and should be half of newDynamicQueueMinThresholdBytes
  // above (as we have 2 ITMs)
  auto loadCurrentMinVal =
      flowletSwitchConfig->getDynamicQueueMinThresholdBytes() >> 1;
  auto loadCurrentMaxVal =
      flowletSwitchConfig->getDynamicQueueMaxThresholdBytes() >> 1;

  std::optional<SaiArsProfileTraits::Attributes::PortLoadFuture> portLoadFuture{
      std::nullopt};
  std::optional<SaiArsProfileTraits::Attributes::PortLoadFutureWeight>
      portLoadFutureWeight{std::nullopt};

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ARS_FUTURE_PORT_LOAD)) {
    portLoadFuture = true;
    portLoadFutureWeight = flowletSwitchConfig->getDynamicQueueExponent();
  }

  bool portLoadCurrent = false;

  if (platform_->getAsic()->isSupported(
          HwAsic::Feature::ARS_CURRENT_PORT_LOAD)) {
    portLoadCurrent = true;
  }

  // Workarounds until 11.7 completely goes away and 13.0 is rolled out
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
  if (samplingInterval < kArsMinSamplingRateNs) {
    // convert microsec to nanosec
    samplingInterval = samplingInterval * 1000;
  }
  std::optional<SaiArsProfileTraits::Attributes::ArsMaxGroups> arsMaxGroups{
      std::nullopt};

  if (flowletSwitchConfig->getMaxArsVirtualGroups().has_value()) {
    arsMaxGroups = std::optional<SaiArsProfileTraits::Attributes::ArsMaxGroups>(
        flowletSwitchConfig->getMaxArsVirtualGroups().value());
  }

  std::optional<SaiArsProfileTraits::Attributes::ArsBaseIndex> arsBaseIndex =
      platform_->getAsic()->getArsBaseIndex()
      ? std::optional<SaiArsProfileTraits::Attributes::ArsBaseIndex>(
            platform_->getAsic()->getArsBaseIndex().value())
      : std::nullopt;

#if defined(BRCM_SAI_SDK_GTE_14_0)
  std::optional<SaiArsProfileTraits::Attributes::EcmpMemberCount>
      ecmpMemberCount = flowletSwitchConfig->getMaxArsVirtualGroupWidth()
      ? std::optional<SaiArsProfileTraits::Attributes::EcmpMemberCount>(
            flowletSwitchConfig->getMaxArsVirtualGroupWidth().value())
      : std::nullopt;
#endif
#else
  if (samplingInterval >= kArsMinSamplingRateNs) {
    // convert nanosec to microsec
    samplingInterval = std::ceil(static_cast<double>(samplingInterval) / 1000);
  }
#endif

  SaiArsProfileTraits::CreateAttributes attributes{
      SAI_ARS_PROFILE_ALGO_EWMA,
      samplingInterval,
      randomSeed,
      false, // EnableIPv4
      false, // EnableIPv6
      true,
      portLoadPastWeight,
      loadPastMinVal,
      loadPastMaxVal,
      portLoadFuture,
      portLoadFutureWeight,
      loadFutureMinVal,
      loadFutureMaxVal,
      portLoadCurrent,
      portLoadExponent,
      loadCurrentMinVal,
      loadCurrentMaxVal
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
      ,
      arsMaxGroups,
      arsBaseIndex
#if defined(BRCM_SAI_SDK_GTE_14_0)
      ,
      ecmpMemberCount
#endif
  };
#else
  };
#endif

  return attributes;
}
#endif

} // namespace facebook::fboss
