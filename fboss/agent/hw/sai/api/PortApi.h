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

#include "fboss/agent/hw/sai/api/SaiApi.h"
#include "fboss/agent/hw/sai/api/SaiAttribute.h"
#include "fboss/agent/hw/sai/api/SaiAttributeDataTypes.h"
#include "fboss/agent/hw/sai/api/SaiVersion.h"
#include "fboss/agent/hw/sai/api/Types.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <vector>

extern "C" {
#include <sai.h>
}

bool operator==(const sai_map_t& lhs, const sai_map_t& rhs);
bool operator!=(const sai_map_t& lhs, const sai_map_t& rhs);

/*
 * SDKs that expose a per-port link up/down debounce hold timer. Leaba has both
 * (SAI_PORT_ATTR_LINK_{UP,DOWN}_DEBOUNCE_PERIOD_MILLISECONDS); Broadcom so far
 * only has the down timer (the SAI_PORT_ATTR_LINK_DOWN_DEBOUNCE_TIMEOUT
 * extension). These gate the corresponding SaiPortTraits::CreateAttributes
 * tuple members, so every CreateAttributes brace-init site has to be gated on
 * the same macro or the tuple arity will not line up.
 */
#if defined(TAJO_SDK_GTE_26_2) || defined(TAJO_SDK_VERSION_25_5_4210)
#define FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD
#endif

#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD) || \
    defined(BRCM_SAI_SDK_GTE_15_4)
#define FBOSS_SAI_PORT_LINK_DOWN_DEBOUNCE_PERIOD
#endif

namespace facebook::fboss {

class PortApi;

struct SaiPortTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_PORT;
  using SaiApiT = PortApi;
  struct Attributes {
    using EnumType = sai_port_attr_t;
    using AdminState = SaiAttribute<EnumType, SAI_PORT_ATTR_ADMIN_STATE, bool>;
    using HwLaneList = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_HW_LANE_LIST,
        std::vector<uint32_t>>;
    struct AttributeSerdesLaneList {
      std::optional<sai_attr_id_t> operator()();
    };
    using SerdesLaneList =
        SaiExtensionAttribute<std::vector<uint32_t>, AttributeSerdesLaneList>;
    struct AttributeDiagModeEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using DiagModeEnable = SaiExtensionAttribute<bool, AttributeDiagModeEnable>;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
    struct AttributeCrcErrorDetect {
      std::optional<sai_attr_id_t> operator()();
    };
    using CrcErrorDetect =
        SaiExtensionAttribute<sai_latch_status_t, AttributeCrcErrorDetect>;
    struct AttributeExtOperStatusLatch {
      std::optional<sai_attr_id_t> operator()();
    };
    using ExtOperStatusLatch =
        SaiExtensionAttribute<sai_latch_status_t, AttributeExtOperStatusLatch>;
#endif
    struct AttributeFdrEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using FdrEnable =
        SaiExtensionAttribute<bool, AttributeFdrEnable, SaiBoolDefaultFalse>;
    using Speed = SaiAttribute<EnumType, SAI_PORT_ATTR_SPEED, sai_uint32_t>;
    using Type = SaiAttribute<EnumType, SAI_PORT_ATTR_TYPE, sai_int32_t>;
    using QosNumberOfQueues = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_NUMBER_OF_QUEUES,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using QosQueueList = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_QUEUE_LIST,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using QosEgressBufferProfileList = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_EGRESS_BUFFER_PROFILE_LIST,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using QosIngressBufferProfileList = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_INGRESS_BUFFER_PROFILE_LIST,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using FecMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FEC_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using OperStatus =
        SaiAttribute<EnumType, SAI_PORT_ATTR_OPER_STATUS, sai_int32_t>;
    using InternalLoopbackMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INTERNAL_LOOPBACK_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
    using FabricIsolate = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FABRIC_ISOLATE,
        bool,
        SaiBoolDefaultFalse>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
    using PortLoopbackMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LOOPBACK_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using UseExtendedFec = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_USE_EXTENDED_FEC,
        bool,
        SaiBoolDefaultFalse>;
    using ExtendedFecMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FEC_MODE_EXTENDED,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
#endif
    using MediaType = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_MEDIA_TYPE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using GlobalFlowControlMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_GLOBAL_FLOW_CONTROL_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using PortVlanId = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PORT_VLAN_ID,
        sai_uint16_t,
        SaiVlanIdDefault>;
    using Mtu = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_MTU,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using Metadata = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_META_DATA,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using QosDscpToTcMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_DSCP_TO_TC_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using QosDot1pToTcMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_DOT1P_TO_TC_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using QosTcAndColorToDot1pMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_TC_AND_COLOR_TO_DOT1P_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using QosTcToQueueMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_TC_TO_QUEUE_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using DisableTtlDecrement = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_DISABLE_DECREMENT_TTL,
        bool,
        SaiBoolDefaultFalse>;
    using InterfaceType = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INTERFACE_TYPE,
        sai_int32_t,
        SaiPortInterfaceTypeDefault>;
    using PktTxEnable = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PKT_TX_ENABLE,
        bool,
        SaiBoolDefaultTrue>;
    using TamObject = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_TAM_OBJECT,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using SerdesId = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PORT_SERDES_ID,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using IngressMirrorSession = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_MIRROR_SESSION,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using EgressMirrorSession = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_EGRESS_MIRROR_SESSION,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using IngressSamplePacketEnable = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_SAMPLEPACKET_ENABLE,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using EgressSamplePacketEnable = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_EGRESS_SAMPLEPACKET_ENABLE,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using IngressSampleMirrorSession = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_SAMPLE_MIRROR_SESSION,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using EgressSampleMirrorSession = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_EGRESS_SAMPLE_MIRROR_SESSION,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using PrbsPolynomial = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRBS_POLYNOMIAL,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using PrbsConfig = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRBS_CONFIG,
        sai_int32_t,
        SaiPrbsConfigDefault>;
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
    using PrbsRxState = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRBS_RX_STATE,
        sai_prbs_rx_state_t,
        SaiPrbsRxStateDefault>;
#endif
    using IngressAcl = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_ACL,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    // Packets ingressing this port are not forwarded to the members of this
    // isolation group. Nullable; SAI_NULL_OBJECT_ID means no isolation.
    using IsolationGroup = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ISOLATION_GROUP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using IngressMacSecAcl = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_MACSEC_ACL,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using EgressMacSecAcl = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_EGRESS_MACSEC_ACL,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    struct AttributeSystemPortId {
      std::optional<sai_attr_id_t> operator()();
    };
    using SystemPortId =
        SaiExtensionAttribute<sai_uint16_t, AttributeSystemPortId>;
    struct AttributeAttachedCoreIndex {
      std::optional<sai_attr_id_t> operator()();
    };
    using AttachedCoreIndex =
        SaiExtensionAttribute<sai_uint32_t, AttributeAttachedCoreIndex>;
    struct AttributeAttachedCorePortIndex {
      std::optional<sai_attr_id_t> operator()();
    };
    using AttachedCorePortIndex =
        SaiExtensionAttribute<sai_uint32_t, AttributeAttachedCorePortIndex>;
    using PtpMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PTP_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using PortEyeValues = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_EYE_VALUES,
        std::vector<sai_port_lane_eye_values_t>,
        SaiPortEyeValuesDefault>;
    using PriorityFlowControlMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_MODE,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using PriorityFlowControl = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL,
        sai_uint8_t,
        SaiIntDefault<sai_uint8_t>>;
#if !defined(TAJO_SDK)
    using PriorityFlowControlRx = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_RX,
        sai_uint8_t,
        SaiIntDefault<sai_uint8_t>>;
    using PriorityFlowControlTx = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PRIORITY_FLOW_CONTROL_TX,
        sai_uint8_t,
        SaiIntDefault<sai_uint8_t>>;
#endif
    using PortErrStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ERR_STATUS_LIST,
        std::vector<sai_port_err_status_t>,
        SaiPortErrStatusDefault>;
    using IngressPriorityGroupList = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_INGRESS_PRIORITY_GROUP_LIST,
        std::vector<sai_object_id_t>,
        SaiObjectIdListDefault>;
    using NumberOfIngressPriorityGroups = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_NUMBER_OF_INGRESS_PRIORITY_GROUPS,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using QosTcToPriorityGroupMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_TC_TO_PRIORITY_GROUP_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
#if defined(SAI_CBFC_SUPPORTED)
    using QosTcToVcMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_TC_TO_VC_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    // CBFC S_P_CL, the cap on credits the sender may hold across all of the
    // port's virtual channels. Range 0..2^20-1.
    using CbfcSenderCreditLimit = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    // Bytes per credit the sender charges; must suit the peer's receiver.
    using CbfcSenderCreditSize = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_SENDER_CREDIT_SIZE,
        sai_uint16_t,
        SaiIntDefault<sai_uint16_t>>;
    // Per-packet overhead in bytes the sender charges. Signed: the SAI range
    // is -16..127.
    using CbfcSenderPacketOverhead = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_SENDER_PACKET_OVERHEAD,
        sai_int16_t,
        SaiIntDefault<sai_int16_t>>;
    // Egress classification: which virtual channel a queue drains into, and
    // so whose credit the sender must hold before transmitting from it.
    using QosQueueToVcMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_QUEUE_TO_VC_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    // READ_ONLY. Hardware derives these from the MMU carving; FBOSS never
    // writes them. They are what the receiver advertises to its peer, so they
    // are the only way to see how a PG carving turns into credits.
    using CbfcReceiverNativeCreditSize = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_CREDIT_SIZE,
        sai_uint16_t>;
    using CbfcReceiverNativePacketOverhead = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_PACKET_OVERHEAD,
        sai_int16_t>;
    using CbfcReceiverNativeTotalCredits = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_CBFC_RECEIVER_NATIVE_TOTAL_CREDITS,
        sai_uint16_t>;
#endif
    using QosPfcPriorityToQueueMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_QUEUE_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using QosPfcPriorityToPriorityGroupMap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_QOS_PFC_PRIORITY_TO_PRIORITY_GROUP_MAP,
        SaiObjectIdT,
        SaiObjectIdDefault>;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
    using RxSignalDetect = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_RX_SIGNAL_DETECT,
        std::vector<sai_port_lane_latch_status_t>>;
    using RxLockStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_RX_LOCK_STATUS,
        std::vector<sai_port_lane_latch_status_t>>;
    using FecAlignmentLock = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FEC_ALIGNMENT_LOCK,
        std::vector<sai_port_lane_latch_status_t>>;
    using PcsRxLinkStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PCS_RX_LINK_STATUS,
        sai_latch_status_t>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
    /*
     * PPM = Parts per million
     * RX_FREQUENCY_OFFSET_PPM => amount that a receiver serdes has to
     * compensate for clock differences from the remote side
     */
    using RxFrequencyPPM = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_RX_FREQUENCY_OFFSET_PPM,
        std::vector<sai_port_frequency_offset_ppm_values_t>>;
    using RxSNR = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_RX_SNR,
        std::vector<sai_port_snr_values_t>>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
    using InterFrameGap = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_IPG,
        sai_uint32_t,
        SaiInt96Default>;
#endif
    using LinkTrainingEnable = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LINK_TRAINING_ENABLE,
        bool,
        SaiBoolDefaultTrue>;
    using LinkTrainingRxStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LINK_TRAINING_RX_STATUS,
        sai_int32_t>;
    using FabricAttached =
        SaiAttribute<EnumType, SAI_PORT_ATTR_FABRIC_ATTACHED, bool>;
    using FabricAttachedPortIndex = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FABRIC_ATTACHED_PORT_INDEX,
        sai_uint32_t>;
    using FabricAttachedSwitchId = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FABRIC_ATTACHED_SWITCH_ID,
        sai_uint32_t>;
    using FabricAttachedSwitchType = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FABRIC_ATTACHED_SWITCH_TYPE,
        sai_uint32_t>;
    using FabricReachability = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_FABRIC_REACHABILITY,
        sai_fabric_port_reachability_t>;

    struct AttributeRxLaneSquelchEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using RxLaneSquelchEnable = SaiExtensionAttribute<
        bool,
        AttributeRxLaneSquelchEnable,
        SaiBoolDefaultFalse>;
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
    using PfcTcDldInterval = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL,
        std::vector<sai_map_t>,
        SaiListDefault<sai_map_list_t>>;
    using PfcTcDlrInterval = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL,
        std::vector<sai_map_t>,
        SaiListDefault<sai_map_list_t>>;
    using PfcTcDldIntervalRange = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PFC_TC_DLD_INTERVAL_RANGE,
        sai_u32_range_t,
        SaiIntRangeDefault<sai_u32_range_t>>;
    using PfcTcDlrIntervalRange = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_PFC_TC_DLR_INTERVAL_RANGE,
        sai_u32_range_t,
        SaiIntRangeDefault<sai_u32_range_t>>;
#endif
    using SystemPort = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_SYSTEM_PORT,
        SaiObjectIdT,
        SaiObjectIdDefault>;
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
    using TxReadyStatus =
        SaiAttribute<EnumType, SAI_PORT_ATTR_HOST_TX_READY_STATUS, sai_int32_t>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    using ArsEnable = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ARS_ENABLE,
        bool,
        SaiBoolDefaultFalse>;
    using ArsPortLoadScalingFactor = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ARS_PORT_LOAD_SCALING_FACTOR,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using ArsPortLoadPastWeight = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ARS_PORT_LOAD_PAST_WEIGHT,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
    using ArsPortLoadFutureWeight = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_ARS_PORT_LOAD_FUTURE_WEIGHT,
        sai_uint32_t,
        SaiIntDefault<sai_uint32_t>>;
#endif
    struct AttributeArsLinkState {
      std::optional<sai_attr_id_t> operator()();
    };
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
    using ArsLinkState = SaiExtensionAttribute<
        sai_int32_t,
        AttributeArsLinkState,
        SaiIntDefault<sai_int32_t>>;
#endif
    using AutoNegotiationMode = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_AUTO_NEG_MODE,
        bool,
        SaiBoolDefaultFalse>;
    struct AttributeCablePropogationDelayNS {
      std::optional<sai_attr_id_t> operator()();
    };
    using CablePropogationDelayNS = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeCablePropogationDelayNS,
        SaiIntValueDefault<uint32_t, std::numeric_limits<uint32_t>::max()>>;
    struct AttributeFabricDataCellsFilterStatus {
      std::optional<sai_attr_id_t> operator()();
    };
    using FabricDataCellsFilterStatus = SaiExtensionAttribute<
        bool,
        AttributeFabricDataCellsFilterStatus,
        SaiBoolDefaultFalse>;
    struct AttributeReachabilityGroup {
      std::optional<sai_attr_id_t> operator()();
    };
    using ReachabilityGroup = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeReachabilityGroup,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeCondEntropyRehashEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using CondEntropyRehashEnable = SaiExtensionAttribute<
        bool,
        AttributeCondEntropyRehashEnable,
        SaiBoolDefaultFalse>;
    struct AttributeCondEntropyRehashPeriodUS {
      std::optional<sai_attr_id_t> operator()();
    };
    using CondEntropyRehashPeriodUS = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeCondEntropyRehashPeriodUS,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeCondEntropyRehashSeed {
      std::optional<sai_attr_id_t> operator()();
    };
    using CondEntropyRehashSeed = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeCondEntropyRehashSeed,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeShelEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using ShelEnable =
        SaiExtensionAttribute<bool, AttributeShelEnable, SaiBoolDefaultFalse>;
    struct AttributeFecErrorDetectEnable {
      std::optional<sai_attr_id_t> operator()();
    };
    using FecErrorDetectEnable = SaiExtensionAttribute<
        bool,
        AttributeFecErrorDetectEnable,
        SaiBoolDefaultFalse>;
    struct AttributeAmIdles {
      std::optional<sai_attr_id_t> operator()();
    };
    using AmIdles =
        SaiExtensionAttribute<bool, AttributeAmIdles, SaiBoolDefaultFalse>;
    struct AttributeResetQueueCreditBalance {
      std::optional<sai_attr_id_t> operator()();
    };
    using ResetQueueCreditBalance = SaiExtensionAttribute<
        bool,
        AttributeResetQueueCreditBalance,
        SaiBoolDefaultFalse>;
    struct AttributeFabricSystemPort {
      std::optional<sai_attr_id_t> operator()();
    };
    using FabricSystemPort = SaiExtensionAttribute<
        sai_object_id_t,
        AttributeFabricSystemPort,
        SaiObjectIdDefault>;
    struct AttributeStaticModuleId {
      std::optional<sai_attr_id_t> operator()();
    };
    using StaticModuleId = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeStaticModuleId,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributePgDropStatus {
      std::optional<sai_attr_id_t> operator()();
    };
    using PgDropStatus = SaiExtensionAttribute<
        std::vector<sai_map_t>,
        AttributePgDropStatus,
        SaiListDefault<sai_map_list_t>>;
    struct AttributeIsHyperPortMember {
      std::optional<sai_attr_id_t> operator()();
    };
    using IsHyperPortMember = SaiExtensionAttribute<
        bool,
        AttributeIsHyperPortMember,
        SaiBoolDefaultFalse>;
    struct AttributeHyperPortMemberList {
      std::optional<sai_attr_id_t> operator()();
    };
    using HyperPortMemberList = SaiExtensionAttribute<
        std::vector<sai_object_id_t>,
        AttributeHyperPortMemberList,
        SaiObjectIdListDefault>;
    struct AttributePfcMonitorDirection {
      std::optional<sai_attr_id_t> operator()();
    };
    using PfcMonitorDirection = SaiExtensionAttribute<
        sai_int32_t,
        AttributePfcMonitorDirection,
        SaiIntDefault<sai_int32_t>>;
    struct AttributeCablePropagationDelayMediaType {
      std::optional<sai_attr_id_t> operator()();
    };
    using CablePropagationDelayMediaType = SaiExtensionAttribute<
        sai_int32_t,
        AttributeCablePropagationDelayMediaType,
        SaiIntDefault<sai_int32_t>>;
    struct AttributePfcPauseDurationOverride {
      std::optional<sai_attr_id_t> operator()();
    };
    using PfcPauseDurationOverride = SaiExtensionAttribute<
        sai_uint16_t,
        AttributePfcPauseDurationOverride,
        SaiIntDefault<sai_uint16_t>>;
    struct AttributeCablePropagationDelayMeasure {
      std::optional<sai_attr_id_t> operator()();
    };
    using CablePropagationDelayMeasure = SaiExtensionAttribute<
        bool,
        AttributeCablePropagationDelayMeasure,
        SaiBoolDefaultFalse>;
    struct AttributeLinkUpDebouncePeriodMs {
      std::optional<sai_attr_id_t> operator()();
    };
    using LinkUpDebouncePeriodMs = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeLinkUpDebouncePeriodMs,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeLinkDownDebouncePeriodMs {
      std::optional<sai_attr_id_t> operator()();
    };
    // The default getter is mandatory: this attr lives in CreateAttributes and
    // is read back for every port on store reload. SDK drops that expose the
    // attribute id but do not implement it fail the get with NOT_SUPPORTED;
    // without a default getter SaiApi rethrows and crashes init. With one it
    // falls back to the default of 0, i.e. "no debounce". The BRCM adapter also
    // refuses to read the timer on a port in SW linkscan, which is the state
    // every port is in during store reload, and reports that as
    // SAI_STATUS_ATTR_NOT_SUPPORTED_0 -- covered by the same fallback.
    using LinkDownDebouncePeriodMs = SaiExtensionAttribute<
        sai_uint32_t,
        AttributeLinkDownDebouncePeriodMs,
        SaiIntDefault<sai_uint32_t>>;
    struct AttributeLinkScanMode {
      std::optional<sai_attr_id_t> operator()();
    };
    using LinkScanMode = SaiExtensionAttribute<
        sai_int32_t,
        AttributeLinkScanMode,
        SaiIntDefault<sai_int32_t>>;
    // Cisco CSIG tag action (sai_port_csig_tag_action_t). The SDK default is
    // PASSTHROUGH (0), not the STRIP the SAI header documents.
    struct AttributeCsigTagActionWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    using CsigTagAction = SaiExtensionAttribute<
        sai_int32_t,
        AttributeCsigTagActionWrapper,
        SaiIntDefault<sai_int32_t>>;
    struct AttributeCsigLinkLocatorWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    using CsigLinkLocator = SaiExtensionAttribute<
        sai_uint8_t,
        AttributeCsigLinkLocatorWrapper,
        SaiIntDefault<sai_uint8_t>>;
    // Read-only counts of how many times a link up/down debounce was
    // retriggered by an additional flap while a debounce timeout was already
    // active.
    struct AttributeLinkUpDebounceRetriggerCount {
      std::optional<sai_attr_id_t> operator()();
    };
    using LinkUpDebounceRetriggerCount = SaiExtensionAttribute<
        sai_uint64_t,
        AttributeLinkUpDebounceRetriggerCount,
        SaiIntDefault<sai_uint64_t>>;
    struct AttributeLinkDownDebounceRetriggerCount {
      std::optional<sai_attr_id_t> operator()();
    };
    using LinkDownDebounceRetriggerCount = SaiExtensionAttribute<
        sai_uint64_t,
        AttributeLinkDownDebounceRetriggerCount,
        SaiIntDefault<sai_uint64_t>>;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
    // UEC Link Layer Retry per-port controls (UE Spec 1.0.2 section 5.1).
    // Mode local/remote enable LLR receive/transmit (section 5.1.3); the
    // profile OID binds the port to a SaiPortLlrProfile; TX/RX status expose
    // the LLR transmit and ACK/NACK state machines (sections 5.1.5, 5.1.7).
    // Default getters are mandatory: these attrs live in CreateAttributes and
    // are read back for every port on store reload. On SDK drops that ship the
    // 1.18 headers but do not yet implement LLR at runtime, the get returns
    // NOT_SUPPORTED; without a default getter SaiApi rethrows and crashes init.
    // With one it falls back to the default (LLR off / null profile), matching
    // how other SDK-gated port attrs (e.g. FdrEnable) behave.
    using LlrModeLocal = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LLR_MODE_LOCAL,
        bool,
        SaiBoolDefaultFalse>;
    using LlrModeRemote = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LLR_MODE_REMOTE,
        bool,
        SaiBoolDefaultFalse>;
    using LlrProfile = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LLR_PROFILE,
        SaiObjectIdT,
        SaiObjectIdDefault>;
    using LlrTxStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LLR_TX_STATUS,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
    using LlrRxStatus = SaiAttribute<
        EnumType,
        SAI_PORT_ATTR_LLR_RX_STATUS,
        sai_int32_t,
        SaiIntDefault<sai_int32_t>>;
#endif
  };
  using AdapterKey = PortSaiId;

  /*
   * On some platforms:
   *  - HwLane values are unique only for given Port Type.
   *  - For example, LOGICAL (NIF) and Fabric port may carry same HwLane id.
   *  - HwLaneList is thus insufficient to unambiguously create ports.
   *  - Instead, port create needs Port Type, HwLaneList (and speed).
   *
   * However, the current SAI Spec (v1.12) defines Port Type as Read Only
   * attribute:
   * https://github.com/opencomputeproject/SAI/blob/v1.12.0/inc/saiport.h#L496-L511
   *
   * In future, we will look to enhance the SAI spec to support Port Type attr
   * during creation. In the meantime, use Port Type as part of the
   * AdapterHostKey, CreateAttributes for such platforms.
   *
   * Note:
   *   - Enhancing AdapterHostKey as well as CreateAttribute means that the
   *     AdatperHostKey remains computable from AdaterKey, CreateAttributes: a
   *     requirement for our design.
   *   - It also means create_port would receive Type, a read-only attribute,
   *     and some adapters reject it only after creating the port, leaving a
   *     half-created port behind. Ports created at runtime (breakout) hit
   *     this, so PortApi::_create leaves a LOGICAL Type out of the attribute
   *     list it passes to create_port; the other types (fabric, recycle,
   *     eventor, management, hyper) are still passed, and Type still keys the
   *     SaiStore.
   *   - Long term the fix is a SAI spec change allowing Type on create.
   */

  using AdapterHostKey = std::tuple<
#if defined(BRCM_SAI_SDK_DNX)
      Attributes::Type,
#endif
      Attributes::HwLaneList>;

  using CreateAttributes = std::tuple<
#if defined(BRCM_SAI_SDK_DNX)
      Attributes::Type,
#endif
      Attributes::HwLaneList,
      Attributes::Speed,
      std::optional<Attributes::AdminState>,
      std::optional<Attributes::FecMode>,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0) && !defined(BRCM_PAI_SDK)
      std::optional<Attributes::UseExtendedFec>,
      std::optional<Attributes::ExtendedFecMode>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
      std::optional<Attributes::FabricIsolate>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
      std::optional<Attributes::PortLoopbackMode>,
#else
      std::optional<Attributes::InternalLoopbackMode>,
#endif
      std::optional<Attributes::MediaType>,
      std::optional<Attributes::GlobalFlowControlMode>,
      std::optional<Attributes::PortVlanId>,
      std::optional<Attributes::Mtu>,
      std::optional<Attributes::QosDscpToTcMap>,
      std::optional<Attributes::QosTcToQueueMap>,
      std::optional<Attributes::DisableTtlDecrement>,
      std::optional<Attributes::InterfaceType>,
      std::optional<Attributes::PktTxEnable>,
      std::optional<Attributes::TamObject>,
      std::optional<Attributes::IngressMirrorSession>,
      std::optional<Attributes::EgressMirrorSession>,
      std::optional<Attributes::IngressSamplePacketEnable>,
      std::optional<Attributes::EgressSamplePacketEnable>,
      std::optional<Attributes::IngressSampleMirrorSession>,
      std::optional<Attributes::EgressSampleMirrorSession>,
      std::optional<Attributes::PrbsPolynomial>,
      std::optional<Attributes::PrbsConfig>,
      std::optional<Attributes::IngressMacSecAcl>,
      std::optional<Attributes::EgressMacSecAcl>,
      std::optional<Attributes::SystemPortId>,
      std::optional<Attributes::PtpMode>,
      std::optional<Attributes::PriorityFlowControlMode>,
      std::optional<Attributes::PriorityFlowControl>,
#if !defined(TAJO_SDK)
      std::optional<Attributes::PriorityFlowControlRx>,
      std::optional<Attributes::PriorityFlowControlTx>,
#endif
      std::optional<Attributes::QosTcToPriorityGroupMap>,
      std::optional<Attributes::QosPfcPriorityToQueueMap>,
      std::optional<Attributes::QosPfcPriorityToPriorityGroupMap>,
#if defined(SAI_CBFC_SUPPORTED)
      std::optional<Attributes::QosTcToVcMap>,
      std::optional<Attributes::QosQueueToVcMap>,
      // In this order: brcm-sai recomputes the credit limit on a credit size
      // or packet overhead set, so the limit must be written after both.
      std::optional<Attributes::CbfcSenderCreditSize>,
      std::optional<Attributes::CbfcSenderPacketOverhead>,
      std::optional<Attributes::CbfcSenderCreditLimit>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
      std::optional<Attributes::InterFrameGap>,
#endif
      std::optional<Attributes::LinkTrainingEnable>,
      std::optional<Attributes::FdrEnable>,
      std::optional<Attributes::RxLaneSquelchEnable>,
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
      std::optional<Attributes::PfcTcDldInterval>,
      std::optional<Attributes::PfcTcDlrInterval>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
      std::optional<Attributes::ArsEnable>,
      std::optional<Attributes::ArsPortLoadScalingFactor>,
      std::optional<Attributes::ArsPortLoadPastWeight>,
      std::optional<Attributes::ArsPortLoadFutureWeight>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
      std::optional<Attributes::ArsLinkState>,
#endif
      std::optional<Attributes::ReachabilityGroup>,
      std::optional<Attributes::CondEntropyRehashEnable>,
      std::optional<Attributes::CondEntropyRehashPeriodUS>,
      std::optional<Attributes::CondEntropyRehashSeed>,
      std::optional<Attributes::ShelEnable>,
#if defined(CHENAB_SAI_SDK)
      std::optional<Attributes::AutoNegotiationMode>,
#endif
      std::optional<Attributes::FecErrorDetectEnable>,
      std::optional<Attributes::AmIdles>,
      std::optional<Attributes::FabricSystemPort>,
      std::optional<Attributes::StaticModuleId>,
      std::optional<Attributes::IsHyperPortMember>,
      std::optional<Attributes::HyperPortMemberList>,
      std::optional<Attributes::PfcMonitorDirection>,
      std::optional<Attributes::QosDot1pToTcMap>,
      std::optional<Attributes::QosTcAndColorToDot1pMap>,
      std::optional<Attributes::QosIngressBufferProfileList>,
      std::optional<Attributes::QosEgressBufferProfileList>,
      std::optional<Attributes::CablePropagationDelayMediaType>,
      // Must stay ahead of LinkDownDebouncePeriodMs: brcm_sai_create_port_cmn
      // replays the create list through set_port_attribute in order, and the
      // BRCM debounce set is rejected outright unless the port is already in
      // SAI_PORT_LINKSCAN_MODE_HW.
      std::optional<Attributes::LinkScanMode>,
#if defined(FBOSS_SAI_PORT_LINK_UP_DEBOUNCE_PERIOD)
      std::optional<Attributes::LinkUpDebouncePeriodMs>,
#endif
#if defined(FBOSS_SAI_PORT_LINK_DOWN_DEBOUNCE_PERIOD)
      std::optional<Attributes::LinkDownDebouncePeriodMs>,
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
      std::optional<Attributes::LlrModeLocal>,
      std::optional<Attributes::LlrModeRemote>,
      std::optional<Attributes::LlrProfile>,
#endif
      std::optional<Attributes::PfcPauseDurationOverride>,
      std::optional<Attributes::IngressAcl>,
      std::optional<Attributes::IsolationGroup>,
      std::optional<Attributes::Metadata>,
      std::optional<Attributes::CsigTagAction>,
      std::optional<Attributes::CsigLinkLocator>>;
  static constexpr std::array<sai_stat_id_t, 16> CounterIdsToRead = {
      SAI_PORT_STAT_IF_IN_OCTETS,
      SAI_PORT_STAT_IF_IN_UCAST_PKTS,
      SAI_PORT_STAT_IF_IN_MULTICAST_PKTS,
      SAI_PORT_STAT_IF_IN_BROADCAST_PKTS,
      SAI_PORT_STAT_IF_IN_DISCARDS,
      SAI_PORT_STAT_IF_IN_ERRORS,
      SAI_PORT_STAT_PAUSE_RX_PKTS,
      SAI_PORT_STAT_IF_OUT_OCTETS,
      SAI_PORT_STAT_IF_OUT_UCAST_PKTS,
      SAI_PORT_STAT_IF_OUT_MULTICAST_PKTS,
      SAI_PORT_STAT_IF_OUT_BROADCAST_PKTS,
      SAI_PORT_STAT_IF_OUT_DISCARDS,
      SAI_PORT_STAT_IF_OUT_ERRORS,
      SAI_PORT_STAT_PAUSE_TX_PKTS,
      SAI_PORT_STAT_WRED_DROPPED_PACKETS,
      SAI_PORT_STAT_ECN_MARKED_PACKETS,
  };
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  // UEC Link Layer Retry counters (UE Spec 1.0.2 section 5.1.11, Table 5-13).
  // TU1 does not support LLR_RX_BAD, LLR_TX_DISCARD, LLR_TX_POISONED or
  // LLR_RX_POISONED (no SDK backing); get_port_stats is all-or-nothing, so
  // fetch only the counters TU1 supports (Broadcom CS00012472055).
  static const std::vector<sai_stat_id_t>& llrStats() {
    static const std::vector<sai_stat_id_t> ids = {
        SAI_PORT_STAT_LLR_TX_OK,
        SAI_PORT_STAT_LLR_TX_REPLAY,
        SAI_PORT_STAT_LLR_RX_OK,
        SAI_PORT_STAT_LLR_RX_MISSING_SEQ,
        SAI_PORT_STAT_LLR_RX_DUPLICATE_SEQ,
        SAI_PORT_STAT_LLR_RX_ACK_NACK_SEQ_ERROR,
        SAI_PORT_STAT_LLR_RX_REPLAY,
        SAI_PORT_STAT_LLR_TX_INIT_CTL_OS,
        SAI_PORT_STAT_LLR_TX_INIT_ECHO_CTL_OS,
        SAI_PORT_STAT_LLR_TX_ACK_CTL_OS,
        SAI_PORT_STAT_LLR_TX_NACK_CTL_OS,
        SAI_PORT_STAT_LLR_RX_INIT_CTL_OS,
        SAI_PORT_STAT_LLR_RX_INIT_ECHO_CTL_OS,
        SAI_PORT_STAT_LLR_RX_ACK_CTL_OS,
        SAI_PORT_STAT_LLR_RX_NACK_CTL_OS,
        SAI_PORT_STAT_LLR_RX_EXPECTED_SEQ_GOOD,
        SAI_PORT_STAT_LLR_RX_EXPECTED_SEQ_POISONED,
        SAI_PORT_STAT_LLR_RX_EXPECTED_SEQ_BAD,
    };
    return ids;
  }
#endif
#if defined(SAI_CBFC_SUPPORTED)
  // UEC Credit-Based Flow Control counters (UE Spec 1.0.2 section 5.2).
  // get_port_stats is all-or-nothing, so fetch only the counters with a BCM
  // counter behind them on Tomahawk Ultra 1. The other three in saiport.h are
  // left out on purpose:
  //   CBFC_SENDER_CREDITS_USED           TU1 B0 stepping only, NOT_SUPPORTED
  //                                      on A0
  //   CBFC_SENDER_CREDITS_USED_WATERMARK no BCM counter on any stepping
  //   CBFC_NUM_CC_UPDATE_MESSAGES_RX     no BCM counter on any stepping
  // Adding any of them makes every CBFC port stat read fail.
  static const std::vector<sai_stat_id_t>& cbfcStats() {
    static const std::vector<sai_stat_id_t> ids = {
        SAI_PORT_STAT_CBFC_NUM_CC_UPDATE_MESSAGES_TX,
        SAI_PORT_STAT_CBFC_NUM_CF_UPDATE_MESSAGES_TX,
        SAI_PORT_STAT_CBFC_NUM_CF_UPDATE_MESSAGES_RX,
    };
    return ids;
  }
#endif
  static constexpr std::array<sai_stat_id_t, 16> PfcCounterIdsToRead = {
      SAI_PORT_STAT_PFC_0_RX_PKTS,
      SAI_PORT_STAT_PFC_1_RX_PKTS,
      SAI_PORT_STAT_PFC_2_RX_PKTS,
      SAI_PORT_STAT_PFC_3_RX_PKTS,
      SAI_PORT_STAT_PFC_4_RX_PKTS,
      SAI_PORT_STAT_PFC_5_RX_PKTS,
      SAI_PORT_STAT_PFC_6_RX_PKTS,
      SAI_PORT_STAT_PFC_7_RX_PKTS,
      SAI_PORT_STAT_PFC_0_TX_PKTS,
      SAI_PORT_STAT_PFC_1_TX_PKTS,
      SAI_PORT_STAT_PFC_2_TX_PKTS,
      SAI_PORT_STAT_PFC_3_TX_PKTS,
      SAI_PORT_STAT_PFC_4_TX_PKTS,
      SAI_PORT_STAT_PFC_5_TX_PKTS,
      SAI_PORT_STAT_PFC_6_TX_PKTS,
      SAI_PORT_STAT_PFC_7_TX_PKTS,
  };
  static constexpr std::array<sai_stat_id_t, 8> PfcXonToXoffCounterIdsToRead = {
      SAI_PORT_STAT_PFC_0_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_1_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_2_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_3_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_4_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_5_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_6_ON2OFF_RX_PKTS,
      SAI_PORT_STAT_PFC_7_ON2OFF_RX_PKTS,
  };
  static constexpr std::array<sai_stat_id_t, 0> CounterIdsToReadAndClear = {};
  static const std::vector<sai_stat_id_t>& macTxDataQueueMinWatermarkStats();
  static const std::vector<sai_stat_id_t>& macTxDataQueueMaxWatermarkStats();
  static const std::vector<sai_stat_id_t>& fabricControlRxPacketStats();
  static const std::vector<sai_stat_id_t>& fabricControlTxPacketStats();
  static const std::vector<sai_stat_id_t>& pfcXoffTotalDurationStats();
  static const std::vector<sai_stat_id_t>& linkDownDebounceRetriggerStats();
  static const std::vector<sai_stat_id_t>& linkUpDebounceRetriggerStats();
  // Broadcom LLR stat extensions. Unlike llrStats() above these are not
  // standard SAI 1.18 enums, so the list is vendor-defined and empty everywhere
  // except a Broadcom SDK new enough to declare them.
  static const std::vector<sai_stat_id_t>& llrExtensionStats();
};

SAI_ATTRIBUTE_NAME(Port, HwLaneList)
SAI_ATTRIBUTE_NAME(Port, Speed)
SAI_ATTRIBUTE_NAME(Port, AdminState)
SAI_ATTRIBUTE_NAME(Port, FecMode)
SAI_ATTRIBUTE_NAME(Port, OperStatus)
SAI_ATTRIBUTE_NAME(Port, InternalLoopbackMode)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
SAI_ATTRIBUTE_NAME(Port, PortLoopbackMode)
SAI_ATTRIBUTE_NAME(Port, UseExtendedFec)
SAI_ATTRIBUTE_NAME(Port, ExtendedFecMode)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 11, 0)
SAI_ATTRIBUTE_NAME(Port, FabricIsolate)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
SAI_ATTRIBUTE_NAME(Port, LlrModeLocal)
SAI_ATTRIBUTE_NAME(Port, LlrModeRemote)
SAI_ATTRIBUTE_NAME(Port, LlrProfile)
SAI_ATTRIBUTE_NAME(Port, LlrTxStatus)
SAI_ATTRIBUTE_NAME(Port, LlrRxStatus)
#endif
SAI_ATTRIBUTE_NAME(Port, MediaType)
SAI_ATTRIBUTE_NAME(Port, GlobalFlowControlMode)
SAI_ATTRIBUTE_NAME(Port, PortVlanId)
SAI_ATTRIBUTE_NAME(Port, Mtu)
SAI_ATTRIBUTE_NAME(Port, Metadata)
SAI_ATTRIBUTE_NAME(Port, QosDscpToTcMap)
SAI_ATTRIBUTE_NAME(Port, QosDot1pToTcMap)
SAI_ATTRIBUTE_NAME(Port, QosTcAndColorToDot1pMap)
SAI_ATTRIBUTE_NAME(Port, QosTcToQueueMap)
SAI_ATTRIBUTE_NAME(Port, DisableTtlDecrement)

SAI_ATTRIBUTE_NAME(Port, QosNumberOfQueues)
SAI_ATTRIBUTE_NAME(Port, QosQueueList)
SAI_ATTRIBUTE_NAME(Port, QosEgressBufferProfileList)
SAI_ATTRIBUTE_NAME(Port, QosIngressBufferProfileList)
SAI_ATTRIBUTE_NAME(Port, Type)
SAI_ATTRIBUTE_NAME(Port, InterfaceType)
SAI_ATTRIBUTE_NAME(Port, PktTxEnable)
SAI_ATTRIBUTE_NAME(Port, TamObject)
SAI_ATTRIBUTE_NAME(Port, SerdesId)
SAI_ATTRIBUTE_NAME(Port, IngressMirrorSession)
SAI_ATTRIBUTE_NAME(Port, EgressMirrorSession)
SAI_ATTRIBUTE_NAME(Port, IngressSamplePacketEnable)
SAI_ATTRIBUTE_NAME(Port, EgressSamplePacketEnable)
SAI_ATTRIBUTE_NAME(Port, IngressSampleMirrorSession)
SAI_ATTRIBUTE_NAME(Port, EgressSampleMirrorSession)

SAI_ATTRIBUTE_NAME(Port, PrbsPolynomial)
SAI_ATTRIBUTE_NAME(Port, PrbsConfig)
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
SAI_ATTRIBUTE_NAME(Port, PrbsRxState)
#endif
SAI_ATTRIBUTE_NAME(Port, IngressAcl)
SAI_ATTRIBUTE_NAME(Port, IsolationGroup)
SAI_ATTRIBUTE_NAME(Port, IngressMacSecAcl)
SAI_ATTRIBUTE_NAME(Port, EgressMacSecAcl)
SAI_ATTRIBUTE_NAME(Port, SystemPortId)
SAI_ATTRIBUTE_NAME(Port, AttachedCoreIndex)
SAI_ATTRIBUTE_NAME(Port, AttachedCorePortIndex)
SAI_ATTRIBUTE_NAME(Port, PtpMode)
SAI_ATTRIBUTE_NAME(Port, PortEyeValues)
SAI_ATTRIBUTE_NAME(Port, PriorityFlowControlMode)
SAI_ATTRIBUTE_NAME(Port, PriorityFlowControl)
#if !defined(TAJO_SDK)
SAI_ATTRIBUTE_NAME(Port, PriorityFlowControlRx)
SAI_ATTRIBUTE_NAME(Port, PriorityFlowControlTx)
#endif
SAI_ATTRIBUTE_NAME(Port, PortErrStatus)
SAI_ATTRIBUTE_NAME(Port, IngressPriorityGroupList)
SAI_ATTRIBUTE_NAME(Port, NumberOfIngressPriorityGroups)
SAI_ATTRIBUTE_NAME(Port, QosTcToPriorityGroupMap)
#if defined(SAI_CBFC_SUPPORTED)
SAI_ATTRIBUTE_NAME(Port, QosTcToVcMap)
SAI_ATTRIBUTE_NAME(Port, CbfcSenderCreditLimit)
SAI_ATTRIBUTE_NAME(Port, CbfcSenderCreditSize)
SAI_ATTRIBUTE_NAME(Port, CbfcSenderPacketOverhead)
SAI_ATTRIBUTE_NAME(Port, QosQueueToVcMap)
SAI_ATTRIBUTE_NAME(Port, CbfcReceiverNativeCreditSize)
SAI_ATTRIBUTE_NAME(Port, CbfcReceiverNativePacketOverhead)
SAI_ATTRIBUTE_NAME(Port, CbfcReceiverNativeTotalCredits)
#endif
SAI_ATTRIBUTE_NAME(Port, QosPfcPriorityToQueueMap)
SAI_ATTRIBUTE_NAME(Port, QosPfcPriorityToPriorityGroupMap)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3) || defined(TAJO_SDK_VERSION_1_42_8)
SAI_ATTRIBUTE_NAME(Port, RxSignalDetect)
SAI_ATTRIBUTE_NAME(Port, RxLockStatus)
SAI_ATTRIBUTE_NAME(Port, FecAlignmentLock)
SAI_ATTRIBUTE_NAME(Port, PcsRxLinkStatus)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
SAI_ATTRIBUTE_NAME(Port, InterFrameGap)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
SAI_ATTRIBUTE_NAME(Port, RxFrequencyPPM)
SAI_ATTRIBUTE_NAME(Port, RxSNR)
#endif
SAI_ATTRIBUTE_NAME(Port, LinkTrainingEnable)
SAI_ATTRIBUTE_NAME(Port, LinkTrainingRxStatus)
SAI_ATTRIBUTE_NAME(Port, SerdesLaneList)
SAI_ATTRIBUTE_NAME(Port, DiagModeEnable)
SAI_ATTRIBUTE_NAME(Port, FdrEnable)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
SAI_ATTRIBUTE_NAME(Port, CrcErrorDetect)
#endif
SAI_ATTRIBUTE_NAME(Port, FabricAttached);
SAI_ATTRIBUTE_NAME(Port, FabricAttachedPortIndex);
SAI_ATTRIBUTE_NAME(Port, FabricAttachedSwitchId);
SAI_ATTRIBUTE_NAME(Port, FabricAttachedSwitchType);
SAI_ATTRIBUTE_NAME(Port, FabricReachability);
SAI_ATTRIBUTE_NAME(Port, RxLaneSquelchEnable);
SAI_ATTRIBUTE_NAME(Port, LinkScanMode);
SAI_ATTRIBUTE_NAME(Port, CsigTagAction);
SAI_ATTRIBUTE_NAME(Port, CsigLinkLocator);
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 2)
SAI_ATTRIBUTE_NAME(Port, PfcTcDldInterval);
SAI_ATTRIBUTE_NAME(Port, PfcTcDlrInterval);
SAI_ATTRIBUTE_NAME(Port, PfcTcDldIntervalRange);
SAI_ATTRIBUTE_NAME(Port, PfcTcDlrIntervalRange);
#endif
SAI_ATTRIBUTE_NAME(Port, SystemPort);
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
SAI_ATTRIBUTE_NAME(Port, TxReadyStatus)
#endif
SAI_ATTRIBUTE_NAME(Port, CablePropogationDelayNS)
SAI_ATTRIBUTE_NAME(Port, FabricDataCellsFilterStatus)
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
SAI_ATTRIBUTE_NAME(Port, ArsEnable)
SAI_ATTRIBUTE_NAME(Port, ArsPortLoadScalingFactor)
SAI_ATTRIBUTE_NAME(Port, ArsPortLoadPastWeight)
SAI_ATTRIBUTE_NAME(Port, ArsPortLoadFutureWeight)
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 0) && defined(BRCM_SAI_SDK_XGS)
SAI_ATTRIBUTE_NAME(Port, ArsLinkState)
#endif
SAI_ATTRIBUTE_NAME(Port, ReachabilityGroup)
SAI_ATTRIBUTE_NAME(Port, CondEntropyRehashEnable)
SAI_ATTRIBUTE_NAME(Port, CondEntropyRehashPeriodUS)
SAI_ATTRIBUTE_NAME(Port, CondEntropyRehashSeed)
SAI_ATTRIBUTE_NAME(Port, ShelEnable)
SAI_ATTRIBUTE_NAME(Port, FecErrorDetectEnable)
SAI_ATTRIBUTE_NAME(Port, AmIdles)
SAI_ATTRIBUTE_NAME(Port, ResetQueueCreditBalance)
SAI_ATTRIBUTE_NAME(Port, FabricSystemPort)
SAI_ATTRIBUTE_NAME(Port, StaticModuleId)
SAI_ATTRIBUTE_NAME(Port, PgDropStatus)
SAI_ATTRIBUTE_NAME(Port, IsHyperPortMember)
SAI_ATTRIBUTE_NAME(Port, HyperPortMemberList)
SAI_ATTRIBUTE_NAME(Port, PfcMonitorDirection)
SAI_ATTRIBUTE_NAME(Port, CablePropagationDelayMediaType)
SAI_ATTRIBUTE_NAME(Port, PfcPauseDurationOverride)
SAI_ATTRIBUTE_NAME(Port, CablePropagationDelayMeasure)
SAI_ATTRIBUTE_NAME(Port, LinkUpDebouncePeriodMs)
SAI_ATTRIBUTE_NAME(Port, LinkDownDebouncePeriodMs)
SAI_ATTRIBUTE_NAME(Port, LinkUpDebounceRetriggerCount)
SAI_ATTRIBUTE_NAME(Port, LinkDownDebounceRetriggerCount)
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
SAI_ATTRIBUTE_NAME(Port, ExtOperStatusLatch)
#endif

#if defined(CHENAB_SAI_SDK)
SAI_ATTRIBUTE_NAME(Port, AutoNegotiationMode)
#endif

template <>
struct SaiObjectHasStats<SaiPortTraits> : public std::true_type {};

struct SaiPortSerdesTraits {
  static constexpr sai_object_type_t ObjectType = SAI_OBJECT_TYPE_PORT_SERDES;
  using SaiApiT = PortApi;
  struct Attributes {
    using EnumType = sai_port_serdes_attr_t;
    using PortId =
        SaiAttribute<EnumType, SAI_PORT_SERDES_ATTR_PORT_ID, SaiObjectIdT>;
    using Preemphasis = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_PREEMPHASIS,
        std::vector<uint32_t>,
        SaiU32ListDefault>;
    using IDriver = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_IDRIVER,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPre1 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_PRE1,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirMain = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_MAIN,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPost1 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_POST1,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPre2 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_PRE2,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPre3 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_PRE3,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPost2 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_POST2,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
    using TxFirPost3 = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_FIR_POST3,
        std::vector<sai_uint32_t>,
        SaiU32ListDefault>;
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    using TxPrecoding = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_TX_PRECODING,
        std::vector<sai_int32_t>,
        SaiS32ListDefault>;
    using RxPrecoding = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_RX_PRECODING,
        std::vector<sai_int32_t>,
        SaiS32ListDefault>;
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
    using CustomCollection = SaiAttribute<
        EnumType,
        SAI_PORT_SERDES_ATTR_CUSTOM_COLLECTION,
        SaiJsonString,
        StdNullOptDefault<SaiJsonString>>;
#endif
    /* extension attributes */
    struct AttributeRxReachWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTransmitPrecodingStateWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeReceivePrecodingStateWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRVgaWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeDcoWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeFltMWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeFltSWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxPfWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxPfLfqWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxPfHfqWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEqP2Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEqP1Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEqMWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEq1Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEq2Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEq3Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxTap2Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxTap1Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTpChn2Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTpChn1Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTpChn0Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxLutModeIdWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxCtleCodeIdWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    struct AttributeRxDspModeIdWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    struct AttributeRxAfeTrimIdWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    struct AttributeRxAcCouplingBypassIdWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    struct AttributeRxAfeAdaptiveEnableWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    using RxReach = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxReachWrapper,
        SaiS32ListDefault>;
    // Standard TxPrecoding/RxPrecoding attributes are supported on 14.0+
    // These vendor extensions work from 13.3
    using TransmitPrecodingState = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTransmitPrecodingStateWrapper,
        SaiS32ListDefault>;
    using ReceivePrecodingState = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeReceivePrecodingStateWrapper,
        SaiS32ListDefault>;
// Alias to vendor extension attributes on bcm SAI
#if defined(BRCM_SAI_SDK_GTE_13_0)
    using TxPrecodingAttr = TransmitPrecodingState;
    using RxPrecodingAttr = ReceivePrecodingState;
#elif SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
    // If not BCM, alias to standard SAI attributes
    using TxPrecodingAttr = TxPrecoding;
    using RxPrecodingAttr = RxPrecoding;
#endif
    using RVga =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRVgaWrapper>;
    using Dco =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeDcoWrapper>;
    using FltM =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeFltMWrapper>;
    using FltS =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeFltSWrapper>;
    using RxPf =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRxPfWrapper>;
    using RxPfLfq = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxPfLfqWrapper>;
    using RxPfHfq = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxPfHfqWrapper>;
    using RxEqP2 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxEqP2Wrapper>;
    using RxEqP1 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxEqP1Wrapper>;
    using RxEqM =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRxEqMWrapper>;
    using RxEq1 =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRxEq1Wrapper>;
    using RxEq2 =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRxEq2Wrapper>;
    using RxEq3 =
        SaiExtensionAttribute<std::vector<sai_uint32_t>, AttributeRxEq3Wrapper>;
    using RxTap2 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxTap2Wrapper>;
    using RxTap1 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeRxTap1Wrapper>;
    using TpChn2 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeTpChn2Wrapper>;
    using TpChn1 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeTpChn1Wrapper>;
    using TpChn0 = SaiExtensionAttribute<
        std::vector<sai_uint32_t>,
        AttributeTpChn0Wrapper>;
    using TxLutMode = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxLutModeIdWrapper>;
    using RxCtleCode = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxCtleCodeIdWrapper>;
    using RxDspMode = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxDspModeIdWrapper>;
    using RxAfeTrim = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxAfeTrimIdWrapper>;
    using RxAcCouplingByPass = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxAcCouplingBypassIdWrapper>;
    using RxAfeAdaptiveEnable = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxAfeAdaptiveEnableWrapper>;
    // Tx Attributes
    struct AttributeTxDiffEncoderEnWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxDigGainWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxFfeCoeff0Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxFfeCoeff1Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxFfeCoeff2Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxFfeCoeff3Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxFfeCoeff4Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxDriverSwingWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeTxLdoBypassWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    // Rx Attributes
    struct AttributeRxInstgBoost1StartWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgBoost1StepWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgBoost1StopWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgBoost2OrHrStartWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgBoost2OrHrStepWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgBoost2OrHrStopWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgC1Start1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgC1Step1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgC1Stop1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgDfeStart1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgDfeStep1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgDfeStop1p7Wrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxEnableScanSelectionWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgScanUseSrSettingsWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxCdrCfgOvEnWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxCdrTdet1stOrdStepOvValWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxCdrTdet2ndOrdStepOvValWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxCdrTdetFineStepOvValWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxLdoBypassWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxDiffEncoderEnWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxInstgEnableScanWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxFfeLengthBitmapWrapper {
      std::optional<sai_attr_id_t> operator()();
    };
    struct AttributeRxFfeLmsDynamicGatingEnWrapper {
      std::optional<sai_attr_id_t> operator()();
    };

    using TxDiffEncoderEn = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxDiffEncoderEnWrapper>;
    using TxDigGain = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxDigGainWrapper>;
    using TxFfeCoeff0 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxFfeCoeff0Wrapper>;
    using TxFfeCoeff1 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxFfeCoeff1Wrapper>;
    using TxFfeCoeff2 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxFfeCoeff2Wrapper>;
    using TxFfeCoeff3 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxFfeCoeff3Wrapper>;
    using TxFfeCoeff4 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxFfeCoeff4Wrapper>;
    using TxDriverSwing = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxDriverSwingWrapper>;
    using TxLdoBypass = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeTxLdoBypassWrapper>;

    using RxInstgBoost1Start = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost1StartWrapper>;
    using RxInstgBoost1Step = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost1StepWrapper>;
    using RxInstgBoost1Stop = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost1StopWrapper>;
    using RxInstgBoost2OrHrStart = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost2OrHrStartWrapper>;
    using RxInstgBoost2OrHrStep = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost2OrHrStepWrapper>;
    using RxInstgBoost2OrHrStop = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgBoost2OrHrStopWrapper>;
    using RxInstgC1Start1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgC1Start1p7Wrapper>;
    using RxInstgC1Step1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgC1Step1p7Wrapper>;
    using RxInstgC1Stop1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgC1Stop1p7Wrapper>;
    using RxInstgDfeStart1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgDfeStart1p7Wrapper>;
    using RxInstgDfeStep1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgDfeStep1p7Wrapper>;
    using RxInstgDfeStop1p7 = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgDfeStop1p7Wrapper>;
    using RxEnableScanSelection = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxEnableScanSelectionWrapper>;
    using RxInstgScanUseSrSettings = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgScanUseSrSettingsWrapper>;
    using RxCdrCfgOvEn = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxCdrCfgOvEnWrapper>;
    using RxCdrTdet1stOrdStepOvVal = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxCdrTdet1stOrdStepOvValWrapper>;
    using RxCdrTdet2ndOrdStepOvVal = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxCdrTdet2ndOrdStepOvValWrapper>;
    using RxCdrTdetFineStepOvVal = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxCdrTdetFineStepOvValWrapper>;
    using RxLdoBypass = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxLdoBypassWrapper>;
    using RxDiffEncoderEn = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxDiffEncoderEnWrapper>;
    using RxInstgEnableScan = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxInstgEnableScanWrapper>;
    using RxFfeLengthBitmap = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxFfeLengthBitmapWrapper>;
    using RxFfeLmsDynamicGatingEn = SaiExtensionAttribute<
        std::vector<sai_int32_t>,
        AttributeRxFfeLmsDynamicGatingEnWrapper>;
  };
  using AdapterKey = PortSerdesSaiId;
  using AdapterHostKey = Attributes::PortId;
  using CreateAttributes = std::tuple<
      Attributes::PortId,
#if !defined(CHENAB_SAI_SDK)
      std::optional<Attributes::Preemphasis>,
#endif
      std::optional<Attributes::IDriver>,
      std::optional<Attributes::TxFirPre1>,
      std::optional<Attributes::TxFirPre2>,
      std::optional<Attributes::TxFirPre3>,
      std::optional<Attributes::TxFirMain>,
      std::optional<Attributes::TxFirPost1>,
      std::optional<Attributes::TxFirPost2>,
      std::optional<Attributes::TxFirPost3>,
      std::optional<Attributes::TxLutMode>,
      std::optional<Attributes::RxCtleCode>,
      std::optional<Attributes::RxDspMode>,
      std::optional<Attributes::RxAfeTrim>,
      std::optional<Attributes::RxAcCouplingByPass>,
      std::optional<Attributes::RxAfeAdaptiveEnable>,
      std::optional<Attributes::TxDiffEncoderEn>,
      std::optional<Attributes::TxDigGain>,
      std::optional<Attributes::TxFfeCoeff0>,
      std::optional<Attributes::TxFfeCoeff1>,
      std::optional<Attributes::TxFfeCoeff2>,
      std::optional<Attributes::TxFfeCoeff3>,
      std::optional<Attributes::TxFfeCoeff4>,
      std::optional<Attributes::TxDriverSwing>,
      std::optional<Attributes::RxInstgBoost1Start>,
      std::optional<Attributes::RxInstgBoost1Step>,
      std::optional<Attributes::RxInstgBoost1Stop>,
      std::optional<Attributes::RxInstgBoost2OrHrStart>,
      std::optional<Attributes::RxInstgBoost2OrHrStep>,
      std::optional<Attributes::RxInstgBoost2OrHrStop>,
      std::optional<Attributes::RxInstgC1Start1p7>,
      std::optional<Attributes::RxInstgC1Step1p7>,
      std::optional<Attributes::RxInstgC1Stop1p7>,
      std::optional<Attributes::RxInstgDfeStart1p7>,
      std::optional<Attributes::RxInstgDfeStep1p7>,
      std::optional<Attributes::RxInstgDfeStop1p7>,
      std::optional<Attributes::RxEnableScanSelection>,
      std::optional<Attributes::RxInstgScanUseSrSettings>,
      std::optional<Attributes::RxCdrCfgOvEn>,
      std::optional<Attributes::RxCdrTdet1stOrdStepOvVal>,
      std::optional<Attributes::RxCdrTdet2ndOrdStepOvVal>,
      std::optional<Attributes::RxCdrTdetFineStepOvVal>,
      std::optional<Attributes::TxLdoBypass>,
      std::optional<Attributes::RxLdoBypass>,
      std::optional<Attributes::RxDiffEncoderEn>,
      std::optional<Attributes::RxInstgEnableScan>,
      std::optional<Attributes::RxFfeLengthBitmap>,
      std::optional<Attributes::RxFfeLmsDynamicGatingEn>
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
      ,
      std::optional<Attributes::CustomCollection>
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) || defined(BRCM_PAI_SDK)
      ,
      std::optional<Attributes::RxReach>
#endif
#if defined(BRCM_SAI_SDK_GTE_13_0) ||            \
    (SAI_API_VERSION >= SAI_VERSION(1, 14, 0) && \
     !defined(BRCM_SAI_SDK_XGS_AND_DNX))
      ,
      std::optional<Attributes::TxPrecodingAttr>,
      std::optional<Attributes::RxPrecodingAttr>
#endif
      >;
};

SAI_ATTRIBUTE_NAME(PortSerdes, PortId);
#if !defined(CHENAB_SAI_SDK)
SAI_ATTRIBUTE_NAME(PortSerdes, Preemphasis);
#endif
SAI_ATTRIBUTE_NAME(PortSerdes, IDriver);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPre1);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPre2);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPre3);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirMain);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPost1);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPost2);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFirPost3);
#if SAI_API_VERSION >= SAI_VERSION(1, 14, 0)
SAI_ATTRIBUTE_NAME(PortSerdes, TxPrecoding);
SAI_ATTRIBUTE_NAME(PortSerdes, RxPrecoding);
#endif
SAI_ATTRIBUTE_NAME(PortSerdes, TxLutMode);
SAI_ATTRIBUTE_NAME(PortSerdes, RxCtleCode);
SAI_ATTRIBUTE_NAME(PortSerdes, RxDspMode);
SAI_ATTRIBUTE_NAME(PortSerdes, RxAfeTrim);
SAI_ATTRIBUTE_NAME(PortSerdes, RxAcCouplingByPass);
SAI_ATTRIBUTE_NAME(PortSerdes, RxAfeAdaptiveEnable);
SAI_ATTRIBUTE_NAME(PortSerdes, TxDiffEncoderEn);
SAI_ATTRIBUTE_NAME(PortSerdes, TxDigGain);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFfeCoeff0);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFfeCoeff1);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFfeCoeff2);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFfeCoeff3);
SAI_ATTRIBUTE_NAME(PortSerdes, TxFfeCoeff4);
SAI_ATTRIBUTE_NAME(PortSerdes, TxDriverSwing);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost1Start);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost1Step);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost1Stop);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost2OrHrStart);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost2OrHrStep);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgBoost2OrHrStop);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgC1Start1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgC1Step1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgC1Stop1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgDfeStart1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgDfeStep1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgDfeStop1p7);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEnableScanSelection);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgScanUseSrSettings);
SAI_ATTRIBUTE_NAME(PortSerdes, RxCdrCfgOvEn);
SAI_ATTRIBUTE_NAME(PortSerdes, RxCdrTdet1stOrdStepOvVal);
SAI_ATTRIBUTE_NAME(PortSerdes, RxCdrTdet2ndOrdStepOvVal);
SAI_ATTRIBUTE_NAME(PortSerdes, RxCdrTdetFineStepOvVal);
SAI_ATTRIBUTE_NAME(PortSerdes, TxLdoBypass);
SAI_ATTRIBUTE_NAME(PortSerdes, RxLdoBypass);
SAI_ATTRIBUTE_NAME(PortSerdes, RxDiffEncoderEn);
SAI_ATTRIBUTE_NAME(PortSerdes, RxInstgEnableScan);
SAI_ATTRIBUTE_NAME(PortSerdes, RxFfeLengthBitmap);
SAI_ATTRIBUTE_NAME(PortSerdes, RxFfeLmsDynamicGatingEn);
SAI_ATTRIBUTE_NAME(PortSerdes, RxReach);
SAI_ATTRIBUTE_NAME(PortSerdes, TransmitPrecodingState);
SAI_ATTRIBUTE_NAME(PortSerdes, ReceivePrecodingState);
SAI_ATTRIBUTE_NAME(PortSerdes, RVga);
SAI_ATTRIBUTE_NAME(PortSerdes, Dco);
SAI_ATTRIBUTE_NAME(PortSerdes, FltM);
SAI_ATTRIBUTE_NAME(PortSerdes, FltS);
SAI_ATTRIBUTE_NAME(PortSerdes, RxPf);
SAI_ATTRIBUTE_NAME(PortSerdes, RxPfLfq);
SAI_ATTRIBUTE_NAME(PortSerdes, RxPfHfq);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEqP2);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEqP1);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEqM);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEq1);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEq2);
SAI_ATTRIBUTE_NAME(PortSerdes, RxEq3);
SAI_ATTRIBUTE_NAME(PortSerdes, RxTap2);
SAI_ATTRIBUTE_NAME(PortSerdes, RxTap1);
SAI_ATTRIBUTE_NAME(PortSerdes, TpChn2);
SAI_ATTRIBUTE_NAME(PortSerdes, TpChn1);
SAI_ATTRIBUTE_NAME(PortSerdes, TpChn0);
#if SAI_API_VERSION >= SAI_VERSION(1, 16, 4)
SAI_ATTRIBUTE_NAME(PortSerdes, CustomCollection);
#endif

struct SaiPortConnectorTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_PORT_CONNECTOR;
  using SaiApiT = PortApi;
  struct Attributes {
    using EnumType = sai_port_connector_attr_t;
    using LineSidePortId = SaiAttribute<
        EnumType,
        SAI_PORT_CONNECTOR_ATTR_LINE_SIDE_PORT_ID,
        SaiObjectIdT>;
    using SystemSidePortId = SaiAttribute<
        EnumType,
        SAI_PORT_CONNECTOR_ATTR_SYSTEM_SIDE_PORT_ID,
        SaiObjectIdT>;
  };
  using AdapterKey = PortConnectorSaiId;
  using AdapterHostKey =
      std::tuple<Attributes::LineSidePortId, Attributes::SystemSidePortId>;
  using CreateAttributes = AdapterHostKey;
};

SAI_ATTRIBUTE_NAME(PortConnector, LineSidePortId);
SAI_ATTRIBUTE_NAME(PortConnector, SystemSidePortId);

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
// UEC Link Layer Retry (LLR) profile (UE Spec 1.0.2 section 5.1). A reusable
// SAI object holding the LLR configuration registers (UE Spec Table 5-9),
// referenced from a port via SAI_PORT_ATTR_LLR_PROFILE. It is a secondary
// object under the Port API (like SAI_OBJECT_TYPE_PORT_CONNECTOR).
struct SaiPortLlrProfileTraits {
  static constexpr sai_object_type_t ObjectType =
      SAI_OBJECT_TYPE_PORT_LLR_PROFILE;
  using SaiApiT = PortApi;
  struct Attributes {
    using EnumType = sai_port_llr_profile_attr_t;
    using OutstandingFramesMax = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_FRAMES_MAX,
        sai_uint32_t>;
    using OutstandingBytesMax = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_OUTSTANDING_BYTES_MAX,
        sai_uint32_t>;
    using ReplayTimerMax = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_REPLAY_TIMER_MAX,
        sai_uint32_t>;
    using ReplayCountMax = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_REPLAY_COUNT_MAX,
        sai_uint8_t>;
    using PcsLostTimeout = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_PCS_LOST_TIMEOUT,
        sai_uint32_t>;
    using DataAgeTimeout = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_DATA_AGE_TIMEOUT,
        sai_uint32_t>;
    using InitLlrFrameAction = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_INIT_LLR_FRAME_ACTION,
        sai_int32_t>;
    using FlushLlrFrameAction = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_FLUSH_LLR_FRAME_ACTION,
        sai_int32_t>;
    using ReInitOnFlush = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_RE_INIT_ON_FLUSH,
        bool>;
    using CtlosTargetSpacing = SaiAttribute<
        EnumType,
        SAI_PORT_LLR_PROFILE_ATTR_CTLOS_TARGET_SPACING,
        sai_uint16_t>;
  };
  using AdapterKey = PortLlrProfileSaiId;
  using AdapterHostKey = std::tuple<
      Attributes::OutstandingFramesMax,
      Attributes::OutstandingBytesMax,
      Attributes::ReplayTimerMax,
      Attributes::ReplayCountMax,
      Attributes::PcsLostTimeout,
      Attributes::DataAgeTimeout,
      Attributes::InitLlrFrameAction,
      Attributes::FlushLlrFrameAction,
      Attributes::ReInitOnFlush,
      Attributes::CtlosTargetSpacing>;
  using CreateAttributes = AdapterHostKey;
};

SAI_ATTRIBUTE_NAME(PortLlrProfile, OutstandingFramesMax);
SAI_ATTRIBUTE_NAME(PortLlrProfile, OutstandingBytesMax);
SAI_ATTRIBUTE_NAME(PortLlrProfile, ReplayTimerMax);
SAI_ATTRIBUTE_NAME(PortLlrProfile, ReplayCountMax);
SAI_ATTRIBUTE_NAME(PortLlrProfile, PcsLostTimeout);
SAI_ATTRIBUTE_NAME(PortLlrProfile, DataAgeTimeout);
SAI_ATTRIBUTE_NAME(PortLlrProfile, InitLlrFrameAction);
SAI_ATTRIBUTE_NAME(PortLlrProfile, FlushLlrFrameAction);
SAI_ATTRIBUTE_NAME(PortLlrProfile, ReInitOnFlush);
SAI_ATTRIBUTE_NAME(PortLlrProfile, CtlosTargetSpacing);
#endif

class PortApi : public SaiApi<PortApi> {
 public:
  static constexpr sai_api_t ApiType = SAI_API_PORT;
  PortApi() {
    sai_status_t status =
        sai_api_query(ApiType, reinterpret_cast<void**>(&api_));
    saiApiCheckError(status, ApiType, "Failed to query for port api");
  }

 private:
  sai_status_t _create(
      PortSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    // SAI_PORT_ATTR_TYPE is read-only in the SAI spec. It is part of the DNX
    // CreateAttributes so that SaiStore can key port types apart; the logical
    // (front-panel) type is not sent to the adapter. An adapter that rejects it
    // only after creating the port (the Broadcom DNX SAI) would otherwise leave
    // a half-created port behind that no retry can replace. Other types
    // (fabric, recycle, eventor, management, hyper port) are still passed.
    std::vector<sai_attribute_t> attrs;
    attrs.reserve(count);
    std::copy_if(
        attr_list,
        attr_list + count,
        std::back_inserter(attrs),
        [](const sai_attribute_t& attr) {
          return attr.id != SAI_PORT_ATTR_TYPE ||
              attr.value.s32 != SAI_PORT_TYPE_LOGICAL;
        });
    return api_->create_port(
        rawSaiId(id), switch_id, attrs.size(), attrs.data());
  }
  sai_status_t _remove(PortSaiId key) const {
    return api_->remove_port(key);
  }
  sai_status_t _getAttribute(PortSaiId key, sai_attribute_t* attr) const {
    return api_->get_port_attribute(key, 1, attr);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  sai_status_t _bulkGetAttribute(
      PortSaiId* keys,
      uint32_t* attrCount,
      sai_attribute_t** attr,
      sai_status_t* retStatus,
      size_t objectCount) const {
    sai_object_id_t rawIds[objectCount];
    for (auto idx = 0; idx < objectCount; idx++) {
      rawIds[idx] = *rawSaiId(&keys[idx]);
    }

    return api_->get_ports_attribute(
        objectCount,
        rawIds,
        attrCount,
        attr,
        SAI_BULK_OP_ERROR_MODE_STOP_ON_ERROR,
        retStatus);
  }
#endif

  sai_status_t _setAttribute(PortSaiId key, const sai_attribute_t* attr) const {
    return api_->set_port_attribute(key, attr);
  }

  sai_status_t _create(
      PortSerdesSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_port_serdes(rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(PortSerdesSaiId id) const {
    return api_->remove_port_serdes(id);
  }

  sai_status_t _getAttribute(PortSerdesSaiId key, sai_attribute_t* attr) const {
    return api_->get_port_serdes_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(PortSerdesSaiId key, const sai_attribute_t* attr)
      const {
    return api_->set_port_serdes_attribute(key, attr);
  }

  sai_status_t _create(
      PortConnectorSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_port_connector(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(PortConnectorSaiId id) const {
    return api_->remove_port_connector(id);
  }

  sai_status_t _getAttribute(PortConnectorSaiId key, sai_attribute_t* attr)
      const {
    return api_->get_port_connector_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(
      PortConnectorSaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_port_connector_attribute(key, attr);
  }

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  sai_status_t _create(
      PortLlrProfileSaiId* id,
      sai_object_id_t switch_id,
      size_t count,
      sai_attribute_t* attr_list) const {
    return api_->create_port_llr_profile(
        rawSaiId(id), switch_id, count, attr_list);
  }

  sai_status_t _remove(PortLlrProfileSaiId id) const {
    return api_->remove_port_llr_profile(id);
  }

  sai_status_t _getAttribute(PortLlrProfileSaiId key, sai_attribute_t* attr)
      const {
    return api_->get_port_llr_profile_attribute(key, 1, attr);
  }

  sai_status_t _setAttribute(
      PortLlrProfileSaiId key,
      const sai_attribute_t* attr) const {
    return api_->set_port_llr_profile_attribute(key, attr);
  }
#endif

  sai_status_t _getStats(
      PortSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids,
      sai_stats_mode_t mode,
      uint64_t* counters) const {
    /*
     * Unfortunately not all vendors implement the ext stats api.
     * ext stats api matter only for modes other than the (default)
     * SAI_STATS_MODE_READ. So play defensive and call ext mode only
     * when called with something other than default
     */
    return mode == SAI_STATS_MODE_READ
        ? api_->get_port_stats(key, num_of_counters, counter_ids, counters)
        : api_->get_port_stats_ext(
              key, num_of_counters, counter_ids, mode, counters);
  }

  sai_status_t _clearStats(
      PortSaiId key,
      uint32_t num_of_counters,
      const sai_stat_id_t* counter_ids) const {
    return api_->clear_port_stats(key, num_of_counters, counter_ids);
  }

  sai_port_api_t* api_;
  friend class SaiApi<PortApi>;
};

} // namespace facebook::fboss
