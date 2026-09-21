package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct InterfaceTrafficModel {
  1: list<TrafficErrorCounters> error_counters;
  2: list<TrafficCounters> traffic_counters;
}

struct TrafficErrorCounters {
  1: string interfaceName;
  2: string peerIf;
  3: string ifStatus;
  4: i64 fcsErrors;
  5: i64 alignErrors;
  6: i64 symbolErrors;
  7: i64 rxErrors;
  8: i64 runtErrors;
  9: i64 giantErrors;
  10: i64 txErrors;
}

struct TrafficCounters {
  1: string interfaceName;
  2: string peerIf;
  3: double inMbps;
  4: double inPct;
  5: double inKpps;
  6: double outMbps;
  7: double outPct;
  8: double outKpps;
  9: i64 portSpeed;
  // False when no rate counter was published for this interface, i.e. the
  // rates above are "not collected" rather than "idle".
  10: bool countersAvailable;
}
