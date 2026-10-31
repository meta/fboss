package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowPortModel {
  1: list<PortEntry> portEntries;
}

struct PortEntry {
  1: i32 id;
  2: string name;
  3: string adminState;
  4: string linkState;
  5: string speed;
  6: string profileId;
  7: i32 tcvrID;
  8: string tcvrPresent;
  9: optional i32 hwLogicalPortId;
  10: i32 numUnicastQueues;
  11: optional string pause;
  12: optional string pfc;
  13: PortHwStatsEntry hwPortStats;
  14: map<i16, string> queueIdToName;
  15: string isDrained;
  16: string activeState;
  17: string activeErrors;
  18: string coreId;
  19: string virtualDeviceId;
  20: string peerSwitchDrained;
  21: string peerPortDrainedOrDown;
  22: bool activeStateMismatch;
  23: string cableLengthMeters;
  24: optional string llrTxStatus;
  25: optional string llrRxStatus;
  // Class ID assigned to packets ingressing on this port, rendered for display.
  // "--" when the port has no class ID configured.
  26: string userMetaData;
  // Ingress ACL table bound directly to this port, or "--" when none is bound.
  27: string ingressAclTable;
  // CBFC profile, VCs and sender values. Unset when the port has no CBFC.
  28: optional string cbfc;
}

struct PortHwStatsEntry {
  1: i64 inUnicastPkts;
  2: i64 inDiscardPkts;
  3: i64 inErrorPkts;
  4: i64 outDiscardPkts;
  5: i64 outCongestionDiscardPkts;
  6: map<i16, i64> queueOutDiscardBytes;
  7: map<i16, i64> queueOutBytes;
  8: optional map<i16, i64> outPfcPriorityPackets;
  9: optional map<i16, i64> inPfcPriorityPackets;
  10: optional i64 outPfcPackets;
  11: optional i64 inPfcPackets;
  12: optional i64 outPausePackets;
  13: optional i64 inPausePackets;
  14: i64 ingressBytes;
  15: i64 egressBytes;
  16: i64 inCongestionDiscards;
  // CBFC credit messages. Set only on ports with CBFC.
  17: optional i64 cbfcCfUpdateTx;
  18: optional i64 cbfcCfUpdateRx;
  19: optional i64 cbfcCcUpdateTx;
  20: map<i16, i64> queueOutPackets;
  21: map<i16, i64> queueOutDiscardPackets;
  22: i64 ingressPkts;
  23: i64 egressPkts;
}
