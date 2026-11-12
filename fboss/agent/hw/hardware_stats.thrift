namespace cpp2 facebook.fboss
namespace py neteng.fboss.hardware_stats
namespace go neteng.fboss.hardware_stats
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.hardware_stats

include "fboss/mka_service/if/mka_structs.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_hw_"}
@hack.LegacyOmitPrefixInNameString
@hack.ConstantsClass{name = "fboss_hw_CONSTANTS"}
@thrift.AllowLegacyMissingUris
package;

const i64 STAT_UNINITIALIZED = -1;

// UEC Link Layer Retry transmit state machine (UE Spec 1.0.2 section 5.1.5,
// Figure 5-3). ADVANCE is the only state that accepts new frames.
enum LlrTxStatus {
  OFF = 0,
  INIT = 1,
  ADVANCE = 2,
  REPLAY = 3,
  FLUSH = 4,
}

// UEC Link Layer Retry receive (ACK/NACK transmit) state machine (UE Spec
// 1.0.2 section 5.1.7, Figure 5-4). Note these are NOT the transmit states.
enum LlrRxStatus {
  OFF = 0,
  SEND_ACKS = 1,
  SEND_NACK = 2,
  NACK_SENT = 3,
}

struct MacsecSciFlowStats {
  1: mka_structs.MKASci sci;
  2: mka_structs.MacsecFlowStats flowStats;
}

struct MacsecSaIdSaStats {
  1: mka_structs.MKASecureAssociationId saId;
  2: mka_structs.MacsecSaStats saStats;
}

struct MacsecStats {
  1: mka_structs.MacsecPortStats ingressPortStats;
  2: mka_structs.MacsecPortStats egressPortStats;
  3: list<MacsecSciFlowStats> ingressFlowStats;
  4: list<MacsecSciFlowStats> egressFlowStats;
  // Secure association stats
  5: list<MacsecSaIdSaStats> rxSecureAssociationStats;
  6: list<MacsecSaIdSaStats> txSecureAssociationStats;
  7: mka_structs.MacsecAclStats ingressAclStats;
  8: mka_structs.MacsecAclStats egressAclStats;
}

struct HwPortStats {
  1: i64 inBytes_ = STAT_UNINITIALIZED;
  2: i64 inUnicastPkts_ = STAT_UNINITIALIZED;
  3: i64 inMulticastPkts_ = STAT_UNINITIALIZED;
  4: i64 inBroadcastPkts_ = STAT_UNINITIALIZED;
  5: i64 inDiscards_ = STAT_UNINITIALIZED;
  6: i64 inErrors_ = STAT_UNINITIALIZED;
  7: i64 inPause_ = STAT_UNINITIALIZED;
  8: i64 inIpv4HdrErrors_ = STAT_UNINITIALIZED;
  9: i64 inIpv6HdrErrors_ = STAT_UNINITIALIZED;
  10: i64 inDstNullDiscards_ = STAT_UNINITIALIZED;
  11: i64 inDiscardsRaw_ = STAT_UNINITIALIZED;

  12: i64 outBytes_ = STAT_UNINITIALIZED;
  13: i64 outUnicastPkts_ = STAT_UNINITIALIZED;
  14: i64 outMulticastPkts_ = STAT_UNINITIALIZED;
  15: i64 outBroadcastPkts_ = STAT_UNINITIALIZED;
  16: i64 outDiscards_ = STAT_UNINITIALIZED;
  17: i64 outErrors_ = STAT_UNINITIALIZED;
  18: i64 outPause_ = STAT_UNINITIALIZED;
  19: i64 outCongestionDiscardPkts_ = STAT_UNINITIALIZED;
  20: i64 wredDroppedPackets_ = STAT_UNINITIALIZED;
  21: map<i16, i64> queueOutDiscardBytes_ = {};
  22: map<i16, i64> queueOutBytes_ = {};
  23: i64 outEcnCounter_ = STAT_UNINITIALIZED;
  24: map<i16, i64> queueOutPackets_ = {};
  25: map<i16, i64> queueOutDiscardPackets_ = {};
  26: map<i16, i64> queueWatermarkBytes_ = {};
  27: i64 fecCorrectableErrors = STAT_UNINITIALIZED;
  28: i64 fecUncorrectableErrors = STAT_UNINITIALIZED;
  29: i64 inPfcCtrl_ = STAT_UNINITIALIZED;
  30: i64 outPfcCtrl_ = STAT_UNINITIALIZED;
  31: map<i16, i64> inPfc_ = {};
  32: map<i16, i64> inPfcXon_ = {};
  33: map<i16, i64> outPfc_ = {};
  34: map<i16, i64> queueWredDroppedPackets_ = {};
  35: map<i16, i64> queueEcnMarkedPackets_ = {};
  36: i64 fecCorrectedBits_ = STAT_UNINITIALIZED;
  /* Map of codewords received (value) with different counts of symbol errors (key).
   * fecCodewords_[0] = number of codewords with 0 symbol errors
   * fecCodewords_[1] = number of codewords with 1 symbol errors etc..
   */
  37: map<i16, i64> fecCodewords_ = {};
  38: optional i64 pqpErrorEgressDroppedPackets_;
  39: optional i64 fabricLinkDownDroppedCells_;
  40: optional i64 linkLayerFlowControlWatermark_;

  // seconds from epoch
  50: i64 timestamp_ = STAT_UNINITIALIZED;
  51: string portName_ = "";
  52: optional MacsecStats macsecStats;
  53: i64 inLabelMissDiscards_ = STAT_UNINITIALIZED;
  54: map<i16, i64> queueWatermarkLevel_ = {};
  55: i64 inCongestionDiscards_ = STAT_UNINITIALIZED;
  56: optional i64 inAclDiscards_;
  57: optional i64 inTrapDiscards_;
  58: optional i64 outForwardingDiscards_;
  // This mismatch is communicated directly via callback
  59: optional i64 fabricConnectivityMismatch_DEPRECATED;
  60: optional i32 logicalPortId;
  61: optional i64 leakyBucketFlapCount_;
  62: optional i64 cableLengthMeters;
  63: optional bool dataCellsFilterOn;
  64: map<i16, i64> egressGvoqWatermarkBytes_ = {};
  65: map<i16, i64> pgInCongestionDiscards_ = {};
  66: optional i64 pfcDeadlockDetection_;
  67: optional i64 pfcDeadlockRecovery_;
  68: map<i16, bool> pgInCongestionDiscardSeen_ = {};
  // MAC transmit data queue min/max watermark is added to
  // monitor for TX stuck conditions which could result in
  // RCI stuck like in S545783. Watermark is in cells and
  // not converted to bytes, details in CS00012417758.
  69: optional i64 macTransmitQueueMinWatermarkCells_;
  70: optional i64 macTransmitQueueMaxWatermarkCells_;
  71: optional bool macTransmitQueueStuck_;
  72: optional i64 fabricControlRxPackets_;
  73: optional i64 fabricControlTxPackets_;
  // The total duration for which a specific priority was
  // paused in RX / TX direction.
  74: map<i16, i64> txPfcDurationUsec_ = {};
  75: map<i16, i64> rxPfcDurationUsec_ = {};
  76: optional i64 outDiscardsSll_;
  77: optional i64 outDiscardsHll_;
  78: optional i64 inSrv6MySidDiscards_;
  79: optional i64 cableDelayNsec;
  // UEC Link Layer Retry counters (UE Spec 1.0.2 section 5.1.11, Table 5-13).
  // Populated only on LLR-capable ASICs (Tomahawk Ultra).
  80: optional i64 llrTxOk_;
  81: optional i64 llrTxReplay_;
  82: optional i64 llrRxOk_;
  // 83 (llrRxBad_) intentionally unused: LLR_RX_BAD has no SDK backing on
  // Tomahawk Ultra (Broadcom CS00012472055); field number reserved, do not reuse.
  84: optional i64 llrRxMissingSeq_;
  85: optional i64 llrRxDuplicateSeq_;
  86: optional i64 llrRxAckNackSeqError_;
  87: optional i64 llrRxReplay_;
  88: optional i64 linkDownDebounceRetriggerCount_;
  89: optional i64 linkUpDebounceRetriggerCount_;
  90: optional i64 llrTxInitCtlOs_;
  91: optional i64 llrTxInitEchoCtlOs_;
  92: optional i64 llrTxAckCtlOs_;
  93: optional i64 llrTxNackCtlOs_;
  94: optional i64 llrRxInitCtlOs_;
  95: optional i64 llrRxInitEchoCtlOs_;
  96: optional i64 llrRxAckCtlOs_;
  97: optional i64 llrRxNackCtlOs_;
  98: optional i64 llrRxExpectedSeqGood_;
  99: optional i64 llrRxExpectedSeqPoisoned_;
  100: optional i64 llrRxExpectedSeqBad_;
  // Link flaps plus debounce retriggers suppressed by the port debounce hold
  // timers.
  101: optional i64 linkFault_;
  102: i64 fecCorrectedSymbols_ = STAT_UNINITIALIZED;
  // Current LLR state machine status, read from hardware once per stats round.
  // Set only for ports with an LLR profile bound on an LLR-capable ASIC, and
  // cleared whenever the read is skipped or fails, so a stale state machine
  // value is never published. A down port reads OFF: LLR is not running.
  //
  // Tomahawk Ultra 1 reports a subset: the Broadcom SDK derives these from
  // bcm_port_llr_status_t, which carries only llr_active_tx/llr_active_rx, so
  // TX is either ADVANCE or OFF and RX is either SEND_ACKS or OFF. OFF on an
  // up, LLR-bound port therefore means LLR did not come up -- including a
  // never-completed INIT handshake, which TU1 cannot report as INIT.
  103: optional LlrTxStatus llrTxStatus_;
  104: optional LlrRxStatus llrRxStatus_;

  // Broadcom LLR stat extensions (sai_port_stat_extensions_t), a separate
  // family from the SAI 1.18 LLR counters above and fetched in a separate read.
  // Eligible/ineligible partition every frame the MAC sends or receives by
  // whether LLR was protecting it, which is the only way to tell a port with
  // LLR running from a port where the profile is bound but the state machine
  // never came up.
  //
  // The SAI names for the last three are misleading, so these follow the SDK
  // counters they resolve to: LLR_REPLAY_EVENT is snmpBcmTxLlrNackReplayPkts
  // (NACK-triggered replay episodes), LLR_TX_TIMER_REPLAY is
  // snmpBcmTxLlrTimerReplayPkts (timer-triggered), and LLR_TOTAL_ERROR is
  // snmpBcmTxLlrErrorPkts, a transmit-side counter despite the name. Broadcom
  // gave the mapping, and confirmed all eight are gettable on Tomahawk Ultra 1,
  // in CS00012472055.
  //
  // SAI_PORT_STAT_LLR_REPLAY is deliberately absent: it resolves to
  // snmpBcmTxLlrReplayedPkts, the same SDK counter as SAI_PORT_STAT_LLR_TX_OK's
  // sibling SAI_PORT_STAT_LLR_TX_REPLAY, already published as llrTxReplay_.
  105: optional i64 llrTxEligiblePkts_;
  106: optional i64 llrTxIneligiblePkts_;
  107: optional i64 llrRxEligiblePkts_;
  108: optional i64 llrRxIneligiblePkts_;
  109: optional i64 llrTxNackReplayEvent_;
  110: optional i64 llrTxTimerReplayEvent_;
  111: optional i64 llrTxError_;

  // UEC Credit-Based Flow Control counters (UE Spec 1.0.2 section 5.2).
  // Populated only on CBFC-capable ASICs (Tomahawk Ultra). Three of the six
  // SAI_PORT_STAT_CBFC_* counters are absent because nothing backs them on
  // TU1: SENDER_CREDITS_USED is B0-stepping only, and
  // SENDER_CREDITS_USED_WATERMARK / NUM_CC_UPDATE_MESSAGES_RX have no BCM
  // counter on any stepping.
  112: optional i64 cbfcCcUpdateTx_;
  113: optional i64 cbfcCfUpdateTx_;
  114: optional i64 cbfcCfUpdateRx_;

  // Per-virtual-channel CBFC counters, keyed by VC index the way the queue
  // maps above are keyed by queue. Availability differs per counter on
  // Tomahawk Ultra 1, so they do not form a coherent set:
  //
  //   SenderCreditsConsumed   bcmCosqStatCbfcTxPortVcCreditConsumed
  //   SenderCreditsFreed      bcmCosqStatCbfcTxPortVcCRCyclicCount
  //   ReceiverCreditsFreed    bcmCosqStatCbfcRxPortVcCFCyclicCount
  //     -- all three are TU1 B0 stepping only, NOT_SUPPORTED on A0.
  //
  //   ReceiverCreditsConsumed bcmCosqStatIngressCbfcPortVcHeadroomBytesCurrent
  //     -- present on A0 and B0, but despite the SAI name it is a current
  //        headroom gauge in BYTES, not a cumulative credit count. Do not
  //        difference it against ReceiverCreditsFreed.
  //
  // So only the sender pair supports a consumed-minus-freed reading of credit
  // outstanding, and only on B0. SENDER_CREDITS_USED and
  // SENDER_CREDITS_USED_WATERMARK would report outstanding credit directly but
  // the SDK implements neither, which is why that reading has to be derived.
  //
  // Kept in the FSDB stats tree only; not exported to fb303/ODS, matching the
  // LLR handshake counters above. Exporting a counter that is NOT_SUPPORTED on
  // the stepping we run would be worse than not charting it.
  115: map<i16, i64> cbfcVcSenderCreditsConsumed_ = {};
  116: map<i16, i64> cbfcVcSenderCreditsFreed_ = {};
  117: map<i16, i64> cbfcVcReceiverCreditsConsumed_ = {};
  118: map<i16, i64> cbfcVcReceiverCreditsFreed_ = {};

  // HW protection (FRR) switchover counters, populated only on Broadcom
  // Tomahawk 5/6 with XGS SDK 13.3+. Events counts primary to backup
  // transitions; drop packets counts packets lost during switchover.
  119: optional i64 hwProtectionSwitchoverEvents_;
  120: optional i64 hwProtectionSwitchoverDropPkts_;

  // Software Fabric Link Monitoring counters, populated by SwSwitch only for
  // monitored ports.
  121: optional i64 fabricLinkMonitoringTxPackets_;
  122: optional i64 fabricLinkMonitoringRxPackets_;
  123: optional i64 fabricLinkMonitoringDroppedPackets_;
  124: optional i64 fabricLinkMonitoringInvalidPayloadPackets_;
  125: optional i64 fabricLinkMonitoringNoPendingSeqNumPackets_;
}

struct HwSysPortStats {
  // These map keys are the queue and the value is the counter value
  1: map<i16, i64> queueOutDiscardBytes_ = {};
  2: map<i16, i64> queueOutBytes_ = {};
  3: map<i16, i64> queueWatermarkBytes_ = {};
  4: map<i16, i64> queueWredDroppedPackets_ = {};
  5: map<i16, i64> queueCreditWatchdogDeletedPackets_ = {};
  6: map<i16, i64> queueLatencyWatermarkNsec_ = {};

  // seconds from epoch
  // Field index at a distance to allow for other stat additions
  100: i64 timestamp_ = STAT_UNINITIALIZED;
  101: string portName_ = "";
}

struct HwTrunkStats {
  1: i64 capacity_ = STAT_UNINITIALIZED;

  2: i64 inBytes_ = STAT_UNINITIALIZED;
  3: i64 inUnicastPkts_ = STAT_UNINITIALIZED;
  4: i64 inMulticastPkts_ = STAT_UNINITIALIZED;
  5: i64 inBroadcastPkts_ = STAT_UNINITIALIZED;
  6: i64 inDiscards_ = STAT_UNINITIALIZED;
  7: i64 inErrors_ = STAT_UNINITIALIZED;
  8: i64 inPause_ = STAT_UNINITIALIZED;
  9: i64 inIpv4HdrErrors_ = STAT_UNINITIALIZED;
  10: i64 inIpv6HdrErrors_ = STAT_UNINITIALIZED;
  11: i64 inDstNullDiscards_ = STAT_UNINITIALIZED;
  12: i64 inDiscardsRaw_ = STAT_UNINITIALIZED;

  13: i64 outBytes_ = STAT_UNINITIALIZED;
  14: i64 outUnicastPkts_ = STAT_UNINITIALIZED;
  15: i64 outMulticastPkts_ = STAT_UNINITIALIZED;
  16: i64 outBroadcastPkts_ = STAT_UNINITIALIZED;
  17: i64 outDiscards_ = STAT_UNINITIALIZED;
  18: i64 outErrors_ = STAT_UNINITIALIZED;
  19: i64 outPause_ = STAT_UNINITIALIZED;
  20: i64 outCongestionDiscardPkts_ = STAT_UNINITIALIZED;
  21: i64 outEcnCounter_ = STAT_UNINITIALIZED;
  22: i64 wredDroppedPackets_ = STAT_UNINITIALIZED;
  23: optional MacsecStats macsecStats;
}

struct HwResourceStats {
  /**
   * Stale flag is set when for whatever reason
   * counter collection fails we log
   * errors and set stale counter to 1 in such scenarios.
   * This should never really happen in practice.
   **/
  1: bool hw_table_stats_stale = true;
  /*
   * Not all platforms will have all of these stats
   * available. Post a platform stat populate,
   * STAT_UNINITIALIZED can be read as the stat being
   * unavailable at the particular hw/platform
   */
  2: i32 l3_host_max = STAT_UNINITIALIZED;
  3: i32 l3_host_used = STAT_UNINITIALIZED;
  4: i32 l3_host_free = STAT_UNINITIALIZED;
  5: i32 l3_ipv4_host_used = STAT_UNINITIALIZED;
  6: i32 l3_ipv4_host_free = STAT_UNINITIALIZED;
  7: i32 l3_ipv6_host_used = STAT_UNINITIALIZED;
  8: i32 l3_ipv6_host_free = STAT_UNINITIALIZED;
  9: i32 l3_nexthops_max = STAT_UNINITIALIZED;
  10: i32 l3_nexthops_used = STAT_UNINITIALIZED;
  11: i32 l3_nexthops_free = STAT_UNINITIALIZED;
  12: i32 l3_ipv4_nexthops_free = STAT_UNINITIALIZED;
  13: i32 l3_ipv6_nexthops_free = STAT_UNINITIALIZED;
  14: i32 l3_ecmp_groups_max = STAT_UNINITIALIZED;
  15: i32 l3_ecmp_groups_used = STAT_UNINITIALIZED;
  16: i32 l3_ecmp_groups_free = STAT_UNINITIALIZED;
  17: i32 l3_ecmp_group_members_free = STAT_UNINITIALIZED;

  // LPM
  18: i32 lpm_ipv4_max = STAT_UNINITIALIZED;
  19: i32 lpm_ipv4_used = STAT_UNINITIALIZED;
  20: i32 lpm_ipv4_free = STAT_UNINITIALIZED;
  21: i32 lpm_ipv6_free = STAT_UNINITIALIZED;
  22: i32 lpm_ipv6_mask_0_64_max = STAT_UNINITIALIZED;
  23: i32 lpm_ipv6_mask_0_64_used = STAT_UNINITIALIZED;
  24: i32 lpm_ipv6_mask_0_64_free = STAT_UNINITIALIZED;
  25: i32 lpm_ipv6_mask_65_127_max = STAT_UNINITIALIZED;
  26: i32 lpm_ipv6_mask_65_127_used = STAT_UNINITIALIZED;
  27: i32 lpm_ipv6_mask_65_127_free = STAT_UNINITIALIZED;
  28: i32 lpm_slots_max = STAT_UNINITIALIZED;
  29: i32 lpm_slots_used = STAT_UNINITIALIZED;
  30: i32 lpm_slots_free = STAT_UNINITIALIZED;

  // ACLs
  31: i32 acl_entries_used = STAT_UNINITIALIZED;
  32: i32 acl_entries_free = STAT_UNINITIALIZED;
  33: i32 acl_entries_max = STAT_UNINITIALIZED;
  34: i32 acl_counters_used = STAT_UNINITIALIZED;
  35: i32 acl_counters_free = STAT_UNINITIALIZED;
  36: i32 acl_counters_max = STAT_UNINITIALIZED;
  37: i32 acl_meters_used = STAT_UNINITIALIZED;
  38: i32 acl_meters_free = STAT_UNINITIALIZED;
  39: i32 acl_meters_max = STAT_UNINITIALIZED;

  // Mirrors
  40: i32 mirrors_used = STAT_UNINITIALIZED;
  41: i32 mirrors_free = STAT_UNINITIALIZED;
  42: i32 mirrors_max = STAT_UNINITIALIZED;
  43: i32 mirrors_span = STAT_UNINITIALIZED;
  44: i32 mirrors_erspan = STAT_UNINITIALIZED;
  45: i32 mirrors_sflow = STAT_UNINITIALIZED;

  // EM
  46: i32 em_entries_used = STAT_UNINITIALIZED;
  47: i32 em_entries_free = STAT_UNINITIALIZED;
  48: i32 em_entries_max = STAT_UNINITIALIZED;
  49: i32 em_counters_used = STAT_UNINITIALIZED;
  50: i32 em_counters_free = STAT_UNINITIALIZED;
  51: i32 em_counters_max = STAT_UNINITIALIZED;

  // VOQ system resources
  52: i32 system_ports_free = STAT_UNINITIALIZED;
  53: i32 voqs_free = STAT_UNINITIALIZED;

  // SRV6 MySid resources
  54: i32 my_sid_entries_free = STAT_UNINITIALIZED;
}

struct HwAsicErrors {
  1: i64 parityErrors;
  2: i64 correctedParityErrors;
  3: i64 uncorrectedParityErrors;
  4: i64 asicErrors;
  // DNX specific errors
  5: optional i64 ingressReceiveEditorErrors;
  6: optional i64 ingressTransmitPipelineErrors;
  7: optional i64 egressPacketNetworkInterfaceErrors;
  8: optional i64 alignerErrors;
  9: optional i64 forwardingQueueProcessorErrors;
  10: optional i64 allReassemblyContextsTaken;
  11: optional i64 reassemblyErrors;
  12: optional i64 fdrFifoOverflowErrors;
  13: optional i64 fdaFifoOverflowErrors;
  14: optional i64 isolationFirmwareCrashes;
  15: optional i64 rxFifoStuckDetected;
  16: optional i64 congestionManagementErrors;
  17: optional i64 dramDataPathErrors;
  18: optional i64 dramQueueManagementErrors;
  19: optional i64 egressCongestionManagementErrors;
  20: optional i64 egressDataBufferErrors;
  21: optional i64 fabricControlReceiveErrors;
  22: optional i64 fabricControlTransmitErrors;
  23: optional i64 fabricDataAggregateErrors;
  24: optional i64 fabricDataReceiveErrors;
  25: optional i64 fabricDataTransmitErrors;
  26: optional i64 fabricMacErrors;
  27: optional i64 ingressPacketSchedulerErrors;
  28: optional i64 ingressPacketTransmitErrors;
  29: optional i64 managementUnitErrors;
  30: optional i64 nifBufferUnitErrors;
  31: optional i64 nifManagementErrors;
  32: optional i64 onChipBufferMemoryErrors;
  33: optional i64 packetDescriptorMemoryErrors;
  34: optional i64 packetQueueProcessorErrors;
  35: optional i64 receiveQueueProcessorErrors;
  36: optional i64 schedulerErrors;
  37: optional i64 sramPacketBufferErrors;
  38: optional i64 sramQueueManagementErrors;
  39: optional i64 tmActionResolutionErrors;
  // DNX aggregated block level errors
  40: optional i64 ingressTmErrors;
  41: optional i64 egressTmErrors;
  42: optional i64 ingressPpErrors;
  43: optional i64 egressPpErrors;
  44: optional i64 dramErrors;
  45: optional i64 counterAndMeterErrors;
  46: optional i64 fabricRxErrors;
  47: optional i64 fabricTxErrors;
  48: optional i64 fabricLinkErrors;
  49: optional i64 fabricTopologyErrors;
  50: optional i64 networkInterfaceErrors;
  // DNX aggregated block level warnings
  51: optional i64 ingressTmWarnings;
  52: optional i64 egressTmWarnings;
  53: optional i64 dramWarnings;
  54: optional i64 fabricRxWarnings;
  55: optional i64 fabricTxWarnings;
  56: optional i64 fabricLinkWarnings;
  57: optional i64 networkInterfaceWarnings;
  // DNX fabric device block level errors
  58: optional i64 fabricControlPathErrors;
  59: optional i64 fabricDataPathErrors;
  60: optional i64 cpuErrors;
  // ASIC reset errors
  61: optional i64 asicSoftResetErrors;
}

struct HwTeFlowStats {
  1: i64 bytes = STAT_UNINITIALIZED;
}

struct TeFlowStats {
  1: map<string, HwTeFlowStats> hwTeFlowStats;
  2: i64 timestamp;
}

struct FabricReachabilityStats {
  1: i64 mismatchCount;
  2: i64 missingCount;
  3: i64 virtualDevicesWithAsymmetricConnectivity;
  4: i64 switchReachabilityChangeCount;
  5: i64 bogusCount;
}

struct HwRxReasonStats {
  1: map<i64, i64> rxReasonStats;
}

// The deviceWatermarkBytes has been moved from HwBufferPoolStats to
// HwSwitchWatermarkStats, but retaining the HwBufferPoolStats but
// will be marked as deprecated everywhere until it has some other
// stats in it.
struct HwBufferPoolStats {
  // Deprecate deviceWatermarkBytes once HwSwitchWatermarkStats is
  // available in prod!
  1: i64 deviceWatermarkBytes;
}

struct HwSwitchWatermarkStats {
  1: optional i64 fdrRciWatermarkBytes;
  2: optional i64 coreRciWatermarkBytes;
  3: optional i64 dtlQueueWatermarkBytes;
  4: i64 deviceWatermarkBytes;
  5: map<string, i64> globalHeadroomWatermarkBytes;
  6: map<string, i64> globalSharedWatermarkBytes;
  7: optional i64 egressCoreBufferWatermarkBytes;
  8: optional i64 sramMinBufferWatermarkBytes;
  9: optional i64 fdrFifoWatermarkBytes;
  10: optional i64 fabricInterCellJitterWatermarkUsec;
}

struct CpuPortStats {
  1: map<i32, i64> queueInPackets_; // TODO: Deprecate this
  2: map<i32, i64> queueDiscardPackets_; // TODO: Deprecate this
  3: map<i32, string> queueToName_; // TODO: Deprecate this
  4: HwPortStats portStats_;
}

struct HwSwitchDropStats {
  1: optional i64 globalDrops;
  2: optional i64 globalReachabilityDrops;
  3: optional i64 packetIntegrityDrops;
  // DNX Specific drop counters
  4: optional i64 fdrCellDrops;
  5: optional i64 voqResourceExhaustionDrops;
  6: optional i64 globalResourceExhaustionDrops;
  7: optional i64 sramResourceExhaustionDrops;
  8: optional i64 vsqResourceExhaustionDrops;
  9: optional i64 dropPrecedenceDrops;
  10: optional i64 queueResolutionDrops;
  11: optional i64 ingressPacketPipelineRejectDrops;
  12: optional i64 corruptedCellPacketIntegrityDrops;
  13: optional i64 missingCellPacketIntegrityDrops;
  14: optional i64 rqpFabricCellCorruptionDrops;
  15: optional i64 rqpNonFabricCellCorruptionDrops;
  16: optional i64 rqpNonFabricCellMissingDrops;
  17: optional i64 rqpParityErrorDrops;
  18: optional i64 tc0RateLimitDrops;
  19: optional i64 dramDataPathPacketError;
  // Switch debug counter drops
  20: optional i64 switchL2InDrops;
  21: optional i64 switchL3InDrops;
  22: optional i64 switchTunnelInDrops;
}

struct HwSwitchDropBitmapStats {
  1: optional i64 ingressMacDropBitmap;
  2: optional i64 egressPipeDropBitmap;
  3: optional i64 egressMacDropBitmap;
  4: optional i64 pipelineGeneralDropBitmap;
  5: optional i64 pipelineMacDropBitmap;
  6: optional i64 pipelineMplsDropBitmap;
  7: optional i64 pipelineTunnelDropBitmap;
  8: optional i64 pipelineIpv4DropBitmap;
  9: optional i64 pipelineIpv6DropBitmap;
  10: optional i64 bufferDropBitmap;
  11: optional i64 queueDropBitmap;
  12: optional i64 hostDropBitmap;
}

struct HwSwitchDramStats {
  1: optional i64 dramEnqueuedBytes;
  2: optional i64 dramDequeuedBytes;
  3: optional i64 dramBlockedTimeNsec;
  4: optional i64 dramQuarantinedBufferCount;
}

struct HwSwitchCreditStats {
  1: optional i64 deletedCreditBytes;
}

struct HwSwitchPipelineStats {
  1: map<i16, i64> rxCells = {};
  2: map<i16, i64> txCells = {};
  3: map<i16, i64> rxBytes = {};
  4: map<i16, i64> txBytes = {};
  5: map<i16, i64> rxWatermarkLevels = {};
  6: map<i16, i64> txWatermarkLevels = {};
  7: map<i16, i64> curOccupancyBytes = {};
  8: map<i16, i64> globalDrops = {};
}

struct HwSwitchTemperatureStats {
  1: map<string, i64> timeStamp = {};
  2: map<string, float> value = {};
}

# Generic counter object that maybe attached to
# diverse set of objects
struct HwSwitchCounter {
  1: optional i64 bytes;
  2: optional i64 packets;
}

struct HwSwitchCounterStats {
  1: map<string, HwSwitchCounter> routeCounters;
}

struct HwSwitchFb303GlobalStats {
  1: i64 tx_pkt_allocated;
  2: i64 tx_pkt_freed;
  3: i64 tx_pkt_sent;
  4: i64 tx_pkt_sent_done;
  5: i64 tx_errors;
  6: i64 tx_pkt_allocation_errors;
  7: i64 parity_errors;
  8: i64 parity_corr;
  9: i64 parity_uncorr;
  10: i64 asic_error;
  11: i64 global_drops;
  12: i64 global_reachability_drops;
  13: i64 packet_integrity_drops;
  14: i64 dram_enqueued_bytes;
  15: i64 dram_dequeued_bytes;
  16: i64 fabric_reachability_missing;
  17: i64 fabric_reachability_mismatch;
  // DNX Specific counters
  18: optional i64 fdr_cell_drops;
  19: optional i64 ingress_receive_editor_errors;
  20: optional i64 ingress_transmit_pipeline_errors;
  21: optional i64 egress_packet_network_interface_errors;
  22: optional i64 aligner_errors;
  23: optional i64 forwarding_queue_processor_errors;
  24: i64 virtual_devices_with_asymmetric_connectivity;
  25: i64 switch_reachability_change;
  /*
   * Until later versions of the chip (B0), cable lengths seen by
   * each port group has a bearing on how efficiently FDR-in buffers on J3 are
   * utilized.
   * If all port groups see roughly the same cable length then they all dequeue
   * cells for a pkt to FDR-out buffers around the same time. If OTOH a port
   * groups sees substantially lower cable lengths, FDR-in corresponding
   * to that port group dequeues to FDR-out faster. This causes cells to
   * sit for longer in FDR-out buffers, leading to more stress on the
   * latter. Our cabling plans try to minimize this. Report the skew seen
   * here to corroborate that cabling was as desired
  */
  26: optional i64 inter_port_group_cable_skew_meters;
  27: optional i64 dram_blocked_time_ns;
  28: optional i64 deleted_credit_bytes;
  29: optional i64 vsq_resource_exhaustion_drops;
  30: optional i64 rqp_fabric_cell_corruption;
  31: optional i64 rqp_non_fabric_cell_corruption;
  32: optional i64 rqp_non_fabric_cell_missing;
  33: optional i64 rqp_parity_error;
  34: i64 fabric_connectivity_bogus;
  35: optional i64 interrupt_masked_events;
  36: optional i64 asic_revision;
  37: optional i64 sram_low_buffer_limit_hit_count;
  38: optional i64 dram_quarantined_buffer_count;
  // Number of SDK register/state dump writes the SDK skipped because the
  // configured dump rate limit window was still active
  39: optional i64 sdk_dump_suppressed_count;
}

struct HwSwitchHardResetStats {
  1: i16 hard_reset_notification_received;
}

struct HwFlowletStats {
  1: i64 l3EcmpDlbFailPackets;
  2: i64 l3EcmpDlbPortReassignmentCount;
}

struct AclStats {
  1: map<string, i64> statNameToCounterMap;
}

struct HwHighFrequencyPfcStats {
  1: optional i64 inPfc;
  2: optional i64 outPfc;
}

struct HwHighFrequencyPortStats {
  1: map<i16, HwHighFrequencyPfcStats> pfcStats = {};
  3: map<i16, i64> queueWatermarkBytes = {};
  4: map<i16, i64> pgSharedWatermarkBytes = {};
}

struct HwHighFrequencyStats {
  1: i64 timestampUs = STAT_UNINITIALIZED;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, HwHighFrequencyPortStats> portStats;
  3: map<i16, i64> itmPoolSharedWatermarkBytes = {};
}

struct HwRouterInterfaceStats {
  1: i64 inBytes_;
  2: i64 inPkts_;
  3: i64 outBytes_;
  4: i64 outPkts_;
  5: i64 inErrorBytes_;
  6: i64 inErrorPkts_;
  7: i64 outErrorBytes_;
  8: i64 outErrorPkts_;
}
