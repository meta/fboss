// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/hw/HwSwitchFb303Stats.h"

namespace facebook::fboss {

void fillHwSwitchDramStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDramStats& /*hwSwitchDramStats*/) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchWatermarkStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchWatermarkStats& /*hwSwitchWatermarkStats*/) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchCreditStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchCreditStats& /*hwSwitchCreditStats*/) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchErrorStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropStats& /*dropStats*/) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchPipelineStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    int /*idx*/,
    HwSwitchPipelineStats& /*hwSwitchPipelineStats*/) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchSaiExtensionDropStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropStats& /* dropStats */) {
  CHECK_EQ(counterId2Value.size(), 0);
}

void fillHwSwitchDropBitmapStats(
    const folly::F14FastMap<sai_stat_id_t, uint64_t>& counterId2Value,
    HwSwitchDropBitmapStats& stats) {
#if defined(CHENAB_SAI_SDK_GTE_2511_36)
  constexpr int kExpectedStatIds = 12;
  CHECK_EQ(SaiSwitchTraits::customDropBitmapStats().size(), kExpectedStatIds)
      << "customDropBitmapStats() size changed — update fillHwSwitchDropBitmapStats switch cases";
  for (const auto& [id, value] : counterId2Value) {
    auto v = static_cast<int64_t>(value);
    switch (id) {
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_INGRESS_MAC_0:
        stats.ingressMacDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_EGRESS_PIPE_0:
        stats.egressPipeDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_EGRESS_MAC_0:
        stats.egressMacDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_GENERAL_0:
        stats.pipelineGeneralDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_MAC_0:
        stats.pipelineMacDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_MPLS_0:
        stats.pipelineMplsDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_TUNNEL_0:
        stats.pipelineTunnelDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_IPV4_0:
        stats.pipelineIpv4DropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_IPV6_0:
        stats.pipelineIpv6DropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_BUFFER_0:
        stats.bufferDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_QUEUE_0:
        stats.queueDropBitmap() = v;
        break;
      case SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_HOST_0:
        stats.hostDropBitmap() = v;
        break;
      default:
        throw FbossError("Got unexpected drop bitmap stat id: ", id);
    }
  }
#else
  CHECK_EQ(counterId2Value.size(), 0);
#endif
}

void logDropBitmapReasons(const HwSwitchDropBitmapStats& /*stats*/) {}

void logDropReasons(
    const std::vector<sai_int32_t>& ingressDropReasons,
    const std::vector<sai_int32_t>& egressDropReasons) {
  CHECK_EQ(ingressDropReasons.size(), 0);
  CHECK_EQ(egressDropReasons.size(), 0);
}

void publishSwitchWatermarks(HwSwitchWatermarkStats& /*watermarkStats*/) {}

void publishSwitchPipelineStats(HwSwitchPipelineStats& /*pipelineStats*/) {}

void publishSwitchTemperatureStats(
    HwSwitchTemperatureStats& /*temperatureStats*/) {}

} // namespace facebook::fboss
