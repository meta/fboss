// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/SwitchApi.h"

#include "saiswitchcustom.h"

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeLedIdWrapper::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeLedResetIdWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAclFieldListWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeEgressPoolAvailableSizeIdWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::HwEccErrorInitiateWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDllPathWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeRestartIssuWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDelayDropCongThreshold::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeForceTrafficOverFabricWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeWarmBootTargetVersionWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSwitchIsolateWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxCoresWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricRemoteReachablePortList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkBootTimeWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeRouteNoImplicitMetaDataWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeRouteAllowImplicitMetaDataWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeMultiStageLocalSwitchIdsWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeNoAclsForTrapsWrapper::operator()() {
  return std::nullopt;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dramStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::rciWatermarkStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dtlWatermarkStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dramBlockTime() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::dramQuarantinedBufferStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::fabricInterCellJitterWatermarkStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::deletedCredits() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::egressCoreBufferWatermarkBytes() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::sramMinBufferWatermarkBytes() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::fdrFifoWatermarkBytes() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressFabricCellError() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressNonFabricCellError() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::egressNonFabricCellUnpackError() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressParityCellError() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::ddpPacketError() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

void SwitchApi::registerSwitchEventCallback(
    SwitchSaiId /*id*/,
    void* /*switch_event_cb*/) const {}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLocalNs::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLocalNs::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLevel1Ns::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLevel1Ns::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLevel2Ns::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLevel2Ns::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeReachabilityGroupList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricLinkLayerFlowControlThreshold::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeSramFreePercentXoffThWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeSramFreePercentXonThWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSystemPortId::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxLocalSystemPortId::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSystemPorts::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxVoqs::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricCllfcTxCreditThWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqDramBoundThWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeCondEntropyRehashPeriodUS::operator()() {
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelSrcIp::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelDstIp::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelSrcMac::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelPeriodicInterval::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareCoreTouse::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareLogFile::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSwitchId::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeArsAvailableFlows::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSflowAggrNofSamplesWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkRegDumpLogPath::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkDumpRateLimitWindow::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkDumpSuppressedCount::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareObjectList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeTcRateLimitList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributePfcTcDldTimerGranularityInterval::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeNumberOfPipes::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePipelineObjectList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDisableSllAndHllTimeout::operator()() {
  return SAI_SWITCH_ATTR_DISABLE_SLL_AND_HLL_TIMEOUT;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAsicRevision::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeCreditRequestProfileSchedulerMode::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeModuleIdToCreditRequestProfileParamList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeTriggerSimulatedEccCorrectableError::operator()() {
  return SAI_SWITCH_ATTR_TRIGGER_SIMULATED_ECC_CORRECTABLE_ERROR;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeTriggerSimulatedEccUnCorrectableError::operator()() {
  return SAI_SWITCH_ATTR_TRIGGER_SIMULATED_ECC_UNCORRECTABLE_ERROR;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDefaultCpuEgressBufferPool::operator()() {
  return SAI_SWITCH_ATTR_DEFAULT_CPU_EGRESS_BUFFER_POOL;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeTechSupportType::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeModuleIdFabricPortList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePfcMonitorEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeCablePropagationDelayMeasurement::operator()() {
#if defined(CHENAB_SAI_SDK_GTE_2511_36)
  return SAI_SWITCH_ATTR_CABLE_PROPAGATION_DELAY_MEASUREMENT;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePortCl72RetryEnable::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAclDropSnoopDisabled::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePacketDropTypeIngressList::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePacketDropTypeEgressList::operator()() {
  return std::nullopt;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::deviceWatermarkBytes() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::customDropBitmapStats() {
#if defined(CHENAB_SAI_SDK_GTE_2511_36)
  // Per-stage HW drop-cause counters from the Chenab pipeline.
  // Each ID aggregates the drop reasons for one packet-processing stage;
  // SaiSwitchManager reads each into the matching HwSwitchDropBitmapStats
  // field. Examples below are representative, not exhaustive.
  static const std::vector<sai_stat_id_t> stats{
      // Ingress port/MAC (L1-L2) RX errors: PHY/FCS, runt/oversize, bad type.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_INGRESS_MAC_0,
      // Generic ingress-pipeline drops not specific to a protocol stage.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_GENERAL_0,
      // L2/MAC pipeline stage: e.g. source MAC multicast, SMAC==DMAC.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_MAC_0,
      // IPv4 routing stage: e.g. loopback/martian DIP, header/TTL errors.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_IPV4_0,
      // IPv6 routing stage: e.g. loopback/multicast DIP, hop-limit errors.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_IPV6_0,
      // MPLS label stage: e.g. invalid/unknown label, label TTL expiry.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_MPLS_0,
      // Tunnel encap/decap stage: e.g. termination errors, decap miss.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_PIPELINE_TUNNEL_0,
      // Ingress buffer admission: e.g. WRED, buffer/headroom exhaustion.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_BUFFER_0,
      // Egress queue/scheduling: e.g. queue tail-drop or WRED.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_QUEUE_0,
      // Egress pipeline stage: post-routing egress processing/ACL drops.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_EGRESS_PIPE_0,
      // Egress port/MAC stage: e.g. MTU exceeded, egress port errors.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_EGRESS_MAC_0,
      // Host/CPU path: e.g. control-plane trap/policer drops to host.
      SAI_SWITCH_STAT_CUSTOM_HW_DROP_CAUSE_HOST_0,
  };
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

} // namespace facebook::fboss
