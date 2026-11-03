package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

include "fboss/agent/if/common.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/nsf_policy.thrift"

struct ShowRouteModel {
  1: list<RouteEntry> routeEntries;
}

struct ShowRouteDetailsModel {
  1: list<RouteDetailEntry> routeEntries;
  2: optional nsf_policy.NsfTeWeightEncoding nsfTeWeightEncoding;
}

struct ShowRouteSummaryModel {
  1: i32 numV4Routes;
  2: i32 numV6Small;
  3: i32 numV6Big;
  4: i32 numV6;
  5: i32 hwEntriesUsed;
}

struct RouteCounterEntry {
  1: string counterID;
  2: optional i64 bytes;
  3: optional i64 packets;
}

struct ShowRouteCountersModel {
  1: list<RouteCounterEntry> routeCounters;
}

struct MplsActionInfo {
  1: string action;
  2: optional i32 swapLabel;
  3: optional list<i32> pushLabels;
}

struct NextHopInfo {
  1: string addr;
  2: i32 weight = 0;
  3: optional MplsActionInfo mplsAction;
  4: optional string ifName;
  5: optional i32 interfaceID;
  6: optional common.NetworkTopologyInformation topologyInfo;
  7: optional list<string> srv6SegmentList;
  8: optional i32 cost;
  9: bool isBackup = false;
  10: optional i32 preNormalizationWeight;
}

struct ClientAndNextHops {
  1: i32 clientId;
  2: list<NextHopInfo> nextHops;
  3: optional string namedNextHopGroup;
  4: string adminDistance;
  5: bool isPreferred;
  6: string counterID;
  7: string classID;
  8: optional i64 clientNextHopSetID;
}

struct RouteEntry {
  1: string networkAddress;
  2: list<NextHopInfo> nextHops;
  3: string overridenEcmpMode;
  4: optional list<NextHopInfo> overridenNextHops;
  5: string addressFamily;
}

struct RouteDetailEntry {
  1: string ip;
  2: i32 prefixLength;
  3: string action;
  4: list<ClientAndNextHops> nextHopMulti;
  5: bool isConnected;
  6: string adminDistance;
  7: list<NextHopInfo> nextHops;
  8: string counterID;
  9: string classID;
  10: string overridenEcmpMode;
  11: optional list<NextHopInfo> overridenNextHops;
  12: i32 nhopsLostDueToOverride = 0;
  13: map<string, common.NetworkTopologyInformation> nhAddressToTopologyInfo;
  14: optional i64 resolvedNextHopSetID;
  15: optional i64 normalizedResolvedNextHopSetID;
  16: optional string namedNextHopGroup;
}
