package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowAclModel {
  1: map<string, list<AclEntry>> aclTableEntries;
}

struct AclEntry {
  1: string name;
  2: i32 priority;
  3: optional i16 proto;
  4: optional i16 srcPort;
  5: optional i16 dstPort;
  6: optional i16 ipFrag;
  7: optional i16 dscp;
  8: optional i16 ipType;
  9: optional i16 icmpType;
  10: optional i16 icmpCode;
  11: optional i16 ttl;
  12: optional i16 l4SrcPort;
  13: optional i16 l4DstPort;
  14: optional string dstMac;
  15: optional i16 lookupClassL2;
  16: string actionType;
  17: bool enabled;
  18: optional string srcIp;
  19: optional i32 srcIpPrefixLength;
  20: optional string dstIp;
  21: optional i32 dstIpPrefixLength;
  // Ingress port class ID this entry matches on, rendered for display.
  // Unset when the entry does not qualify on the ingress port class ID.
  22: optional string lookupClassPort;
  23: optional string etherType;
  24: optional i64 dstIpV6Word3;
  25: optional i64 dstIpV6Word2;
  26: optional i16 trafficClass;
  27: optional i64 nextHopGroupId;
  28: optional i64 redirectNextHopGroupId;
  29: optional string counterName;
  30: optional i64 packets;
  31: optional i64 bytes;
}
