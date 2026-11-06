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

#include "fboss/agent/hw/sai/fake/FakeManager.h"

#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

extern "C" {
#include <sai.h>
#include "fboss/agent/hw/sai/api/fake/saifakeextensions.h"
}

namespace facebook::fboss {

struct FakePort {
  FakePort(std::vector<uint32_t> lanes, sai_uint32_t speed)
      : lanes(lanes), speed(speed) {}
  sai_object_id_t id;
  bool adminState{false};
  std::vector<uint32_t> lanes;
  uint32_t speed{0};
  sai_port_fec_mode_t fecMode{SAI_PORT_FEC_MODE_NONE};
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 0)
  bool useExtendedFec{false};
  sai_port_fec_mode_extended_t extendedFecMode{SAI_PORT_FEC_MODE_EXTENDED_NONE};
#endif

#if SAI_API_VERSION >= SAI_VERSION(1, 12, 0)
  sai_port_loopback_mode_t loopbackMode{SAI_PORT_LOOPBACK_MODE_NONE};
#endif
  sai_port_internal_loopback_mode_t internalLoopbackMode{
      SAI_PORT_INTERNAL_LOOPBACK_MODE_NONE};
  bool fabricIsolate{false};
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
  bool llrModeLocal{false};
  bool llrModeRemote{false};
  sai_object_id_t llrProfile{SAI_NULL_OBJECT_ID};
  sai_int32_t llrTxStatus{SAI_PORT_LLR_TX_STATUS_OFF};
  sai_int32_t llrRxStatus{SAI_PORT_LLR_RX_STATUS_OFF};
#endif
  sai_port_flow_control_mode_t globalFlowControlMode{
      SAI_PORT_FLOW_CONTROL_MODE_DISABLE};
  sai_port_media_type_t mediaType{SAI_PORT_MEDIA_TYPE_NOT_PRESENT};
  sai_vlan_id_t vlanId{0};
  std::vector<sai_object_id_t> queueIdList;
  std::vector<sai_object_id_t> egressBufferProfileIdList;
  std::vector<sai_object_id_t> ingressBufferProfileIdList;
  std::vector<uint32_t> preemphasis;
  sai_uint32_t mtu{1514};
  sai_uint32_t metadata{0};
  sai_object_id_t qosDscpToTcMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosDot1pToTcMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosTcAndColorToDot1pMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosTcToQueueMap{SAI_NULL_OBJECT_ID};
  bool disableTtlDecrement{false};
  sai_port_interface_type_t interface_type{SAI_PORT_INTERFACE_TYPE_NONE};
  bool txEnable{true};
  std::vector<sai_object_id_t> tamObjectList;
  std::vector<sai_object_id_t> ingressMirrorList;
  std::vector<sai_object_id_t> egressMirrorList;
  sai_object_id_t ingressSamplePacket{SAI_NULL_OBJECT_ID};
  sai_object_id_t egressSamplePacket{SAI_NULL_OBJECT_ID};
  std::vector<sai_object_id_t> ingressSampleMirrorList;
  std::vector<sai_object_id_t> egressSampleMirrorList;
  uint32_t prbsPolynomial{0};
  int32_t prbsConfig{0};
#if SAI_API_VERSION >= SAI_VERSION(1, 8, 1)
  sai_prbs_rx_state_t prbsRxState{SAI_PORT_PRBS_RX_STATUS_LOCK_WITH_ERRORS, 1};
#endif
  sai_object_id_t ingressAcl{SAI_NULL_OBJECT_ID};
  sai_object_id_t isolationGroup{SAI_NULL_OBJECT_ID};
  sai_object_id_t ingressMacsecAcl{SAI_NULL_OBJECT_ID};
  sai_object_id_t egressMacsecAcl{SAI_NULL_OBJECT_ID};
  uint16_t systemPortId{0};
  sai_port_ptp_mode_t ptpMode{SAI_PORT_PTP_MODE_NONE};
  sai_port_eye_values_list_t portEyeValues{};
#if SAI_API_VERSION >= SAI_VERSION(1, 10, 3)
  sai_port_lane_latch_status_list_t portRxSignalDetect{};
  sai_port_lane_latch_status_list_t portRxLockStatus{};
  sai_port_lane_latch_status_list_t portFecAlignmentLockStatus{};
  sai_latch_status_t portPcsLinkStatus{};
  sai_latch_status_t portCrcErrDetect{};
  sai_latch_status_t portExtOperStatusLatch{};
#endif
#if SAI_API_VERSION >= SAI_VERSION(1, 13, 0)
  sai_port_frequency_offset_ppm_list_t portRxPPM{};
  sai_port_snr_list_t portRxSNR{};
#endif
  sai_port_priority_flow_control_mode_t priorityFlowControlMode{
      SAI_PORT_PRIORITY_FLOW_CONTROL_MODE_COMBINED};
  sai_uint8_t priorityFlowControl{0xff};
  sai_uint8_t priorityFlowControlRx{0xff};
  sai_uint8_t priorityFlowControlTx{0xff};
  sai_port_err_status_list_t portError{};
  std::vector<sai_port_err_status_t> portErrStatusList;
  std::vector<sai_object_id_t> ingressPriorityGroupList;
  sai_uint32_t numberOfIngressPriorityGroups{0};
  sai_object_id_t qosTcToPriorityGroupMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosTcToVcMap{SAI_NULL_OBJECT_ID};
  sai_uint32_t cbfcSenderCreditLimit{0};
  sai_uint16_t cbfcSenderCreditSize{128};
  sai_int16_t cbfcSenderPacketOverhead{0};
  sai_object_id_t qosQueueToVcMap{SAI_NULL_OBJECT_ID};
  // READ_ONLY on real hardware, derived from the MMU carving. Fake has no
  // MMU, so these are canned. The negative overhead is deliberate: it is a
  // legal value (spec range -16..127) and catches modelling the attribute as
  // unsigned, which would read back as 65520.
  sai_uint16_t cbfcReceiverNativeCreditSize{256};
  sai_int16_t cbfcReceiverNativePacketOverhead{-16};
  sai_uint16_t cbfcReceiverNativeTotalCredits{1000};
  sai_object_id_t qosPfcPriorityToQueueMap{SAI_NULL_OBJECT_ID};
  sai_object_id_t qosPfcPriorityToPriorityGroupMap{SAI_NULL_OBJECT_ID};
#if SAI_API_VERSION >= SAI_VERSION(1, 9, 0)
  sai_uint32_t interFrameGap{96};
#endif
  bool linkTrainingEnable{false};
  sai_int32_t linkTrainingRxStatus{0}; // NOT_TRAINED
  bool fdrEnable{false};
  bool rxLaneSquelchEnable{false};
  std::optional<std::vector<sai_map_t>> pfcTcDldInterval{};
  std::optional<std::vector<sai_map_t>> pfcTcDlrInterval{};
  bool ars_enable{false};
  sai_uint32_t ars_port_load_scaling_factor{400};
  sai_uint32_t ars_port_load_past_weight{60};
  sai_uint32_t ars_port_load_future_weight{20};
  std::vector<sai_map_t> portPgPktDropStatus{};
  sai_object_id_t fabricSystemPort{};
  sai_uint32_t staticModuleId{};
  bool resetQueueCreditBalance{false};
  sai_int32_t pfcMonitorDirection{0};
  sai_int32_t cablePropagationDelayMediaType{0};
  sai_uint16_t pfcPauseDurationOverride{0};
  bool cablePropagationDelayMeasure{false};
  sai_int32_t linkScanMode{0};
  std::function<void()> onGetAttribute; // test-only hook
};

struct FakePortSerdes {
  explicit FakePortSerdes(sai_object_id_t _port) : port(_port) {}
  sai_object_id_t id;
  sai_object_id_t port;
  std::vector<uint32_t> preemphasis;
  std::vector<uint32_t> iDriver;
  std::vector<uint32_t> txFirPre1;
  std::vector<uint32_t> txFirPre2;
  std::vector<uint32_t> txFirPre3;
  std::vector<uint32_t> txFirMain;
  std::vector<uint32_t> txFirPost1;
  std::vector<uint32_t> txFirPost2;
  std::vector<uint32_t> txFirPost3;
  std::vector<uint32_t> txLutMode;
  std::vector<int32_t> rxReach;
  std::vector<int32_t> rxCtlCode;
  std::vector<int32_t> rxDspMode;
  std::vector<int32_t> rxAfeTrim;
  std::vector<int32_t> rxCouplingByPass;
  std::vector<int32_t> rxAfeTrimAdaptiveEnable;
  std::vector<int32_t> txDiffEncoderEn;
  std::vector<int32_t> txDigGain;
  std::vector<int32_t> txFfeCoeff0;
  std::vector<int32_t> txFfeCoeff1;
  std::vector<int32_t> txFfeCoeff2;
  std::vector<int32_t> txFfeCoeff3;
  std::vector<int32_t> txFfeCoeff4;
  std::vector<int32_t> txDriverSwing;
  std::vector<int32_t> rxInstgBoost1Start;
  std::vector<int32_t> rxInstgBoost1Step;
  std::vector<int32_t> rxInstgBoost1Stop;
  std::vector<int32_t> rxInstgBoost2OrHrStart;
  std::vector<int32_t> rxInstgBoost2OrHrStep;
  std::vector<int32_t> rxInstgBoost2OrHrStop;
  std::vector<int32_t> rxInstgC1Start1p7;
  std::vector<int32_t> rxInstgC1Step1p7;
  std::vector<int32_t> rxInstgC1Stop1p7;
  std::vector<int32_t> rxInstgDfeStart1p7;
  std::vector<int32_t> rxInstgDfeStep1p7;
  std::vector<int32_t> rxInstgDfeStop1p7;
  std::vector<int32_t> rxEnableScanSelection;
  std::vector<int32_t> rxInstgScanUseSrSettings;
  std::vector<int32_t> rxCdrCfgOvEn;
  std::vector<int32_t> rxCdrTdet1stOrdStepOvVal;
  std::vector<int32_t> rxCdrTdet2ndOrdStepOvVal;
  std::vector<int32_t> rxCdrTdetFineStepOvVal;
  std::vector<int32_t> txLdoBypass;
  std::vector<int32_t> rxLdoBypass;
  std::vector<int32_t> rxDiffEncoderEn;
  std::vector<int32_t> rxInstgEnableScan;
  std::vector<int32_t> rxFfeLengthBitmap;
  std::vector<int32_t> rxFfeLmsDynamicGatingEn;
  std::vector<int32_t> txPrecoding;
  std::vector<int32_t> rxPrecoding;
  std::string serdesCustomCollection;
};

struct FakePortConnector {
  explicit FakePortConnector(
      sai_object_id_t _linePort,
      sai_object_id_t _systemPort)
      : linePort(_linePort), systemPort(_systemPort) {}
  sai_object_id_t id;
  sai_object_id_t linePort;
  sai_object_id_t systemPort;
};

#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
struct FakePortLlrProfile {
  explicit FakePortLlrProfile(
      sai_uint32_t _outstandingFramesMax,
      sai_uint32_t _outstandingBytesMax,
      sai_uint32_t _replayTimerMax,
      sai_uint8_t _replayCountMax,
      sai_uint32_t _pcsLostTimeout,
      sai_uint32_t _dataAgeTimeout,
      sai_int32_t _initLlrFrameAction,
      sai_int32_t _flushLlrFrameAction,
      bool _reInitOnFlush,
      sai_uint16_t _ctlosTargetSpacing)
      : outstandingFramesMax(_outstandingFramesMax),
        outstandingBytesMax(_outstandingBytesMax),
        replayTimerMax(_replayTimerMax),
        replayCountMax(_replayCountMax),
        pcsLostTimeout(_pcsLostTimeout),
        dataAgeTimeout(_dataAgeTimeout),
        initLlrFrameAction(_initLlrFrameAction),
        flushLlrFrameAction(_flushLlrFrameAction),
        reInitOnFlush(_reInitOnFlush),
        ctlosTargetSpacing(_ctlosTargetSpacing) {}
  sai_object_id_t id;
  sai_uint32_t outstandingFramesMax;
  sai_uint32_t outstandingBytesMax;
  sai_uint32_t replayTimerMax;
  sai_uint8_t replayCountMax;
  sai_uint32_t pcsLostTimeout;
  sai_uint32_t dataAgeTimeout;
  sai_int32_t initLlrFrameAction;
  sai_int32_t flushLlrFrameAction;
  bool reInitOnFlush;
  sai_uint16_t ctlosTargetSpacing;
};
#endif

using FakePortManager = FakeManager<sai_object_id_t, FakePort>;
using FakePortSerdesManager = FakeManager<sai_object_id_t, FakePortSerdes>;
using FakePortConnectorManager =
    FakeManager<sai_object_id_t, FakePortConnector>;
#if SAI_API_VERSION >= SAI_VERSION(1, 18, 0)
// Base the ids at 1: id 0 is SAI_NULL_OBJECT_ID, which a port carries to mean
// "no profile bound", so a real profile must never share it.
using FakePortLlrProfileManager =
    FakeManager<sai_object_id_t, FakePortLlrProfile, 1>;
#endif

void populate_port_api(sai_port_api_t** port_api);

} // namespace facebook::fboss
