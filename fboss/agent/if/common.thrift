namespace cpp2 facebook.fboss
namespace go neteng.fboss.common
namespace py neteng.fboss.common
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.common

include "fboss/agent/if/mpls.thrift"
include "common/network/if/Address.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_"}
@hack.LegacyOmitPrefixInNameString
@thrift.AllowLegacyMissingUris
package;

@cpp.Type{name = "::folly::fbstring"}
typedef binary fbbinary
@cpp.Type{name = "::folly::fbstring"}
typedef string fbstring

struct ClientInformation {
  1: optional fbstring username;
  2: optional fbstring hostname;
  3: optional fbstring reason;
}

// The field names are kept consistent with configerator/source/neteng/bgp_policy/thrift/nsf_policy.thrift
struct NetworkTopologyInformation {
  // identifier
  1: optional i32 rack_id;
  2: optional i32 plane_id;
  // capacity
  3: optional i32 remote_rack_capacity;
  4: optional i32 spine_capacity;
  5: optional i32 local_rack_capacity;
  6: optional i32 spine_id;
  // BGP populates one of these field combinations:
  // (a) 1, 2, 3, 4
  // (b) 1, 2, 5
  // (c) 1, 3, 6
}

enum NextHopRole {
  PRIMARY = 0,
  BACKUP = 1,
}

struct NextHopThrift {
  1: Address.BinaryAddress address;
  // Default weight of 0 represents an ECMP route.
  // This default is chosen for two reasons:
  // 1) A next hop at weight 0 states no explicit weighting. When route
  //    resolution combines it with explicitly weighted next hops, it counts
  //    as one effective share; a set with no explicit weights remains ECMP.
  // 2) A client which does not set a value will result in
  //    0 being populated even with strange behavior in the client language
  //    which is consistent with C++
  2: i32 weight = 0;
  // MPLS encapsulation information for IP->MPLS and MPLS routes
  3: optional mpls.MplsAction mplsAction;
  4: optional bool disableTTLDecrement;
  5: list<Address.BinaryAddress> srv6SegmentList;
  6: optional TunnelType tunnelType;
  7: optional string tunnelId;
  8: optional string overlayDstMac;

  /**
  * For capturing topology information to assist Agent path pruning decisions
  */
  15: optional i32 adjustedWeight;
  16: optional NetworkTopologyInformation topologyInfo;
  17: optional i64 cost;
  18: NextHopRole role = NextHopRole.PRIMARY;

  // Headend/Binding SID FRR: per Nexthop protection
  //   Empty: if no FRR for this Nexthop
  //   Non-empty: Backup ECMP Nexthops providing FRR for this Nexthop
  19: optional list<NextHopThrift> backupNexthops;
  //
  // Using NexthopRole and backupNexthops
  // ====================================
  //
  // RBB/BBF Adjacency SID FRR:
  //    o addAdjacencyFRR(FrrProtectedObject protectedObject,
  //          list<common.NexthopThrift> backupNexthops)
  //    o every NexthopThrift in backupNexthops passed to addAdjacencyFRR
  //       - role = NextHopRole.BACKUP,
  //       - list<backupNexthops> member is empty.
  //
  // BBF Headend/bindingSID FRR:
  //    o addOrUpdateNamedNextHopGroups
  //       - Passes NextHopGroup
  //       - NextHopGroup contains list<NexthopThrift> nexthops
  //    o every nexthop in that list is PRIMARY
  //       - role = NextHopRole.PRIMARY,
  //       - list<backupNexthops> member is non-empty
  //    o every nexthop in this list<backupNexthops>
  //       - role = NextHopRole.BACKUP,
  //       - list<backupNexthops> member is empty.
}

/*
* named next hop group is regular set of next hops but identified by name.
* address of each next hop is recursively resolved.
* if any next hop has MPLS push action, then recursive resolution may expand label stack..
* if any next hop has MPLS swap action, then recursive resolution may expand label stack.
* if any next hop has MPLS php action, then recursive resolution may not expand label stack.
* if any next hop has MPLS pop action, then all next hops must have MPLS pop action, address of nexthop is ignored.
*/
struct NextHopGroup {
  1: optional string name;
  2: list<NextHopThrift> nexthops;
  3: optional bool isProgrammed;
  // NextHopSetId of the group. Output only; ignored when adding groups.
  4: optional i64 id;
}

/*
 * Forwarding Class
 */
enum ForwardingClass {
  DEFAULT = 0, // internal use only
  CLASS_1 = 1,
  CLASS_2 = 2,
  CLASS_3 = 3,
  CLASS_4 = 4,
  CLASS_5 = 5,
  CLASS_6 = 6,
  CLASS_7 = 7,
}

/*
 * Packet type for transmit and receive, primarily used for fabric ports
 */
enum PacketType {
  DEFAULT = 0,
  FABRIC_LINK_MONITORING = 1,
}

typedef map<byte, ForwardingClass> DscpToForwardingClassMap
typedef map<ForwardingClass, NextHopGroup> ForwardingClassToNamedNhg

/*
 * Class based traffic forwarding policy
 */
struct ClassBasedPolicy {
  1: string name;
  2: string defaultNexthopGroup;
  3: ForwardingClassToNamedNhg class2NextHopGroup;
}

/*
 * Traffic redirection policy
 */
union Policy {
  1: ClassBasedPolicy cbfPolicy;
}

union NamedRouteDestination {
  // list of named next hop groups
  1: string nextHopGroup;
  // traffic redirection policy name
  2: string policyName;
}

// SwSwitch run states. SwSwitch moves forward from a
// lower numbered state to the next
enum SwitchRunState {
  UNINITIALIZED = 0,
  INITIALIZED = 1,
  CONFIGURED = 2,
  FIB_SYNCED = 3,
  EXITING = 4,
  ROLLBACK = 5,
}

enum RemoteInterfaceType {
  /*
   * Remote interfaces dynamically created by DSF Control Plane Sync.
   */
  DYNAMIC_ENTRY = 0,

  /*
   * Remote interfaces statically created by DSF Node map processing.
   */
  STATIC_ENTRY = 1,
}

enum RemoteSystemPortType {
  /*
   * Remote System ports dynamically created by DSF Control Plane Sync.
   */
  DYNAMIC_ENTRY = 0,

  /*
   * Remote System ports statically created by DSF Node map processing.
   */
  STATIC_ENTRY = 1,
}

enum LivenessStatus {
  /*
   * Remote System Ports or Remote Interfaces confirmed by DSF Control Plane.
   */
  LIVE = 0,

  /*
   * Remote System Ports or Remote Interfaces not confirmed by DSF Control Plane
   */
  STALE = 1,
}

enum HwWriteBehavior {
  FAIL = 0,
  SKIP = 1,
  WRITE = 2,
  LOG_FAIL = 3,
}

struct BufferPoolFields {
  1: string id;
  2: optional i32 headroomBytes;
  3: i32 sharedBytes;
  4: optional i32 reservedBytes;
}

enum DeltaApplicationMode {
  // Apply the full StateDelta vector
  APPLY_ALL = 0,
  // Rollback StateDelta vector at the end after application
  ROLLBACK = 1,
  // Rollback StateDelta vector at a specific index
  ROLLBACK_AT_INDEX = 2,
}

struct StateDeltaApplication {
  1: DeltaApplicationMode mode = DeltaApplicationMode.APPLY_ALL;
  2: optional i32 rollbackIndex;
}

enum TunnelType {
  IP_IN_IP_DECAP = 0,
  SRV6_ENCAP = 1,
  IP_IN_IP_ENCAP = 2,
  SRV6_DECAP = 3,
  VXLAN = 4,
}

enum MySidType {
  ADJACENCY_MICRO_SID = 0,
  NODE_MICRO_SID = 1,
  DECAPSULATE_AND_LOOKUP = 2,
  BINDING_MICRO_SID = 3,
}
