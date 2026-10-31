#
# Copyright 2004-present Facebook. All Rights Reserved.
#
namespace go neteng.fboss.switch_config
namespace py neteng.fboss.switch_config
namespace py3 neteng.fboss
namespace py.asyncio neteng.fboss.asyncio.switch_config
namespace cpp2 facebook.fboss.cfg

include "fboss/agent/if/common.thrift"
include "fboss/agent/if/mpls.thrift"
include "fboss/lib/if/fboss_common.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/python.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

@hack.NamePrefix{prefix = "fboss_switch_config_"}
@hack.LegacyOmitPrefixInNameString
@hack.ConstantsClass{name = "fboss_switch_config_CONSTANTS"}
@thrift.AllowLegacyMissingUris
package;

@cpp.Type{name = "uint64_t"}
typedef i64 u64

const i16 defaultVlanId = 0;
const i64 arpTimeoutDefault = 60;
const i64 ndpTimeoutDefault = 60;
const i32 arpAgerIntervalDefault = 5;
const i32 arpRefreshSecondsDefault = 20;
const i32 maxNeighborProbesDefault = 300;
const i64 staleEntryIntervalDefault = 10;

enum PortDescriptorType {
  Physical = 0,
  Aggregate = 1,
  SystemPort = 2,
}

struct PortDescriptor {
  1: i16 portId;
  2: PortDescriptorType portType;
}

/**
 * Administrative port state. ie "should the port be enabled?"
 */
enum PortState {
  // Deprecated from when this capture administrative and operational state.
  DOWN = 0,

  DISABLED = 1, // Disabled, no signal transmitted
  ENABLED = 2,
}

/**
 * The parser type for a port.
 * This controls how much of the packet header will be parsed.
 */
enum ParserType {
  L2 = 0,
  L3 = 1,
  L4 = 2,
  ALL = 3,
}

enum PfcWatchdogRecoveryAction {
  /**
   * This option intends to recover from PFC deadlock by ignoring
   * PFC and continuing with packet transmit, most common.
   */
  NO_DROP = 0,

  /**
   * Drop the packets when in deadlock mode to help recover.
   */
  DROP = 1,
}

struct PfcWatchdog {
  1: i32 detectionTimeMsecs;
  2: i32 recoveryTimeMsecs;
  3: PfcWatchdogRecoveryAction recoveryAction = NO_DROP;
}

struct PortPfc {
  1: bool tx = false;
  2: bool rx = false;
  3: PortPgConfigName portPgConfigName;
  4: optional PfcWatchdog watchdog;
  5: optional bool txPfcDurationEnable;
  6: optional bool rxPfcDurationEnable;
}

/**
 * The spanning tree state for a VlanPort.
 *
 * This maps to the FocalPoint API's FM_STP_STATE_* constants.
 */
enum SpanningTreeState {
  BLOCKING = 0, // Receive BPDUs only, no traffic
  DISABLED = 1, // Receive nothing
  FORWARDING = 2, // Process BDPUs and traffic
  LEARNING = 3, // Send and receive BDPUs, but no traffic
  LISTENING = 4, // Send and receive BPDUs, but no learning or traffic
}

/**
 * The speed value for a port
 */
enum PortSpeed {
  DEFAULT = 0, // Default for that port defined by HW
  GIGE = 1000, // Gig Ethernet
  XG = 10000, // 10G
  TWENTYG = 20000, // 20G
  TWENTYFIVEG = 25000, // 25G
  FORTYG = 40000, // 40G
  FIFTYG = 50000, // 50G
  FIFTYTHREEPOINTONETWOFIVEG = 53125, //53.125G
  HUNDREDG = 100000, // 100G
  HUNDREDANDSIXPOINTTWOFIVEG = 106250, //106.25G
  TWOHUNDREDG = 200000, // 200G
  TWOHUNDREDANDTWELVEPOINTFIVEG = 212500, // 212.5G
  FOURHUNDREDG = 400000, // 400G
  EIGHTHUNDREDG = 800000, // 800G
  ONEPOINTSIXT = 1600000, // 1.6T
  THREEPOINTTWOT = 3200000, // 3.2T
}

// <speed>_<num_lanes>_<modulation>_<fec>
// <num_lanes>_<modulation>_<fec> is the outmoster layer settings.
enum PortProfileID {
  PROFILE_DEFAULT = 0,
  PROFILE_10G_1_NRZ_NOFEC = 1,
  PROFILE_20G_2_NRZ_NOFEC = 2,
  PROFILE_25G_1_NRZ_NOFEC = 3,
  PROFILE_40G_4_NRZ_NOFEC = 4,
  PROFILE_50G_2_NRZ_NOFEC = 5,
  PROFILE_100G_4_NRZ_NOFEC = 6,
  PROFILE_100G_4_NRZ_CL91 = 7,
  PROFILE_100G_4_NRZ_RS528 = 8,
  PROFILE_200G_4_PAM4_RS544X2N = 9,
  PROFILE_400G_8_PAM4_RS544X2N = 10,
  PROFILE_10G_1_NRZ_NOFEC_COPPER = 11,
  PROFILE_10G_1_NRZ_NOFEC_OPTICAL = 12,
  PROFILE_20G_2_NRZ_NOFEC_COPPER = 13,
  PROFILE_25G_1_NRZ_NOFEC_COPPER = 14,
  PROFILE_25G_1_NRZ_CL74_COPPER = 15,
  PROFILE_25G_1_NRZ_RS528_COPPER = 16,
  PROFILE_40G_4_NRZ_NOFEC_COPPER = 17,
  PROFILE_40G_4_NRZ_NOFEC_OPTICAL = 18,
  PROFILE_50G_2_NRZ_NOFEC_COPPER = 19,
  PROFILE_50G_2_NRZ_CL74_COPPER = 20,
  PROFILE_50G_2_NRZ_RS528_COPPER = 21,
  PROFILE_100G_4_NRZ_RS528_COPPER = 22,
  PROFILE_100G_4_NRZ_RS528_OPTICAL = 23,
  PROFILE_200G_4_PAM4_RS544X2N_COPPER = 24,
  PROFILE_200G_4_PAM4_RS544X2N_OPTICAL = 25,
  PROFILE_400G_8_PAM4_RS544X2N_OPTICAL = 26,
  PROFILE_100G_4_NRZ_CL91_COPPER = 27,
  PROFILE_100G_4_NRZ_CL91_OPTICAL = 28,
  PROFILE_20G_2_NRZ_NOFEC_OPTICAL = 29,
  PROFILE_25G_1_NRZ_NOFEC_OPTICAL = 30,
  PROFILE_50G_2_NRZ_NOFEC_OPTICAL = 31,
  PROFILE_100G_4_NRZ_NOFEC_COPPER = 32,
  // Special profiles for supporting Wedge100 with YV3_T1 rack so that we can
  // use different serdes values for these profiles
  PROFILE_100G_4_NRZ_CL91_COPPER_RACK_YV3_T1 = 33,
  PROFILE_25G_1_NRZ_NOFEC_COPPER_RACK_YV3_T1 = 34,
  PROFILE_400G_8_PAM4_RS544X2N_COPPER = 35,
  PROFILE_53POINT125G_1_PAM4_RS545_COPPER = 36,
  PROFILE_53POINT125G_1_PAM4_RS545_OPTICAL = 37,
  PROFILE_400G_4_PAM4_RS544X2N_OPTICAL = 38,
  PROFILE_800G_8_PAM4_RS544X2N_OPTICAL = 39,
  PROFILE_100G_2_PAM4_RS544X2N_OPTICAL = 40,
  PROFILE_106POINT25G_1_PAM4_RS544_COPPER = 41,
  PROFILE_106POINT25G_1_PAM4_RS544_OPTICAL = 42,
  PROFILE_50G_1_PAM4_RS544_COPPER = 43,
  PROFILE_50G_1_PAM4_RS544_OPTICAL = 44,
  PROFILE_400G_4_PAM4_RS544X2N_COPPER = 45,
  PROFILE_100G_2_PAM4_RS544X2N_COPPER = 46,
  PROFILE_100G_1_PAM4_RS544_OPTICAL = 47,
  PROFILE_50G_2_NRZ_RS528_OPTICAL = 48,
  PROFILE_100G_1_PAM4_NOFEC_COPPER = 49,
  PROFILE_800G_8_PAM4_RS544X2N_COPPER = 50,
  PROFILE_400G_2_PAM4_RS544X2N_OPTICAL = 51,
  PROFILE_800G_4_PAM4_RS544X2N_OPTICAL = 52,
  PROFILE_200G_1_PAM4_RS544X2N_OPTICAL = 53,
  PROFILE_200G_2_PAM4_RS544_COPPER = 54,
  PROFILE_100G_2_PAM4_RS544_COPPER = 55,
  PROFILE_100G_1_PAM4_RS544_COPPER = 56,
  PROFILE_800G_4_PAM4_RS544X2N_COPPER = 57,
  PROFILE_400G_2_PAM4_RS544X2N_COPPER = 58,
  PROFILE_200G_1_PAM4_RS544X2N_COPPER = 59,
  PROFILE_100G_1_PAM4_RS544X2N_COPPER = 60,
  PROFILE_200G_2_PAM4_RS544X2N_OPTICAL = 61,
  PROFILE_25G_1_NRZ_RS528_OPTICAL = 62,
  PROFILE_1600G_8_PAM4_RS544X2N_OPTICAL = 63,
  PROFILE_212POINT5G_1_PAM4_RS544X2N_OPTICAL = 64,
  PROFILE_212POINT5G_1_PAM4_RS544X2N_COPPER = 65,
  PROFILE_1600G_8_PAM4_RS544X2N_COPPER = 66,
}

enum Scope {
  LOCAL = 0,
  GLOBAL = 1,
}

/**
 * The pause setting for a port
 */
struct PortPause {
  1: bool tx = false;
  2: bool rx = false;
}

/**
 * Using this as default is slightly better for generated bindings w/ nullable
 * languages, most notably python. Setting this as default ensures that the
 * attribute is non-null, but defaults both members to false.
*/
const PortPause NO_PAUSE = {"tx": false, "rx": false};

const string TeFlowTableName = "TeFlowTable";

/**
 *  [DEPRECATED] TODO(joseph5wu)
 *  A Range for L4 port range checker
 *  Define a range bewteen [min, max]
 */
struct L4PortRange {
  1: i32 min;
  2: i32 max;
  3: bool invert = false;
}

/**
 *  [DEPRECATED] TODO(joseph5wu)
 *  A Range for packet length
 *  Define a packet length range [min, max]
 */
struct PktLenRange {
  1: i16 min;
  2: i16 max;
  3: bool invert = false;
}

enum IpType {
  ANY = 0,
  IP = 1,
  IP4 = 2,
  IP6 = 3,
  ARP_REQUEST = 4,
  ARP_REPLY = 5,
  NON_IP = 6,
}

enum EtherType {
  ANY = 0x0000,
  IPv4 = 0x0800,
  IPv6 = 0x86DD,
  EAPOL = 0x888E,
  MACSEC = 0x88E5,
  LLDP = 0x88CC,
  ARP = 0x0806,
  LACP = 0x8809,
  AIFM = 0x88B6,
  MPLS = 0x8847,
}

struct Ttl {
  1: i16 value;
  2: i16 mask = 0xFF;
}

enum IpFragMatch {
  // not fragment
  MATCH_NOT_FRAGMENTED = 0,
  // first fragment
  MATCH_FIRST_FRAGMENT = 1,
  // not fragment or the first fragment
  MATCH_NOT_FRAGMENTED_OR_FIRST_FRAGMENT = 2,
  // fragment but not the first frament
  MATCH_NOT_FIRST_FRAGMENT = 3,
  // any fragment
  MATCH_ANY_FRAGMENT = 4,
}

struct GreTunnel {
  1: string ip;
  2: optional Ttl ttl;
}

struct SflowTunnel {
  1: string ip;
  2: optional i32 udpSrcPort;
  3: optional i32 udpDstPort;
  4: optional Ttl ttl;
}

/*
 * Either greTunnel or sflowTunnel (but not both) must be set to specify
 * destination IP and encapsulation type.
 * a tunnel source ip can be specified optionally if mirror collector wants
 * a specific IP to be in L3 header
 */
struct MirrorTunnel {
  1: optional GreTunnel greTunnel;
  2: optional SflowTunnel sflowTunnel;
  3: optional string srcIp;
}

union MirrorEgressPort {
  @python.Name{name = "name_"}
  1: string name;
  2: i32 logicalID;
}

/*
 * Mirror destination
 * At least one of egressPort and tunnel must be set
 *
 *  egressPort - name or ID of port from which mirror traffic egresses
 *               required for SPAN
 *               optional for ERSPAN port mirroring
 *  ip - to be deprecated. ip address is now given in "tunnel"
 *  tunnel - configure the tunnel (sflow or gre) used to mirror the packet
 *           to a remote (IPv4) destination.
 */
struct MirrorDestination {
  1: optional MirrorEgressPort egressPort;
  // ip to be deprecated
  2: optional string ip;
  3: optional MirrorTunnel tunnel;
}

// default DSCP marking for mirrored packets
const byte DEFAULT_MIRROR_DSCP = 0;

/*
 * Mirror,
 *   - name, a logical identifier of mirror
 *   - destination, to deliver mirrored traffic
 *   - dscp, mark the mirrored packets with a DSCP marking for QoS
 *           classification. Note: DSCP marking is only 6 bits (0-63),
 *           not a full byte.
 *   - truncate, flag whether mirrored packets should be truncated (if supported
 *               by platform)
 *
 * Mirroring allows replication of data plane traffic to another destination
 * This destination can be specified either as an egress port, an egress port
 * and a tunnel type with remote routable ip (v4) address, or only a tunnel
 * type with remote routable ip (v4) address.
 *
 * If only egress port is specified, then mirrored data-plane traffic egresses
 * the specified port.
 *
 * If egress port & tunnel with remote ip are specified, then mirrored
 * data-plane traffic egresses the specified port and is delivered to
 * destination ip address.
 *
 * If only tunnel with remote ip is specified, then mirrored data-plane traffic
 * is delivered to the destination ip address via any possible egress port.
 *
 * Traffic delivered to remote IP address can be either sFLow or ERSPAN (type 1)
 * encapsulated.
 * The source and destination UDP ports for sFLow can be specified
 * however the header will be a shim header (rather than full v4/v5 header)
 * whose format can be found in BCM DocSafe document 56960-PG104-RDS pg 595.
 * ERSPAN is basically a "GRE" tunnel with protocol type 0x88be.
 * L2 frame is encapsulated in the selected tunnel and delivered to remote
 * routable destination.
 *
 * A traffic to mirror, can be selected using two ways
 *    1) a traffic ingressing or egressing a particular port
 *    2) a traffic ingressing or egressing that meets "acl" criteria,
 *    (this mirrors a traffic ingressing/egressing switch's acl engine)
 *
 *  mirror direction if INGRESS, only ingressing traffic is mirrored
 *  mirror direction if EGRESS, only egressing traffic is mirrored
 *  mirror direction if BOTH, both ingress/egress traffic is mirrored
 *
 * Only four mirror destinations are possible
 *
 * Mirroring should be used with caution and in circumstances to debug traffic
 * issues, as this feature duplicates traffic and so doubles traffic load.
 */
struct Mirror {
  1: string name;
  2: MirrorDestination destination;
  3: byte dscp = DEFAULT_MIRROR_DSCP;
  4: bool truncate = false;
  /*
   * 0 - no sampling
   * 1 - sample all packets
   * Any other integer value represent the 1 out of samplingRate
   * packets will be mirrored.
   */
  5: optional i32 samplingRate;
}

/*
 * Aggregation of mirror-on-drop reasons. The exact drop reasons corresponding to each
 * enum is platform-specific and is converted in the platform-specific SaiTamManagers.
 */
enum MirrorOnDropReasonAggregation {
  UNEXPECTED_REASON_DISCARDS = 0,

  INGRESS_MISC_DISCARDS = 1,
  INGRESS_PACKET_PROCESSING_DISCARDS = 2,
  INGRESS_QUEUE_RESOLUTION_DISCARDS = 3,
  INGRESS_SOURCE_CONGESTION_DISCARDS = 4, // e.g. VSQ
  INGRESS_DESTINATION_CONGESTION_DISCARDS = 5, // e.g. VOQ
  INGRESS_GLOBAL_CONGESTION_DISCARDS = 6,
}

/**
 * Aging group of an event, which controls the granularity at which MOD packets
 * are sent:
 * - At most one packet per interval will be generated for GLOBAL events
 * - At most one packet per port pre interval will be generated for PORT events
 * - ...and so on
 */
enum MirrorOnDropAgingGroup {
  GLOBAL = 0,
  PORT = 1,
  PRIORITY_GROUP = 2,
  VOQ = 3,
}

/**
 * Configuration for each event ID under a MirrorOnDropReport. An event can have its own
 * aging granularity setting and associated drop reasons.
 */
struct MirrorOnDropEventConfig {
  1: list<MirrorOnDropReasonAggregation> dropReasonAggregations;
  2: optional MirrorOnDropAgingGroup agingGroup; // defaults to 0 i.e. GLOBAL
}

struct MirrorOnDropReport {
  1: string name;
  // Deprecated, use mirrorPort.
  2: optional i32 mirrorPortId;
  // Source IP will be populated based on switch IP at runtime, so not configurable.
  3: i16 localSrcPort;
  4: string collectorIp;
  5: i16 collectorPort;
  6: i16 mtu;
  // Contents of the dropped packet will be truncated when mirroring.
  7: i16 truncateSize = 128;
  8: byte dscp = 0;
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  9: optional i32 agingIntervalUsecs_DEPRECATED;
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  10: map<
    byte,
    list<MirrorOnDropReasonAggregation>
  > eventIdToDropReasons_DEPRECATED;
  // Configuration for each event ID.
  11: map<byte, MirrorOnDropEventConfig> modEventToConfigMap;
  // Aging interval (how often to send packets) for each aging group in usecs.
  12: map<MirrorOnDropAgingGroup, i32> agingGroupAgingIntervalUsecs;
  /*
   * Currently only supports using a MirrorEgressPort in MirrorDestination. Possible options are:
   *
   * 1. Recycle port: MOD packets will be injected back into the pipeline via recycle port.
   * 2. Eventor port: MOD packets will be injected back into the pipeline via eventor port. Provides the option to pack multiple MOD packets.
   * 3. Front panel Ethernet port: Not supported.
   *
   * If neither mirrorPortId or mirrorPort is specified, agent will attempt to pick the first local scoped recycle port.
   */
  13: optional MirrorDestination mirrorPort;
  /*
   * Optional sampling rate for MOD packets. When set, only 1 in samplingRate
   * MOD packets will be sent to the collector. If not set, all MOD packets
   * are sent (no sampling).
   */
  14: optional i32 samplingRate;
  /*
   * Optional packets-per-second cap on MoD report generation, applied
   * separately to each drop reason. The total report rate grows with the
   * number of drop reasons dropping at once: a threshold of X with 10 active
   * drop reasons allows up to 10X reports/sec. Use
   * aggregateDropPacketRateThreshold to cap the total. Drops above the cap
   * produce no report.
   */
  15: optional i32 dropPacketRateThreshold;
  /*
   * Optional packets-per-second cap on the combined MoD report rate across
   * all drop reasons, applied after the per-reason dropPacketRateThreshold.
   * Maps to SAI_TAM_REPORT_ATTR_MAX_REPORT_RATE on the MoD report. If not
   * set, no aggregate cap is applied.
   */
  16: optional i32 aggregateDropPacketRateThreshold;
}

/**
 * The action for an access control entry
 */
enum AclActionType {
  /** Drop the packet. Maps to SAI_PACKET_ACTION_DROP. */
  DENY = 0,

  PERMIT = 1,

  /**
   * Drop the packet, and additionally cancel any copy to CPU requested for it
   * by a lower priority ACL or by a host interface trap. Maps to
   * SAI_PACKET_ACTION_DENY, which the SAI spec defines as a combination of
   * COPY_CANCEL and DROP.
   *
   * Unlike DENY, this also suppresses a punt that the forwarding lookup would
   * otherwise take, because implementations may realize host interface traps
   * as lower priority ACLs.
   *
   * Not supported on every ASIC. Where the underlying packet action is
   * unavailable, an entry using it fails to program.
   */
  DENY_DATA_AND_CONTROL_PLANE = 2,
}

/**
 * The look up class for an acl
 */
enum AclLookupClass {
  /*
   * Use cases:
   * 1. on sai switches: class ID for my ip /32 /128 routes pointing to cpu
   * 2. on native bcm switches: ipv4 routes point to cpu
   */
  DST_CLASS_L3_LOCAL_1 = 1,

  /*
   * Use cases:
   * 1. on sai switches: routes that uses single nexthop and are unresolved points to
   * cpu port as the nexthop to trigger neighbor resolution. Associate
   * a class ID for those routes which will be matched against an ACL
   * to send the packet to default queue. Refer to S390808 for more details.
   * 2. on sai switches: class ID for connected subnet routes pointing to router interface.
   * 3. on native bcm switches: ipv6 routes point to cpu
   */
  DST_CLASS_L3_LOCAL_2 = 2,

  // Class for DROP ACL
  CLASS_DROP = 9,

  /*
   * RSW Downlink ports connected to MH-NIC setups configure a queue per host
   * using ACLs. These ACLs use following classes.
   */
  CLASS_QUEUE_PER_HOST_QUEUE_0 = 10,
  CLASS_QUEUE_PER_HOST_QUEUE_1 = 11,
  CLASS_QUEUE_PER_HOST_QUEUE_2 = 12,
  CLASS_QUEUE_PER_HOST_QUEUE_3 = 13,
  CLASS_QUEUE_PER_HOST_QUEUE_4 = 14,
  CLASS_QUEUE_PER_HOST_QUEUE_5 = 15,
  CLASS_QUEUE_PER_HOST_QUEUE_6 = 16,
  CLASS_QUEUE_PER_HOST_QUEUE_7 = 17,
  CLASS_QUEUE_PER_HOST_QUEUE_8 = 18,
  CLASS_QUEUE_PER_HOST_QUEUE_9 = 19,

  // set by BGP for deterministic path routes
  DST_CLASS_L3_DPR = 20,

  // will be replaced by DST_CLASS_L3_LOCAL_1 and DST_CLASS_L3_LOCAL_2
  DEPRECATED_CLASS_UNRESOLVED_ROUTE_TO_CPU = 21,
  DEPRECATED_CLASS_CONNECTED_ROUTE_TO_INTF = 22,
}

enum AclLookupClassPort {
  CLASS_PORT_UNCONSTRAINED = 0,
  CLASS_PORT_RESTRICTED = 1,
  CLASS_PORT_BLOCKED = 2,
}

enum PacketLookupResultType {
  PACKET_LOOKUP_RESULT_MPLS_NO_MATCH = 1,
}

struct AclUdfEntry {
  1: string udfGroup;

  2: list<byte> roceBytes;

  3: list<byte> roceMask;
}

/**
 * An access control entry
 */
struct AclEntry {
  /**
   * IP addresses with mask. e.g. 192.168.0.0/16. Can be either V4 or V6
   */
  3: optional string srcIp;
  4: optional string dstIp;

  /**
   * [DEPRECATED]
   * L4 port ranges (TCP/UDP)
   */
  5: optional L4PortRange srcL4PortRange_DEPRECATED;
  6: optional L4PortRange dstL4PortRange_DEPRECATED;

  /**
   * IP Protocol. e.g, 6 for TCP
   * Valid value should between 0-255.
   * Note: Should use uint8, but `byte` in thrift is int8, so use i16 instead.
   * Otherwise we will have to use [-127,-1] to represent [128,255], which is
   * not straight-forward at all.
   */
  7: optional i16 proto;

  /**
   * TCP flags(aka Control bits) Bit Map. e.g, 16 for ACK set
   * Starting from the FIN flag which is bit 0 and bit 7 is the CWR bit
   * Valid value should between 0-255.
   */
  8: optional i16 tcpFlagsBitMap;

  /**
   * Physical switch ports.
   */
  10: optional i16 srcPort;
  11: optional i16 dstPort;

  /**
   * [DEPRECATED]
   * Packet length range
   */
  12: optional PktLenRange pktLenRange_DEPRECATED;

  /**
   * Ip fragment
   */
  13: optional IpFragMatch ipFrag;

  /**
   * Icmp type and code
   * Valid value should between 0-255.
   * Code can only be set if type is set.
   * "proto" field must be 1 (icmpv4) or 58 (icmpv6)
   */
  14: optional i16 icmpType;
  15: optional i16 icmpCode;
  /*
  * Match on DSCP values
  */
  16: optional byte dscp;
  /*
   * Identifier used to refer to the ACL
   */
  17: string name;

  18: AclActionType actionType = PERMIT;

  19: optional string dstMac;

  /* Match TTL if IPv4 and HopLimit if IPv6 */
  20: optional Ttl ttl;

  /* IP type (IPv4, IPv6, ARP, MPLS... */
  21: optional IpType ipType;

  /**
   * L4 port (TCP/UDP)
   */
  22: optional i32 l4SrcPort;
  23: optional i32 l4DstPort;

  /*
   * We switched to using more granular lookupClassNeighbor and
   * lookupClassRoute.
   */
  /* dstClassL3 */
  24: optional AclLookupClass lookupClass_DEPRECATED;

  /* dstClassL2 */
  25: optional AclLookupClass lookupClassL2;

  26: optional AclLookupClass lookupClassNeighbor;

  27: optional AclLookupClass lookupClassRoute;

  28: optional PacketLookupResultType packetLookupResult;

  29: optional EtherType etherType;

  30: optional i32 vlanID;

  /* 31-34 not to be used for SAI */
  31: optional list<string> udfGroups;

  32: optional byte roceOpcode;

  33: optional list<byte> roceBytes;

  34: optional list<byte> roceMask;

  35: optional list<AclUdfEntry> udfTable;

  36: optional Range l4DstPortRange;

  // Thrift has no unsigned 32-bit integer type. Use i64 as the carrier type so
  // the full IPv6 word range [0, 0xffffffff] is representable. ACL config
  // application validates the range before programming.
  //
  // dstIpV6Word3 matches destination IPv6 bits 127:96, and dstIpV6Word2
  // matches bits 95:64. For AAAA:BBBB:CCCC:DDDD:EEEE:FFFF:1111:2222,
  // word3 is AAAA:BBBB and word2 is CCCC:DDDD.
  37: optional i64 dstIpV6Word3;
  38: optional i64 dstIpV6Word2;

  /* Match lookup class assigned to the packet's ingress port. */
  39: optional AclLookupClassPort lookupClassPort;

  /*
   * Match the TTL of the outermost MPLS label. Distinct from ttl, which
   * matches the IP header TTL. Pair with etherType MPLS to scope the match to
   * MPLS traffic.
   */
  40: optional Ttl mplsLabel0Ttl;

  /**
   * Mask applied to tcpFlagsBitMap: a packet matches when
   * (packet flags & tcpFlagsMask) == (tcpFlagsBitMap & tcpFlagsMask).
   * Valid values [1-63], and valid only when tcpFlagsBitMap is set.
   */
  41: optional i16 tcpFlagsMask;
}

enum AclTableActionType {
  PACKET_ACTION = 0,
  COUNTER = 1,
  SET_TC = 2,
  SET_DSCP = 3,
  MIRROR_INGRESS = 4,
  MIRROR_EGRESS = 5,
  SET_USER_DEFINED_TRAP = 6,
  DISABLE_ARS_FORWARDING = 7,
  SET_ARS_OBJECT = 8,
  L3_SWITCH_CANCEL = 9,
  REDIRECT = 10,
}

enum AclTableQualifier {
  SRC_IPV6 = 0,
  DST_IPV6 = 1,
  SRC_IPV4 = 2,
  DST_IPV4 = 3,
  L4_SRC_PORT = 4,
  L4_DST_PORT = 5,
  IP_PROTOCOL_NUMBER = 6,
  TCP_FLAGS = 7,
  SRC_PORT = 8,
  OUT_PORT = 9,
  IP_FRAG = 10,
  ICMPV4_TYPE = 11,
  ICMPV4_CODE = 12,
  ICMPV6_TYPE = 13,
  ICMPV6_CODE = 14,
  DSCP = 15,
  DST_MAC = 16,
  IP_TYPE = 17,
  TTL = 18,
  LOOKUP_CLASS_L2 = 19,
  LOOKUP_CLASS_NEIGHBOR = 20,
  LOOKUP_CLASS_ROUTE = 21,
  ETHER_TYPE = 22,
  OUTER_VLAN = 23,
  UDF = 24,
  BTH_OPCODE = 25,
  IPV6_NEXT_HEADER = 26,
  L4_DST_PORT_RANGE = 27,
  TC = 28,
  NEXT_HOP_GROUP_ID = 29,
  DST_IPV6_WORD3 = 30,
  DST_IPV6_WORD2 = 31,
  LOOKUP_CLASS_PORT = 32,
  MPLS_LABEL0_TTL = 33,
}

enum AclTableGroupBindPoint {
  SWITCH = 0,
  PORT = 1,
}

struct AclTable {
  1: string name;
  2: i16 priority;
  3: list<AclEntry> aclEntries = [];
  4: list<AclTableActionType> actionTypes = [];
  5: list<AclTableQualifier> qualifiers = [];
  6: list<string> udfGroups = [];
}

enum AclStage {
  INGRESS = 0,
  INGRESS_MACSEC = 1,
  EGRESS_MACSEC = 2,
  INGRESS_POST_LOOKUP = 3,
}

// startdocs_AclTableGroup_struct
struct AclTableGroup {
  1: string name;
  2: list<AclTable> aclTables = [];
  3: AclStage stage = AclStage.INGRESS;
  4: AclTableGroupBindPoint bindPoint = AclTableGroupBindPoint.SWITCH;
}
// enddocs_AclTableGroup_struct

/*
 * We only support unicast in FBOSS, but for completeness sake
 */
enum StreamType {
  UNICAST = 0,
  MULTICAST = 1,
  ALL = 2,
  FABRIC_TX = 3,
}

// Bcm native SDK supports directly setting action send to queue.
// SAI translates queueId to tc and then do action set tc.
// So, need to convert SAI to use action SetTcAction.
struct QueueMatchAction {
  1: i16 queueId;
}

// used by SAI ACL action send to cpu queue
struct UserDefinedTrapAction {
  1: i16 queueId;
}

struct PacketCounterMatchAction {
  1: string counterName;
}

struct SetDscpMatchAction {
  1: byte dscpValue;
}

struct SetTcAction {
  1: byte tcValue;
}

enum ToCpuAction {
  /**
  * Send Copy of the packet to CPU, forward original packet as usual.
  */
  COPY = 0,

  /**
   * Redirect original packet to CPU.
   */
  TRAP = 1,
}

enum MacsecFlowPacketAction {
  DROP = 0,
  FORWARD = 1,
  MACSEC_FLOW = 2,
}

struct MacsecFlowAction {
  1: u64 flowId;
  2: MacsecFlowPacketAction action;
}

struct RedirectNextHop {
  // NextHop IP addresses
  1: string ip;
  2: optional i32 intfID;
  3: optional common.TunnelType tunnelType;
  4: optional string tunnelId;
}

// Redirect packet to a different nexthop
struct RedirectToNextHopAction {
  // deprecated - use redirectNextHops
  1: list<string> nexthops_DEPRECATED;
  2: list<RedirectNextHop> redirectNextHops;
  // Named NHG redirect target for PBR. The RIB resolves this name to a
  // NextHopSetID (stored in switch state) at synthesis time; the id is not
  // known at config time.
  3: optional string redirectNextHopGroup;
}

enum FlowletAction {
  /**
  * Forward the packet to DLB engine.
  */
  FORWARD = 1,
  /**
  * Disable the packet to DLB engine.
  */
  DISABLE = 2,
}

// Support for Set Hash Algorithm action
struct SetEcmpHashAction {
  1: SwitchingMode switchingMode;
}

struct MatchAction {
  1: optional QueueMatchAction sendToQueue;
  2: optional PacketCounterMatchAction packetCounter_DEPRECATED;
  3: optional SetDscpMatchAction setDscp;
  4: optional string ingressMirror;
  5: optional string egressMirror;
  6: optional string counter;
  7: optional ToCpuAction toCpuAction;
  8: optional MacsecFlowAction macsecFlow;
  9: optional RedirectToNextHopAction redirectToNextHop;
  10: optional SetTcAction setTc;
  11: optional UserDefinedTrapAction userDefinedTrap;
  12: optional FlowletAction flowletAction;
  13: optional SetEcmpHashAction ecmpHashAction;
}

struct MatchToAction {
  // This references an ACL added to the acls list in the SwitchConfig object
  1: string matcher;
  2: MatchAction action;
}

enum MMUScalingFactor {
  ONE = 0,
  EIGHT = 1,
  ONE_128TH = 2,
  ONE_64TH = 3,
  ONE_32TH = 4,
  ONE_16TH = 5,
  ONE_8TH = 6,
  ONE_QUARTER = 7,
  ONE_HALF = 8,
  TWO = 9,
  FOUR = 10,
  ONE_32768TH = 11,
  ONE_HUNDRED_TWENTY_EIGHT = 12,
}

// This determines how packets are scheduled on a per queue basis
enum QueueScheduling {
  // The round robin runs on all queues set to use WRR for a port
  // Required to set a weight when using this
  WEIGHTED_ROUND_ROBIN = 0,
  // All packets in this type of queue are dequeued before moving on
  // to the next (lower priority) queue
  // This means this can cause starvation on other queues if not
  // configured properly
  STRICT_PRIORITY = 1,
  // The round robin runs on all queues set to use WRR with deficit counter
  // for a port
  // Required to set a weight when using this
  DEFICIT_ROUND_ROBIN = 2,
  // For certain queue types (viz. Fabric port queues) scheduling details are
  // not exposed for programming.
  INTERNAL = 1000,
}

enum HighLowPriority {
  PRIORITY_LOW = 0,
  PRIORITY_HIGH = 1,
}

union SchedulingParam {
  1: HighLowPriority spPriority;
  2: i16 wrrWeight;
}

// Detection based on average queue length in bytes with two thresholds.
// If the queue length is below the minimum threshold, then never consider the
// queue congested. If the queue length is above the maximum threshold, then
// always consider the queue congested. If the queue length is in between the
// two thresholds, then probabilistically consider the queue congested.
// The probability grows linearly between the two thresholds. That is,
// if we call the minimum threshold m and the maximum threshold M then the
// formula for the probability at queue length m+k is:
// P(m+k) = k/(M-m) for k between 0 and M-m
struct LinearQueueCongestionDetection {
  1: i32 minimumLength;
  2: i32 maximumLength;
  3: i32 probability = 100;
}

// Determines when we will consider a queue to be experiencing congestion
// for the purposes of Active Queue Management
union QueueCongestionDetection {
  1: LinearQueueCongestionDetection linear;
}

// Two behaviors are supported for Active Queue Management:
// drops and ECN marking. ECN marking is also conditional on the entity
// entering the queue being an IP packet marked as ECN-enabled. Thus, the
// two booleans can configure the following behavior during congestion:
// earlyDrop false; ecn false: Tail drops for all packets
// earlyDrop true; ecn false: Early drops for all packets
// earlyDrop false; ecn true: Tail drops for ECN-disabled, ECN for ECN-enabled
// earlyDrop true; ecn true: Early drops for ECN-disabled, ECN for ECN-enabled
enum QueueCongestionBehavior {
  EARLY_DROP = 0, // Drop packets before congested queues are totally full using
  // an algorithm like WRED
  ECN = 1, // Mark congestion enabled packets with Congestion Experienced
}

// Configuration for Active Queue Management of a PortQueue.
// Follows the principles outlined in RFC 7567.
struct ActiveQueueManagement {
  // How we answer the question "Is the queue congested?"
  1: QueueCongestionDetection detection;
  // How we handle packets on queues experiencing congestion
  2: QueueCongestionBehavior behavior;
}

struct Range {
  1: i32 minimum;
  2: i32 maximum;
}

struct Range64 {
  1: i64 minimum;
  2: i64 maximum;
}

union PortQueueRate {
  1: Range pktsPerSec;
  2: Range kbitsPerSec;
}

// It is only necessary to define PortQueues for those that you want to
// change settings on
// It is only necessary to define PortQueues for those where you do not want to
// use the default settings.
// Any queues not described by config will use the system defaults of weighted
// round robin with a default weight of 1
struct PortQueue {
  1: i16 id;
  // We only use unicast in Fabric
  2: StreamType streamType = StreamType.UNICAST;
  // This value is ignored if STRICT_PRIORITY is chosen
  3: optional i32 weight;
  4: optional i32 reservedBytes;
  5: optional MMUScalingFactor scalingFactor;
  6: QueueScheduling scheduling;
  7: optional string name;
  // 8: optional i32 length (deprecated)
  /*
   * Refer PortQueueRate which is a generalized version and allows configuring
   * pps as well as kbps.
   */
  9: optional i32 packetsPerSec_DEPRECATED;
  // this specifies the static max threshold in buffer profile
  10: optional i32 sharedBytes;
  // Only Unicast queue supports aqms
  11: optional list<ActiveQueueManagement> aqms;
  12: optional PortQueueRate portQueueRate;

  13: optional i32 bandwidthBurstMinKbits;
  14: optional i32 bandwidthBurstMaxKbits;
  // this specifies the dynamic max threshold in buffer profile,
  // e.g. when scalingFactor/alpha is used
  15: optional i32 maxDynamicSharedBytes;
  // Specifies the buffer pool that should be used for this queue.
  // An option to force a queue to use a non-default buffer pool.
  16: optional string bufferPoolName;
}

struct DscpQosMap {
  1: i16 internalTrafficClass;
  2: list<byte> fromDscpToTrafficClass;
  3: optional byte fromTrafficClassToDscp;
}

struct ExpQosMap {
  1: i16 internalTrafficClass;
  2: list<byte> fromExpToTrafficClass;
  3: optional byte fromTrafficClassToExp;
}

struct PcpQosMap {
  1: i16 internalTrafficClass;
  2: list<byte> fromPcpToTrafficClass;
  3: optional byte fromTrafficClassToPcp;
}

struct QosMap {
  1: list<DscpQosMap> dscpMaps;
  2: list<ExpQosMap> expMaps;
  // TC to egress queue (EGQ) mapping
  3: map<i16, i16> trafficClassToQueueId;
  // for rx PFC, map the incoming PFC pkts
  // priority to the corresponding queueId
  4: optional map<i16, i16> pfcPriorityToQueueId;
  // used for pfc. Incoming traffic class is mapped
  // to given PG
  5: optional map<i16, i16> trafficClassToPgId;
  //  Maps PFC priority
  // to PG id. Used mainly for TX of PFC where
  // PG is mapped to outgoing PFC priority
  6: optional map<i16, i16> pfcPriorityToPgId;
  // TC to VOQ mapping, only used on VOQ platforms.
  // On J3 CPU/RCY ports: 8 VOQs map to WRR scheduler with only 2 EGQs.
  // So, different from trafficClassToQueueId that contains 2 EGQs.
  // On J3 NIF ports: same as trafficClassToQueueId, since
  // there is a 1:1 mapping between VOQ and EGQ. For cleanliness,
  // we still generate config for NIF ports on VOQ  platforms even
  // when they are the same as trafficClassToQueueId.
  7: optional map<i16, i16> trafficClassToVoqId;
  //  dot1q priority code point to traffic class
  8: optional list<PcpQosMap> pcpMaps;
  // Maps a traffic class to a CBFC virtual channel (UE Spec 1.0.2 section
  // 5.2). Receiver-side classification: decides which VC's credit counters an
  // arriving packet is charged against.
  9: optional map<i16, i16> trafficClassToVcId;
  // Maps an egress queue to a CBFC virtual channel (UE Spec 1.0.2 section
  // 5.2). Sender-side classification: decides whose credit must be held before
  // transmitting from that queue, and which queues are best-effort and so skip
  // the credit check entirely.
  10: optional map<i16, i16> queueToVcId;
}

struct QosRule {
  1: i16 queueId;
  2: list<i16> dscp = [];
}

struct QosPolicy {
  1: string name;
  2: list<QosRule> rules;
  3: optional QosMap qosMap;
}

struct TrafficPolicyConfig {
  // Order of entries determines priority of acls when applied
  1: list<MatchToAction> matchToAction = [];
  2: optional string defaultQosPolicy;
  3: optional map<i32, string> portIdToQosPolicy;
}

struct PacketRxReasonToQueue {
  1: PacketRxReason rxReason;
  2: i16 queueId;
}

struct CPUTrafficPolicyConfig {
  1: optional TrafficPolicyConfig trafficPolicy;
  // TODO(joseph5wu) Will be deprecated once we begin to use rxReasonToQueueList
  2: optional map<PacketRxReason, i16> rxReasonToCPUQueue;
  // Order of entries determines priority of reasons when applied
  3: optional list<PacketRxReasonToQueue> rxReasonToQueueOrderedList;
}

enum PacketRxReason {
  UNMATCHED = 0, // all packets for which there's not a explicit match rule
  ARP = 1,
  DHCP = 2,
  BPDU = 3,
  L3_SLOW_PATH = 4, // L3 slow path processed pkt
  L3_DEST_MISS = 5, // L3 DIP miss
  TTL_1 = 6, // L3UC or IPMC packet with TTL equal to 1
  CPU_IS_NHOP = 7, // The CPU port is the next-hop in the routing table
  NDP = 8, // NDP discovery messages
  LLDP = 9, // LLDP
  ARP_RESPONSE = 10, // ARP RESPONSE
  BGP = 11, // V4 BGP
  BGPV6 = 12, // V6 BGP
  LACP = 13, // LACP, Slow Protocol
  L3_MTU_ERROR = 14, // Packet size exceeds L3 MTU
  MPLS_TTL_1 = 15, // SAI only for MPLS packet with TTL is 1
  MPLS_UNKNOWN_LABEL = 16, // SAI only for MPLS packet with unprogrammed label
  DHCPV6 = 17, // DHCPv6
  SAMPLEPACKET = 18, // Sample Packet
  TTL_0 = 19, // Packets with TTL as 0
  EAPOL = 20, // EAPOL for Macsec
  PORT_MTU_ERROR = 21, // Packet size exceeds port MTU, should not use together with L3_MTU_ERROR
  HOST_MISS = 22, // Packet is destined for an unresolved neighbor in the subnet of a connected RIF
}

enum PortLoopbackMode {
  NONE = 0,
  PHY = 1,
  MAC = 2,
  NIF = 3,
}

enum LLDPTag {
  PDU_END = 0,
  CHASSIS = 1,
  PORT = 2,
  TTL = 3,
  PORT_DESC = 4,
  SYSTEM_NAME = 5,
  SYSTEM_DESC = 6,
}

// Packets sampled on a port can be sent to CPU or a Mirror Destination
enum SampleDestination {
  CPU = 0,
  MIRROR = 1,
}

const u64 CPU_PORT_LOGICALID = 0;

typedef string PortQueueConfigName

typedef string PortPgConfigName

typedef string BufferPoolConfigName

typedef string PortFlowletConfigName

typedef string LlrConfigName
typedef string CbfcConfigName

typedef string IsolationGroupName

typedef string FirmwareName

const i32 DEFAULT_PORT_MTU = 9412;

const string DEFAULT_INGRESS_ACL_TABLE_GROUP = "ingress-ACL-Table-Group";

const string DEFAULT_INGRESS_ACL_TABLE = "AclTable1";

const string DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE_GROUP = "post-lookup-ingress-ACL-Table-Group";

const string DEFAULT_POST_LOOKUP_INGRESS_ACL_TABLE = "PostLookupAclTable1";

const string DEFAULT_PBR_ACL_TABLE = "PbrAclTable";

enum PortType {
  INTERFACE_PORT = 0,
  FABRIC_PORT = 1,
  CPU_PORT = 2,
  RECYCLE_PORT = 3,
  MANAGEMENT_PORT = 4,
  EVENTOR_PORT = 5,
  HYPER_PORT = 6,
  HYPER_PORT_MEMBER = 7,
}

struct PortNeighbor {
  1: string remoteSystem;
  2: string remotePort;
}

enum PortDrainState {
  UNDRAINED = 0,
  DRAINED = 1,
}

/*
 * Who notices that a port's link status changed: the SDK's software
 * linkscan thread, or the ASIC itself.
 *
 * Unset leaves whatever the SDK came up with alone - FBOSS never
 * programs the mode. Either value is written down to the SDK.
 */
enum LinkScanMode {
  SOFTWARE = 1,
  HARDWARE = 2,
}

/*
 * What a port does with the CSIG (Congestion Signaling) tag on routed packets
 * egressing it. Values match sai_port_csig_tag_action_t.
 *
 * PASSTHROUGH: keep the tag unchanged (SDK default).
 * STRIP: remove the tag.
 * UPDATE: keep the tag and rewrite its signal with this port's measured
 *         available bandwidth, stamping the port's CSIG link locator.
 */
enum CsigTagAction {
  PASSTHROUGH = 0,
  STRIP = 1,
  UPDATE = 2,
}

/**
 * Configuration for a single logical port
 */
struct Port {
  1: i32 logicalID;
  /*
   * Whether the port is enabled.  Set this to ENABLED for normal operation.
   */
  2: PortState state = PortState.DISABLED;
  /*
   * Packets ingressing on this port will be dropped if they are smaller than
   * minFrameSize.  64 bytes is the minimum frame size allowed by ethernet.
   */
  3: i32 minFrameSize = 64;
  /*
   * Packets ingressing on this port will be dropped if they are larger than
   * maxFrameSize.  This should generally match the MTUs for the VLANs that
   * this port belongs to.
   */
  4: i32 maxFrameSize = DEFAULT_PORT_MTU;
  /*
   * This parameter controls how packets ingressing on this port will be
   * parsed.  If set to L2, only the L2 headers will be parsed.  You need to
   * set this to at least L3 to enable routing.  Set this to L4 if you need
   * features that examine TCP/UDP port numbers, such as ECMP.
   */
  5: ParserType parserType;
  /*
   * This parameter must be set to true for packets ingressing on this port to
   * be routed.  If this is set to False, ingress traffic will only be switched
   * at layer 2.
   */
  6: bool routable;
  /*
   * The VLAN ID for untagged packets ingressing on this port.
   */
  7: i32 ingressVlan;
  /**
   * The speed of the interface in mbps.
   * Value 0 means default setting based on the HW and port type.
   */
  8: PortSpeed speed = DEFAULT;

  /**
   * A configurable string describing the name of the port. If this
   * is not set, it will default to 'port-logicalID'
   */
  9: optional string name;

  /**
   * An optional configurable string describing the port.
   */
  10: optional string description;

  /**
   * Refer portQueueConfigName, which offers similar functionality without
   * having to repeat identical queue config across ports.
   */
  12: list<PortQueue> queues_DEPRECATED = [];

  /**
   * pause configuration
   */
  13: PortPause pause = NO_PAUSE;

  /**
   * sFlow sampling rate for ingressing packets.
   * Every 1/sFlowIngressRate ingressing packets will be sampled.
   * 0 indicates no sampling while 1 indicates sampling all packets.
   */
  14: i64 sFlowIngressRate = 0;

  /**
   * sFlow sampling rate for egressing packets.
   * Every 1/sFlowEgressRate egressing packets will be sampled.
   * 0 indicates no sampling while 1 indicates sampling all packets.
   */
  15: i64 sFlowEgressRate = 0;

  /*
   * Setup port in loopback mode
   */
  17: PortLoopbackMode loopbackMode = PortLoopbackMode.NONE;
  /**
   * if port is mirrored?
   */
  18: optional string ingressMirror;
  19: optional string egressMirror;

  /**
   * The LLDP values we expect to receive on this port, to validate
   * against the actual values received from the neighbour.
   *
   * Only LLDP tags with a string representation, or a clear transform to
   * a string (e.g. uint16_t) can be specified.
   *
   * For composite tags, such as Port/Chassis ID, only the string portion
   * is compared.
   */
  20: map<LLDPTag, string> expectedLLDPValues = {};

  /**
   * Specify the destination of sFlow sampling: CPU or Mirror.
   * If sampleDest is specified as MIRROR, sFlowEgressRate must be 0 and
   * egressMirror must be unset, because egress sampling to mirror
   * destinations is currently unsupported.
   */
  21: optional SampleDestination sampleDest;

  /*
   * Allows overriding default queue config on individual port.
   *
   * if portQueueConfigName is provided,
   *     port queue config = SwitchConfig::portQueueConfigs[portQueueConfigName]
   * else
   *     port queue config = SwitchConfig::defaultPortQueues
   */
  22: optional PortQueueConfigName portQueueConfigName;

  /*
   * If non-empty, host/route entry with this port as egress gets a ClassID from
   * below list. When the list is exhausted, wrap around.
   */
  23: list<AclLookupClass> lookupClasses = [];

  /*
   * The speed profile id for a port.
   * As FBOSS supports more and more speeds and there're a lot of config
   * combinations(like fec, lane_utilizations, medium) occured for those new
   * platforms. So we'll gradually deprecate the `speed` and `fec` fields from
   * Port struct and only use this `profileID` to program the port.
   * Besides, each platform can support different kinds of speed profiles, and
   * it should be defined in the `PlatformConfig`(platform_config.thrift)
   */
  24: PortProfileID profileID = PortProfileID.PROFILE_DEFAULT;

  25: optional PortPfc pfc;

  /*
   * Counter tags associated with this port, useful to support
   * pre-aggregated ODS counters
   */
  26: optional list<string> counterTags;
  /*
   * Port type to convey type for this port
   */
  27: PortType portType = PortType.INTERFACE_PORT;

  /*
   * List of neighbors reachable over this link
   * includes information on remote system and ports
   */
  28: list<PortNeighbor> expectedNeighborReachability = [];

  /*
   * Represents if this port is drained in DSF
   */
  29: PortDrainState drainState = PortDrainState.UNDRAINED;

  /*
   * PortFlowletConfigName to covey the flowlet config profile used for DLB
   */
  30: optional PortFlowletConfigName flowletConfigName;

  31: Scope scope = Scope.LOCAL;
  32: optional PortQueueConfigName portVoqConfigName;

  /*
   * DSF Interface node to enable conditional entropy, rotating hash seed periodically to increase entropy.
   */
  33: bool conditionalEntropyRehash = false;

  /*
   * DSF Interface node to enable SHEL messages - port UP/DOWN notification to other interface nodes.
   */
  34: optional bool selfHealingECMPLagEnable;

  /*
   * DSF option to enable FEC error detection on port to prevent any
   * errored cells from making it to the forwarding pipeline.
   */
  35: optional bool fecErrorDetectEnable;

  /*
   * Inter-packet gap configuration in bits.
   */
  36: optional i32 interPacketGapBits;

  /*
   * AM (Alignment Marker) idles configuration.
   * Controls whether AM idles are enabled on the port.
   */
  37: optional bool amIdles;

  /*
   * Cable Length Measurement (CLM) enable configuration.
   * Controls whether cable length measurement is enabled on the port.
   */
  38: optional bool clmEnable;

  /*
   * Link Training (IEEE 802.3 Cl.72/93/162) enable configuration.
   * Controls whether link training is enabled on the port.
   * When enabled, TX equalization is negotiated between link partners.
   * When not set, the agent uses ASIC-level default behavior (disabled).
   */
  39: optional bool linkTraining;

  /*
   * Hold timer (in milliseconds) before reporting a port
   * link-down event. The port stays in its previous state until the new
   * state has been stable for this duration, debouncing brief flaps.
   * Unset = leave SDK default untouched.
   */
  40: optional i32 portDownHoldoffTimeMs;

  /*
   * Hold timer (in milliseconds) before reporting a port
   * link-up event. The port stays in its previous state until the new
   * state has been stable for this duration, debouncing brief flaps.
   * Unset = leave SDK default untouched.
   */
  41: optional i32 portUpHoldoffTimeMs;
  // UEC Link Layer Retry: name of the LlrConfig profile to apply to this port.
  // Presence enables LLR on the port (UE Spec 1.0.2 section 5.1).
  42: optional LlrConfigName llrConfigName;

  // Controls whether TX precoding settings from the platform mapping are
  // applied to the port.
  43: optional bool txPrecoding;
  // Controls whether RX precoding settings from the platform mapping are
  // applied to the port.
  44: optional bool rxPrecoding;

  /*
   * Whether link status changes on this port are noticed by the SDK's
   * software linkscan thread or by the ASIC.
   * Unset = leave whatever the SDK came up with untouched.
  */
  45: optional LinkScanMode linkScanMode;

  // Ingress ACL table bound directly to this port.
  46: optional string ingressAclTableName;

  /* Lookup class assigned to packets ingressing on this port. */
  47: optional AclLookupClassPort userMetaData;
  // Names an entry in SwitchConfig.cbfcConfigs. Deliberately on Port rather
  // than nested inside PortPfc: CBFC and PFC are independent mechanisms that
  // may coexist (UE Spec 1.0.2 section 5.2.3).
  48: optional CbfcConfigName cbfcConfigName;
  // Traffic ingressing this port is not forwarded to the members of this
  // isolation group. Names a key of SwitchConfig.isolationGroups.
  49: optional IsolationGroupName isolationGroup;
  // CSIG tag action for routed packets egressing this port. The SDK enables
  // CSIG parsing (TPID 0x9900) at init on supporting ASICs.
  // Unset = leave SDK default (PASSTHROUGH) untouched.
  50: optional CsigTagAction csigTagAction;
  // CSIG link locator metadata (7 bits, 0-127) stamped into the tag by an
  // UPDATE port. Unset = leave SDK default untouched.
  51: optional i16 csigLinkLocator;
}

enum LacpPortRate {
  SLOW = 0,
  FAST = 1,
}

enum LacpPortActivity {
  PASSIVE = 0,
  ACTIVE = 1,
}

const i16 DEFAULT_LACP_HOLD_TIMER_MULTIPLIER = 3;

enum AggregatePortType {
  LAG_PORT = 0,
  HYPER_PORT = 1,
}

struct AggregatePortMember {
  /**
   * Member ports are identified according to their logicalID, as defined in
   * struct Port.
   */
  1: i32 memberPortID;
  2: i32 priority;
  3: LacpPortRate rate = FAST;
  4: LacpPortActivity activity = ACTIVE;
  5: i16 holdTimerMultiplier = DEFAULT_LACP_HOLD_TIMER_MULTIPLIER;
}

union MinimumCapacity {
  1: byte linkCount;
  2: double linkPercentage;
}

const MinimumCapacity ALL_LINKS = {"linkPercentage": 1};

struct AggregatePort {
  1: i16 key;
  2: string name;
  3: string description;
  4: list<AggregatePortMember> memberPorts;
  5: MinimumCapacity minimumCapacity = ALL_LINKS;

  /*
   * Counter tags associated with this port, useful to support
   * pre-aggregated ODS counters
   */
  6: optional list<string> counterTags;
  7: AggregatePortType aggregatePortType = LAG_PORT;
  /*
   * If minimumCapacityToUp is provided, it will be used with minimumCapacity to
   * provide a flap-resistant way to decide AggregatePort Up/Down.
   * a. If active link count < minimumCapacity, bring AggregatePort down.
   * b. If active link count >= minimumCapacityToUp, bring AggregatePort up.
   * c. If minimumCapacity <= active link count < minimumCapacityToUp,
   * keep AggregatePort current oper state.
   */
  8: optional MinimumCapacity minimumCapacityToUp;
  /*
   * Extended key field to support i32 values for LAG identification.
   *
   * The i16 key field limits LAG identification to 32,767, which is insufficient
   * for RBB (Rack-Based Backbone) use cases that require larger LAG key values.
   * This i32 field allows values beyond the i16 range while maintaining backward
   * compatibility - when extendedKey is set, it takes precedence over the i16 key.
   *
   * Note: FBOSS internally retains the full 32-bit value for LAG identification.
   * However, the LACP protocol spec only supports 16-bit key fields in LACP PDUs,
   * so the key is truncated to the lower 16 bits when used in LACP packets.
   * Therefore, LAG keys must have unique lower 16 bits to avoid LACP conflicts.
   */
  9: i32 extendedKey = 0;
}

struct Lacp {
  1: string systemID; // = cpuMAC
  2: i32 systemPriority; // = (2 ^ 16) - 1
}

/**
 * Configuration for a VLAN.
 *
 * For simplicity, we use a single list of VLANs for the entire controller
 * process.  The FocalPoint API has an independent list of VLANs for each
 * switch.  The controller will create each VLAN on each configured switch.
 */
struct Vlan {
  1: string name;
  2: i32 id;
  /**
   * Whether or not to enable stats collection for this VLAN in hardware.
   * (The hardware is only able to collect stats on a limited number of VLANs.)
   */
  3: bool recordStats = 1;
  /**
   * Whether packets ingressing on this VLAN should be routed.
   * When this is false, ingressing traffic will only be switched at layer 2.
   *
   * Note that you must also set the routable attribute to true on each port in
   * the vlan.
   */
  5: bool routable;
  /**
   * IP addresses associated with this interface.
   *
   * IP addresses are only useful if the routable flag is set to true for this
   * VLAN.
   */
  6: list<string> ipAddresses;
  /* V4 DHCP relay address */
  7: optional string dhcpRelayAddressV4;
  /* V6 DHCP relay address */
  8: optional string dhcpRelayAddressV6;

  /* Override DHCPv4/6 relayer on a per host basis */
  9: optional map<string, string> dhcpRelayOverridesV4;
  10: optional map<string, string> dhcpRelayOverridesV6;

  /* Interface ID associated with the vlan */
  11: optional i32 intfID;
}

/**
 * A VLAN <--> Port mapping.
 *
 * The specified logical port will be added to the specified VLAN.
 * The VlanPort structure is separate from Port since a single Port may belong
 * to multiple VLANs.
 */
struct VlanPort {
  1: i32 vlanID;
  2: i32 logicalPort;
  3: SpanningTreeState spanningTreeState;
  /**
   * Whether or not to emit the VLAN tag in outgoing ethernet frames.
   * (Primarily useful when there are multiple VLANs on the same port.)
   */
  4: bool emitTags;
  // whether to emit priority tags with vlan id 0 outgoing ethernet frames
  5: bool emitPriorityTags;
}

/**
 * IPv6 NDP configuration settings for an interface
 */
struct NdpConfig {
  /**
   * How often to send out router advertisements, in seconds.
   *
   * If this is 0 or negative, no router advertisements will be sent.
   */
  1: i32 routerAdvertisementSeconds = 0;
  /**
   * The recommended hop limit that hosts should put in outgoing IPv6 packets.
   *
   * This value may be between 0 and 255, inclusive.
   * 0 means unspecified.
   */
  2: i32 curHopLimit = 255;
  /**
   * The router lifetime to send in advertisement messasges.
   *
   * This should generally be larger than the router advertisement interval.
   */
  3: i32 routerLifetime = 1800;
  /**
   * The lifetime to advertise for IPv6 prefixes on this interface.
   */
  4: i32 prefixValidLifetimeSeconds = 2592000;
  /**
   * The preferred lifetime to advertise for IPv6 prefixes on this interface.
   *
   * This must be smaller than prefixValidLifetimeSeconds
   */
  5: i32 prefixPreferredLifetimeSeconds = 604800;
  /**
  * Managed bit setting for router advertisements
  * When set, it indicates that addresses are available via
  * Dynamic Host Configuration Protocol
  */
  6: bool routerAdvertisementManagedBit = 0;
  /**
  * Other bit setting for router advertisements
  * When set, it indicates that other configuration information is
  * available via DHCPv6.  Examples of such information
  * are DNS-related information or information on other
  * servers within the network.
  */
  7: bool routerAdvertisementOtherBit = 0;
  /*
   * IP Address to use in router advertisement.
   * This must be one of the Interface IP addresses.
   * In absence of this config, implementation can
   * choose any of the interface addresses as Router
   * address.
   */
  8: optional string routerAddress;
}
enum InterfaceType {
  VLAN = 1,
  SYSTEM_PORT = 2,
  PORT = 3,
}

enum AsicType {
  ASIC_TYPE_FAKE = 1,
  ASIC_TYPE_MOCK = 2,
  ASIC_TYPE_TRIDENT2 = 3,
  ASIC_TYPE_TOMAHAWK = 4,
  ASIC_TYPE_TOMAHAWK3 = 5,
  ASIC_TYPE_TOMAHAWK4 = 6,
  ASIC_TYPE_ELBERT_8DD = 7,
  ASIC_TYPE_EBRO = 8,
  ASIC_TYPE_GARONNE = 9,
  ASIC_TYPE_SANDIA_PHY = 10,
  ASIC_TYPE_JERICHO2 = 11,
  ASIC_TYPE_RAMON = 12,
  ASIC_TYPE_TOMAHAWK5 = 13,
  ASIC_TYPE_JERICHO3 = 14,
  ASIC_TYPE_YUBA = 15,
  ASIC_TYPE_RAMON3 = 16,
  ASIC_TYPE_CHENAB = 17,
  ASIC_TYPE_TOMAHAWK6 = 18,
  ASIC_TYPE_AGERA3 = 19,
  ASIC_TYPE_G202X = 20,
  ASIC_TYPE_FAKE_NO_WARMBOOT = 21,
  ASIC_TYPE_TOMAHAWKULTRA1 = 22,
  ASIC_TYPE_QUMRAN4D = 23,
  ASIC_TYPE_JERICHO4 = 24,
  ASIC_TYPE_CHENAB2 = 25,
  ASIC_TYPE_P200 = 26,
}
struct VrfConfig {
  1: i32 vrfId;
  2: optional string name;
}

/**
 * The configuration for an interface
 */
struct Interface {
  1: i32 intfID;
  /** The VRF where the interface belongs to */
  2: i32 routerID = 0;
  /**
   * The VLAN where the interface sits. Valid only
   * for interface of type VLAN. When present
   * there shall be maximum one interface per VLAN
   */
  3: i32 vlanID;
  /** The description of the interface */
  4: optional string name;
  /**
   * The MAC address to be used by this interface.
   * If it is not specified here, it is up to the platform
   * to pick up a MAC for it.
   */
  5: optional string mac;
  /**
   * IP addresses with netmask associated with this interface
   * in the format like: 10.0.0.0/8
   */
  6: list<string> ipAddresses;
  /**
   * IPv6 NDP configuration
   */
  7: optional NdpConfig ndp;

  /**
   * MTU size
   */
  8: optional i32 mtu;
  /**
   * is_virtual is set to true for logical interfaces
   * (e.g. loopbacks). No ports are thus associated with
   * such a interface and the interface is expected to
   * always be up.
   */
  9: bool isVirtual = 0;
  /**
  * this flag is set to true if we need to
  * disable auto-state feature for interface.
  */
  10: bool isStateSyncDisabled = 0;

  11: InterfaceType type = InterfaceType.VLAN;

  /* V4 DHCP relay address */
  12: optional string dhcpRelayAddressV4;
  /* V6 DHCP relay address */
  13: optional string dhcpRelayAddressV6;

  /* Override DHCPv4/6 relayer on a per host basis */
  14: optional map<string, string> dhcpRelayOverridesV4;
  15: optional map<string, string> dhcpRelayOverridesV6;
  16: Scope scope = Scope.LOCAL;

  /* valid only for port type of interface */
  17: optional i32 portID;
  /* valid only for port type of interface
   * These fields contains information of remote GPU */
  18: optional string desiredPeerName;
  19: optional string desiredPeerAddressIPv6;
  /* valid only for port type of interface. Holds the key of the
   * cfg::AggregatePort this interface is bound to, for a port router interface
   * over an aggregate port. Exactly one of portID and aggregatePortID is set.
   * Kept separate from portID because AggregatePortID and PortID share a
   * numeric space, so a single field could not be disambiguated. */
  20: optional i32 aggregatePortID;
}

struct StaticRouteWithNextHops {
  /** The VRF to which the static route belongs to */
  1: i32 routerID = 0;
  /* Prefix in the format like 10.0.0.0/8 */
  2: string prefix;
  /* IP Address next hops like 1.1.1.1 */
  3: list<string> nexthops;
}

struct StaticRouteNoNextHops {
  /** The VRF where the static route belongs to */
  1: i32 routerID = 0;
  /* Prefix in the format like 10.0.0.0/8 */
  2: string prefix;
}

struct StaticIp2MplsRoute {
  /** The VRF where the static route belongs to */
  1: i32 routerID = 0;
  /* Prefix in the format like 10.0.0.0/8 */
  2: string prefix;
  /* mpls next hops with push labels */
  3: list<common.NextHopThrift> nexthops;
}

struct StaticMplsRouteWithNextHops {
  /* Look up MPLS packet based on label */
  1: mpls.MplsLabel ingressLabel;
  /* forward MPLS packet to nexthops */
  2: list<common.NextHopThrift> nexthops;
}

struct StaticMplsRouteNoNextHops {
  1: mpls.MplsLabel ingressLabel;
}

struct SflowCollector {
  1: string ip;
  2: i16 port;
}

enum LoadBalancerID {
  ECMP = 1,
  AGGREGATE_PORT = 2,
}

enum IPv4Field {
  SOURCE_ADDRESS = 1,
  DESTINATION_ADDRESS = 2,
}

enum IPv6Field {
  SOURCE_ADDRESS = 1,
  DESTINATION_ADDRESS = 2,
  FLOW_LABEL = 3,
}

enum TransportField {
  SOURCE_PORT = 1,
  DESTINATION_PORT = 2,
}

enum MPLSField {
  TOP_LABEL = 1,
  SECOND_LABEL = 2,
  THIRD_LABEL = 3,
}

struct Fields {
  1: set<IPv4Field> ipv4Fields;
  2: set<IPv6Field> ipv6Fields;
  3: set<TransportField> transportFields;
  4: set<MPLSField> mplsFields;
  5: list<string> udfGroups = [];
}

enum HashingAlgorithm {
  CRC16_CCITT = 1,

  CRC32_LO = 3,
  CRC32_HI = 4,

  CRC32_ETHERNET_LO = 5,
  CRC32_ETHERNET_HI = 6,

  CRC32_KOOPMAN_LO = 7,
  CRC32_KOOPMAN_HI = 8,
  CRC = 9,
}

struct LoadBalancer {
  1: LoadBalancerID id;
  2: Fields fieldSelection;
  3: HashingAlgorithm algorithm;
  // If not set, the seed will be chosen so as to remain constant across process
  // restarts
  4: optional i32 seed;
}

enum CounterType {
  PACKETS = 0,
  BYTES = 1,
}

struct TrafficCounter {
  1: string name;
  2: list<CounterType> types = [PACKETS];
}

enum L2LearningMode {
  HARDWARE = 0,
  SOFTWARE = 1,
  DISABLED = 2,
}

enum SwitchDrainState {
  UNDRAINED = 0,
  DRAINED = 1,
  DRAINED_DUE_TO_ASIC_ERROR = 2,
}

/*
 * Used for QCM. Weight factors used to compute interest
 * function to evaluate flows which  are source of congestion
 */
enum BurstMonitorWeight {
  FLOW_MAX_RX_BYTES = 0,
  FLOW_SUM_RX_BYTES = 1,
  QUEUE_MAX_BUF_BYTES = 2,
  QUEUE_MAX_SUM_BYTES = 3,
  QUEUE_MAX_DROP_BYTES = 4,
  QUEUE_SUM_DROP_BYTES = 5,
  QUEUE_MAX_RX_BYTES = 6,
  QUEUE_SUM_RX_BYTES = 7,
  MAX_TM_UTIL = 8,
  AVG_TM_UTIL = 9,
  RANDOM_NUM = 10,
  VIEW_ID = 11,
  MAX_VIEW_ID = 12,
  VIEW_ID_THRESHOLD = 13,
  FLOW_MAX_RX_PKTS = 14,
  FLOW_SUM_RX_PKTS = 15,
  QUEUE_MAX_DROP_PKTS = 16,
  QUEUE_SUM_DROP_PKTS = 17,
  QUEUE_MAX_RX_PKTS = 18,
  QUEUE_SUM_RX_PKTS = 19,
  QUEUE_MAX_TX_BYTES = 20,
  QUEUE_SUM_TX_BYTES = 21,
  QUEUE_MAX_TX_PKTS = 22,
  QUEUE_SUM_TX_PKTS = 23,
}

const i16 DEFAULT_QCM_COLLECTOR_DST_PORT = 30000;
const i32 DEFAULT_QCM_AGING_INTERVAL_MSECS = 50;
const i32 DEFAULT_QCM_EXPORT_THRESHOLD = 1;
const i32 DEFAULT_QCM_SCAN_INTERVAL_USECS = 1000;
const i32 DEFAULT_QCM_NUM_FLOWS_TO_CLEAR = 100;
const i32 DEFAULT_QCM_FLOW_LIMIT = 1000;
const i32 DEFAULT_QCM_FLOWS_PER_VIEW = 16;

/* allow configuration of qcm functionality */
struct QcmConfig {
  1: i32 numFlowSamplesPerView = DEFAULT_QCM_FLOWS_PER_VIEW;
  2: i32 flowLimit = DEFAULT_QCM_FLOW_LIMIT;
  3: i32 numFlowsClear = DEFAULT_QCM_NUM_FLOWS_TO_CLEAR;
  4: i32 scanIntervalInUsecs = DEFAULT_QCM_SCAN_INTERVAL_USECS;
  5: i32 exportThreshold = DEFAULT_QCM_EXPORT_THRESHOLD;
  6: map<BurstMonitorWeight, i16> flowWeights = {
    BurstMonitorWeight.RANDOM_NUM: 1,
  };
  7: i32 agingIntervalInMsecs = DEFAULT_QCM_AGING_INTERVAL_MSECS;
  8: string collectorDstIp;
  10: i16 collectorSrcPort;
  // Arbitrary high number to keep the dstPort unique
  11: i16 collectorDstPort = DEFAULT_QCM_COLLECTOR_DST_PORT;
  12: optional i16 collectorDscp;
  13: optional i32 ppsToQcm;
  14: string collectorSrcIp;
  15: list<i32> monitorQcmPortList = [];
  16: map<i32, list<i32>> port2QosQueueIds;
  17: bool monitorQcmCfgPortsOnly = false;
}

struct Neighbor {
  1: i32 vlanID;
  2: string ipAddress;
}

struct MacAndVlan {
  1: i32 vlanID;
  2: string macAddress;
}

enum SwitchType {
  NPU = 0,
  PHY = 1,
  VOQ = 2,
  FABRIC = 3,
}

struct ExactMatchTableConfig {
  1: string name;
  2: optional i32 dstPrefixLength;
}

const i16 DEFAULT_FLOWLET_TABLE_SIZE = 4096;
const i64 DEFAULT_PORT_ID_RANGE_MIN = 0;
const i64 DEFAULT_PORT_ID_RANGE_MAX = 2047;
const i64 DEFAULT_DUAL_STAGE_3Q_2Q_PORT_ID_RANGE_MIN = 0;
const i64 DEFAULT_DUAL_STAGE_3Q_2Q_PORT_ID_RANGE_MAX = 65536;

struct SystemPortRanges {
  1: list<Range64> systemPortRanges;
}

enum FirmwareLoadType {
  FIRMWARE_LOAD_TYPE_START = 0,
  FIRMWARE_LOAD_TYPE_STOP = 1,
}

struct FirmwareInfo {
  1: i32 coreToUse;
  2: string path;
  3: string logPath;
  4: FirmwareLoadType firmwareLoadType;
}

struct SelfHealingEcmpLagConfig {
  1: string shelSrcIp;
  2: string shelDstIp;
  3: i32 shelPeriodicIntervalMS;
}

struct SwitchInfo {
  1: SwitchType switchType;
  2: AsicType asicType;
  // local switch identifier
  3: i16 switchIndex;
  4: Range64 portIdRange;
  5: optional Range64 systemPortRange_DEPRECATED;
  6: optional string switchMac;
  7: optional string connectionHandle;
  8: SystemPortRanges systemPortRanges;
  // Offset from where to start local system port
  // ID allocation from
  9: optional i32 localSystemPortOffset;
  // Offset from where to start global system port
  // ID allocation from
  10: optional i32 globalSystemPortOffset;
  // Inband port ID - port used by this DSF node
  // for inband communication. This must be known
  // as part of config for other nodes to bootstrap
  // communication to this node
  11: optional i32 inbandPortId;
  12: map<FirmwareName, FirmwareInfo> firmwareNameToFirmwareInfo;

  /*
   * VOQ switch may use these thresholds as below:
   *  - During init, create switch device Isolated.
   *  - When numActiveLinks > minLinksToJoinVOQDomain => Unisolate device.
   *  - When numActiveLinks < minLinksToRemainInVOQDomain => Isolate device.
   *
   * In practice, these thresholds will be configured to provide hysteresis:
   *  - 0 < minLinksToRemainInVOQDomain < minLinksToJoinVOQDomain < maxActiveLinks
   *  - numActiveLinks in [0, minLinksToRemainInVOQDomain) => device isolated.
   *  - numActiveLinks in (minLinksToJoinVOQDomain, maxActiveLinks] => device unisolated
   *  - numActiveLinks in [minLinksToRemainInVOQDomain, minLinksToJoinVOQDomain]
   *    => Whether or not the device is isolated depends on how we got to this state.
   *    => For example, during init, as links gradually turn active, the device
   *       will be isolated for these numActiveLinks as minLinksToJoinVOQDomain is not
   *       yet hit.
   *    => On the other hand, if the links are active but start turning inactive,
   *       the device will be unisolated for these numActiveLinks since
   *       minLinksToRemainInVOQDomain is not yet thit.
   *
   * TODO: This will be enhanced to work for Fabric switches as well.
   */
  13: optional i32 minLinksPerDeviceToRemainInVOQDomain;
  14: optional i32 minLinksPerDeviceToJoinVOQDomain;
  15: SystemPortRanges localSystemPortRanges;

  // L3 interface ID of this switch's portless (virtual) loopback interface.
  // A portless interface has no member ports, so its owning ASIC cannot be
  // inferred; naming it here binds it to this switch.
  16: optional i32 loopbackIntfId;
}

/*
 * Packet forwarding mode for the switch ASIC pipeline.
 * Controls whether the switch waits for the entire packet before
 * forwarding (store-and-forward) or begins forwarding after reading
 * just the header (cut-through).
 */
enum PacketForwardingMode {
  CUT_THROUGH = 0,
  STORE_AND_FORWARD = 1,
}

/*
 * Switch specific settings: global to the switch
 */
/**
 * Which kind of ECMP group a setting applies to.
 *
 * These are FBOSS level categories, not SAI group types. ARS,
 * ECMP_FIXED_ASSIGNMENT and ECMP_SPRAY are all SAI_NEXT_HOP_GROUP_TYPE_ECMP;
 * they are told apart by whether an ARS object is attached and by the group's
 * switching mode. Where a group could match more than one category, the group
 * type wins: an FRR backup is programmed with random spray, but it is
 * FRR_BACKUP, not ECMP_SPRAY.
 */
enum EcmpGroupType {
  // Plain ECMP with an ARS (DLB) object attached.
  ARS = 0,
  // FRR protection parent (SAI_NEXT_HOP_GROUP_TYPE_PROTECTION).
  FRR_PRIMARY = 1,
  // FRR protection backup (SAI_NEXT_HOP_GROUP_TYPE_HW_PROTECTION).
  FRR_BACKUP = 2,
  // Plain ECMP, fixed assignment switching mode.
  ECMP_FIXED_ASSIGNMENT = 3,
  // Plain ECMP, per packet random spray.
  ECMP_SPRAY = 4,
}

struct EcmpGroupSettings {
  /**
   * Stops a group from egressing a packet on the port it arrived on. On an FRR
   * protection parent this arms source port based failover to the backup group;
   * on the backup it enables tertiary member selection; on an ARS group it
   * prunes the source port member within the group.
   */
  1: bool enableSplitHorizon = false;
}

struct SwitchSettings {
  1: L2LearningMode l2LearningMode = L2LearningMode.HARDWARE;
  2: bool qcmEnable = false;
  3: bool ptpTcEnable = false;

  /**
   * Set L2 Aging to 5 mins by default, same as Arista -
   * https://www.arista.com/en/um-eos/eos-section-19-3-mac-address-table
   *
   * Time to transition L2 from hit -> miss -> removed
   */
  4: i32 l2AgeTimerSeconds = 300;
  5: i32 maxRouteCounterIDs_DEPRECATED = 0;

  // neighbors to block egress traffic to
  6: list<Neighbor> blockNeighbors = [];

  // MAC addrs to block the egress traffic to
  7: list<MacAndVlan> macAddrsToBlock = [];
  // Switch type
  8: SwitchType switchType = SwitchType.NPU;
  // Switch id (only applicable for VOQ based systems)
  9: optional i64 switchId;
  10: list<ExactMatchTableConfig> exactMatchTableConfigs = [];
  11: map<i64, SwitchType> switchIdToSwitchType_DEPRECATED;
  12: SwitchDrainState switchDrainState = SwitchDrainState.UNDRAINED;
  13: map<i64, SwitchInfo> switchIdToSwitchInfo;

  /*
   * VOQ switch may use these thresholds as below:
   *  - During init, create switch device Isolated.
   *  - When numActiveLinks > minLinksToJoinVOQDomain => Unisolate device.
   *  - When numActiveLinks < minLinksToRemainInVOQDomain => Isolate device.
   *
   * In practice, these thresholds will be configured to provide hysteresis:
   *  - 0 < minLinksToRemainInVOQDomain < minLinksToJoinVOQDomain < maxActiveLinks
   *  - numActiveLinks in [0, minLinksToRemainInVOQDomain) => device isolated.
   *  - numActiveLinks in (minLinksToJoinVOQDomain, maxActiveLinks] => device unisolated
   *  - numActiveLinks in [minLinksToRemainInVOQDomain, minLinksToJoinVOQDomain]
   *    => Whether or not the device is isolated depends on how we got to this state.
   *    => For example, during init, as links gradually turn active, the device
   *       will be isolated for these numActiveLinks as minLinksToJoinVOQDomain is not
   *       yet hit.
   *    => On the other hand, if the links are active but start turning inactive,
   *       the device will be unisolated for these numActiveLinks since
   *       minLinksToRemainInVOQDomain is not yet thit.
   */
  14: optional i32 minLinksToRemainInVOQDomain;
  15: optional i32 minLinksToJoinVOQDomain;
  // MAC OUIs (24-bit MAC address prefix) used by vendor NICs.
  // When queue-per-host is enabled, MACs matching any OUI from this list will get a dedicated queue.
  16: list<string> vendorMacOuis = [];
  // MAC OUIs used by meta for VM purpose.
  // When queue-per-host is enabled, MACs matching any OUI from this list could get any queue.
  17: list<string> metaMacOuis = [];
  18: optional bool needL2EntryForNeighbor;
  // Once the SRAM free buffers fall below this threshold,
  // specified as a percent of total SRAM buffers, send XOFF.
  19: optional byte sramGlobalFreePercentXoffThreshold;
  // Once the SRAM free buffers goes above this threshold,
  // specified as a percent of total SRAM buffers, send XON.
  20: optional byte sramGlobalFreePercentXonThreshold;
  // Fabric side threshold tracking the minimum needed
  // fifo free space on the peer device fifo.
  21: optional i16 linkFlowControlCreditThreshold;
  // SRAM2DRAM threshold on VOQ. Single parameter as of now
  // controlling both bounds and recovery thresholds.
  22: optional i32 voqDramBoundThreshold;
  // Conditional Entropy Rehash Period for VOQ devices
  23: optional i32 conditionalEntropyRehashPeriodUS;
  24: optional string firmwarePath;
  // SHEL attributes to configure 1. SHEL message SrcIP, 2. DstIp, and 3. Interval for SHEL periodic messages
  25: optional SelfHealingEcmpLagConfig selfHealingEcmpLagConfig;
  // Specify the maximum expected latency for local, remote l1,
  // remote l2 VOQs. Any latency exceeding the specified latency
  // will be flagged in the VoQ latency watermark counters with
  // the out of bounds latency value configured.
  26: optional i32 localVoqMaxExpectedLatencyNsec;
  27: optional i32 remoteL1VoqMaxExpectedLatencyNsec;
  28: optional i32 remoteL2VoqMaxExpectedLatencyNsec;
  29: optional i32 voqOutOfBoundsLatencyNsec;
  // Number of sflow samples to pack in a single packet being sent out
  30: optional byte numberOfSflowSamplesPerPacket;
  31: optional map<i32, i32> tcToRateLimitKbps;
  // PFC watchdog timer granularity which can be 1ms, 10ms or 100ms.
  32: optional i32 pfcWatchdogTimerGranularityMsec;
  33: optional i32 ecmpCompressionThresholdPct;
  // Offset from where to start connected system port ID allocation from.
  // Connected system ports are for fabric links and used in fabric link
  // monitoring feature.
  34: optional i32 fabricLinkMonitoringSystemPortOffset;
  35: optional bool measureCableLengths;
  36: optional PacketForwardingMode packetForwardingMode;
  // Max ECMP width; also implies the UCMP normalization factor. Config-sourced
  // replacement for FLAGS_ecmp_width. Changing it requires a coldboot.
  37: optional i32 ecmpWidth;
  /**
   * Split horizon, and any future per group knobs, keyed by group type. Lets a
   * deployment enable it for some group types and not others -- ARS but not
   * FRR, or the reverse.
   *
   * An empty map means nobody is using the feature, and is the default. A
   * missing key in a non-empty map means the feature is off for that group
   * type. The agent still programs the attribute explicitly on FRR groups in
   * that case, because the vendor SDK defaults it to TRUE on an FRR primary
   * when the attribute is omitted, and inheriting that default blackholes
   * traffic.
   *
   * FRR_PRIMARY and FRR_BACKUP must be set to the same value. Split horizon on
   * the parent alone suppresses the source port with no tertiary path on the
   * backup to catch the traffic.
   *
   * Split horizon is CREATE_ONLY on a next hop group, so changing an FRR entry
   * only affects groups created afterwards. The ARS entry is settable and takes
   * effect on existing groups.
   */
  38: map<EcmpGroupType, EcmpGroupSettings> ecmpGroupSettings;
  // Additional router MACs. Packets to these MACs on the given VLAN are routed,
  // in addition to packets to the interface MAC.
  39: list<MacAndVlan> myMacs = [];
}

// Global buffer pool
//  (1) shared by {port, pgs} at ingress
//  (2) shared by {port, queues} at egress
struct BufferPoolConfig {
  1: i32 sharedBytes;
  2: optional i32 headroomBytes;
  3: optional i32 reservedBytes;
}

// max PG/port supported
const i16 PORT_PG_VALUE_MAX = 7;
const i16 PFC_PRIORITY_VALUE_MAX = 7;

// max CBFC virtual channel index per port (UE Spec 1.0.2 section 5.2.3,
// SAI_VIRTUAL_CHANNEL_ATTR_INDEX range 0-31)
const i16 PORT_VC_VALUE_MAX = 31;

// max CBFC sender port credit limit, S_P_CL (UE Spec 1.0.2 Table 5-27,
// same range on SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT)
const i64 PORT_CBFC_SENDER_CREDIT_LIMIT_MAX = 1048575;

// Configuration for one CBFC virtual channel on a port (UE Spec 1.0.2
// section 5.2). A VC is not a buffer: it is the per-link, per-channel credit
// relationship with the peer. Lossless delivery comes from the sender holding
// credit before it transmits, so unlike a PortPgConfig there is no headroom,
// no resume offset and no watchdog.
struct PortVcConfig {
  // Virtual channel index, 0..PORT_VC_VALUE_MAX.
  1: i16 id;
  2: optional string name;
  // Enable credit-gated transmission on this VC (CBFC_SENDER_ENABLE). Set
  // per VC, so one port can carry both lossless and best-effort VCs.
  3: bool senderEnable = false;
  // Enable credit accounting for traffic arriving on this VC
  // (CBFC_RECEIVER_ENABLE).
  4: bool receiverEnable = false;
  // Credits guaranteed to this VC, the analogue of PortPgConfig.minLimitBytes.
  // This is a floor, not an allocation: it lands on the SDK's VC_MIN_LIMIT and
  // does not cap the VC. The ceiling is CbfcConfig.senderCreditLimit, shared by
  // every VC on the port.
  //
  // There is deliberately no shared-threshold field. SAI defines
  // THRESHOLD_MODE / SHARED_{DYNAMIC,STATIC}_TH on the credit profile, the
  // analogue of PortPgConfig.scalingFactor, but brcm-sai 16.0_ea_odp rejects
  // all three.
  5: optional i64 reservedCreditSize;
}

// CBFC configuration for a set of ports, named by Port.cbfcConfigName.
struct CbfcConfig {
  1: list<PortVcConfig> virtualChannels;
  // Total credits the port may have outstanding across all its VCs
  // (SAI_PORT_ATTR_CBFC_SENDER_CREDIT_LIMIT). Port-scoped rather than per-VC,
  // and the closest analogue of the ingress buffer pool shared by every PG.
  // Optional: the SDK treats an unset limit as no port ceiling configured.
  2: optional i64 senderCreditLimit;
  // Bytes per credit the sender charges (SAI_PORT_ATTR_CBFC_SENDER_CREDIT_SIZE).
  // Must match the peer's receiver, or the two ends count different credits for
  // the same packet and the link stalls. Required on a port that uses this
  // config. Against another TU1, use 56: the TU1 receiver's credit size is
  // hardcoded in the SDK and is not SAI writable.
  3: optional i16 senderCreditSize;
  // Per-packet overhead in bytes the sender charges
  // (SAI_PORT_ATTR_CBFC_SENDER_PACKET_OVERHEAD). Same rules as
  // senderCreditSize; against another TU1, use 16.
  4: optional i16 senderPacketOverhead;
}

// Defines PG (priority group) configuration for ports
// This configuration defines the PG buffer settings for given port(s)
struct PortPgConfig {
  1: i16 id;
  2: optional string name;
  3: optional MMUScalingFactor scalingFactor;
  // Min buffer available to each PG, port
  4: i32 minLimitBytes;
  // Buffer available after XOFF is hit (i.e. after
  // PFC is triggerred). Intent of this buffer is to
  // gurantee lossless operation by absorbing in-flight
  // packets
  5: optional i32 headroomLimitBytes;
  // Offset from XOFF before allowing XON
  // resumeOffsetBytes and resumeBytes should not be set at the same time.
  6: optional i32 resumeOffsetBytes;
  // global buffer pool as used by this PG
  7: string bufferPoolName;
  // Shared buffer min/max threshold range at which to trigger PFC
  8: optional i64 maxSharedXoffThresholdBytes;
  9: optional i64 minSharedXoffThresholdBytes;
  // SRAM/OCB buffer min/max threshold range at which to trigger PFC
  10: optional i64 maxSramXoffThresholdBytes;
  11: optional i64 minSramXoffThresholdBytes;
  // Offset from XOFF in SRAM before allowing XON
  12: optional i64 sramResumeOffsetBytes;
  // Not all implementations support specifying an offset at which to send XON.
  // Allowing configuring an absolute value at which to send XON in such cases.
  // resumeOffsetBytes and resumeBytes should not be set at the same time.
  13: optional i32 resumeBytes;
  // Scaling factor for SRAM usage
  14: optional MMUScalingFactor sramScalingFactor;
  // Static shared buffer threshold. If this is set, scalingFactor is ignored.
  15: optional i64 staticLimitBytes;
}

// asicSdk: Native SDK version. may or may not support SAI
// saiSdk: Set to SAI SDK version on SAI device if asicSdk does not support SAI
// firmware: Set to firmware version if a device uses firmware
//
// asicSdk and saiSdk are consumed by the wrapper script.
// firmware version will be consumed by Agent.
struct SdkVersion {
  1: optional string asicSdk;
  2: optional string saiSdk;
  3: optional string firmware;
}

enum TunnelMode {
  UNIFORM = 0,
  PIPE = 1,
  USER = 2,
}

enum TunnelTerminationType {
  P2P = 1,
  P2MP = 2,
  MP2P = 3,
  MP2MP = 4,
}

struct IpInIpTunnel {
  1: string ipInIpTunnelId;
  2: i32 underlayIntfID;
  3: string dstIp;
  4: optional string srcIp;
  5: optional string dstIpMask;
  6: optional string srcIpMask;
  7: optional TunnelMode ttlMode;
  8: optional TunnelMode dscpMode;
  9: optional TunnelMode ecnMode;
  10: optional TunnelTerminationType tunnelTermType;
  11: optional common.TunnelType tunnelType;
}

struct Srv6Tunnel {
  1: string srv6TunnelId;
  2: i32 underlayIntfID;
  3: optional string srcIp;
  4: optional string dstIp;
  5: optional TunnelMode ttlMode;
  6: optional TunnelMode dscpMode;
  7: optional TunnelMode ecnMode;
  8: optional TunnelTerminationType tunnelTermType;
  9: common.TunnelType tunnelType;
}

struct VxLanTunnel {
  1: string vxlanTunnelId;
  2: i32 underlayIntfID;
  3: optional string srcIp;
  4: optional string dstIp;
  5: optional TunnelMode ttlMode;
  6: optional TunnelMode dscpMode;
  7: optional TunnelMode ecnMode;
  8: optional TunnelTerminationType tunnelTermType;
  9: common.TunnelType tunnelType;
  10: optional map<i64, i64> vrf2Vni;
  11: optional map<i64, i64> vniToVrf;
}
// MySID (My SRv6 SID) configuration types.
// MySID entries define how the switch handles packets matching its SRv6 SIDs.

// uA (adjacency) MySID: forward to the neighbor on a port.
// portName is either a physical port (e.g., "eth1/1/1") or a
// port-channel (e.g., "Port-Channel301").
struct AdjacencyMySidConfig {
  1: string portName;
  // Whether to resolve IPv6 (true) or IPv4 (false) neighbor on the port
  2: bool isV6 = true;
  // Optional: explicit neighbor address. If unset, resolves from NDP/ARP.
  3: optional string address;
}

// uN (node) MySID: shift SID and forward to next SRv6 node.
struct NodeMySidConfig {
  1: string nodeAddress;
}

// uDT46 (decap) MySID: decapsulate and do VRF lookup on inner packet.
struct DecapMySidConfig {}

// A single MySID entry config — exactly one variant must be set.
union MySidEntryConfig {
  1: AdjacencyMySidConfig adjacency;
  2: NodeMySidConfig node;
  3: DecapMySidConfig decap;
}

struct MySidConfig {
  // Deprecated: use locatorPrefixes.
  1: string locatorPrefix;
  // Map from function ID (i16) to MySID entry config
  2: map<i16, MySidEntryConfig> entries;
  // Every entry is programmed under each prefix; empty uses locatorPrefix.
  3: list<string> locatorPrefixes;
}

enum DsfNodeType {
  FABRIC_NODE = 1,
  INTERFACE_NODE = 2,
}

struct DsfNode {
  1: string name;
  2: i64 switchId;
  3: DsfNodeType type;
  4: list<string> loopbackIps;
  5: optional Range64 systemPortRange_DEPRECATED;
  6: optional string nodeMac;
  7: AsicType asicType;
  8: fboss_common.PlatformType platformType;
  // used by two stage ramon test setup to figure out
  // switches inside the same local capsule.
  // In prod, this info could be figured out from name
  // like rdsw001.c085.n001.snc1, where 85 is cluster id.
  9: optional i32 clusterId;
  // Applicable only for FABRIC_NODES
  // Denotes the level for fabric switch in
  // the DSF n/w topology. Value is either 1 or 2
  10: optional i32 fabricLevel;
  // Offset from where to start local system port
  // ID allocation from
  11: optional i32 localSystemPortOffset;
  // Offset from where to start local system port
  // ID allocation from
  12: optional i32 globalSystemPortOffset;
  // Global system port ranges
  13: SystemPortRanges systemPortRanges;
  // Inband port ID - port used by this DSF node
  // for inband communication. This must be known
  // as part of config for other nodes to bootstrap
  // communication to this node
  14: optional i32 inbandPortId;
  // Prioritization between credit requests from different remote DSF nodes, default no priorization
  15: QueueScheduling scheduling = QueueScheduling.INTERNAL;
  // If strict priority, using spPriority
  // If weighted round robin, use wrrWeight
  16: optional SchedulingParam schedulingParam;
  17: SystemPortRanges localSystemPortRanges;
}

/**
 * UDF related struct definitions
 */
enum UdfBaseHeaderType {
  UDF_L2_HEADER = 1,
  UDF_L3_HEADER = 2,
  UDF_L4_HEADER = 3,
}

enum UdfGroupType {
  HASH = 0,
  ACL = 1,
}

enum UdfMatchL2Type {
  // match any l2 pkt
  UDF_L2_PKT_TYPE_ANY = 0,
  // match eth pkt only
  UDF_L2_PKT_TYPE_ETH = 1,
}

enum UdfMatchL3Type {
  // match any l3 pkt
  UDF_L3_PKT_TYPE_ANY = 0,
  // match ipv4 l3 pkt only
  UDF_L3_PKT_TYPE_IPV4 = 1,
  // match ipv6 l3 pkt only
  UDF_L3_PKT_TYPE_IPV6 = 2,
}

enum UdfMatchL4Type {
  // match any l4 packet
  UDF_L4_PKT_TYPE_ANY = 0,
  // udp
  UDF_L4_PKT_TYPE_UDP = 1,
  // tcp
  UDF_L4_PKT_TYPE_TCP = 2,
}

/**
 * Generic objects for UDF are UdfGroup, UdfMatchPacketConfig
 * For extracting any one field in the packet.
 * 2 possible scenarios covered:
 * Case 1. We have 2 different packets and want to extract the same field
 * e.g. dst_queue_pair from ipv4_roce, dst_queue_pair from ipv6_roce.
 * UDFGroup1 -> {UdfMatchPacket1, UdfMatchPacket2}
 * Case 2. We have 1 packet but want to extract 2 different fields in the packet
 * to be used at the same time. e.g. dst_queue_pair from ipv4_roce
 * src_queue_pair from ipv4_roce
 * UDFGroup1 -> {UdfMatchPacket1}
 * UDFGroup2 -> {UdfMatchPacket1}
 */
struct UdfPacketMatcher {
  1: string name;
  2: UdfMatchL2Type l2PktType = UDF_L2_PKT_TYPE_ANY;
  3: UdfMatchL3Type l3pktType = UDF_L3_PKT_TYPE_ANY;
  4: UdfMatchL4Type l4PktType = UDF_L4_PKT_TYPE_ANY;
  // l4 dstPort to match on
  5: optional i16 UdfL4DstPort;
}

struct UdfGroup {
  // identifier
  1: string name;
  // maps to unique udf cfgs
  2: UdfBaseHeaderType header;
  // bytes from the start of header
  3: i32 startOffsetInBytes;
  // number of bytes to extract (can use for hashing)
  4: i32 fieldSizeInBytes;
  5: list<string> udfPacketMatcherIds;
  6: optional UdfGroupType type;
}

struct UdfConfig {
  1: map<string, UdfGroup> udfGroups;
  2: map<string, UdfPacketMatcher> udfPacketMatcher;
}

struct PortFlowletConfig {
  // port scaling factor for dynamic load balancing
  1: i16 scalingFactor;
  // weight of traffic load in determining ports quality
  2: i16 loadWeight;
  // weight of total queue size in determining port quality
  3: i16 queueWeight;
}

// Behavior for LLR-desired frames while the LLR TX state machine is in the
// INIT or FLUSH state (UE Spec 1.0.2 section 5.1.5).
//
// The spec permits all three in both states. Tomahawk Ultra accepts only INIT
// in {BLOCK, BEST_EFFORT} and FLUSH in {BLOCK}, rejecting anything else at SAI
// profile create. Broadcom confirmed in CS00012472686 that this is a hardware
// design limit rather than an SDK gap, and that TU2 behaves the same.
enum LlrFrameAction {
  DISCARD = 0,
  BLOCK = 1,
  BEST_EFFORT = 2,
}

enum IsolationGroupType {
  // The isolation group consists of ports: its members are the ports that
  // traffic must not reach.
  PORT = 0,
}

// A named set of ports that traffic must not reach. Referenced per-port by
// Port.isolationGroup: packets ingressing a port that references this group are
// never forwarded to any of its members, whether switched or routed.
//
// One group may be referenced by many ports, and a port may be a member of the
// group it references. The intended model is to declare a single group holding
// every port in an isolation domain, then attach it to whichever of those ports
// should be isolated -- so membership and attachment are decoupled and either
// can change without touching the other.
struct IsolationGroup {
  2: IsolationGroupType type = IsolationGroupType.PORT;
  // cfg::Port.logicalID of each isolated member.
  3: set<i32> memberPorts;
}

// UEC Link Layer Retry (LLR) profile: the configuration registers defined in
// UE Spec 1.0.2 section 5.1.4 (Table 5-9). Referenced per-port by name via
// Port.llrConfigName.
struct LlrConfig {
  // Fields backed by a u32 SAI attribute use i64: thrift has no unsigned type,
  // and i32 cannot represent the full 0..2^32-1 range. ApplyThriftConfig
  // validates each field against its SAI attribute width (see validateLlrConfig).
  // Max unacknowledged frames held in the replay buffer (outstanding_seq_max).
  1: i64 outstandingFramesMax;
  // Max unacknowledged bytes; SHOULD be the link bandwidth-delay product so
  // pause/PFC keep working (outstanding_data_max).
  2: i64 outstandingBytesMax;
  // Timer (ns) after which a replay is initiated (replay_timer_max).
  3: i64 replayTimerMax;
  // Max replays before entering FLUSH; 255 = unlimited (replay_ct_max).
  // Meta sim study recommends >= 2.
  4: i16 replayCountMax = 2;
  // PCS-lost duration (ns) before FLUSH (pcs_lost_status_timer_max).
  5: i64 pcsLostTimeout;
  // Max time (ns) a frame may reside in the replay buffer (data_age_timer_max).
  6: i64 dataAgeTimeout;
  // Action for LLR-desired frames in INIT state (llr_init_behavior).
  7: LlrFrameAction initFrameAction = LlrFrameAction.BEST_EFFORT;
  // Action for LLR-desired frames in FLUSH state (llr_flush_behavior). BLOCK is
  // the only value Tomahawk Ultra accepts, hence the default; see
  // LlrFrameAction.
  8: LlrFrameAction flushFrameAction = LlrFrameAction.BLOCK;
  // Re-initialize LLR on FLUSH (re_init_on_discard).
  9: bool reInitOnFlush = false;
  // Target bytes between successive ACK/NACK CtlOS (ctlos_target_spacing).
  10: i32 ctlosTargetSpacing = 2048;
}

enum SwitchingMode {
  // flowlet regular quality based reassignments
  FLOWLET_QUALITY = 0,
  // per packet assignments
  PER_PACKET_QUALITY = 1,
  // flowlet is disabled
  FIXED_ASSIGNMENT = 2,
  // per packet random, no quality
  PER_PACKET_RANDOM = 3,
}

struct FlowletSwitchingConfig {
  // wait for lack of activitiy interval on the flow before load balancing
  1: i16 inactivityIntervalUsecs;
  // flow set table size
  2: i16 flowletTableSize = DEFAULT_FLOWLET_TABLE_SIZE;
  // EWMA (exponentially weighted moving average) of historical member load in bytes
  3: i16 dynamicEgressLoadExponent;
  // EWMA of historical member queued bytes
  4: i16 dynamicQueueExponent;
  // minimum threshold, in bytes, used to quantize historical member queued bytes
  // cumulative across all memory buffers on chip
  5: i32 dynamicQueueMinThresholdBytes;
  // maximum threshold, in bytes, used to quantize historical member queued bytes
  // cumulative across all memory buffers on chip
  6: i32 dynamicQueueMaxThresholdBytes;
  // number of times historical member load and queued bytes are computed in a second
  7: i32 dynamicSampleRate;
  // minimum threshold, in mbps, used to quantize historical member load
  8: i32 dynamicEgressMinThresholdBytes;
  // maximum threshold, in mbps, used to quantize historical member load
  9: i32 dynamicEgressMaxThresholdBytes;
  // EWMA of historical member bytes in physical queue
  10: i16 dynamicPhysicalQueueExponent;
  // maximum links used for flowlet switching.
  // Needed for scaling flowset table
  11: i16 maxLinks;
  // switching mode
  12: SwitchingMode switchingMode = FLOWLET_QUALITY;
  // fall back switching mode if DLB groups are exhausted
  13: SwitchingMode backupSwitchingMode = FIXED_ASSIGNMENT;
  // primary path quality threshold
  14: optional i32 primaryPathQualityThreshold;
  // alternate path cost
  15: optional i32 alternatePathCost;
  // alternate path bias
  16: optional i32 alternatePathBias;
  // minimum width for ARS virtual group
  17: optional i32 minWidthForArsVirtualGroup;
  // ARS virtual group max width
  18: optional i32 maxArsVirtualGroupWidth;
  // maximum number of ARS virtual groups
  19: optional i32 maxArsVirtualGroups;
  // standby DLB group switching mode
  20: optional SwitchingMode standbySwitchingMode;
  // wait for lack of activity interval on the flow before load balancing,
  // for standby DLB groups
  21: optional i16 standbyInactivityIntervalUsecs;
  // flow set table size for standby DLB groups
  22: optional i16 standbyFlowletTableSize;
  // slots of maxArsVirtualGroupWidth reserved for alternate members. The rest
  // are primary members, which caps how wide a next hop group backed by the
  // virtual group can be programmed
  23: optional i32 arsVirtualGroupAlternateMembers;
  // how many members shared by every virtual group in the super group there
  // have to be before the adapter starts promoting them to alternate members
  24: optional i32 arsVirtualGroupCommonMembersThreshold;
  // members per ARS group
  25: optional i32 arsGroupWidth;
}

/*
 * configuration for a static MAC entry
 */
struct StaticMacEntry {
  1: i32 vlanID;
  2: string macAddress;
  3: i32 egressLogicalPortID;
}

/**
 * The configuration for a switch.
 *
 * This contains all of the hardware-independent configuration for a
 * single switch/router in the network.
 */
struct SwitchConfig {
  1: i64 version = 0;
  2: list<Port> ports;
  3: list<Vlan> vlans = [];
  4: list<VlanPort> vlanPorts = [];
  /*
   * Broadcom chips have the notion of a "default" VLAN.
   * This VLAN must always exist.  It is used as the default VLAN for ingress
   * packets when a default ingress VLAN hasn't been set on a per-port basis.
   * We always set a per-port ingress VLAN, so this setting doesn't really have
   * much effect for us.
   */
  5: i32 defaultVlan;
  6: list<Interface> interfaces = [];
  7: i32 arpTimeoutSeconds = arpTimeoutDefault;
  8: i32 arpRefreshSeconds = arpRefreshSecondsDefault;
  9: i32 arpAgerInterval = arpAgerIntervalDefault;
  10: bool proactiveArp = 0;
  // The MAC address to use for the switch CPU.
  11: optional string cpuMAC;
  // Static routes with next hops
  12: list<StaticRouteWithNextHops> staticRoutesWithNhops = [];
  // Prefixes for which to drop traffic
  13: list<StaticRouteNoNextHops> staticRoutesToNull = [];
  // Prefixes for which to send traffic to CPU
  14: list<StaticRouteNoNextHops> staticRoutesToCPU = [];
  // List of all ACLs that are available for use by various agent components
  // ACLs declared here can be referenced in other places in order to tie
  // actions to them, e.g. as part of a MatchToAction
  // Only DROP acls are applied directly, all others require being referenced
  // elsewhere in order to be meaningful
  // Ordering of DROP acls define their priority
  15: list<AclEntry> acls = [];
  // Set max number of probes to a sufficiently high value
  // to allow for the cases where
  // a) We are probing and the agent on other end is restarting.
  // b) The other end is having a hard time and responses to ARP/NDP
  // packets are delayed. This is more of a protection mechanism. We
  // saw a case where upstream switches were not able to respond to ARP
  // in time. The underlying cause was that the upstream switch was getting
  // flooded with control plane traffic while ARP traffic was set to goto a low
  // priority queue. This lead to ARP responses to be delayed by upto 50
  // seconds. We have since fixed the issues, but it was decided to add an extra
  // safety measure here to avoid catastrophic failure if such a situation
  // arises again.On vendor devices we set ARP expiry to be as high as 1500
  // seconds.
  16: i32 maxNeighborProbes = maxNeighborProbesDefault;
  17: i32 staleEntryInterval = staleEntryIntervalDefault;
  18: list<AggregatePort> aggregatePorts = [];
  // What admin distance to use for each potential clientID
  // These mappings map a ClientID to a AdminDistance
  // Predefined values for these can be found at
  // fboss/agent/if/ctrl.thrift
  /*
   * AdminDistance for each clientID. Lower AdminDistance means higher priority.
   * If same prefix is requested for programming from multiple clientID, then
   * clientID with lowest AdminDistance will be programmed.
   * Predefined values for AdminDistance & ClientID can be found at
   * fboss/agent/if/ctrl.thrift
   * NOTE: Ordered as per increasing admin distance i.e most preferred to least
   */
  19: map<i32, i32> clientIdToAdminDistance = {
    2: 0, // INTERFACE_ROUTE
    3: 0, // LINKLOCAL_ROUTE
    4: 0, // REMOTE_INTERFACE_ROUTE
    1: 1, // STATIC_ROUTE
    800: 2, // TE_AGENT
    786: 10, // OPENR
    0: 20, // BGPD
    700: 255, // STATIC_INTERNAL
  };
  /* Override source IP for DHCP relay packet to the DHCP server */
  20: optional string dhcpRelaySrcOverrideV4;
  21: optional string dhcpRelaySrcOverrideV6;
  /*
   * Override source IP for DHCP reply packet to client host.
   * This IP has to match one of interface IPs, as it is used to determines
   * which interface/VLAN the client host MAC is searched against in order to
   * send the reply to.
   */
  22: optional string dhcpReplySrcOverrideV4;
  23: optional string dhcpReplySrcOverrideV6;
  24: optional TrafficPolicyConfig globalEgressTrafficPolicy_DEPRECATED;
  25: optional string config_version;
  26: list<SflowCollector> sFlowCollectors = [];
  27: optional Lacp lacp;
  28: list<PortQueue> cpuQueues = [];
  29: optional CPUTrafficPolicyConfig cpuTrafficPolicy;
  30: list<LoadBalancer> loadBalancers = [];
  31: optional TrafficPolicyConfig dataPlaneTrafficPolicy;
  32: list<Mirror> mirrors = [];
  33: list<TrafficCounter> trafficCounters = [];
  34: list<QosPolicy> qosPolicies = [];
  35: list<PortQueue> defaultPortQueues = [];
  // Static MPLS routes with next hops
  36: list<StaticMplsRouteWithNextHops> staticMplsRoutesWithNhops = [];
  // MPLS labels for which to drop traffic
  37: list<StaticMplsRouteNoNextHops> staticMplsRoutesToNull = [];
  // MPLS labels for which to send traffic to CPU
  38: list<StaticMplsRouteNoNextHops> staticMplsRoutesToCPU = [];
  // ingress LER routes
  39: list<StaticIp2MplsRoute> staticIp2MplsRoutes = [];

  // Map of named PortQueueConfigs.
  40: map<PortQueueConfigName, list<PortQueue>> portQueueConfigs = {};

  41: SwitchSettings switchSettings;
  42: optional QcmConfig qcmConfig;
  43: optional map<PortPgConfigName, list<PortPgConfig>> portPgConfigs;
  44: optional map<BufferPoolConfigName, BufferPoolConfig> bufferPoolConfigs;
  45: optional AclTableGroup aclTableGroup;
  // agent sdk versions
  46: optional SdkVersion sdkVersion;
  47: optional list<IpInIpTunnel> ipInIpTunnels;
  // When part of a DSF cluster, the following info
  // will be populated to reflect global DSF node info
  48: map<i64, DsfNode> dsfNodes = {};
  49: optional UdfConfig udfConfig;
  50: optional FlowletSwitchingConfig flowletSwitchingConfig;
  51: list<PortQueue> defaultVoqConfig = [];
  52: optional map<PortFlowletConfigName, PortFlowletConfig> portFlowletConfigs;
  // When there's no IPv4 addresses configured, what address to use to source IPv4 ICMP packets from.
  53: optional string icmpV4UnavailableSrcAddress;
  // Overrides the system hostname, useful in ICMP responses
  54: optional string hostname;
  55: optional list<PortQueue> cpuVoqs;
  // list of ACL table groups, prefer this over aclTableGroup, aclTableGroup will be deprecated
  56: optional list<AclTableGroup> aclTableGroups;
  57: list<MirrorOnDropReport> mirrorOnDropReports = [];
  58: optional list<StaticMacEntry> staticMacAddrs;
  59: optional list<Srv6Tunnel> srv6Tunnels;
  60: optional MySidConfig mySidConfig;
  // Named UEC Link Layer Retry (LLR) profiles, referenced per-port by
  // Port.llrConfigName (UE Spec 1.0.2 section 5.1).
  61: optional map<LlrConfigName, LlrConfig> llrConfigs;
  // Named CBFC configurations, referenced by Port.cbfcConfigName.
  62: optional map<CbfcConfigName, CbfcConfig> cbfcConfigs;
  // Named isolation groups, referenced per-port by Port.isolationGroup.
  63: optional map<IsolationGroupName, IsolationGroup> isolationGroups;
  64: optional list<VxLanTunnel> vxlanTunnels;
  65: optional list<VrfConfig> vrfs;
}
