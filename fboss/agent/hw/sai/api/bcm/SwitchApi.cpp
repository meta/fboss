// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/api/SwitchApi.h"

extern "C" {
#include <brcm_sai_extensions.h>
#include <sai.h>

#include <experimental/saiswitchextensions.h>
}

namespace facebook::fboss {

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxCoresWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX)
  return SAI_SWITCH_ATTR_MAX_CORES;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeLedIdWrapper::operator()() {
  return SAI_SWITCH_ATTR_LED;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeLedResetIdWrapper::operator()() {
  return SAI_SWITCH_ATTR_LED_PROCESSOR_RESET;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAclFieldListWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeEgressPoolAvailableSizeIdWrapper::operator()() {
  return SAI_SWITCH_ATTR_DEFAULT_EGRESS_BUFFER_POOL_SHARED_SIZE;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::HwEccErrorInitiateWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDllPathWrapper::operator()() {
#if defined(BRCM_SAI_SDK_XGS)
  return SAI_SWITCH_ATTR_ISSU_CUSTOM_DLL_PATH;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeRestartIssuWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeWarmBootTargetVersionWrapper::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkBootTimeWrapper::operator()() {
  return SAI_SWITCH_ATTR_CUSTOM_RANGE_START + 1;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeSramFreePercentXoffThWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SRAM_FREE_PERCENT_XOFF_TH;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeSramFreePercentXonThWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SRAM_FREE_PERCENT_XON_TH;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricCllfcTxCreditThWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_FABRIC_CLLFC_TX_CREDIT_TH;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqDramBoundThWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_DRAM_BOUND_TH;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSflowAggrNofSamplesWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_SFLOW_AGGR_NOF_SAMPLES;
#else
  return std::nullopt;
#endif
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dramStats() {
#if defined(BRCM_SAI_SDK_DNX)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DEVICE_DRAM_ENQUEUED_BYTES,
      SAI_SWITCH_STAT_DEVICE_DRAM_DEQUEUED_BYTES};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::rciWatermarkStats() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DEVICE_RCI_WATERMARK_BYTES,
      SAI_SWITCH_STAT_DEVICE_CORE_RCI_WATERMARK_BYTES};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dtlWatermarkStats() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_HIGHEST_QUEUE_CONGESTION_LEVEL};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::dramBlockTime() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DEVICE_DRAM_BLOCK_TOTAL_TIME};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::dramQuarantinedBufferStats() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DRAM_QUARANTINE_BUFFER_STATUS};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::fabricInterCellJitterWatermarkStats() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EXTENSION_FABRIC_INTER_CELL_JITTER_MAX_IN_NSEC};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::egressCoreBufferWatermarkBytes() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DEVICE_EGRESS_DB_WM};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::deletedCredits() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DEVICE_DELETED_CREDIT_COUNTER};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::sramMinBufferWatermarkBytes() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_ING_MIN_SRAM_BUFFER_BYTES};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::fdrFifoWatermarkBytes() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_FDR_RX_QUEUE_WM_LEVEL};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressFabricCellError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EGRESS_FABRIC_CELL_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressNonFabricCellError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EGRESS_NON_FABRIC_CELL_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>&
SaiSwitchTraits::egressNonFabricCellUnpackError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EGRESS_CUP_NON_FABRIC_CELL_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::egressParityCellError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EGRESS_PARITY_CELL_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::ddpPacketError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_DDP_PACKET_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::packetIntegrityError() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  static const std::vector<sai_stat_id_t> stats{
      SAI_SWITCH_STAT_EGR_RX_PACKET_ERROR};
#else
  static const std::vector<sai_stat_id_t> stats;
#endif
  return stats;
}

void SwitchApi::registerSwitchEventCallback(
    SwitchSaiId id,
    void* switch_event_cb) const {
#if defined(BRCM_SAI_SDK_XGS_AND_DNX)
  sai_attribute_t attr;
  attr.value.ptr = switch_event_cb;
  attr.id = SAI_SWITCH_ATTR_SWITCH_EVENT_NOTIFY;
  sai_attribute_t eventAttr;
  eventAttr.id = SAI_SWITCH_ATTR_SWITCH_EVENT_TYPE;
  if (switch_event_cb) {
    // Consider the following sequence:
    //   (A) Register switch event callback
    //   (B) Register switch events.
    //
    //   Any switch events received in time between (A) and (B) will be lost,
    //   as a SAI implementation will discard those events thinking those are
    //   not of interest.
    //   Thus, register switch events first, and then register the switch event
    //   callback.

    // Register switch events
    // SAI_SWITCH_EVENT_TYPE_FABRIC_AUTO_LINK_DISABLE is available for
    // 12.2 and 14.x only.
#if defined(SAI_VERSION_12_2_0_0_DNX_ODP) || \
    defined(SAI_VERSION_14_0_EA_DNX_ODP) ||  \
    defined(SAI_VERSION_14_2_0_0_DNX_ODP)
    std::array<uint32_t, 13> events = {
        SAI_SWITCH_EVENT_TYPE_PARITY_ERROR,
        SAI_SWITCH_EVENT_TYPE_STABLE_FULL,
        SAI_SWITCH_EVENT_TYPE_STABLE_ERROR,
        SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN,
        SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE,
        SAI_SWITCH_EVENT_TYPE_INTERRUPT,
        SAI_SWITCH_EVENT_TYPE_FABRIC_AUTO_ISOLATE,
        SAI_SWITCH_EVENT_TYPE_FIRMWARE_CRASHED,
        SAI_SWITCH_EVENT_TYPE_REMOTE_LINK_CHANGE,
        SAI_SWITCH_EVENT_TYPE_RX_FIFO_STUCK_DETECTED,
        SAI_SWITCH_EVENT_TYPE_DEVICE_SOFT_RESET,
        SAI_SWITCH_EVENT_TYPE_INTERRUPT_MASKED,
        SAI_SWITCH_EVENT_TYPE_FABRIC_AUTO_LINK_DISABLE,
    };
#elif defined(BRCM_SAI_SDK_DNX_GTE_11_7)
    std::array<uint32_t, 12> events = {
        SAI_SWITCH_EVENT_TYPE_PARITY_ERROR,
        SAI_SWITCH_EVENT_TYPE_STABLE_FULL,
        SAI_SWITCH_EVENT_TYPE_STABLE_ERROR,
        SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN,
        SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE,
        SAI_SWITCH_EVENT_TYPE_INTERRUPT,
        SAI_SWITCH_EVENT_TYPE_FABRIC_AUTO_ISOLATE,
        SAI_SWITCH_EVENT_TYPE_FIRMWARE_CRASHED,
        SAI_SWITCH_EVENT_TYPE_REMOTE_LINK_CHANGE,
        SAI_SWITCH_EVENT_TYPE_RX_FIFO_STUCK_DETECTED,
        SAI_SWITCH_EVENT_TYPE_DEVICE_SOFT_RESET,
        SAI_SWITCH_EVENT_TYPE_INTERRUPT_MASKED,
    };
#elif defined(BRCM_SAI_SDK_GTE_11_0)
    std::array<uint32_t, 7> events = {
        SAI_SWITCH_EVENT_TYPE_PARITY_ERROR,
        SAI_SWITCH_EVENT_TYPE_STABLE_FULL,
        SAI_SWITCH_EVENT_TYPE_STABLE_ERROR,
        SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN,
        SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE,
        SAI_SWITCH_EVENT_TYPE_INTERRUPT,
        SAI_SWITCH_EVENT_TYPE_FABRIC_AUTO_ISOLATE};
#else
    std::array<uint32_t, 5> events = {
        SAI_SWITCH_EVENT_TYPE_PARITY_ERROR,
        SAI_SWITCH_EVENT_TYPE_STABLE_FULL,
        SAI_SWITCH_EVENT_TYPE_STABLE_ERROR,
        SAI_SWITCH_EVENT_TYPE_UNCONTROLLED_SHUTDOWN,
        SAI_SWITCH_EVENT_TYPE_WARM_BOOT_DOWNGRADE};
#endif
    eventAttr.value.u32list.count = events.size();
    eventAttr.value.u32list.list = events.data();

    {
      auto g{SaiApiLock::getInstance()->lock()};
      auto rv = _setAttribute(id, &eventAttr);
      saiLogError(rv, ApiType, "Unable to register parity error switch events");
    }

    {
      // Register switch event callback function
      auto g{SaiApiLock::getInstance()->lock()};
      auto rv = _setAttribute(id, &attr);
      saiLogError(
          rv, ApiType, "Unable to register parity error switch event callback");
    }
  } else {
    // This is reverse of the registration sequence.
    // First, unregister callback, then unregister events.

    {
      auto g{SaiApiLock::getInstance()->lock()};
      // First unregister callback function
      auto rv = _setAttribute(id, &attr);
      saiLogError(rv, ApiType, "Unable to unregister TAM event callback");
    }

    {
      auto g{SaiApiLock::getInstance()->lock()};
      // Then unregister switch events
      eventAttr.value.u32list.count = 0;
      auto rv = _setAttribute(id, &eventAttr);
      saiLogError(rv, ApiType, "Unable to unregister switch events");
    }
  }
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDelayDropCongThreshold::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeForceTrafficOverFabricWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX)
  return SAI_SWITCH_ATTR_FORCE_TRAFFIC_OVER_FABRIC;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSwitchIsolateWrapper::operator()() {
#if defined(BRCM_SAI_SDK_DNX)
  return SAI_SWITCH_ATTR_SWITCH_ISOLATE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricRemoteReachablePortList::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_FABRIC_REMOTE_REACHABLE_PORT_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeRouteNoImplicitMetaDataWrapper::operator()() {
#if defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_ROUTE_NO_IMPLICIT_META_DATA;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeRouteAllowImplicitMetaDataWrapper::operator()() {
#if defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_ROUTE_ALLOW_IMPLICIT_META_DATA;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeMultiStageLocalSwitchIdsWrapper::operator()() {
#if defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_MULTI_STAGE_LOCAL_SWITCH_IDS;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLocalNs::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MIN_LOCAL;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLocalNs::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MAX_LOCAL;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLevel1Ns::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MIN_LEVEL_1;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLevel1Ns::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MAX_LEVEL_1;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMinLevel2Ns::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MIN_LEVEL_2;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeVoqLatencyMaxLevel2Ns::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_VOQ_LATENCY_MAX_LEVEL_2;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeReachabilityGroupList::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  return SAI_SWITCH_ATTR_REACHABILITY_GROUP_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeFabricLinkLayerFlowControlThreshold::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  return SAI_SWITCH_ATTR_FABRIC_LLFC_THRESHOLD;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeNoAclsForTrapsWrapper::operator()() {
#if defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_NO_ACLS_FOR_TRAPS;
#else
  return std::nullopt;
#endif
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSystemPortId::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  return SAI_SWITCH_ATTR_MAX_SYSTEM_PORT_ID;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxLocalSystemPortId::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0) && \
    !defined(BRCM_SAI_SDK_GTE_13_0)
  return SAI_SWITCH_ATTR_MAX_LOCAL_SYSTEM_PORT_ID;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSystemPorts::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  return SAI_SWITCH_ATTR_MAX_SYSTEM_PORTS;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxVoqs::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_12_0)
  return SAI_SWITCH_ATTR_MAX_VOQS;
#endif
  return std::nullopt;
}
std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeCondEntropyRehashPeriodUS::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_COND_ENTROPY_REHASH_PERIOD_US;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelSrcIp::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SHEL_SRC_IP;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelDstIp::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SHEL_DST_IP;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelSrcMac::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SHEL_SRC_MAC;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeShelPeriodicInterval::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_0)
  return SAI_SWITCH_ATTR_SHEL_PERIODIC_INTERVAL;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareCoreTouse::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  return SAI_SWITCH_ATTR_FIRMWARE_CORE_TO_USE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareLogFile::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  return SAI_SWITCH_ATTR_FIRMWARE_LOG_PATH_NAME;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeMaxSwitchId::operator()() {
#if defined(BRCM_SAI_SDK_DNX) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_MAX_SWITCH_ID;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeArsAvailableFlows::operator()() {
#if defined(BRCM_SAI_SDK_XGS) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_ARS_AVAILABLE_FLOWS;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkRegDumpLogPath::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  return SAI_SWITCH_ATTR_SDK_DUMP_LOG_PATH_NAME;
#endif
  return std::nullopt;
}

// Time window in milliseconds within which the SDK writes at most one
// register/state dump for a NORMAL priority event - masked interrupt, soft
// reset, RX FIFO stuck, and the interrupt status log taken at switch create.
// HIGH priority events - hard reset, fabric auto isolate and an on demand tech
// support request - bypass the limiter and always write. Two values are
// special: 0 disables rate limiting, which is also the SDK default, and
// 0xFFFFFFFF suppresses NORMAL priority dumps entirely. The SDK fails open and
// allows the write if it cannot read the configured window.
//
// The id only exists on the DNX releases carrying the CS00012465650 backport,
// see SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED. Returning nullopt elsewhere is what
// keeps SaiExtensionAttribute from CHECK failing on an unresolvable id.
std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkDumpRateLimitWindow::operator()() {
#if defined(SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED)
  return SAI_SWITCH_ATTR_SDK_DUMP_RATE_LIMIT_WINDOW;
#endif
  return std::nullopt;
}

// Number of register/state dump writes the SDK rate limiter has skipped. The
// SDK clears the counter to zero on every read, so a read yields the count
// accumulated since the previous read and never a running total - callers have
// to sum the deltas themselves. READ_ONLY, so it is not a create attribute.
//
// The id only exists on the DNX releases carrying the CS00012465650 backport,
// see SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED. Returning nullopt elsewhere is what
// keeps SaiExtensionAttribute from CHECK failing on an unresolvable id.
std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeSdkDumpSuppressedCount::operator()() {
#if defined(SAI_SDK_DUMP_RATE_LIMIT_SUPPORTED)
  return SAI_SWITCH_ATTR_SDK_DUMP_SUPPRESSED_COUNT;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeFirmwareObjectList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_11_7)
  return SAI_SWITCH_ATTR_FIRMWARE_OBJECTS;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeTcRateLimitList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_TC_RATE_LIMIT_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeTechSupportType::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_TECH_SUPPORT_TYPE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributePfcTcDldTimerGranularityInterval::operator()() {
#if defined(BRCM_SAI_SDK_XGS) && defined(BRCM_SAI_SDK_GTE_11_0)
  return SAI_SWITCH_ATTR_PFC_TC_DLD_TIMER_INTERVAL;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeNumberOfPipes::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_NUMBER_OF_PIPES;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePipelineObjectList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_PIPELINE_OBJECTS;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDisableSllAndHllTimeout::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAsicRevision::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  return SAI_SWITCH_ATTR_ASIC_REVISION;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeCreditRequestProfileSchedulerMode::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_CREDIT_REQUEST_PROFILE_SCHEDULER_MODE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeModuleIdToCreditRequestProfileParamList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_12_0)
  return SAI_SWITCH_ATTR_MODULE_ID_TO_CREDIT_REQUEST_PROFILE_PARAM_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeTriggerSimulatedEccCorrectableError::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeTriggerSimulatedEccUnCorrectableError::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeDefaultCpuEgressBufferPool::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeModuleIdFabricPortList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  return SAI_SWITCH_ATTR_MODULE_ID_FABRIC_PORT_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeLocalSystemPortIdRangeList::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_13_0)
  return SAI_SWITCH_ATTR_LOCAL_SYSTEM_PORT_ID_RANGE_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePfcMonitorEnable::operator()() {
#if defined(BRCM_SAI_SDK_GTE_13_0) && !defined(BRCM_SAI_SDK_DNX)
  return SAI_SWITCH_ATTR_PFC_MONITOR_ENABLE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t> SaiSwitchTraits::Attributes::
    AttributeCablePropagationDelayMeasurement::operator()() {
#if defined(BRCM_SAI_SDK_DNX_GTE_14_0) || defined(BRCM_SAI_SDK_XGS_GTE_14_2)
  return SAI_SWITCH_ATTR_CABLE_PROPAGATION_DELAY_MEASUREMENT;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePortCl72RetryEnable::operator()() {
#if defined(BRCM_SAI_SDK_XGS_GTE_14_2)
  return SAI_SWITCH_ATTR_PORT_CL72_RETRY_ENABLE;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributeAclDropSnoopDisabled::operator()() {
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePacketDropTypeIngressList::operator()() {
#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
  return SAI_SWITCH_ATTR_EXT_PACKET_DROP_TYPE_INGRESS_LIST;
#endif
  return std::nullopt;
}

std::optional<sai_attr_id_t>
SaiSwitchTraits::Attributes::AttributePacketDropTypeEgressList::operator()() {
#if defined(BRCM_SAI_SDK_XGS_GTE_15_0)
  return SAI_SWITCH_ATTR_EXT_PACKET_DROP_TYPE_EGRESS_LIST;
#endif
  return std::nullopt;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::deviceWatermarkBytes() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

const std::vector<sai_stat_id_t>& SaiSwitchTraits::customDropBitmapStats() {
  static const std::vector<sai_stat_id_t> stats;
  return stats;
}

} // namespace facebook::fboss
