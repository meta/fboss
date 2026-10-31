namespace cpp2 facebook.fboss
namespace go neteng.fboss.ctrl
namespace py neteng.fboss.ctrl
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.ctrl

include "fboss/agent/if/fboss.thrift"
include "common/fb303/if/fb303.thrift"
include "common/network/if/Address.thrift"
include "fboss/agent/agent_stats.thrift"
include "fboss/agent/if/mpls.thrift"
include "configerator/structs/neteng/fboss/thrift/common.thrift" as fboss_common
include "fboss/agent/if/common.thrift"
include "fboss/agent/if/product_info.thrift"
include "fboss/qsfp_service/if/transceiver.thrift"
include "fboss/agent/switch_config.thrift"
include "fboss/agent/platform_config.thrift"
include "fboss/lib/phy/phy.thrift"
include "fboss/lib/phy/prbs.thrift"
include "fboss/agent/hw/hardware_stats.thrift"
include "thrift/annotation/python.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_"}
@hack.LegacyAlwaysIncludeNamePrefixInProcessor
@hack.LegacyOmitPrefixInNameString
@hack.ConstantsClass{name = "fboss_CONSTANTS"}
@thrift.AllowLegacyMissingUris
package;

typedef common.fbbinary fbbinary
typedef common.fbstring fbstring
typedef common.ClientInformation ClientInformation

const i32 DEFAULT_CTRL_PORT = 5909;
const i32 NO_VLAN = -1;

// Using the defaults from here:
// https://en.wikipedia.org/wiki/Administrative_distance
enum AdminDistance {
  DIRECTLY_CONNECTED = 0,
  STATIC_ROUTE = 1,
  TE_AGENT = 2,
  OPENR = 10,
  EBGP = 20,
  IBGP = 200,
  MAX_ADMIN_DISTANCE = 255,
}

typedef common.SwitchRunState SwitchRunState

enum SSLType {
  DISABLED = 0,
  PERMITTED = 1,
  REQUIRED = 2,
}

enum PortLedExternalState {
  NONE = 0,
  CABLING_ERROR = 1,
  EXTERNAL_FORCE_ON = 2,
  EXTERNAL_FORCE_OFF = 3,
  CABLING_ERROR_LOOP_DETECTED = 4,
}

enum PortError {
  ERROR_DISABLE_LOOP_DETECTED = 1,
  LANE_SWAP_DETECTED = 2,
  MISMATCHED_NEIGHBOR = 3,
  MISSING_EXPECTED_NEIGHBOR = 4,
  LINK_DISABLED_BY_FIRMWARE = 5,
}

struct IpPrefix {
  1: Address.BinaryAddress ip;
  2: i16 prefixLength;
}

enum RouteForwardAction {
  DROP = 0,
  TO_CPU = 1,
  NEXTHOPS = 2,
}

enum L3TrafficLoopState {
  // Setup is in progress.
  CONFIGURING = 0,
  // The loop is configured and active.
  READY = 1,
  // Teardown is in progress.
  CLEARING = 2,
  // No loop is configured on this port.
  NOT_CONFIGURED = 3,
  // Cleanup failed; partial configuration may remain.
  CLEANUP_FAILED = 4,
  // This switch or port does not support L3 traffic loops.
  UNSUPPORTED = 5,
}

struct L3TrafficLoopStatus {
  1: L3TrafficLoopState state;
  // Unix time in milliseconds.
  2: optional i64 expirationTimeMs;
}

typedef string RouteCounterID

struct UnicastRoute {
  1: IpPrefix dest;
  // NOTE: nextHopAddrs was once required. While we work on
  // fully deprecating it, we need to be extra careful and
  // ensure we don't crash clients/servers that still see it as required.
  2: list<Address.BinaryAddress> nextHopAddrs;
  3: optional AdminDistance adminDistance;
  4: list<common.NextHopThrift> nextHops;
  // Optional forwarding action. If not specified (for backward compatibility)
  // infer from nexthops
  5: optional RouteForwardAction action;
  // use this instead of next hops for using policy based routing or named next hop group
  6: common.NamedRouteDestination namedRouteDestination;
  7: optional RouteCounterID counterID;
  8: optional switch_config.AclLookupClass classID;
  9: optional switch_config.SwitchingMode overrideEcmpSwitchingMode;
  10: optional list<common.NextHopThrift> overrideNextHops;
}

struct MplsRoute {
  1: mpls.MplsLabel topLabel;
  3: optional AdminDistance adminDistance;
  4: list<common.NextHopThrift> nextHops;
  // use this instead of next hops for using policy based routing or named next hop group
  6: common.NamedRouteDestination namedRouteDestination;
}

struct MySidEntry {
  1: common.MySidType type;
  # MySid entry in ip/mask format. 32 bits of this are
  # locator block len and 32-maskLen are sid bits
  2: Address.IPPrefix mySid;
  # Only one of nextHops or namedNextHops must be set
  3: list<common.NextHopThrift> nextHops;
  4: optional common.NamedRouteDestination namedNextHops;
  # Resolved next hops are next hops post resolution.
  # Must not be set in addMySidEntries. These are populated
  # when reporting nexthops back to the client
  5: list<common.NextHopThrift> resolvedNextHops;
  # Backup next hops for SRv6 midpoint FRR, as requested via
  # addAdjacencyFrr. Symmetric to nextHops (field 3).
  # Must not be set in addMySidEntries. These are populated
  # when reporting nexthops back to the client
  6: list<common.NextHopThrift> backupUnresolvedNextHops;
  # Backup next hops post resolution. Symmetric to
  # resolvedNextHops (field 5). Must not be set in
  # addMySidEntries. These are populated when reporting
  # nexthops back to the client
  7: list<common.NextHopThrift> backupResolvedNextHops;
}

struct ClientAndNextHops {
  1: i32 clientId;
  // Deprecated in favor of '3: nextHops'
  2: list<Address.BinaryAddress> nextHopAddrs;
  3: list<common.NextHopThrift> nextHops;
  // will be populated if policy based route or named next hop group is used
  4: optional common.NamedRouteDestination namedRouteDestination;
  5: optional AdminDistance adminDistance;
  6: optional bool isPreferred;
  7: optional RouteCounterID counterID;
  8: optional switch_config.AclLookupClass classID;
  // ID assigned by NextHopIDManager to this per-client nexthop set.
  9: optional i64 clientNextHopSetID;
}

struct IfAndIP {
  1: i32 interfaceID;
  2: Address.BinaryAddress ip;
}

struct RouteDetails {
  1: IpPrefix dest;
  2: string action;
  // Deprecated in favor of '7: nextHops'
  3: list<IfAndIP> fwdInfo;
  4: list<ClientAndNextHops> nextHopMulti;
  5: bool isConnected;
  6: optional AdminDistance adminDistance;
  7: list<common.NextHopThrift> nextHops;
  // use this for policy based route or with named next hop groups
  8: optional common.NamedRouteDestination namedRouteDestination;
  9: optional RouteCounterID counterID;
  10: optional switch_config.AclLookupClass classID;
  11: optional switch_config.SwitchingMode overridenEcmpMode;
  12: optional list<common.NextHopThrift> overridenNextHops;
  13: optional i64 resolvedNextHopSetID;
  14: optional i64 normalizedResolvedNextHopSetID;
  // Resolved next hops before hardware normalization.
  15: optional list<common.NextHopThrift> resolvedNextHops;
}

struct MplsRouteDetails {
  1: mpls.MplsLabel topLabel;
  2: string action;
  3: list<ClientAndNextHops> nextHopMulti;
  4: AdminDistance adminDistance;
  5: list<common.NextHopThrift> nextHops;
  // use this for policy based route or with named next hop groups
  6: optional common.NamedRouteDestination namedRouteDestination;
}

struct ArpEntryThrift {
  1: string mac;
  2: i32 port;
  3: string vlanName;
  4: Address.BinaryAddress ip;
  // VlanId populated only for interfaces of type VLAN
  5: i32 vlanID = NO_VLAN;
  6: string state;
  7: i32 ttl;
  8: i32 classID;
  9: bool isLocal = true;
  10: optional i64 switchId;
  11: optional i64 resolvedSince;
  12: i32 interfaceID;
  13: switch_config.PortDescriptor portDescriptor;
  14: optional i32 probesLeft;
  15: optional i32 maxNeighborProbes;
}

enum L2EntryType {
  L2_ENTRY_TYPE_PENDING = 0,
  L2_ENTRY_TYPE_VALIDATED = 1,
}

enum L2EntryUpdateType {
  L2_ENTRY_UPDATE_TYPE_DELETE = 0,
  L2_ENTRY_UPDATE_TYPE_ADD = 1,
}

struct L2EntryThrift {
  1: string mac;
  2: i32 port;
  3: i32 vlanID;
  // Add information about l2 entries associated with
  // trunk ports. Only one of port, trunk is valid. If
  // trunk is set we look at that.
  4: optional i32 trunk;
  5: L2EntryType l2EntryType;
  6: optional i32 classID;
}

enum LacpPortRateThrift {
  SLOW = 0,
  FAST = 1,
}

enum LacpPortActivityThrift {
  PASSIVE = 0,
  ACTIVE = 1,
}

struct AggregatePortMemberThrift {
  1: i32 memberPortID;
  2: bool isForwarding;
  3: i32 priority;
  4: LacpPortRateThrift rate;
  5: LacpPortActivityThrift activity;
}

struct AggregatePortThrift {
  1: i32 key;
  2: list<AggregatePortMemberThrift> memberPorts;
  3: string name;
  4: string description;
  5: i32 systemPriority;
  6: string systemID;
  7: byte minimumLinkCount;
  8: bool isUp;
  9: optional byte minimumLinkCountToUp;
}

struct LacpStateThrift {
  1: bool active;
  2: bool shortTimeout;
  3: bool aggregatable;
  4: bool inSync;
  5: bool collecting;
  6: bool distributing;
  7: bool defaulted;
  8: bool expired;
}

struct LacpEndpoint {
  1: i32 systemPriority;
  2: string systemID;
  3: i32 key;
  4: i32 portPriority;
  5: i32 port;
  6: LacpStateThrift state;
}

struct LacpPartnerPair {
  1: LacpEndpoint localEndpoint;
  2: LacpEndpoint remoteEndpoint;
}

struct InterfaceDetail {
  1: string interfaceName;
  2: i32 interfaceId;
  // VlanId populated only for interfaces of type VLAN
  3: i32 vlanId = NO_VLAN;
  4: i32 routerId;
  5: string mac;
  6: list<IpPrefix> address;
  7: i32 mtu;
  8: optional common.RemoteInterfaceType remoteIntfType;
  9: optional common.LivenessStatus remoteIntfLivenessStatus;
  10: switch_config.Scope scope = switch_config.Scope.LOCAL;
  // PortId populated only for interfaces of type PORT
  11: i32 portId;
  12: optional string desiredPeerAddressIPv6;
  13: switch_config.InterfaceType interfaceType;
  // used in CLI display
  14: list<string> portNames;
  // AggregatePortId populated only for interfaces of type PORT that are bound
  // to an aggregate port rather than a physical port
  15: optional i32 aggregatePortId;
}

struct VrfInfo {
  1: i32 vrfId;
  2: optional string name;
  3: list<i32> interfaceIds;
}

/*
 * Values in these counters are cumulative since the last time the agent
 * started.
 */
struct PortErrors {
  1: i64 errors;
  2: i64 discards;
}

struct QueueStats {
  1: i64 congestionDiscards;
  2: i64 outBytes;
}

/*
 * Fabric link monitoring statistics for tracking packet transmission
 * and reception on fabric ports
 */
struct FabricLinkMonPortStats {
  1: i64 txCount;
  2: i64 rxCount;
  3: i64 droppedCount;
  4: i64 invalidPayloadCount;
  5: i64 noPendingSeqNumCount;
  6: i64 sequenceNumber;
}

/*
 * Per-port statistics for CPU latency monitoring.
 * Tracks round-trip latency of ICMPv6 probe packets sent out ethernet
 * ports and received back via the IP2ME (CPU_IS_NHOP) trap mechanism.
 */
struct CpuLatencyPortStats {
  1: double latencyUs;
}

/*
 * Fabric monitoring detail for a single fabric port
 */
struct FabricMonitoringDetail {
  1: string portName;
  2: i32 portId;
  3: string neighborSwitch;
  4: string neighborPortName;
  5: i32 virtualDevice;
  6: i32 linkSwitchId;
  7: string linkSystemPort;
}

/*
 * Values in these counters are cumulative since the last time the agent
 * started.
 */
struct PortCounters {
  // avoid typechecker error here as bytes is a py3 reserved keyword
  @python.Name{name = "bytes_"}
  1: i64 bytes;
  2: i64 ucastPkts;
  3: i64 multicastPkts;
  4: i64 broadcastPkts;
  5: PortErrors errors;
  6: list<QueueStats> unicast = [];
}

enum PortAdminState {
  DISABLED = 0,
  ENABLED = 1,
}

enum PortOperState {
  DOWN = 0,
  UP = 1,
}

enum PortActiveState {
  INACTIVE = 0,
  ACTIVE = 1,
}

enum PortLoopbackMode {
  NONE = 0,
  MAC = 1,
  PHY = 2,
  NIF = 3,
}

struct LinearQueueCongestionDetection {
  1: i32 minimumLength;
  2: i32 maximumLength;
  3: i32 probability;
}

struct QueueCongestionDetection {
  1: optional LinearQueueCongestionDetection linear;
}

enum QueueCongestionBehavior {
  EARLY_DROP = 0,
  ECN = 1,
}

struct ActiveQueueManagement {
  1: QueueCongestionDetection detection;
  2: QueueCongestionBehavior behavior;
}

struct Range {
  1: i32 minimum;
  2: i32 maximum;
}

union PortQueueRate {
  1: Range pktsPerSec;
  2: Range kbitsPerSec;
}

struct PortQueueThrift {
  1: i32 id;
  2: string name = "";
  3: string mode;
  4: optional i32 weight;
  5: optional i32 reservedBytes;
  6: optional string scalingFactor;
  7: optional list<ActiveQueueManagement> aqms;
  8: optional PortQueueRate portQueueRate;
  9: optional i32 bandwidthBurstMinKbits;
  10: optional i32 bandwidthBurstMaxKbits;
  11: optional list<byte> dscps;
  12: optional i32 maxDynamicSharedBytes;
}

struct PfcConfig {
  1: bool tx;
  2: bool rx;
  3: bool watchdog;
}

// CBFC profile bound to a port. The sender values are the configured ones;
// on Tomahawk Ultra 1 the hardware credit limit differs (CS00012481990).
struct CbfcInfo {
  1: string configName;
  2: list<i16> virtualChannels;
  3: optional i16 senderCreditSize;
  4: optional i16 senderPacketOverhead;
  5: optional i64 senderCreditLimit;
}

struct PortInfoThrift {
  1: i32 portId;
  2: i64 speedMbps;
  3: PortAdminState adminState;
  4: PortOperState operState;
  5: list<i32> vlans;

  10: PortCounters output;
  11: PortCounters input;
  12: string name;
  13: string description;
  // TODO(pgardideh): remove this in favor of fecMode
  14: bool fecEnabled; // Forward Error Correction port setting
  15: bool txPause = false;
  16: bool rxPause = false;
  17: list<PortQueueThrift> portQueues = [];
  18: string fecMode;
  19: string profileID;
  20: optional PfcConfig pfc;
  21: optional PortHardwareDetails hw;
  22: optional TransceiverIdxThrift transceiverIdx;
  23: optional i32 hwLogicalPortId;
  24: bool isDrained;
  25: optional PortActiveState activeState;
  26: list<PortError> activeErrors;
  27: optional string expectedLLDPeerName;
  28: optional string expectedLLDPPeerPort;
  29: optional i32 coreId;
  30: optional i32 virtualDeviceId;
  31: switch_config.PortType portType;
  32: switch_config.Scope scope;
  33: list<switch_config.PortNeighbor> expectedNeighborReachability;
  34: optional i64 cableLengthMeters;
  // Current UEC LLR state machine status. Set only for ports with an LLR
  // profile bound on an LLR-capable ASIC.
  35: optional hardware_stats.LlrTxStatus llrTxStatus;
  36: optional hardware_stats.LlrRxStatus llrRxStatus;

  // Lookup class assigned to packets ingressing on this port. Mirrors
  // switch_config.Port.userMetaData. Unset when the port has no class ID.
  37: optional switch_config.AclLookupClassPort userMetaData;

  // Ingress ACL table bound directly to this port. Mirrors
  // switch_config.Port.ingressAclTableName. Unset when no table is bound.
  38: optional string ingressAclTableName;

  // Unset when the port has no cbfcConfigName.
  39: optional CbfcInfo cbfc;

  40: optional i32 ingressVlan;
}

// Port queueing configuration
struct PortQueueFields {
  1: i16 id = 0;
  2: i32 weight = 1;
  3: optional i32 reserved;
  // TODO: replace with switch_config.MMUScalingFactor?
  4: optional string scalingFactor;
  // TODO: replace with switch_config.QueueScheduling?
  5: string scheduling = "WEIGHTED_ROUND_ROBIN";
  // TODO: replace with switch_config.StreamType?
  6: string streamType = "UNICAST";
  7: optional list<switch_config.ActiveQueueManagement> aqms;
  8: optional string name;
  /*
  * Refer PortQueueRate which is a generalized version and allows configuring
  * pps as well as kbps.
  */
  10: optional i32 packetsPerSec_DEPRECATED;
  11: optional i32 sharedBytes;
  12: optional switch_config.PortQueueRate portQueueRate;

  13: optional i32 bandwidthBurstMinKbits;
  14: optional i32 bandwidthBurstMaxKbits;
  15: optional i16 trafficClass;
  16: optional list<i16> pfcPriorities;
  17: optional i32 maxDynamicSharedBytes;
  18: optional string bufferPoolName;
  19: optional common.BufferPoolFields bufferPoolConfig;
}

@fboss_common.AllowSkipThriftCow
struct SystemPortThrift {
  1: i64 portId;
  2: i64 switchId;
  3: string portName; // switchId::portName
  4: i64 coreIndex;
  5: i64 corePortIndex;
  6: i64 speedMbps;
  7: i64 numVoqs;
  // System ports are always enabled
  9: bool enabled_DEPRECATED = true;
  10: optional string qosPolicy;
  11: list<PortQueueFields> queues;
  /*
   * Set only on Remote System Ports of VOQ switches.
   */
  12: optional common.RemoteSystemPortType remoteSystemPortType;
  /*
   * Set only on Remote System Ports of VOQ switches.
   */
  13: optional common.LivenessStatus remoteSystemPortLivenessStatus;
  14: switch_config.Scope scope = switch_config.Scope.LOCAL;
  15: bool shelDestinationEnabled_DEPRECATED = false;
  16: optional bool shelDestinationEnabled;
  17: switch_config.PortType portType = switch_config.PortType.INTERFACE_PORT;
  // VoQs under this system port do not need credits to send traffic out
  18: optional bool pushQueueEnabled;
}

struct PortHardwareDetails {
  1: switch_config.PortProfileID profile;
  2: phy.PortProfileConfig profileConfig;
  3: phy.PortPinConfig pinConfig;
  4: map<string, phy.DataPlanePhyChip> chips;
}

struct NdpEntryThrift {
  1: Address.BinaryAddress ip;
  2: string mac;
  3: i32 port;
  4: string vlanName;
  // VlanId populated only for interfaces of type VLAN
  5: i32 vlanID = NO_VLAN;
  6: string state;
  7: i32 ttl;
  8: i32 classID;
  9: bool isLocal = true;
  10: optional i64 switchId;
  11: optional i64 resolvedSince;
  12: i32 interfaceID;
  13: switch_config.PortDescriptor portDescriptor;
  14: optional i32 probesLeft;
  15: optional i32 maxNeighborProbes;
}

enum BootType {
  UNINITIALIZED = 0,
  COLD_BOOT = 1,
  WARM_BOOT = 2,
}

struct TransceiverIdxThrift {
  1: i32 transceiverId;
  2: optional i32 channelId; # deprecated
  3: optional list<i32> channels;
}

struct PortStatus {
  1: bool enabled;
  2: bool up;
  3: optional bool present; # deprecated
  4: optional TransceiverIdxThrift transceiverIdx;
  5: i64 speedMbps; // TODO: i32 (someone is optimistic about port speeds)
  6: string profileID;
  7: bool drained;
}

enum CaptureDirection {
  CAPTURE_ONLY_RX = 0,
  CAPTURE_ONLY_TX = 1,
  CAPTURE_TX_RX = 2,
}

enum CpuCosQueueId {
  LOPRI = 0,
  DEFAULT = 1,
  MIDPRI = 2,
  HIPRI = 9,
}

struct RxCaptureFilter {
  1: list<CpuCosQueueId> cosQueues;
  # can put additional Rx filters here if need be
}

struct CaptureFilter {
  1: RxCaptureFilter rxCaptureFilter;
}

struct CaptureInfo {
  // A name identifying the packet capture
  1: string name;
  /*
   * Stop capturing after the specified number of packets.
   *
   * This parameter is required to make sure the capture will stop.
   * In case your filter matches more packets than you expect, we don't want
   * to consume lots of space and other resources by capturing an extremely
   * large number of packets.
   */
  2: i32 maxPackets;
  3: CaptureDirection direction = CAPTURE_TX_RX;
  /*
   * set of criteria that packet must meet to be captured
   */
  4: CaptureFilter filter;
}

struct RouteUpdateLoggingInfo {
  // The prefix to log route updates for
  1: IpPrefix prefix;
  /*
   * A name to split up requests for logging. Allows two different clients
   * to separately request starting/stopping logging for the same prefixes
   * without affecting each other.
   */
  2: string identifier;
  // Should we log an update to a route to a more specific prefix than "prefix"
  3: bool exact;
}

struct MplsRouteUpdateLoggingInfo {
  // The label to log route updates for label, -1 for all labels
  1: mpls.MplsLabel label;
  /*
   * A name to split up requests for logging. Allows two different clients
   * to separately request starting/stopping logging for the same prefixes
   * without affecting each other.
   */
  2: string identifier;
}

/*
 * Information about an LLDP neighbor
 */
struct LinkNeighborThrift {
  1: i32 localPort;
  2: i32 localVlan;
  3: i32 chassisIdType;
  4: i32 portIdType;
  5: i32 originalTTL;
  6: i32 ttlSecondsLeft;
  7: string srcMac;
  8: binary chassisId;
  9: string printableChassisId;
  10: binary portId;
  11: string printablePortId;
  12: optional string systemName;
  13: optional string systemDescription;
  14: optional string portDescription;
  15: optional string localPortName;
}

enum ClientID {
  /*
   * Routes from BGP daemon on the box
   */
  BGPD = 0,

  /*
   * Routes defined in Agent configuration. These are by definition static
   */
  STATIC_ROUTE = 1,

  /*
   * Routes derived from interface addresses. Programmed for punting packets
   * destined to interface address to CPU.
   */
  INTERFACE_ROUTE = 2,

  /*
   * These are special interface routes that are link-local. Aka they are
   * bounded by link scope. Routes here are exclusive to INTERFACE_ROUTE.
   */
  LINKLOCAL_ROUTE = 3,

  /*
   * Interface routes that are derived from remote interface nodes in the DSF cluster.
   * These routes are propagated by DSF subscriptions.
   */
  REMOTE_INTERFACE_ROUTE = 4,

  /*
   * Auto generated routes by Agent. Agent by default programs default (v4 & v6)
   * route pointing to NULL if they're not specified by any other clients. These
   * routes should be least preferred and act as last resort.
   */
  STATIC_INTERNAL = 700,

  /**
   * Routes from Open/R daemon on the box
   */
  OPENR = 786,

  /**
   * Routes and MySid entries programmed via Thrift RPC by the TE agent /
   * controller (e.g. addUnicastRoutes, addMySidEntries).
  */
  TE_AGENT = 800,
}

struct AclEntryThrift {
  1: i32 priority;
  2: string name;
  3: Address.BinaryAddress srcIp;
  4: i32 srcIpPrefixLength;
  5: Address.BinaryAddress dstIp;
  6: i32 dstIpPrefixLength;
  7: optional byte proto;
  8: optional byte tcpFlagsBitMap;
  9: optional i16 srcPort;
  10: optional i16 dstPort;
  11: optional byte ipFrag;
  12: optional byte icmpType;
  13: optional byte icmpCode;
  14: optional byte dscp;
  15: optional byte ipType;
  16: optional i16 ttl;
  17: optional string dstMac;
  18: optional i16 l4SrcPort;
  19: optional i16 l4DstPort;
  20: optional byte lookupClass;
  21: string actionType;
  22: optional byte lookupClassL2;
  23: optional bool enabled;
  24: optional list<string> udfGroups;

  // Ingress port class ID this entry matches on. Mirrors
  // switch_config.AclEntry.lookupClassPort.
  25: optional switch_config.AclLookupClassPort lookupClassPort;
}

struct AclTableThrift {
  1: map<string, list<AclEntryThrift>> aclTableEntries;
}

enum HwObjectType {
  PORT = 0,
  LAG = 1,
  VIRTUAL_ROUTER = 2,
  NEXT_HOP = 3,
  NEXT_HOP_GROUP = 4,
  ROUTER_INTERFACE = 5,
  CPU_TRAP = 6,
  HASH = 7,
  MIRROR = 8,
  QOS_MAP = 9,
  QUEUE = 10,
  SCHEDULER = 11,
  L2_ENTRY = 12,
  NEIGHBOR_ENTRY = 13,
  ROUTE_ENTRY = 14,
  VLAN = 15,
  BRIDGE = 16,
  BUFFER = 17,
  ACL = 18,
  DEBUG_COUNTER = 19,
  TELEMETRY = 20,
  LABEL_ENTRY = 21,
  MACSEC = 22,
  SAI_MANAGED_OBJECTS = 23,
  IPTUNNEL = 24,
  SYSTEM_PORT = 25,
  FIRMWARE = 26,
  SRV6 = 27,
  NEXT_HOP_GROUP_MEMBER = 28,
  SAMPLE_PACKET = 29,
  MY_MAC = 30,
  VXLAN = 31,
}

exception FbossFibUpdateError {
  1: map<i32, list<IpPrefix>> vrf2failedAddUpdatePrefixes;
  2: map<i32, list<IpPrefix>> vrf2failedDeletePrefixes;
  3: list<mpls.MplsLabel> failedAddUpdateMplsLabels;
  4: list<mpls.MplsLabel> failedDeleteMplsLabels;
}

struct ConfigAppliedInfo {
  // Get last time(ms since epoch) of the config is applied since the latest
  // agent service starts.
  1: i64 lastAppliedInMs;
  // Get last time(ms since epoch) of the config is applied during coldboot
  // since the latest agent service starts.
  // If wedge_agent just has a warmboot, this should be null
  2: optional i64 lastColdbootAppliedInMs;
}

struct TeFlow {
  1: optional i32 srcPort;
  2: optional IpPrefix dstPrefix;
}

typedef string TeCounterID

struct FlowEntry {
  1: TeFlow flow;
  // DEPRECATED: nextHops is replaced by the optional nexthops field.
  2: list<common.NextHopThrift> nextHops;
  3: optional TeCounterID counterID;
  4: optional list<common.NextHopThrift> nexthops;
}

safe stateful server exception FbossTeUpdateError {
  1: list<TeFlow> failedAddUpdateFlows;
  2: list<TeFlow> failedDeleteFlows;
}

struct TeFlowDetails {
  1: TeFlow flow;
  3: list<common.NextHopThrift> nexthops;
  4: list<common.NextHopThrift> resolvedNexthops;
  // Enabled tracks state of flow rule. If there are
  // no valid nexthops, agent will disable the rule
  5: bool enabled;
  6: optional ctrl.TeCounterID counterID;
}

struct FabricEndpoint {
  1: i64 switchId;
  2: optional string switchName;
  3: i32 portId;
  4: optional string portName;
  // Is the port attached to anything on the
  // other side. All other fields are relevant
  // only when isAttached == true
  5: bool isAttached = false;
  6: switch_config.SwitchType switchType;
  7: optional i64 expectedSwitchId;
  8: optional i32 expectedPortId;
  9: optional string expectedPortName;
  10: optional string expectedSwitchName;
}

struct FsdbSubscriptionThrift {
  1: string name;
  2: list<string> paths;
  3: string state;
  4: string ip;
  // Unique ID for subscription to name, ip
  5: string subscriptionId;
}

enum DsfSessionState {
  IDLE = 1,
  CONNECT = 2,
  WAIT_FOR_REMOTE = 3,
  ESTABLISHED = 4,
  DISCONNECTED = 5,
  REMOTE_DISCONNECTED = 6,
}

struct DsfSessionThrift {
  1: string remoteName;
  2: DsfSessionState state;
  3: optional i64 lastEstablishedAt;
  4: optional i64 lastDisconnectedAt;
}

struct MultiSwitchRunState {
  1: SwitchRunState swSwitchRunState;
  // SwitchIndex to SwitchRunState
  2: map<i32, SwitchRunState> hwIndexToRunState;
  3: bool multiSwitchEnabled;
}

struct EcmpDetails {
  1: i32 ecmpId;
  2: bool flowletEnabled;
  3: i16 flowletInterval;
  4: i32 flowletTableSize;
}

struct RouteCount {
  1: i64 v4Count;
  2: i64 v6Count;
}

enum FirmwareOpStatus {
  UNKNOWN = 0,
  LOADED = 1,
  NOT_LOADED = 2,
  RUNNING = 3,
  STOPPED = 4,
  ERROR = 5,
}

enum FirmwareFuncStatus {
  UNKNOWN = 0,
  ISOLATED = 1,
  MONITORING = 2,
}

struct FirmwareInfo {
  1: string version;
  2: FirmwareOpStatus opStatus;
  3: FirmwareFuncStatus funcStatus;
}

/*
 * Fast ReRoute (FRR) object to protect.
 * For RBB, the object is identified by uA mySID for the LAG.
 * For BBF, the object is identified by adjacency SID label for the LAG.
 */
union FrrProtectedObject {
  1: Address.IPPrefix mySid;
  2: mpls.MplsLabel mplsLabel;
}

service FbossCtrl extends phy.FbossCommonPhyCtrl {
  /*
   * Retrieve up-to-date counters from the hardware, and publish all
   * thread-local stats to the global state.  This way the next getCounters()
   * call will receive up-to-date stats.
   *
   * Without this call, the stats may be delayed by up to 1 second.  In
   * general, we expect most users to be okay with the 1 second delay.
   * However, some tests use this API to check for up-to-date statistics
   * immediately after an event has occurred.
   */
  void flushCountersNow() throws (1: fboss.FbossBaseError error);

  /*
   * Add/Delete IPv4/IPV6 routes
   * - decide if it is v4 or v6 from destination ip address
   * - using clientID to identify who is adding routes, BGP or static
   */
  void addUnicastRoute(1: i16 clientId, 2: UnicastRoute r) throws (
    1: fboss.FbossBaseError error,
    2: FbossFibUpdateError fibError,
  );
  void deleteUnicastRoute(1: i16 clientId, 2: IpPrefix r) throws (
    1: fboss.FbossBaseError error,
  );
  void addUnicastRoutes(1: i16 clientId, 2: list<UnicastRoute> r) throws (
    1: fboss.FbossBaseError error,
    2: FbossFibUpdateError fibError,
  );
  void deleteUnicastRoutes(1: i16 clientId, 2: list<IpPrefix> r) throws (
    1: fboss.FbossBaseError error,
  );
  void syncFib(1: i16 clientId, 2: list<UnicastRoute> routes) throws (
    1: fboss.FbossBaseError error,
    2: FbossFibUpdateError fibError,
  );

  void addUnicastRouteInVrf(
    1: i16 clientId,
    2: UnicastRoute r,
    3: i32 vrf,
  ) throws (1: fboss.FbossBaseError error, 2: FbossFibUpdateError fibError);
  void deleteUnicastRouteInVrf(
    1: i16 clientId,
    2: IpPrefix r,
    3: i32 vrf,
  ) throws (1: fboss.FbossBaseError error);
  void addUnicastRoutesInVrf(
    1: i16 clientId,
    2: list<UnicastRoute> r,
    3: i32 vrf,
  ) throws (1: fboss.FbossBaseError error, 2: FbossFibUpdateError fibError);
  void deleteUnicastRoutesInVrf(
    1: i16 clientId,
    2: list<IpPrefix> r,
    3: i32 vrf,
  ) throws (1: fboss.FbossBaseError error);
  void syncFibInVrf(
    1: i16 clientId,
    2: list<UnicastRoute> routes,
    3: i32 vrf,
  ) throws (1: fboss.FbossBaseError error, 2: FbossFibUpdateError fibError);

  void addMySidEntries(1: list<MySidEntry> mySidEntries) throws (
    1: fboss.FbossBaseError error,
  );
  void deleteMySidEntries(1: list<IpPrefix> prefixes) throws (
    1: fboss.FbossBaseError error,
  );
  list<MySidEntry> getMySidEntries() throws (1: fboss.FbossBaseError error);

  // Get route counter values
  map<string, i64> getRouteCounterBytes(1: list<string> counters) throws (
    1: fboss.FbossBaseError error,
  );
  map<string, i64> getAllRouteCounterBytes() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Send packets in binary or hex format to controller.
   *
   * This injects the packet into the controller, as if it had been received
   * from a front-panel port.
   */
  void sendPkt(
    1: i32 port,
    2: i32 vlan,
    3: fbbinary data,
    4: i32 numOfPkts = 1,
    5: i32 intervalInMs = 10,
  ) throws (1: fboss.FbossBaseError error);
  void sendPktHex(
    1: i32 port,
    2: i32 vlan,
    3: fbstring hex,
    4: i32 numOfPkts = 1,
    5: i32 intervalInMs = 10,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Transmit a packet out a specific front panel port.
   *
   * The data should contain the full ethernet frame (not including the final
   * frame check sequence).  It must have an explicit VLAN tag indicating what
   * VLAN to send the packet on.  (The VLAN tag may be stripped out of the
   * packet actually sent by the hardware, based on the hardware VLAN tagging
   * configuration for this VLAN+port.)
   */
  void txPkt(
    1: i32 port,
    2: fbbinary data,
    3: i32 numOfPkts = 1,
    4: i32 intervalInMs = 10,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Transmit a packet out to a specific VLAN.
   *
   * This causes the packet to be sent out the front panel ports that belong to
   * this VLAN.
   *
   * The data should contain the full ethernet frame (not including the final
   * frame check sequence).  It must have an explicit VLAN tag indicating what
   * VLAN to send the packet on.  (The VLAN tag may be stripped out of the
   * packet actually sent by the hardware, based on the hardware VLAN tagging
   * configuration for this VLAN.)
   */
  void txPktL2(
    1: fbbinary data,
    2: i32 numOfPkts = 1,
    3: i32 intervalInMs = 10,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Transmit an L3 packet.
   *
   * The payload should consist only of the layer 3 header and payload.
   * The controller will add an appropriate ethernet frame header.  It will
   * contain the correct next hop information based on the layer 3 header.
   */
  void txPktL3(
    1: fbbinary payload,
    2: i32 numOfPkts = 1,
    3: i32 intervalInMs = 10,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Flush the ARP/NDP entry with the specified IP address.
   *
   * If the vlanId is 0, then this command will iterate through all VLANs
   * and flush all entries it finds with this IP.
   *
   * Returns the number of entries flushed.
   */
  i32 flushNeighborEntry(1: Address.BinaryAddress ip, 2: i32 vlanId);

  /*
   * Flush a list of ARP/NDP entries in bulk.
   *
   * Each entry specifies an IP address and a vlanId (interfaceID).
   *
   * Returns the total number of entries flushed.
   */
  i32 flushNeighborEntries(1: list<IfAndIP> entries);

  /*
   * Inband addresses
   */
  list<Address.Address> getVlanAddresses(1: i32 vlanId) throws (
    1: fboss.FbossBaseError error,
  );
  list<Address.Address> getVlanAddressesByName(1: string vlan) throws (
    1: fboss.FbossBaseError error,
  );
  list<Address.BinaryAddress> getVlanBinaryAddresses(1: i32 vlanId) throws (
    1: fboss.FbossBaseError error,
  );
  list<Address.BinaryAddress> getVlanBinaryAddressesByName(
    1: string vlan,
  ) throws (1: fboss.FbossBaseError error);
  /*
   * Returns a list of IP route as per the route table for the
   * given address
   *
   * TODO (allwync): get rid of getIpRoute after agent code with thrift
   * implementation of getIpRouteDetails is pushed everywhere
   */
  UnicastRoute getIpRoute(1: Address.Address addr, 2: i32 vrfId) throws (
    1: fboss.FbossBaseError error,
  );
  RouteDetails getIpRouteDetails(1: Address.Address addr, 2: i32 vrfId) throws (
    1: fboss.FbossBaseError error,
  );
  map<i32, InterfaceDetail> getAllInterfaces() throws (
    1: fboss.FbossBaseError error,
  );
  list<string> getInterfaceList() throws (1: fboss.FbossBaseError error);
  /*
   * TODO (allwync): get rid of getRouteTable after agent code with thrift
   * implementation of getRouteTableDetails is pushed everywhere
   */
  list<UnicastRoute> getRouteTable() throws (1: fboss.FbossBaseError error);
  list<UnicastRoute> getRouteTableByClient(1: i16 clientId) throws (
    1: fboss.FbossBaseError error,
  );
  list<RouteDetails> getRouteTableDetails() throws (
    1: fboss.FbossBaseError error,
  );
  list<RouteDetails> getRouteTableDetailsByClients(
    1: list<i16> clientId,
  ) throws (1: fboss.FbossBaseError error);
  InterfaceDetail getInterfaceDetail(1: i32 interfaceId) throws (
    1: fboss.FbossBaseError error,
  );
  list<VrfInfo> getVrfs() throws (1: fboss.FbossBaseError error);

  /* MPLS route API */
  void addMplsRoutes(1: i16 clientId, 2: list<MplsRoute> routes) throws (
    1: fboss.FbossBaseError error,
    2: FbossFibUpdateError fibError,
  );

  void deleteMplsRoutes(1: i16 clientId, 2: list<i32> topLabels) throws (
    1: fboss.FbossBaseError error,
  );

  /* Flush previous routes and install new routes without disturbing traffic.
    * Similar to syncFib API */
  void syncMplsFib(1: i16 clientId, 2: list<MplsRoute> routes) throws (
    1: fboss.FbossBaseError error,
    2: FbossFibUpdateError fibError,
  );

  /* Retrieve list of MPLS routes per client */
  list<MplsRoute> getMplsRouteTableByClient(1: i16 clientId) throws (
    1: fboss.FbossBaseError error,
  );

  /* Retrieve list of MPLS entries */
  list<MplsRouteDetails> getAllMplsRouteDetails() throws (
    1: fboss.FbossBaseError error,
  );

  /* Retrieve MPLS entry for given label */
  MplsRouteDetails getMplsRouteDetails(1: mpls.MplsLabel topLabel) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Return the admin and oper state of ports in the list (all ports
   * if list is empty).
   */
  map<i32, PortStatus> getPortStatus(1: list<i32> ports) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Administratively enable/disable a port.  Useful to temporarily change
   * the admin state of a port, for example to simulate a link flap.  This
   * does not affect the configuraton, so the port will return to configured
   * state after, for example, restart.
   */
  void setPortState(1: i32 portId, 2: bool enable) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Set drain state for a port
   */
  void setPortDrainState(1: i32 portId, 2: bool drain) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Set loopback mode for a port. Primarily used by tests
   */
  void setPortLoopbackMode(1: i32 portId, 2: PortLoopbackMode mode) throws (
    1: fboss.FbossBaseError error,
  );

  map<i32, PortLoopbackMode> getAllPortLoopbackMode() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Program all internal phy ports for specified transceiver
   */
  map<i32, switch_config.PortProfileID> programInternalPhyPorts(
    1: transceiver.TransceiverInfo transceiver,
    2: bool force,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Change the PRBS setting on a port. Useful when debugging a link
   * down or flapping issue.
   */
  void setPortPrbs(
    1: i32 portId,
    2: phy.PortComponent component,
    3: bool enable,
    4: i32 polynominal,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the PRBS stats on a port. Useful when debugging a link
   * down or flapping issue.
   */
  phy.PrbsStats getPortPrbsStats(
    1: i32 portId,
    2: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Clear the PRBS stats counter on a port. Useful when debugging a link
   * down or flapping issue.
   * This clearPortPrbsStats will result in:
   * 1. reset ber (due to reset accumulated error count if implemented)
   * 2. reset maxBer
   * 3. reset numLossOfLock to 0
   * 4. set timeLastCleared to now
   * 5. set timeLastLocked to timeLastCollect if locked else epoch
   * 6. locked status not changed
   * 7. timeLastCollect not changed
   */
  void clearPortPrbsStats(
    1: i32 portId,
    2: phy.PortComponent component,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Return info related to the port including name, description, speed,
   * counters, ...
   */
  PortInfoThrift getPortInfo(1: i32 portId) throws (
    1: fboss.FbossBaseError error,
  );
  map<i32, PortInfoThrift> getAllPortInfo() throws (
    1: fboss.FbossBaseError error,
  );

  /* clear stats for specified port(s) */
  void clearPortStats(1: list<i32> ports);

  /* clears stats for all ports */
  void clearAllPortStats();

  /* Legacy names for getPortInfo() and getAllPortInfo() */
  PortInfoThrift getPortStats(1: i32 portId) throws (
    1: fboss.FbossBaseError error,
  );
  map<i32, PortInfoThrift> getAllPortStats() throws (
    1: fboss.FbossBaseError error,
  );
  map<string, hardware_stats.HwSysPortStats> getSysPortStats() throws (
    1: fboss.FbossBaseError error,
  );
  map<string, hardware_stats.HwPortStats> getHwPortStats() throws (
    1: fboss.FbossBaseError error,
  );
  map<string, hardware_stats.HwSwitchCounter> getRouteCounters() throws (
    1: fboss.FbossBaseError error,
  );

  map<
    string,
    hardware_stats.HwRouterInterfaceStats
  > getHwRouterInterfaceStats() throws (1: fboss.FbossBaseError error);

  hardware_stats.CpuPortStats getCpuPortStats() throws (
    1: fboss.FbossBaseError error,
  );
  map<i32, hardware_stats.CpuPortStats> getAllCpuPortStats() throws (
    1: fboss.FbossBaseError error,
  );
  hardware_stats.FabricReachabilityStats getFabricReachabilityStats() throws (
    1: fboss.FbossBaseError error,
  );

  map<
    i16,
    agent_stats.HwAgentEventSyncStatus
  > getHwAgentConnectionStatus() throws (1: fboss.FbossBaseError error);

  /* Return running config */
  string getRunningConfig() throws (1: fboss.FbossBaseError error);

  list<ArpEntryThrift> getArpTable() throws (1: fboss.FbossBaseError error);
  list<NdpEntryThrift> getNdpTable() throws (1: fboss.FbossBaseError error);
  list<L2EntryThrift> getL2Table() throws (1: fboss.FbossBaseError error);
  AclTableThrift getAclTableGroup() throws (1: fboss.FbossBaseError error);
  list<AclEntryThrift> getAclTable() throws (1: fboss.FbossBaseError error);

  AggregatePortThrift getAggregatePort(1: i32 aggregatePortID) throws (
    1: fboss.FbossBaseError error,
  );
  list<AggregatePortThrift> getAggregatePortTable() throws (
    1: fboss.FbossBaseError error,
  );

  LacpPartnerPair getLacpPartnerPair(1: i32 portID) throws (
    1: fboss.FbossBaseError error,
  );
  list<LacpPartnerPair> getAllLacpPartnerPairs() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Information about Hw state, often useful for debugging
   * on a box
   */
  string getHwDebugDump();

  /*
   * Enable or disable the SDK dumping register/state logs to disk
   * (SAI_SWITCH_ATTR_SDK_DUMP_LOG_PATH_NAME). Disabling clears the attribute so
   * the SDK stops dumping. Throws if the device/ASIC does not support this.
   */
  void setSdkRegDumpEnabled(1: bool enabled) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * String formatted information of givens Hw Objects.
   */
  string listHwObjects(1: list<HwObjectType> objects, 2: bool cached) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Type of boot performed by the controller
   */
  BootType getBootType();

  /*
   * Get the list of neighbors discovered via LLDP
   */
  list<LinkNeighborThrift> getLldpNeighbors() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Start a packet capture
   */
  void startPktCapture(1: CaptureInfo info) throws (
    1: fboss.FbossBaseError error,
  );
  void stopPktCapture(1: string name) throws (1: fboss.FbossBaseError error);
  void stopAllPktCaptures() throws (1: fboss.FbossBaseError error);

  /*
   * Log all updates to routes that match this prefix, or are more
   * specific.
   */
  void startLoggingRouteUpdates(1: RouteUpdateLoggingInfo info);
  void stopLoggingRouteUpdates(1: IpPrefix prefix, 2: string identifier);
  void stopLoggingAnyRouteUpdates(1: string identifier);

  list<RouteUpdateLoggingInfo> getRouteUpdateLoggingTrackedPrefixes();

  /*
   * Log all updates to mpls routes for given label
   */
  void startLoggingMplsRouteUpdates(1: MplsRouteUpdateLoggingInfo info);
  void stopLoggingMplsRouteUpdates(1: MplsRouteUpdateLoggingInfo info);
  void stopLoggingAnyMplsRouteUpdates(1: string identifier);
  list<MplsRouteUpdateLoggingInfo> getMplsRouteUpdateLoggingTrackedLabels();

  void keepalive();

  i32 getIdleTimeout() throws (1: fboss.FbossBaseError error);

  /*
   * Return product information
   */
  product_info.ProductInfo getProductInfo() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Force reload configurations from the config file. Useful when config file
   * has changed since the agent started.
   */
  void reloadConfig();

  /*
   * Get last time(ms since epoch) of the config is applied.
   * NOTE: If no config has ever been applied, the default timestamp is 0.
   * TODO(joseph5wu) Will deprecate such api and use getConfigAppliedInfo()
   * instead
   */
  i64 getLastConfigAppliedInMs();

  /*
   * Get config applied information, which includes last config applied time(ms)
   * and last coldboot config applied time(ms).
   */
  ConfigAppliedInfo getConfigAppliedInfo() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Serialize switch state at thrift path
   */
  string getCurrentStateJSON(1: string path);

  /*
   * Get live serialized switch state for provided paths
   */
  map<string, string> getCurrentStateJSONForPaths(1: list<string> paths);

  /*
   * Apply every json Patch to specified path.
   * json Patch must be a valid JSON object string.
   */
  void patchCurrentStateJSONForPaths(1: map<string, string> pathToJsonPatch);

  /*
  * Switch run state
  */
  SwitchRunState getSwitchRunState();

  MultiSwitchRunState getMultiSwitchRunState();

  SSLType getSSLPolicy() throws (1: fboss.FbossBaseError error);

  /*
  * Enable external control for port LED lights
  */
  void setExternalLedState(
    1: i32 portNum,
    2: PortLedExternalState ledState,
  ) throws (1: fboss.FbossBaseError error);

  /*
  * Return the system's platform mapping (see platform_config.thrift)
  */
  platform_config.PlatformMapping getPlatformMapping() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * API to add named next hop groups, named next hop group with same name will be replaced
   * With combineDuplicatedNextHops, next hops repeated within a group are collapsed into a
   * single weighted next hop instead of the repeats being dropped.
   */
  void addNamedNextHopGroups(
    1: list<common.NextHopGroup> nextHopGroups,
    2: bool combineDuplicatedNextHops = false,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * API to add named next hop groups, named next hop group with same name will be replaced
   * With combineDuplicatedNextHops, next hops repeated within a group are collapsed into a
   * single weighted next hop instead of the repeats being dropped.
   * Deprecated. Use addNamedNextHopGroups.
   */
  void addOrUpdateNamedNextHopGroups(
    1: list<common.NextHopGroup> nextHopGroups,
    2: bool combineDuplicatedNextHops = false,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * API to remove named next hop groups
   */
  void deleteNamedNextHopGroups(1: list<string> name) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * API to get next hop groups
   * With replicateWeightedNexthops, a next hop carrying weight w > 1 is
   * expanded back into w next hops, undoing combineDuplicatedNextHops.
   */
  list<common.NextHopGroup> getNextHopGroups(
    1: bool replicateWeightedNexthops = false,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * API to get named next hop groups, optionally filtered by name
   * With replicateWeightedNexthops, a next hop carrying weight w > 1 is
   * expanded back into w next hops, undoing combineDuplicatedNextHops.
   */
  list<common.NextHopGroup> getNamedNextHopGroups(
    1: list<string> names,
    2: bool replicateWeightedNexthops = false,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Set all DSCP values for a given forwarding class
   */
  void setDscpToForwardingClass(
    1: list<byte> dscp,
    2: common.ForwardingClass fc,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Clear all DSCP values for a given forwarding class, cleared DSCP values map to internal default forwarding class
   */
  void clearForwardingClassForDscp(1: list<byte> dscp) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get forwarding class for a given DSCP
   */
  common.ForwardingClass getForwardingClassForDscp(1: byte dscp) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get DSCP mapping to forwarding class
   */
  common.DscpToForwardingClassMap getDscpToForwardingClassMap();

  /*
   * Add one or multiple traffic redirection policy objects.
   */
  void addPolicies(1: list<common.Policy> policy) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Remove one or multiple traffic redirection policy objects.
   */
  void deletePolicies(1: list<string> policyName) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get one or multiple traffic redirection policy objects.
   */
  list<common.Policy> getPolicies(1: list<string> policyNames) throws (
    1: fboss.FbossBaseError error,
  );
  list<common.Policy> getAllPolicies();

  /*
   * Set named next hop groups for given class to a policy
   */
  void setNextHopGroups(
    1: string policyName,
    2: common.ForwardingClass fc,
    3: list<string> nextHopGroups,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Clear all next hop groups for given class from a policy
   */
  void clearNextHopGroups(
    1: string policyName,
    2: common.ForwardingClass fc,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get all next hop group names for class in a policy
   */
  list<common.NextHopGroup> getNextHopGroupsForPolicy(
    1: string policyName,
    2: common.ForwardingClass fc,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the list of neighbors traffic is blocked for.
   */
  list<switch_config.Neighbor> getBlockedNeighbors() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Set neighbors to block. Useful to temporarily block traffic to a list of
   * neighbors. This does not affect the configuration, the list will be
   * cleared out due to config reapplication post agent restart.
   *
   * To clear, set neighborsToBlock to empty.
   */
  void setNeighborsToBlock(
    1: list<switch_config.Neighbor> neighborsToBlock,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the list of MAC addrs the traffic is blocked for.
   */
  list<switch_config.MacAndVlan> getMacAddrsToBlock() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Set MAC addrs to block the traffic to. Useful to temporarily block traffic
   * to a list of MAC addrs. This does not affect the configuration, the list
   * will be cleared out due to config reapplication post agent restart.
   *
   * To clear, set macAddrsToBlock to empty.
   */
  void setMacAddrsToBlock(
    1: list<switch_config.MacAndVlan> macAddrsToblock,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get the list of additional router MACs (MY_MAC). Packets to these MACs on
   * the given VLAN are routed, in addition to packets to the interface MAC.
   * Empty if none are configured.
   */
  list<switch_config.MacAndVlan> getMyMacs() throws (
    1: fboss.FbossBaseError error,
  );

  # Deprecated
  list<TeFlowDetails> getTeFlowTableDetails() throws (
    1: fboss.FbossBaseError error,
  );
  map<string, FabricEndpoint> getFabricConnectivity() throws (
    1: fboss.FbossBaseError error,
  );
  # Deprecated. To be deleted after migrating clients
  map<string, FabricEndpoint> getFabricReachability() throws (
    1: fboss.FbossBaseError error,
  );
  map<string, list<string>> getSwitchReachability(
    1: list<string> switchNames,
  ) throws (1: fboss.FbossBaseError error);
  map<i64, switch_config.DsfNode> getDsfNodes() throws (
    1: fboss.FbossBaseError error,
  );
  list<FsdbSubscriptionThrift> getDsfSubscriptions() throws (
    1: fboss.FbossBaseError error,
  );
  string getDsfSubscriptionClientId() throws (1: fboss.FbossBaseError error);
  /*
   * Get bi-directional session status for dsf subscriptions.
   */
  list<DsfSessionThrift> getDsfSessions() throws (
    1: fboss.FbossBaseError error,
  );

  map<i64, SystemPortThrift> getSystemPorts() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Only applicable to DSF Fabric Switch
   */
  bool isSwitchDrained() throws (1: fboss.FbossBaseError error);

  /*
   * On VOQ switches, actual switch drain state is determined by the number of
   * fabric ports UP and configured thresholds, so could be different from the
   * configured desired switch drain state.
   */
  map<i64, switch_config.SwitchDrainState> getActualSwitchDrainState() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get all the ecmp object details in the HW
   */
  list<EcmpDetails> getAllEcmpDetails() throws (1: fboss.FbossBaseError error);

  /*
   * Get switch indices for interfaces
   */
  map<i16, list<string>> getSwitchIndicesForInterfaces(
    1: list<string> interfaces,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Get SwitchID to SwitchInfo for all SwitchIDs.
   */
  map<i64, switch_config.SwitchInfo> getSwitchIdToSwitchInfo();

  /*
   * Get fabric link monitoring statistics for all fabric ports
   */
  map<i32, FabricLinkMonPortStats> getAllFabricLinkMonitoringStats() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get fabric monitoring details for all fabric ports
   */
  list<FabricMonitoringDetail> getFabricMonitoringDetails() throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Get total route count (v4 and v6) from the FIB
   */
  RouteCount getRouteTableSize() throws (1: fboss.FbossBaseError error);

  /*
   * Change the PRBS setting on a list of ports.
   */
  void setInterfacesPrbs(
    1: list<string> portNames,
    2: phy.PortComponent component,
    3: prbs.InterfacePrbsState state,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Configure this switch as one endpoint of each L3 traffic loop. Each list
   * is indexed by loop and must have the same length. qualificationAddresses
   * contains the unique IPv6 /128 destinations reserved for qualification
   * traffic. An empty expectedPeerMacs entry means no peer MAC is supplied.
   * This call returns only after all local loops are READY. The traffic-
   * injection RPC does not check loop readiness, so callers must use
   * getL3TrafficLoopStatus to wait for successful setup on both endpoints
   * before injecting traffic.
   * timeoutSeconds is the lease duration for the corresponding loop.
   */
  void setupL3TrafficLoops(
    1: list<Address.BinaryAddress> qualificationAddresses,
    2: list<string> portNames,
    3: list<Address.BinaryAddress> peerNextHops,
    4: list<string> expectedPeerMacs,
    5: list<i32> timeoutSeconds,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Extend the expiry of each READY loop. If any requested loop is not READY,
   * throws fboss.FbossBaseError with a message indicating the loop is not
   * ready yet; no loop is extended.
   */
  void extendL3TrafficLoops(
    1: list<string> portNames,
    2: i32 timeoutSeconds = 60,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Safely stop each loop and restore the switch's pre-test state. Attempts
   * cleanup for every requested port before throwing if any cleanup fails.
   */
  void teardownL3TrafficLoops(1: list<string> portNames) throws (
    1: fboss.FbossBaseError error,
  );

  /*
   * Return support and local setup/cleanup status for the requested ports, or
   * all ports if portNames is empty. Results are keyed by port name.
   */
  map<string, L3TrafficLoopStatus> getL3TrafficLoopStatus(
    1: list<string> portNames,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Add Adjacency FRR to protect an object with given backup nexthops.
   * If the protected object already exists, replaces its backup nexthops.
   *
   * FrrProtectedObject is a union. Fill either uA mySID or MPLS label to
   * protect.
   *
   * Number of backup nexthops in the list:
   *  = 0: throws Error
   *  = 1: single backup
   *  > 1: ECMP of backups
   */
  void addAdjacencyFrr(
    1: FrrProtectedObject protectedObject,
    2: list<common.NextHopThrift> backupNextHops,
  ) throws (1: fboss.FbossBaseError error);

  /*
   * Delete Adjacency FRR.
   *
   * protectedObject:
   *  non-existing MySID: throws error
   *  existing MySID without FRR protection: no-op
   *  existing MySID with FRR protection: removes FRR protection
   *
   * FrrProtectedObject is a union. Fill either protected uA mySID or MPLS
   * label.
   */
  void deleteAdjacencyFrr(1: FrrProtectedObject protectedObject) throws (
    1: fboss.FbossBaseError error,
  );
}

service NeighborListenerClient extends fb303.FacebookService {
  /*
   * Sends list of neighbors that have changed to the subscriber.
   *
   * These come in the form of ip address strings which have been added
   * since the last notification. Changes are not queued between
   * subscriptions.
   */
  void neighborsChanged(1: list<string> added, 2: list<string> removed) throws (
    1: fboss.FbossBaseError error,
  );
}
