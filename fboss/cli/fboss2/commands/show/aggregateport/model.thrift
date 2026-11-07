package "facebook.com/fboss/cli"

namespace cpp2 facebook.fboss.cli

struct ShowAggregatePortModel {
  1: list<AggregatePortEntry> aggregatePortEntries;
}

struct AggregatePortEntry {
  1: string name;
  2: string description;
  3: i32 activeMembers;
  4: i32 configuredMembers;
  5: i32 minMembers;
  6: list<AggregateMemberPortEntry> members;
  7: optional i32 minMembersToUp;
  8: list<AggregatePortRifEntry> rifs;
  // False when the agent runs without LACP (static LAG); the per-member
  // activity is then configured-only and actor/partner are unset.
  9: bool lacpEnabled;
  10: i32 systemPriority;
  11: string systemID;
}

struct AggregatePortRifEntry {
  1: string osIfName;
  2: i32 rifID;
  3: list<string> addresses;
}

struct AggregateMemberPortEntry {
  1: string name;
  2: i32 id;
  3: bool isUp;
  4: string lacpRate;
  5: bool isLinkUp;
  6: string lacpActivity;
  7: optional LacpEndpointEntry actor;
  8: optional LacpEndpointEntry partner;
}

struct LacpEndpointEntry {
  1: i32 systemPriority;
  2: string systemID;
  3: i32 key;
  4: i32 portPriority;
  5: i32 port;
  6: string activity;
  7: string timeout;
  8: bool aggregatable;
  9: bool inSync;
  10: bool collecting;
  11: bool distributing;
  12: bool defaulted;
  13: bool expired;
}
