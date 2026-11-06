/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/tracer/PortApiTracer.h"
#include <typeindex>
#include <utility>

#include "fboss/agent/hw/sai/api/PortApi.h"
#include "fboss/agent/hw/sai/tracer/Utils.h"

using folly::to;

namespace {
std::map<int32_t, std::pair<std::string, std::size_t>> _PortMap{
    SAI_ATTR_MAP(Port, HwLaneList),
    SAI_ATTR_MAP(Port, Speed),
    SAI_ATTR_MAP(Port, AdminState),
    SAI_ATTR_MAP(Port, FecMode),
    SAI_ATTR_MAP(Port, OperStatus),
    SAI_ATTR_MAP(Port, InternalLoopbackMode),
    SAI_ATTR_MAP(Port, MediaType),
    SAI_ATTR_MAP(Port, GlobalFlowControlMode),
    SAI_ATTR_MAP(Port, PortVlanId),
    SAI_ATTR_MAP(Port, Mtu),
    SAI_ATTR_MAP(Port, Metadata),
    SAI_ATTR_MAP(Port, QosDscpToTcMap),
    SAI_ATTR_MAP(Port, QosDot1pToTcMap),
    SAI_ATTR_MAP(Port, QosTcAndColorToDot1pMap),
    SAI_ATTR_MAP(Port, QosTcToQueueMap),
    SAI_ATTR_MAP(Port, DisableTtlDecrement),
    SAI_ATTR_MAP(Port, QosNumberOfQueues),
    SAI_ATTR_MAP(Port, QosQueueList),
    SAI_ATTR_MAP(Port, QosEgressBufferProfileList),
    SAI_ATTR_MAP(Port, QosIngressBufferProfileList),
    SAI_ATTR_MAP(Port, Type),
    SAI_ATTR_MAP(Port, InterfaceType),
    SAI_ATTR_MAP(Port, PktTxEnable),
    SAI_ATTR_MAP(Port, TamObject),
    SAI_ATTR_MAP(Port, SerdesId),
    SAI_ATTR_MAP(Port, IngressMirrorSession),
    SAI_ATTR_MAP(Port, EgressMirrorSession),
    SAI_ATTR_MAP(Port, IngressSamplePacketEnable),
    SAI_ATTR_MAP(Port, EgressSamplePacketEnable),
    SAI_ATTR_MAP(Port, IngressSampleMirrorSession),
    SAI_ATTR_MAP(Port, EgressSampleMirrorSession),
    SAI_ATTR_MAP(Port, PrbsPolynomial),
    SAI_ATTR_MAP(Port, PrbsConfig),
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
    SAI_ATTR_MAP(Port, PrbsRxState),
#endif
    SAI_ATTR_MAP(Port, IngressAcl),
    SAI_ATTR_MAP(Port, IsolationGroup),
    SAI_ATTR_MAP(Port, IngressMacSecAcl),
    SAI_ATTR_MAP(Port, EgressMacSecAcl),
    SAI_ATTR_MAP(Port, PtpMode),
    SAI_ATTR_MAP(Port, PortEyeValues),
    SAI_ATTR_MAP(Port, PriorityFlowControlMode),
    SAI_ATTR_MAP(Port, PriorityFlowControl),
#if !defined(TAJO_SDK)
    SAI_ATTR_MAP(Port, PriorityFlowControlRx),
    SAI_ATTR_MAP(Port, PriorityFlowControlTx),
#endif
    SAI_ATTR_MAP(Port, PortErrStatus),
    SAI_ATTR_MAP(Port, IngressPriorityGroupList),
    SAI_ATTR_MAP(Port, NumberOfIngressPriorityGroups),
    SAI_ATTR_MAP(Port, QosTcToPriorityGroupMap),
#if defined(SAI_CBFC_SUPPORTED)
    SAI_ATTR_MAP(Port, QosTcToVcMap),
    SAI_ATTR_MAP(Port, CbfcSenderCreditLimit),
    SAI_ATTR_MAP(Port, CbfcSenderCreditSize),
    SAI_ATTR_MAP(Port, CbfcSenderPacketOverhead),
    SAI_ATTR_MAP(Port, QosQueueToVcMap),
    SAI_ATTR_MAP(Port, CbfcReceiverNativeCreditSize),
    SAI_ATTR_MAP(Port, CbfcReceiverNativePacketOverhead),
    SAI_ATTR_MAP(Port, CbfcReceiverNativeTotalCredits),
#endif
    SAI_ATTR_MAP(Port, QosPfcPriorityToQueueMap),
    SAI_ATTR_MAP(Port, QosPfcPriorityToPriorityGroupMap),
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    SAI_ATTR_MAP(Port, PortLoopbackMode),
    SAI_ATTR_MAP(Port, UseExtendedFec),
    SAI_ATTR_MAP(Port, ExtendedFecMode),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
    SAI_ATTR_MAP(Port, FabricIsolate),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    SAI_ATTR_MAP(Port, LlrModeLocal),
    SAI_ATTR_MAP(Port, LlrModeRemote),
    SAI_ATTR_MAP(Port, LlrProfile),
    SAI_ATTR_MAP(Port, LlrTxStatus),
    SAI_ATTR_MAP(Port, LlrRxStatus),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
    SAI_ATTR_MAP(Port, RxSignalDetect),
    SAI_ATTR_MAP(Port, RxLockStatus),
    SAI_ATTR_MAP(Port, FecAlignmentLock),
    SAI_ATTR_MAP(Port, PcsRxLinkStatus),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    SAI_ATTR_MAP(Port, InterFrameGap),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
    SAI_ATTR_MAP(Port, RxFrequencyPPM),
    SAI_ATTR_MAP(Port, RxSNR),
#endif
    SAI_ATTR_MAP(Port, LinkTrainingEnable),
    SAI_ATTR_MAP(Port, LinkTrainingRxStatus),
    SAI_ATTR_MAP(Port, FabricAttached),
    SAI_ATTR_MAP(Port, FabricAttachedPortIndex),
    SAI_ATTR_MAP(Port, FabricAttachedSwitchId),
    SAI_ATTR_MAP(Port, FabricAttachedSwitchType),
    SAI_ATTR_MAP(Port, FabricReachability),
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    SAI_ATTR_MAP(Port, PfcTcDldInterval),
    SAI_ATTR_MAP(Port, PfcTcDlrInterval),
    SAI_ATTR_MAP(Port, PfcTcDldIntervalRange),
    SAI_ATTR_MAP(Port, PfcTcDlrIntervalRange),
#endif
    SAI_ATTR_MAP(Port, SystemPort),
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
    SAI_ATTR_MAP(Port, TxReadyStatus),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    SAI_ATTR_MAP(Port, ArsEnable),
    SAI_ATTR_MAP(Port, ArsPortLoadScalingFactor),
    SAI_ATTR_MAP(Port, ArsPortLoadPastWeight),
    SAI_ATTR_MAP(Port, ArsPortLoadFutureWeight),
#endif
#if defined(CHENAB_SAI_SDK)
    SAI_ATTR_MAP(Port, AutoNegotiationMode),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _PortSerdesMap{
    SAI_ATTR_MAP(PortSerdes, PortId),
#if !defined(CHENAB_SAI_SDK)
    SAI_ATTR_MAP(PortSerdes, Preemphasis),
#endif
    SAI_ATTR_MAP(PortSerdes, IDriver),
    SAI_ATTR_MAP(PortSerdes, TxFirPre1),
    SAI_ATTR_MAP(PortSerdes, TxFirPre2),
    SAI_ATTR_MAP(PortSerdes, TxFirPre3),
    SAI_ATTR_MAP(PortSerdes, TxFirMain),
    SAI_ATTR_MAP(PortSerdes, TxFirPost1),
    SAI_ATTR_MAP(PortSerdes, TxFirPost2),
    SAI_ATTR_MAP(PortSerdes, TxFirPost3),
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    SAI_ATTR_MAP(PortSerdes, TxPrecoding),
    SAI_ATTR_MAP(PortSerdes, RxPrecoding),
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
    SAI_ATTR_MAP(PortSerdes, CustomCollection),
#endif
};

std::map<int32_t, std::pair<std::string, std::size_t>> _PortConnectorMap{
    SAI_ATTR_MAP(PortConnector, LineSidePortId),
    SAI_ATTR_MAP(PortConnector, SystemSidePortId),
};

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
std::map<int32_t, std::pair<std::string, std::size_t>> _PortLlrProfileMap{
    SAI_ATTR_MAP(PortLlrProfile, OutstandingFramesMax),
    SAI_ATTR_MAP(PortLlrProfile, OutstandingBytesMax),
    SAI_ATTR_MAP(PortLlrProfile, ReplayTimerMax),
    SAI_ATTR_MAP(PortLlrProfile, ReplayCountMax),
    SAI_ATTR_MAP(PortLlrProfile, PcsLostTimeout),
    SAI_ATTR_MAP(PortLlrProfile, DataAgeTimeout),
    SAI_ATTR_MAP(PortLlrProfile, InitLlrFrameAction),
    SAI_ATTR_MAP(PortLlrProfile, FlushLlrFrameAction),
    SAI_ATTR_MAP(PortLlrProfile, ReInitOnFlush),
    SAI_ATTR_MAP(PortLlrProfile, CtlosTargetSpacing),
};
#endif

void handleExtensionAttributes() {
  SAI_EXT_ATTR_MAP(Port, SystemPortId)
  SAI_EXT_ATTR_MAP(Port, SerdesLaneList)
  SAI_EXT_ATTR_MAP(Port, DiagModeEnable)
  SAI_EXT_ATTR_MAP(Port, FdrEnable)
  SAI_EXT_ATTR_MAP(Port, RxLaneSquelchEnable)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  SAI_EXT_ATTR_MAP(Port, CrcErrorDetect)
#endif
  SAI_EXT_ATTR_MAP(PortSerdes, RxCtleCode)
  SAI_EXT_ATTR_MAP(PortSerdes, RxDspMode)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAfeTrim)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAcCouplingByPass)
  SAI_EXT_ATTR_MAP(PortSerdes, RxAfeAdaptiveEnable)
  SAI_EXT_ATTR_MAP(PortSerdes, TxLutMode)
  SAI_EXT_ATTR_MAP(Port, CablePropogationDelayNS)
  SAI_EXT_ATTR_MAP(Port, FabricDataCellsFilterStatus)
  SAI_EXT_ATTR_MAP(Port, ReachabilityGroup)
  SAI_EXT_ATTR_MAP(Port, FecErrorDetectEnable)
  SAI_EXT_ATTR_MAP(Port, AmIdles)
  SAI_EXT_ATTR_MAP(Port, ResetQueueCreditBalance)
  SAI_EXT_ATTR_MAP(PortSerdes, TxDiffEncoderEn)
  SAI_EXT_ATTR_MAP(PortSerdes, TxDigGain)
  SAI_EXT_ATTR_MAP(PortSerdes, TxFfeCoeff0)
  SAI_EXT_ATTR_MAP(PortSerdes, TxFfeCoeff1)
  SAI_EXT_ATTR_MAP(PortSerdes, TxFfeCoeff2)
  SAI_EXT_ATTR_MAP(PortSerdes, TxFfeCoeff3)
  SAI_EXT_ATTR_MAP(PortSerdes, TxFfeCoeff4)
  SAI_EXT_ATTR_MAP(PortSerdes, TxDriverSwing)
  SAI_EXT_ATTR_MAP(PortSerdes, TxLdoBypass)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost1Start)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost1Step)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost1Stop)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost2OrHrStart)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost2OrHrStep)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgBoost2OrHrStop)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgC1Start1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgC1Step1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgC1Stop1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgDfeStart1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgDfeStep1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgDfeStop1p7)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEnableScanSelection)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgScanUseSrSettings)
  SAI_EXT_ATTR_MAP(Port, LinkScanMode)
  SAI_EXT_ATTR_MAP(Port, CsigTagAction)
  SAI_EXT_ATTR_MAP(Port, CsigLinkLocator)
  SAI_EXT_ATTR_MAP(PortSerdes, RxCdrCfgOvEn)
  SAI_EXT_ATTR_MAP(PortSerdes, RxCdrTdet1stOrdStepOvVal)
  SAI_EXT_ATTR_MAP(PortSerdes, RxCdrTdet2ndOrdStepOvVal)
  SAI_EXT_ATTR_MAP(PortSerdes, RxCdrTdetFineStepOvVal)
  SAI_EXT_ATTR_MAP(PortSerdes, RxLdoBypass)
  SAI_EXT_ATTR_MAP(PortSerdes, RxDiffEncoderEn)
  SAI_EXT_ATTR_MAP(PortSerdes, RxInstgEnableScan)
  SAI_EXT_ATTR_MAP(PortSerdes, RxFfeLengthBitmap)
  SAI_EXT_ATTR_MAP(PortSerdes, RxFfeLmsDynamicGatingEn)
  SAI_EXT_ATTR_MAP(Port, CondEntropyRehashEnable)
  SAI_EXT_ATTR_MAP(Port, CondEntropyRehashPeriodUS)
  SAI_EXT_ATTR_MAP(Port, CondEntropyRehashSeed)
  SAI_EXT_ATTR_MAP(Port, ShelEnable)
  SAI_EXT_ATTR_MAP(Port, PgDropStatus)
  SAI_EXT_ATTR_MAP(Port, FabricSystemPort)
  SAI_EXT_ATTR_MAP(Port, StaticModuleId)
  SAI_EXT_ATTR_MAP(Port, PfcMonitorDirection)
  SAI_EXT_ATTR_MAP(Port, CablePropagationDelayMediaType)
  SAI_EXT_ATTR_MAP(Port, PfcPauseDurationOverride)
  SAI_EXT_ATTR_MAP(Port, CablePropagationDelayMeasure)
  SAI_EXT_ATTR_MAP(Port, LinkUpDebouncePeriodMs)
  SAI_EXT_ATTR_MAP(Port, LinkDownDebouncePeriodMs)
  SAI_EXT_ATTR_MAP(Port, LinkUpDebounceRetriggerCount)
  SAI_EXT_ATTR_MAP(Port, LinkDownDebounceRetriggerCount)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  SAI_EXT_ATTR_MAP(Port, ExtOperStatusLatch)
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0)
  SAI_EXT_ATTR_MAP(PortSerdes, Dco)
  SAI_EXT_ATTR_MAP(PortSerdes, FltM)
  SAI_EXT_ATTR_MAP(PortSerdes, FltS)
  SAI_EXT_ATTR_MAP(PortSerdes, RxReach)
  SAI_EXT_ATTR_MAP(PortSerdes, TransmitPrecodingState)
  SAI_EXT_ATTR_MAP(PortSerdes, ReceivePrecodingState)
  SAI_EXT_ATTR_MAP(PortSerdes, RVga)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEq1)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEq2)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEq3)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEqM)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEqP1)
  SAI_EXT_ATTR_MAP(PortSerdes, RxEqP2)
  SAI_EXT_ATTR_MAP(PortSerdes, RxPf)
  SAI_EXT_ATTR_MAP(PortSerdes, RxPfLfq)
  SAI_EXT_ATTR_MAP(PortSerdes, RxPfHfq)
  SAI_EXT_ATTR_MAP(PortSerdes, RxTap1)
  SAI_EXT_ATTR_MAP(PortSerdes, RxTap2)
  SAI_EXT_ATTR_MAP(PortSerdes, TpChn0)
  SAI_EXT_ATTR_MAP(PortSerdes, TpChn1)
  SAI_EXT_ATTR_MAP(PortSerdes, TpChn2)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
  SAI_EXT_ATTR_MAP(Port, ArsLinkState)
#endif
#if defined(BRCM_PAI_SDK)
  SAI_EXT_ATTR_MAP(PortSerdes, RxReach)
#endif
  SAI_EXT_ATTR_MAP(Port, IsHyperPortMember)
  SAI_EXT_ATTR_MAP(Port, HyperPortMemberList)
}

} // namespace

namespace facebook::fboss {

WRAP_CREATE_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_REMOVE_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_SET_ATTR_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_GET_ATTR_FUNC(port, SAI_OBJECT_TYPE_PORT, port);

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
WRAP_BULK_GET_ATTR_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
#endif

WRAP_GET_STATS_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_GET_STATS_EXT_FUNC(port, SAI_OBJECT_TYPE_PORT, port);
WRAP_CLEAR_STATS_FUNC(port, SAI_OBJECT_TYPE_PORT, port);

WRAP_CREATE_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_REMOVE_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_SET_ATTR_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);
WRAP_GET_ATTR_FUNC(port_serdes, SAI_OBJECT_TYPE_PORT_SERDES, port);

WRAP_CREATE_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_REMOVE_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_SET_ATTR_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);
WRAP_GET_ATTR_FUNC(port_connector, SAI_OBJECT_TYPE_PORT_CONNECTOR, port);

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
WRAP_CREATE_FUNC(port_llr_profile, SAI_OBJECT_TYPE_PORT_LLR_PROFILE, port);
WRAP_REMOVE_FUNC(port_llr_profile, SAI_OBJECT_TYPE_PORT_LLR_PROFILE, port);
WRAP_SET_ATTR_FUNC(port_llr_profile, SAI_OBJECT_TYPE_PORT_LLR_PROFILE, port);
WRAP_GET_ATTR_FUNC(port_llr_profile, SAI_OBJECT_TYPE_PORT_LLR_PROFILE, port);
#endif

sai_status_t wrap_clear_port_all_stats(sai_object_id_t port_id) {
  return SaiTracer::getInstance()->portApi_->clear_port_all_stats(port_id);
}

sai_status_t wrap_create_port_pool(
    sai_object_id_t* port_pool_id,
    sai_object_id_t switch_id,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  return SaiTracer::getInstance()->portApi_->create_port_pool(
      port_pool_id, switch_id, attr_count, attr_list);
}

sai_status_t wrap_remove_port_pool(sai_object_id_t port_pool_id) {
  return SaiTracer::getInstance()->portApi_->remove_port_pool(port_pool_id);
}

sai_status_t wrap_set_port_pool_attribute(
    sai_object_id_t port_pool_id,
    const sai_attribute_t* attr) {
  return SaiTracer::getInstance()->portApi_->set_port_pool_attribute(
      port_pool_id, attr);
}

sai_status_t wrap_get_port_pool_attribute(
    sai_object_id_t port_pool_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_attribute(
      port_pool_id, attr_count, attr_list);
}

sai_status_t wrap_get_port_pool_stats(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    uint64_t* counters) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_stats(
      port_pool_id, number_of_counters, counter_ids, counters);
}

sai_status_t wrap_get_port_pool_stats_ext(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids,
    sai_stats_mode_t mode,
    uint64_t* counters) {
  return SaiTracer::getInstance()->portApi_->get_port_pool_stats_ext(
      port_pool_id, number_of_counters, counter_ids, mode, counters);
}

sai_status_t wrap_clear_port_pool_stats(
    sai_object_id_t port_pool_id,
    uint32_t number_of_counters,
    const sai_stat_id_t* counter_ids) {
  return SaiTracer::getInstance()->portApi_->clear_port_pool_stats(
      port_pool_id, number_of_counters, counter_ids);
}

sai_port_api_t* wrappedPortApi() {
  handleExtensionAttributes();
  static sai_port_api_t portWrappers;

  portWrappers.create_port = &wrap_create_port;
  portWrappers.remove_port = &wrap_remove_port;
  portWrappers.set_port_attribute = &wrap_set_port_attribute;
  portWrappers.get_port_attribute = &wrap_get_port_attribute;
  portWrappers.get_port_stats = &wrap_get_port_stats;
  portWrappers.get_port_stats_ext = &wrap_get_port_stats_ext;
  portWrappers.clear_port_stats = &wrap_clear_port_stats;
  portWrappers.clear_port_all_stats = &wrap_clear_port_all_stats;
  portWrappers.create_port_serdes = &wrap_create_port_serdes;
  portWrappers.remove_port_serdes = &wrap_remove_port_serdes;
  portWrappers.set_port_serdes_attribute = &wrap_set_port_serdes_attribute;
  portWrappers.get_port_serdes_attribute = &wrap_get_port_serdes_attribute;
  portWrappers.create_port_connector = &wrap_create_port_connector;
  portWrappers.remove_port_connector = &wrap_remove_port_connector;
  portWrappers.set_port_connector_attribute =
      &wrap_set_port_connector_attribute;
  portWrappers.get_port_connector_attribute =
      &wrap_get_port_connector_attribute;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  portWrappers.create_port_llr_profile = &wrap_create_port_llr_profile;
  portWrappers.remove_port_llr_profile = &wrap_remove_port_llr_profile;
  portWrappers.set_port_llr_profile_attribute =
      &wrap_set_port_llr_profile_attribute;
  portWrappers.get_port_llr_profile_attribute =
      &wrap_get_port_llr_profile_attribute;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  portWrappers.get_ports_attribute = &wrap_get_ports_attribute;
#endif
  return &portWrappers;
}

SET_SAI_ATTRIBUTES(Port)
SET_SAI_ATTRIBUTES(PortSerdes)
SET_SAI_ATTRIBUTES(PortConnector)
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
SET_SAI_ATTRIBUTES(PortLlrProfile)
#endif

} // namespace facebook::fboss
