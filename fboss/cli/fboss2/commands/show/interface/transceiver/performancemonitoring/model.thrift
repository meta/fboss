package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

// min/max/avg/cur summary of a VDM parameter over the last PM interval.
struct VdmPerfMonitorParamVal {
  1: double min;
  2: double max;
  3: double avg;
  4: double cur;
}

// A LinkPm parameter paired with its display name, kept as a list so the
// C-CMIS Page 35h ordering is preserved in the output.
struct VdmNamedPerfMonitorParam {
  1: string name;
  2: VdmPerfMonitorParamVal value;
}

struct VdmLaneStats {
  1: i32 lane;
  2: optional double snr;
  3: optional double pam4Level0SD;
  4: optional double pam4Level1SD;
  5: optional double pam4Level2SD;
  6: optional double pam4Level3SD;
  7: optional double pam4MPI;
  8: optional double pam4LTP;
  9: optional bool pam4MPIAlarmHigh;
  10: optional bool pam4MPIAlarmLow;
  11: optional bool pam4MPIWarnHigh;
  12: optional bool pam4MPIWarnLow;
}

// Lane FEC performance monitoring, C-CMIS Page 34h.
struct VdmCoherentFecPm {
  1: optional i64 rxBitsPm;
  2: optional i64 rxBitsSubIntPm;
  3: optional i64 rxCorrBitsPm;
  4: optional i64 rxMinCorrBitsSubIntPm;
  5: optional i64 rxMaxCorrBitsSubIntPm;
  6: optional i32 rxFramesPm;
  7: optional i32 rxFramesSubIntPm;
  8: optional i32 rxFramesUncorrErrPm;
  9: optional i32 rxMinFramesUncorrErrSubIntPm;
  10: optional i32 rxMaxFramesUncorrErrSubIntPm;
}

// Coherent (DCO / 800G ZR) VDM parameters, C-CMIS Section 7.3.1.
struct VdmCoherentStats {
  1: optional double modulatorBiasXI;
  2: optional double modulatorBiasXQ;
  3: optional double modulatorBiasYI;
  4: optional double modulatorBiasYQ;
  5: optional double modulatorBiasXPhase;
  6: optional double modulatorBiasYPhase;
  7: optional double cdLowGranularity;
  8: optional double sopmdLowGranularity;
  9: optional VdmCoherentFecPm fecPm;
  10: list<VdmNamedPerfMonitorParam> linkPm;
}

struct VdmPortSideStats {
  // "Media" or "Host"
  1: string side;
  2: VdmPerfMonitorParamVal datapathBER;
  3: VdmPerfMonitorParamVal datapathErroredFrames;
  4: optional i16 fecTailCurr;
  5: optional i16 fecTailMax;
  6: optional i16 maxSupportedFecTail;
  7: list<VdmLaneStats> laneStats;
  8: optional VdmCoherentStats coherentStats;
}

struct VdmPortStats {
  1: string portName;
  2: i64 statsCollectionTime;
  3: i64 intervalStartTime;
  4: list<VdmPortSideStats> sideStats;
}

struct ShowInterfaceTransceiverPerformanceMonitoringModel {
  1: list<VdmPortStats> portStats;
}
