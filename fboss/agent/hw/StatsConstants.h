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

#include <folly/Range.h>

namespace facebook::fboss {

inline folly::StringPiece constexpr kCapacity() {
  return "capacity";
}

inline folly::StringPiece constexpr kInBytes() {
  return "in_bytes";
}

inline folly::StringPiece constexpr kInUnicastPkts() {
  return "in_unicast_pkts";
}

inline folly::StringPiece constexpr kInMulticastPkts() {
  return "in_multicast_pkts";
}

inline folly::StringPiece constexpr kInBroadcastPkts() {
  return "in_broadcast_pkts";
}

inline folly::StringPiece constexpr kInErrors() {
  return "in_errors";
}

inline folly::StringPiece constexpr kInPause() {
  return "in_pause_frames";
}

inline folly::StringPiece constexpr kInPfc() {
  return "in_pfc_frames";
}

inline folly::StringPiece constexpr kInPfcXon() {
  return "in_pfc_xon_frames";
}

inline folly::StringPiece constexpr kInIpv4HdrErrors() {
  return "in_ipv4_header_errors";
}

inline folly::StringPiece constexpr kInIpv6HdrErrors() {
  return "in_ipv6_header_errors";
}

inline folly::StringPiece constexpr kInDiscardsRaw() {
  return "in_discards_raw";
}

inline folly::StringPiece constexpr kInDiscards() {
  return "in_discards";
}

inline folly::StringPiece constexpr kInDstNullDiscards() {
  return "in_dst_null_discards";
}

inline folly::StringPiece constexpr kInSrv6MySidDiscards() {
  return "in_srv6_mysid_discards";
}

inline folly::StringPiece constexpr kLinkDownDebounceRetriggerCount() {
  return "link_down_debounce_retrigger_count";
}

inline folly::StringPiece constexpr kLinkUpDebounceRetriggerCount() {
  return "link_up_debounce_retrigger_count";
}

inline folly::StringPiece constexpr kInDroppedPkts() {
  return "in_dropped_pkts";
}

inline folly::StringPiece constexpr kInPkts() {
  return "in_pkts";
}

inline folly::StringPiece constexpr kOutBytes() {
  return "out_bytes";
}

inline folly::StringPiece constexpr kOutUnicastPkts() {
  return "out_unicast_pkts";
}

inline folly::StringPiece constexpr kOutMulticastPkts() {
  return "out_multicast_pkts";
}

inline folly::StringPiece constexpr kOutBroadcastPkts() {
  return "out_broadcast_pkts";
}

inline folly::StringPiece constexpr kOutDiscards() {
  return "out_discards";
}

inline folly::StringPiece constexpr kOutErrors() {
  return "out_errors";
}

inline folly::StringPiece constexpr kOutPause() {
  return "out_pause_frames";
}

inline folly::StringPiece constexpr kOutPfc() {
  return "out_pfc_frames";
}

inline folly::StringPiece constexpr kOutCongestionDiscards() {
  return "out_congestion_discards";
}

inline folly::StringPiece constexpr kOutCongestionDiscardsBytes() {
  return "out_congestion_discards_bytes";
}

inline folly::StringPiece constexpr kOutEcnCounter() {
  return "out_ecn_counter";
}

inline folly::StringPiece constexpr kOutPkts() {
  return "out_pkts";
}

inline folly::StringPiece constexpr kCreditWatchdogDeletedPackets() {
  return "credit_watchdog_deleted_packets";
}

inline folly::StringPiece constexpr kFecCorrectable() {
  return "fec_correctable_errors";
}

inline folly::StringPiece constexpr kFecUncorrectable() {
  return "fec_uncorrectable_errors";
}

inline folly::StringPiece constexpr kLeakyBucketFlapCnt() {
  return "leaky_bucket_flap_cnt";
}

// Cumulative per-port counts of phy transitions, qualified by side because
// updatePmdInfo() runs once per side on XPHY and the two must not share a
// monotonic counter.
inline folly::StringPiece constexpr kLineRxSignalDetectChanged() {
  return "line.rx_signal_detect_changed";
}

inline folly::StringPiece constexpr kSystemRxSignalDetectChanged() {
  return "system.rx_signal_detect_changed";
}

inline folly::StringPiece constexpr kLineRxCdrLockChanged() {
  return "line.rx_cdr_lock_changed";
}

inline folly::StringPiece constexpr kSystemRxCdrLockChanged() {
  return "system.rx_cdr_lock_changed";
}

// Line side only: RS layer fault status is never collected for the system
// side, so there is no system.* counterpart to these.
inline folly::StringPiece constexpr kLineLocalFaultChanged() {
  return "line.local_fault_changed";
}

inline folly::StringPiece constexpr kLineRemoteFaultChanged() {
  return "line.remote_fault_changed";
}

inline folly::StringPiece constexpr kLinkDownDuringWarmBoot() {
  return "link_down_during_warmboot";
}

inline folly::StringPiece constexpr kInLabelMissDiscards() {
  return "in_label_miss_discards";
}

inline folly::StringPiece constexpr kInAclDiscards() {
  return "in_acl_discards";
}

inline folly::StringPiece constexpr kInTrapDiscards() {
  return "in_trap_discards";
}

inline folly::StringPiece constexpr kWredDroppedPackets() {
  return "wred_dropped_packets";
}

inline folly::StringPiece constexpr kObmLossyHighPriDroppedPkts() {
  return "obm_lossy_high_pri_dropped_pkts";
}

inline folly::StringPiece constexpr kObmLossyHighPriDroppedBytes() {
  return "obm_lossy_high_pri_dropped_bytes";
}

inline folly::StringPiece constexpr kObmLossyLowPriDroppedPkts() {
  return "obm_lossy_low_pri_dropped_pkts";
}

inline folly::StringPiece constexpr kObmLossyLowPriDroppedBytes() {
  return "obm_lossy_low_pri_dropped_bytes";
}

inline folly::StringPiece constexpr kObmHighWatermark() {
  return "obm_high_watermark";
}

inline folly::StringPiece constexpr kErrorsPerCodeword() {
  return "errors_per_codeword";
}

inline folly::StringPiece constexpr kLatencyWatermarkNsec() {
  return "latency_watermark_nsec";
}

/**
 * Maximum FEC errors we can ever see under any config
 */
constexpr int kMaxFecErrors = 16;
/*
 * Macsec constants
 */

inline folly::StringPiece constexpr kInPreMacsecDropPkts() {
  return "in_premacsec_drop_pkts";
}

inline folly::StringPiece constexpr kInMacsecControlPkts() {
  return "in_macsec_control_pkts";
}

inline folly::StringPiece constexpr kInMacsecDataPkts() {
  return "in_macsec_data_pkts";
}

inline folly::StringPiece constexpr kInMacsecDecryptedBytes() {
  return "in_macsec_decrypted_bytes";
}

inline folly::StringPiece constexpr kInMacsecBadOrNoTagDroppedPkts() {
  return "in_macsec_no_or_bad_tag_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecNoSciDroppedPkts() {
  return "in_macsec_no_sci_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecUnknownSciPkts() {
  return "in_macsec_unknonwn_sci_pkts";
}

inline folly::StringPiece constexpr kInMacsecOverrunDroppedPkts() {
  return "in_macsec_overrun_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecDelayedPkts() {
  return "in_macsec_delayed_pkts";
}

inline folly::StringPiece constexpr kInMacsecLateDroppedPkts() {
  return "in_macsec_late_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecNotValidDroppedPkts() {
  return "in_macsec_not_valid_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecInvalidPkts() {
  return "in_macsec_invalid_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecNoSADroppedPkts() {
  return "in_macsec_no_sa_dropped_pkts";
}

inline folly::StringPiece constexpr kInMacsecUnusedSAPkts() {
  return "in_macsec_unused_sa_pkts";
}

inline folly::StringPiece constexpr kInMacsecUntaggedPkts() {
  return "in_macsec_untagged_pkts";
}

inline folly::StringPiece constexpr kInMacsecCurrentXpn() {
  return "in_macsec_sa_current_xpn";
}

inline folly::StringPiece constexpr kOutPreMacsecDropPkts() {
  return "out_premacsec_drop_pkts";
}

inline folly::StringPiece constexpr kOutMacsecControlPkts() {
  return "out_macsec_control_pkts";
}

inline folly::StringPiece constexpr kOutMacsecDataPkts() {
  return "out_macsec_data_pkts";
}

inline folly::StringPiece constexpr kOutMacsecEncryptedBytes() {
  return "out_macsec_encrypted_bytes";
}
inline folly::StringPiece constexpr kOutMacsecUntaggedPkts() {
  return "out_macsec_untagged_pkts";
}

inline folly::StringPiece constexpr kOutMacsecTooLongDroppedPkts() {
  return "out_macsec_too_long_dropped_pkts";
}

inline folly::StringPiece constexpr kOutMacsecCurrentXpn() {
  return "out_macsec_sa_current_xpn";
}

inline folly::StringPiece constexpr kInCongestionDiscards() {
  return "in_congestion_discards";
}

inline folly::StringPiece constexpr kInCongestionDiscardSeen() {
  return "in_congestion_discard_seen";
}

inline folly::StringPiece constexpr kOutForwardingDiscards() {
  return "out_forwarding_discards";
}

inline folly::StringPiece constexpr kPqpErrorEgressDroppedPackets() {
  return "pqp_error_egress_dropped_packets";
}

inline folly::StringPiece constexpr kFabricLinkDownDroppedCells() {
  return "fabric_link_down_dropped_cells";
}

inline folly::StringPiece constexpr kCableLengthMeters() {
  return "cable_length_meters";
}

inline folly::StringPiece constexpr kCableDelayNsec() {
  return "cable_delay_nsec";
}

inline folly::StringPiece constexpr kDataCellsFilterOn() {
  return "data_cells_filter_on";
}

inline folly::StringPiece constexpr kLinkLayerFlowControlWatermark() {
  return "link_layer_flow_control_watermark";
}

inline folly::StringPiece constexpr kPfcDeadlockDetection() {
  return "pfc_deadlock_detection";
}

inline folly::StringPiece constexpr kPfcDeadlockRecovery() {
  return "pfc_deadlock_recovery";
}

inline folly::StringPiece constexpr kMacTransmitQueueStuck() {
  return "mac_transmit_queue_stuck";
}

inline folly::StringPiece constexpr kFabricControlRxPackets() {
  return "fabric_control_rx_packets";
}

inline folly::StringPiece constexpr kFabricControlTxPackets() {
  return "fabric_control_tx_packets";
}

inline folly::StringPiece constexpr kRxPfcDurationUsec() {
  return "rx_pfc_duration_usec";
}

inline folly::StringPiece constexpr kTxPfcDurationUsec() {
  return "tx_pfc_duration_usec";
}

inline folly::StringPiece constexpr kOutDiscardsSll() {
  return "out_discards_switch_lifetime_limit";
}

inline folly::StringPiece constexpr kOutDiscardsHll() {
  return "out_discards_headroom_lifetime_limit";
}

// UEC Link Layer Retry counters (UE Spec 1.0.2 section 5.1). Exported only on
// LLR-capable ASICs (Tomahawk Ultra). The remaining LLR counters in HwPortStats
// (the control-ordered-set handshake counters and expected_seq_good) are kept
// in the FSDB stats tree but intentionally not exported to fb303/ODS.
inline folly::StringPiece constexpr kLlrTxOk() {
  return "llr_tx_ok";
}

inline folly::StringPiece constexpr kLlrRxOk() {
  return "llr_rx_ok";
}

inline folly::StringPiece constexpr kLlrTxReplay() {
  return "llr_tx_replay";
}

inline folly::StringPiece constexpr kLlrRxReplay() {
  return "llr_rx_replay";
}

inline folly::StringPiece constexpr kLlrRxMissingSeq() {
  return "llr_rx_missing_seq";
}

inline folly::StringPiece constexpr kLlrRxDuplicateSeq() {
  return "llr_rx_duplicate_seq";
}

inline folly::StringPiece constexpr kLlrRxAckNackSeqError() {
  return "llr_rx_ack_nack_seq_error";
}

inline folly::StringPiece constexpr kLlrRxExpectedSeqPoisoned() {
  return "llr_rx_expected_seq_poisoned";
}

inline folly::StringPiece constexpr kLlrRxExpectedSeqBad() {
  return "llr_rx_expected_seq_bad";
}

// Current LLR state machine status, exported as an fb303 counter (a gauge)
// rather than a timeseries. The value is the LlrTxStatus/LlrRxStatus thrift
// enum value: TX 0=OFF 1=INIT 2=ADVANCE 3=REPLAY 4=FLUSH, RX 0=OFF
// 1=SEND_ACKS 2=SEND_NACK 3=NACK_SENT. A healthy LLR link with no loss sits at
// TX ADVANCE / RX SEND_ACKS; a link whose partner never completes the INIT
// handshake sits at TX INIT.
inline folly::StringPiece constexpr kLlrTxStatus() {
  return "llr_tx_status";
}

inline folly::StringPiece constexpr kLlrRxStatus() {
  return "llr_rx_status";
}

// UEC Credit-Based Flow Control counters (UE Spec 1.0.2 section 5.2). Exported
// only on CBFC-capable ASICs (Tomahawk Ultra). CF_Update carries the per-VC
// credit state, CC_Update the periodic credit refresh, so a CF rate of zero on
// a CBFC-configured port means the sender is not advertising credit.
inline folly::StringPiece constexpr kCbfcCcUpdateTx() {
  return "cbfc_cc_update_tx";
}

inline folly::StringPiece constexpr kCbfcCfUpdateTx() {
  return "cbfc_cf_update_tx";
}

inline folly::StringPiece constexpr kCbfcCfUpdateRx() {
  return "cbfc_cf_update_rx";
}

// Broadcom LLR stat extensions.
//
// A non-zero ineligible rate on a port with an LLR profile bound is the signal
// that LLR is not protecting traffic -- it is what a port whose TX state
// machine never left OFF looks like from ODS.
inline folly::StringPiece constexpr kLlrTxIneligiblePkts() {
  return "llr_tx_ineligible_pkts";
}

inline folly::StringPiece constexpr kLlrRxIneligiblePkts() {
  return "llr_rx_ineligible_pkts";
}

// The eligible counters are the protected-frame count. They were originally
// left FSDB-only on the reasoning that eligible plus ineligible is just the
// port packet count already carried by out_unicast_pkts / in_unicast_pkts, so
// the protected fraction was derivable from the ineligible counter alone. That
// no longer holds: on Tomahawk Ultra in_unicast_pkts, llr_tx_ok and llr_rx_ok
// all under-report by a factor of 25/6, while the eligible counters agree with
// out_unicast_pkts and with the byte counters. Until Broadcom explains the
// discrepancy these are the only correct protected-frame count, so they are
// exported rather than derived.
inline folly::StringPiece constexpr kLlrTxEligiblePkts() {
  return "llr_tx_eligible_pkts";
}

inline folly::StringPiece constexpr kLlrRxEligiblePkts() {
  return "llr_rx_eligible_pkts";
}

// Replay episodes, as opposed to the frames replayed in them that
// llr_tx_replay already counts. NACK-triggered means the partner detected a
// bad frame; timer-triggered means an ACK never arrived, which is the
// precursor to a data-age flush and a distinct failure to alert on.
inline folly::StringPiece constexpr kLlrTxNackReplayEvent() {
  return "llr_tx_nack_replay_event";
}

inline folly::StringPiece constexpr kLlrTxTimerReplayEvent() {
  return "llr_tx_timer_replay_event";
}

inline folly::StringPiece constexpr kLlrTxError() {
  return "llr_tx_error";
}

// HW protection (FRR) switchover counters. Populated only on Broadcom
// Tomahawk 5/6 with XGS SDK 13.3+.
inline folly::StringPiece constexpr kHwProtectionSwitchoverEvents() {
  return "hw_protection_switchover_events";
}

inline folly::StringPiece constexpr kHwProtectionSwitchoverDropPkts() {
  return "hw_protection_switchover_drop_pkts";
}
} // namespace facebook::fboss
