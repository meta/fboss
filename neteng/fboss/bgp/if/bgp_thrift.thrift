/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

cpp_include "folly/container/F14Map.h"

include "common/fb303/if/fb303.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/rib_policy.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/bgp_policy.thrift"
include "configerator/structs/neteng/fboss/bgp/bgp_config.thrift"
include "configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift"
include "neteng/fboss/bgp/if/bgp_route_types.thrift"
include "neteng/fboss/bgp/if/policy_thrift.thrift"
include "thrift/annotation/hack.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"

@thrift.AllowLegacyMissingUris
package;

namespace py neteng.fboss.bgp_thrift
namespace py.asyncio neteng.fboss.asyncio.bgp_thrift
namespace py3 neteng.fboss
namespace cpp2 facebook.neteng.fboss.bgp.thrift
namespace go neteng.fboss.bgp_thrift

/**
 * BGP state machine
 */
enum TBgpPeerState {
  IDLE = 0,
  CONNECT = 1,
  ACTIVE = 2,
  OPEN_SENT = 3,
  OPEN_CONFIRMED = 4,
  ESTABLISHED = 5,
  CLOSING = 6,
  IDLE_ADMIN = 7,
}

/*
 * Events in BGP Initialization Process.
 */
enum BgpInitializationEvent {
  /*
   * BGP Initialization process starts
   */
  INITIALIZING = 0,

  /*
   * Platform agent is ready to accept route programming thrift request
   */
  AGENT_CONFIGURED = 1,

  /*
   * Subscribed to FSDB for link status update
   */
  FSDB_SUBSCRIBED = 2,

  /*
   * All peer information has been loaded to establish BGP sessions
   */
  PEER_INFO_LOADED = 3,

  /*
   * All EoR(End-of-Rib) signal have been received for initial RIB computation
   */
  ALL_EOR_RECEIVED = 4,

  /*
   * EoR timer expired to force initial RIB computation
   */
  EOR_TIMER_EXPIRED = 5,

  /*
   * Initial RIB(Routing Information Base) computation has completed
   */
  RIB_COMPUTED = 6,

  /*
   * Initial FIB(Forwarding Information Base) programming based on RIB
   * computation has completed
   */
  FIB_SYNCED = 7,

  /*
   * BGP finished initial FIB sync and advertised best-path selection result
   */
  EOR_SENT = 8,

  /*
   * BGP Initialization process has completed
   */
  INITIALIZED = 9,
}

struct TGoldenPrefixesPolicyStatus {
  1: rib_policy.TRouteFilterPolicy policy;
  2: bool isPolicyActive;
}

/**
 * This is the locally announced BGP network:
 */
struct TBgpNetwork {
  1: bgp_attr.TIpPrefix prefix;
  2: bgp_attr.TAsPath as_path;
  3: list<bgp_attr.TBgpCommunity> communities;
  4: i32 local_pref;
  5: string next_hop4;
  6: string next_hop6;
  7: optional list<bgp_route_types.TBgpExtCommunity> extCommunities;
}

/**
 * Key parameters for a BGP session. Here peer_id is some
 * unique opaque identifier for a peer, e.g. IPv4 address string
 */
struct TBgpPeer {
  1: string peer_id;
  /* To be deprecated: use i64 version instead */
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  2: i32 local_as;
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  3: i32 remote_as;
  4: i32 hold_time;
  /** only duplex peer has those two, as it may announce routes */
  5: bgp_attr.TIpPrefix next_hop4;
  6: bgp_attr.TIpPrefix next_hop6;
  7: TBgpPeerState peer_state;
  8: bool graceful; // we sent/received GR Capability to/from peer
  9: i64 lastResetHoldTimer; // time is in millisecond
  10: i64 lastSentKeepAlive; // time is in millisecond
  11: i64 lastRcvdKeepAlive; // time is in millisecond
  12: optional bgp_attr.AddPath add_path; // Add path mode if enabled

  // RFC 6793
  13: i64 local_as_4_byte; // unsigned int 32
  14: i64 remote_as_4_byte; // unsigned int 32
  15: i64 lastResetKeepAliveTimer; // time is in millisecond
}

/**
 * Enum for three types of connections
 */
enum TBgpSessionConnectMode {
  PASSIVE_ACTIVE = 1,
  PASSIVE_ONLY = 2,
  ACTIVE_ONLY = 3,
}

/**
 * Negotiated AddPathCapability
 */
struct TBgpAddPathNegotiated {
  1: bgp_attr.TBgpAfi afi;
  2: bgp_attr.AddPath add_path;
}

/**
 * Additional information provided in bgp neighbor command
 */
struct TBgpSessionDetail {
  1: bool confed_peer;
  2: i64 remote_bgp_id; // i64 used to store uint32
  5: bool rr_client;
  6: TBgpSessionConnectMode connect_mode;
  7: i32 peer_port; // i32 used to store uint16
  8: string local_router_id;
  9: i32 local_port; // i32 used to store uint16
  10: bool ipv4_unicast;
  11: bool ipv6_unicast;
  12: i64 gr_restart_time;
  13: i64 gr_remote_restart_time;
  14: i64 eor_sent_time; // time since epoch in milliseconds
  15: i64 eor_received_time; // time since epoch in milliseconds
  16: list<TBgpAddPathNegotiated> add_path_capabilities;
  17: i64 num_of_flaps; // number of terminations for TCP sessions
  18: optional i64 active_time; // time since the socket has been created (BGP ACTIVE state)
  21: i64 sent_update_announcements_ipv4;
  22: i64 sent_update_announcements_ipv6;
  23: i64 recv_update_announcements_ipv4;
  24: i64 recv_update_announcements_ipv6;
  25: i64 sent_update_withdrawals;
  26: i64 recv_update_withdrawals;
  27: i64 enforce_first_as_rejects;
  28: i64 prepolicy_rcvd_prefix_count_ipv4;
  29: i64 prepolicy_rcvd_prefix_count_ipv6;
  30: i64 postpolicy_rcvd_prefix_count_ipv4;
  31: i64 postpolicy_rcvd_prefix_count_ipv6;
  32: i64 prepolicy_sent_prefix_count_ipv4;
  33: i64 prepolicy_sent_prefix_count_ipv6;
  34: i64 postpolicy_sent_prefix_count_ipv4;
  35: i64 postpolicy_sent_prefix_count_ipv6;
  /**
   * TTL Security / GTSM (RFC 5082) status.
   * ttl_security_enabled is derived from whether ttlSecurityHops is configured.
   */
  36: optional bool ttl_security_enabled;
  37: optional i32 ttl_security_hops;
  /*
   * Data-plane per-message-type PDUs actually written to / read from the peer's
   * socket. Source of truth: SessionManager (I/O) thread.
   */
  38: i64 socket_tx_open_msgs;
  39: i64 socket_tx_update_msgs;
  40: i64 socket_tx_keepalive_msgs;
  41: i64 socket_tx_notification_msgs;
  42: i64 socket_tx_route_refresh_msgs;
  43: i64 socket_tx_eor_msgs;
  44: i64 socket_rx_open_msgs;
  45: i64 socket_rx_update_msgs;
  46: i64 socket_rx_keepalive_msgs;
  47: i64 socket_rx_notification_msgs;
  48: i64 socket_rx_route_refresh_msgs;
  49: i64 socket_rx_eor_msgs;
  /*
   * Control-plane per-message-type PDUs generated / consumed by PeerManager
   * (AdjRib). The AdjRib layer only produces/consumes UPDATE and EoR PDUs.
   * These converge with the socket-layer counters above for cross-module
   * (control vs socket) validation, e.g. the health validator:
   *   adjrib_sent_update_msgs <-> socket_tx_update_msgs
   *   adjrib_sent_eor_msgs    <-> socket_tx_eor_msgs
   *   adjrib_recv_update_msgs <-> socket_rx_update_msgs
   *   adjrib_recv_eor_msgs    <-> socket_rx_eor_msgs
   * adjrib_sent/recv_update_msgs mirror the legacy top-level
   * TBgpSession.sent/recv_update_msgs, which are kept for existing consumers.
   */
  50: i64 adjrib_sent_update_msgs;
  51: i64 adjrib_sent_eor_msgs;
  52: i64 adjrib_recv_update_msgs;
  53: i64 adjrib_recv_eor_msgs;
  /**
   * True when this peer receives IPv4-unicast routes as RFC 4271 classic NLRI +
   * NEXT_HOP (attr 3) instead of MP_REACH_NLRI -- i.e. it advertised no MP-EXT
   * capability. Derived from the AdjRib update-group key.
   */
  54: bool legacy_v4_nlri_encoding;
}

/**
 * BGP Session - Outlines transport, peer, capabilities, and statistics
 */
struct TBgpSession {
  1: TBgpPeer peer;
  2: string my_addr;
  3: string peer_addr;
  4: i16 rcvd_prefix_count;
  5: i16 sent_prefix_count;
  6: string description;
  8: i64 uptime;
  /* uptime in msecs */
  9: optional TBgpSessionDetail details;
  10: i64 prepolicy_rcvd_prefix_count;
  11: i64 postpolicy_sent_prefix_count;
  // BGP ID in IPv4 format
  12: string peer_bgp_id;

  # UCMP configurations for this BGP peer
  13: bgp_attr.AdvertiseLinkBandwidth advertise_link_bandwidth;
  14: bgp_attr.ReceiveLinkBandwidth receive_link_bandwidth;
  # This is a typo. Here bandwidth is in Bytes/sec not Bits/sec
  15: optional float link_bandwidth_bps;
  16: i64 reset_time;
  17: i64 num_resets;
  18: string last_reset_reason;
  // DEPRECATED: prefer the per-message-type control-plane counters
  // TBgpSessionDetail.adjrib_sent_update_msgs / adjrib_recv_update_msgs, which
  // are grouped with the socket_* counters for cross-module validation. These
  // top-level fields are retained for existing consumers (show bgp summary,
  // NOWA/NetRCA) and should not be used in new code.
  19: i64 sent_update_msgs;
  20: i64 recv_update_msgs;
  /*
   * Cached RIB version - the max version this peer has consumed from RIB.
   * Used for backpressure visibility to see how caught up this peer is
   * with the current RIB state. A value of 0 indicates the peer is new
   * or down (displayed as "N/A" in CLI).
   */
  21: optional i64 rib_version;
  // Policy names configured for this BGP session
  22: optional string ingress_policy_name;
  23: optional string egress_policy_name;
  // Update-group ID assigned by UpdateGroupManager (monotonically increasing)
  24: optional i64 update_group_id;
  // Peer's state in the update-group state machine (e.g., JOINED_RUNNING)
  25: optional string peer_state_update_group;
  26: i64 postpolicy_rcvd_prefix_count;
  /*
   * Per-peer count of received routes dropped because the peer reached its
   * configured pre-filter max prefix limit (RouteLimit.max_routes), reset when
   * the session goes down. A non-zero value means the peer is actively shedding
   * received (PR) routes. Surfaced as the PRD column in `show bgp summary` and
   * reused by the health validator, so operators can spot drops without digging
   * through logs.
   */
  27: optional i64 prepolicy_rcvd_dropped_prefix_count;
  /*
   * Current session's End-of-RIB completion, not a startup timestamp.
   * Present even when false so CLI clients can distinguish a pending EoR
   * from an older bgpd that does not support these fields.
   */
  28: optional bool eor_received;
  29: optional bool eor_sent;
}

struct TPeerEgressStats {
  /*
   * Used to calculate percentile statistics over peers according to the desired
   * grouping. The default grouping is a peer's AdjRibOutGroup.
   */
  1: optional string group_name;
  /*
   * Using these specific session fields from TBgpSession:
   *   - peer id
   *   - sent prefixes
   *   - rcvd prefixes
   *   - sent update messages
   *   - rcvd update messages
   *   - peering state
   *   - peer description
   */
  2: optional TBgpSession session;
  /*
   * Number of transient route updates suppressed; i.e. processed
   * without being advertised in AdjRibOut.
   */
  3: optional i64 transient_route_updates_suppressed;
  /* Number of times boundedAdjRibOutQueue_ blocked on a push attempt. */
  4: optional i64 adjribout_queue_blocks;
  /* Total boundedAdjRibOutQueue_ block duration. */
  5: optional i64 adjribout_queue_total_block_duration;
  /* Number of times sendQueue_ blocked on a push attempt. */
  6: optional i64 send_queue_blocks;
  /* Total sendQueue_ block duration. */
  7: optional i64 send_queue_total_block_duration;
  /* Total writes buffered in AsyncSocket (i.e. socket backpressured). */
  8: optional i64 total_async_socket_buffered;
  /* Last epoch time (ms) that boundedAdjRibOutQueue_ blocked. */
  9: optional i64 last_adjribout_queue_block_time;
  /* Last epoch time (ms) that sendQueue_ blocked. */
  10: optional i64 last_send_queue_block_time;
  /* Last epoch time (ms) that AsyncSocket buffered a message. */
  11: optional i64 last_socket_buffered_time;
}

/**
 * Thrift representation of UpdateGroupKey — the criteria for grouping
 * peers into an update group. Mirrors AdjRibStructs.h UpdateGroupKey.
 */
struct TUpdateGroupKey {
  /* Egress policy name applied to peers in this group. */
  1: string egress_policy_name;

  /* Peer device regex or peer-group name for route filtering. */
  2: string route_filter_stmt_name;

  /* Outbound delay in seconds before sending updates. */
  3: i64 out_delay_seconds;

  /* Session type: "EBGP", "IBGP", or "ConfedEBGP". */
  4: string session_type;

  /* Whether IPv4 AFI is negotiated for this group. */
  5: bool afi_ipv4_negotiated;

  /* Whether IPv6 AFI is negotiated for this group. */
  6: bool afi_ipv6_negotiated;

  /* Whether peers in this group are confederation peers. */
  7: bool is_confed_peer;

  /* Whether peers in this group are route reflector clients. */
  8: bool is_rr_client;

  /* Link bandwidth advertisement mode. */
  9: optional bgp_attr.AdvertiseLinkBandwidth advertise_link_bandwidth;

  /* Link bandwidth receive mode. */
  10: optional bgp_attr.ReceiveLinkBandwidth receive_link_bandwidth;

  /* Static link bandwidth in bps for UCMP. Null = use ECMP, zero = blackhole. */
  11: optional i64 link_bandwidth_bps;

  /* Whether private AS numbers are stripped before advertising. */
  12: bool remove_private_asn;

  /* Whether Add-Path send is enabled for this group. */
  13: bool send_add_path;

  /* Whether 4-byte ASN capability is negotiated. */
  14: bool as4_byte_capable;

  /* Whether RFC5549 extended nexthop encoding is negotiated. */
  15: bool ext_nh_encoding_capable;

  /* Peer group name this group belongs to. */
  16: string peer_group_name;

  /* Whether peer has per-peer egress policy override. */
  17: bool peer_override;

  /*
   * Whether IPv4-unicast announcements to this group use RFC 4271 classic NLRI
   * + NEXT_HOP (for peers that advertised no MP-EXT capability) instead of
   * MP_REACH_NLRI.
   */
  18: bool legacy_v4_nlri_encoding;

  /*
   * Local AS advertised to this group. Per-peer overridable via the peer /
   * peer-group local_as cascade (RFC-7705), and it is the ASN prepended to
   * AS_PATH on egress, so peers with different local AS must not share a group.
  */
  19: i64 local_as;

  /* Confederation identifier used by the egress AS_PATH transform. */
  20: optional i64 as_confed_id;
}

/**
 * Update group accumulated statistics reported per AdjRibOutGroup.
 */
struct TUpdateGroupStats {
  /* Number of times a slow peer was detached from the group. */
  1: i64 slow_peer_detachments;

  /* Number of DFP (Divergence-Free Peer) rejoin events. */
  2: i64 dfp_rejoin_events;

  /* Number of entries corrected during detached peer rejoin entry collapse. */
  3: i64 collapse_entries_corrected;

  /* Number of DSP (Diverged-State Peer) rejoin events. */
  4: i64 dsp_rejoin_events;

  /* Number of times lazy clone was invoked for detached peers. */
  5: i64 lazy_clone_events;

  /**
   * Cumulative count of IPv4 UPDATE announcement PDUs sent by this group
   * (monotonic counter, one bump per BgpUpdate2, NOT per prefix). Mirrors the
   * per-peer TBgpSessionDetail.sent_update_announcements_ipv4.
   */
  6: i64 total_sent_announcement_msgs_ipv4;

  /**
   * Cumulative count of IPv6 UPDATE announcement PDUs sent by this group
   * (monotonic counter, one bump per BgpUpdate2, NOT per prefix). Mirrors the
   * per-peer TBgpSessionDetail.sent_update_announcements_ipv6.
   */
  7: i64 total_sent_announcement_msgs_ipv6;

  /* Number of IPv4 update messages sent by this group. */
  8: i64 group_update_messages_ipv4;

  /* Number of IPv6 update messages sent by this group. */
  9: i64 group_update_messages_ipv6;

  /**
   * Cumulative count of withdrawal UPDATE PDUs sent by this group (monotonic
   * counter, one bump per BgpUpdate2, NOT per prefix).
   */
  10: i64 total_sent_withdrawal_msgs;

  /* Total queue wait time (ms) across all sync peers in the group. */
  11: i64 group_total_queue_wait_ms;

  /* Total number of queue blocks across all sync peers in the group. */
  12: i64 group_total_queue_blocks;

  /* Last epoch time (ms) that a queue block occurred in this group; unset if never. */
  13: optional i64 last_group_queue_block_time;

  /* Current point-in-time count of prefixes in post-policy RIB-OUT (gauge). */
  14: i64 post_out_prefix_count;

  /* Current point-in-time count of IPv4 prefixes in post-policy RIB-OUT. */
  15: i64 post_out_prefix_count_ipv4;

  /* Current point-in-time count of IPv6 prefixes in post-policy RIB-OUT. */
  16: i64 post_out_prefix_count_ipv6;
}

/**
 * Per-peer info within an update group.
 */
struct TUpdateGroupPeerInfo {
  /* Peer address (IPv4 or IPv6). */
  1: string peer_addr;

  /* Peer's state in the update-group state machine (e.g., "JOINED_RUNNING"). */
  2: string peer_state;

  /* Peer's bit position within the group bitmap. */
  3: i64 bit_position;

  /* Whether peer is in-sync with the group (sharing group updates). */
  4: bool is_in_sync;

  /* Whether peer is currently TCP-backpressured. */
  5: bool is_blocked;

  /* Whether peer is detached from the group. */
  6: bool is_detached;

  /* "DFP" or "DSP" if detached, unset otherwise. */
  7: optional string detach_type;

  /* RIB version when peer was detached; unset if not detached. */
  8: optional i64 detached_rib_version;

  /* BGP session state. */
  9: TBgpPeerState session_state;

  /* Peer description from config. */
  10: string description;

  /* Peer's remote AS number. */
  11: i64 remote_as;

  /* Last RIB version seen by this peer. */
  12: i64 last_seen_rib_version;

  /* Current per-peer adjribOut queue depth. */
  13: i64 queue_size;

  /* Number of per-peer adjRibEntry keys (prefix count). */
  14: i64 entry_count;

  /* Epoch time (ms) when EoR was sent to this peer; unset if not sent. */
  15: optional i64 eor_sent_time_ms;

  /*
   * Epoch time (ms) of this peer's last update-group state transition; unset if
   * the peer has never left its initial state.
   */
  16: optional i64 last_modified_peer_update_state_time_ms;

  /*
   * Why the peer was most recently detached ("Blocking", "Policy",
   * "RouteRefresh"); retained after the peer rejoins. Unset if the peer has
   * never been detached.
   */
  17: optional string last_detach_reason;

  /*
   * Epoch time (ms) of the peer's most recent detachment; unset if the peer has
   * never been detached.
   */
  18: optional i64 last_detach_time_ms;
}

/**
 * Lightweight update-group state used by the all-groups summary view.
 * Every field is available directly from cached group state; producing this
 * structure must not walk group members or RIB-OUT trees.
 */
struct TUpdateGroupSummary {
  1: i64 group_id;
  2: string egress_policy_name;
  3: string group_state;
  4: i64 member_count;
  5: i64 in_sync_peer_count;
  6: i64 detached_peer_count;
  7: i64 post_out_prefix_count;
  8: i64 post_out_prefix_count_ipv4;
  9: i64 post_out_prefix_count_ipv6;
  10: i64 last_seen_rib_version;
}

/**
 * Complete update-group information for CLI display.
 * Organized by mutability with reserved field ranges for extensibility:
 *   1-2:   Identity (immutable)
 *   3-10:  Per-group configuration (static after group creation)
 *   11-20: Runtime state (changes frequently)
 *   21-30: Accumulated stats
 *   31-40: Per-peer membership
 *   41-50: Diagnostics
 *
 * Note: Slow-peer detection config is global (UpdateGroupConfig in
 * bgp_config.thrift), not per-group. It is displayed in the summary view.
 */
struct TUpdateGroupInfo {
  /* ---- Group identity (fields 1-2) ---- */

  /* Monotonically increasing group ID assigned by UpdateGroupManager. */
  1: i64 group_id;

  /* Full grouping criteria that defines this update group. */
  2: TUpdateGroupKey group_key;

  /* ---- Per-group configuration (fields 3-10) ---- */
  /* fields 3-10 reserved for future per-group configuration */

  /* ---- Runtime state (fields 11-20, changes frequently) ---- */

  /* Group state: "UNINITIALIZED", "IDLE", "READY", "WAITING". */
  11: string group_state;

  /* Total number of peers in this group. */
  12: i64 member_count;

  /* Number of peers currently in-sync with the group. */
  13: i64 in_sync_peer_count;

  /* Number of peers currently detached from the group. */
  14: i64 detached_peer_count;

  /* Number of peers currently blocked (TCP backpressure). */
  15: i64 blocked_peer_count;

  /* Last RIB version seen by this group. */
  16: i64 last_seen_rib_version;

  /* fields 17-20 reserved for future runtime state */

  /* ---- Accumulated stats (fields 21-30) ---- */

  /* Per-group accumulated counters. */
  21: TUpdateGroupStats stats;

  /* fields 22-30 reserved for future stats */

  /* ---- Per-peer membership details (fields 31-40) ---- */

  /* List of peers in this group with their individual state. */
  31: list<TUpdateGroupPeerInfo> peers;

  /* fields 32-40 reserved for future per-peer data */

  /* ---- Diagnostics (fields 41-50) ---- */

  /* Epoch time (ms) when initial RIB dump completed for this group. */
  41: optional i64 initial_dump_completion_time_ms;

  /* Total number of entry discrepancies detected during rejoin collapse. */
  42: i64 total_discrepancies;

  /* Per-PeerUpdateState count breakdown (e.g., "JOINED_RUNNING" -> 3). */
  43: map<string, i64> peer_state_counts;
  /* fields 44-50 reserved for future diagnostics */
}

/**
 * Request parameters for the single-group detail endpoint.
 */
struct TGetUpdateGroupInfoRequest {
  /* Required by the handler. An unset ID returns no detail record. */
  1: optional i64 group_id;
}

/**
 * Response wrapper for getUpdateGroupInfo().
 * Allows adding response metadata (e.g., pagination, timestamp)
 * without breaking the API signature.
 */
struct TGetUpdateGroupInfoResponse {
  1: list<TUpdateGroupInfo> update_groups;

  /* Whether the update-group feature is enabled. */
  2: bool enable_update_group;
}

/** Response for the lightweight all-groups summary endpoint. */
struct TGetUpdateGroupSummariesResponse {
  1: list<TUpdateGroupSummary> update_groups;
  2: bool enable_update_group;
}

/**
 * BGP Stream Session attributes
 */
struct TBgpStreamSession {
  1: string subscriber_name;
  // subscription time in msecs
  2: i64 uptime;
  // Remote BGP Peer ID
  3: i32 peer_id;
  // Peer address in IPv4 format
  4: string peer_addr;
  // Number of advertised routes
  5: i64 sent_prefix_count;
  // State of the session
  6: TBgpPeerState state;
  // Number of times session was brought down
  7: i64 num_flaps;
}

/**
 * Local BGP Config information
 */
struct TBgpLocalConfig {
  1: i64 my_router_id; // In network byte order
  /* To be deprecated, use 4 byte version instead */
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  2: i32 local_as;
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  3: optional i32 local_confed_as;

  # UCMP configurations for route programming
  4: bool program_ucmp_weights;
  5: i32 ucmp_width;

  /* RFC 6793 */
  6: i64 local_as_4_byte; // unsigned int32
  7: i64 local_confed_as_4_byte; // unsigned int32

  8: bool enable_update_group;
  9: optional bool enable_route_refresh;
}

/**
 * Nexthop information for a prefix.
 *
 * Fields 1-3 and 5-6 back the compact list view. Fields 4 and 7-8 add detail
 * when a specific nexthop is queried.
 */
struct TNexthopInfo {
  1: bgp_attr.TIpPrefix next_hop;
  // Reachability reported by the nexthop resolver.
  2: bool is_reachable;
  3: optional i32 igp_cost;
  // Whether the nexthop is directly connected; unset = unknown.
  4: optional bool is_connected;
  // Whether the nexthop is eligible for best-path selection (may differ from
  // is_reachable: a reachable nexthop without an IGP cost can still be
  // selectable under FBOSS semantics).
  5: bool is_resolved_for_selection;
  // Number of BGP paths currently using this nexthop.
  6: i64 route_count;
  // Seconds since source reachability last changed; unset if the source has
  // never reported the nexthop as reachable.
  7: optional i64 last_reachability_change_age_s;
  // Seconds since the IGP cost last changed; unset if no cost was received.
  8: optional i64 last_igp_cost_change_age_s;
}

/**
 * CLI result wrapper for `show bgpcpp nexthopinfo`, so the renderer stays a pure
 * function of its input rather than depending on out-of-band view-mode state on
 * the command object.
 *
 * detailed = false: compact list of every nexthop tracked by the RIB.
 * detailed = true : per-nexthop "zoom" view for the specifically queried IP(s).
 * queried_nexthops is index-parallel to entries in detailed mode, so a cache
 * miss can be reported against the exact address the operator asked for.
 */
struct TNexthopInfoQueryResult {
  1: bool detailed;
  2: list<TNexthopInfo> entries;
  3: list<string> queried_nexthops;
}

/**
 * Originated route along with host information that we are querying against
 */
struct TOriginatedRouteWithHost {
  1: list<TOriginatedRoute> tOriginatedRoutes;
  2: string host;
  3: string ip;
  4: string oobName;
}

/**
* Locally originated routes
*/
struct TOriginatedRoute {
  1: bgp_attr.TIpPrefix prefix;
  // Deprecated:
  //    Instead newer API returns the path which is associated with
  //    attributes containing local-pref, community, origin code etc.
  //    For backward compatibility with existing CLIs communities will
  //    be filled-in, but support will be removed in future.
  2: optional list<bgp_attr.TBgpCommunity> communities;
  3: bgp_route_types.TBgpPath path;
  4: i32 minimum_supporting_routes = 0;
  5: bool install_to_fib = false;
  6: i32 supporting_route_count = 0;
  7: optional string policy_name;
  8: optional bool require_nexthop_resolution; // only originate if nexthop is resolved
}

struct NetworkPathWithHost {
  @thrift.AllowUnsafeNonSealedKeyType
  1: map<
    bgp_attrTIpPrefix_cpptemplate_stdmap_895,
    list<bgp_route_types.TBgpPath>
  > networkPath;
  2: string host;
  3: string ip;
  4: string oobName;
}

/**
 * Attributes for locally originating routes
 */
struct TBgpAttributes {
  1: list<bgp_attr.TBgpCommunity> communities;
  2: optional bgp_attr.TAsPath as_path;
  3: optional i32 local_pref;
  4: optional list<bgp_route_types.TBgpExtCommunity> extCommunities;
  5: optional i32 origin;
  6: optional bgp_attr.TIpPrefix nexthop;
  7: optional bool install_to_fib;
}

/** Direction filter for BGP statistics. */
enum TDirectionFilter {
  INGRESS = 0,
  EGRESS = 1,
  BOTH = 2,
}

/**
* Policy stage filter for attribute statistics
*/
enum TPolicyStageFilter {
  PRE_POLICY = 0,
  POST_POLICY = 1,
  BOTH = 2,
}

/**
* Get Attribute memory statistics
*/
enum TAttributeStatsPayloadKind {
  UNKNOWN = 0,
  LEGACY_ATTRIBUTE_STATS = 1,
  DEDUPLICATOR_STATS = 2,
}

struct TAttributeStats {
  1: i64 total_num_of_attributes;
  2: i64 total_unique_attributes;
  3: double avg_attribute_refcount;
  4: double avg_community_list_len;
  5: double avg_extcommunity_list_len;
  6: double avg_as_path_len;
  7: double avg_cluster_list_len;
  8: double avg_topology_info_len;

  /**
   * Live size of each DeDuplicator<T>, i.e. how many DISTINCT values of that
   * type the daemon is currently storing. Read straight from
   * `DeDuplicator::size()`, so these are O(1) and, unlike fields 1-8, cost
   * nothing to produce -- they do not require walking the RIB.
   *
   * Six SEPARATE collections. They nest by containment, but each one counts
   * distinct values at ITS OWN level:
   *
   *   L1  dedup_bgp_path        BgpPathC       = attrs pointer + nexthop
   *                                              + topologyInfo
   *   L2    dedup_bgp_attributes  BgpAttributesC = the attribute bundle L1
   *                                              points at; no nexthop
   *   L3      dedup_as_path / dedup_communities / dedup_cluster_list /
   *           dedup_ext_communities, held BY the bundle as deduplicated
   *           POINTERS, so each is counted once here however many bundles
   *           reference it.
   *
   * A LEVEL IS NOT THE SUM OF THE LEVEL BELOW IT, in either direction. L2
   * counts distinct COMBINATIONS: A as_paths x C community sets can reach A*C
   * bundles, far above the L3 sum, while pairing them 1:1 gives max(A, C),
   * below it. `BgpAttributesC` also carries med / isMedSet / localPref /
   * atomicAggregate / aggregator / originatorId / weight, none of which have a
   * deduplicator -- bundles differing only in MED add L2 entries and no L3
   * entries at all.
   *
   * Nor does L1 bound L2: many paths differing only in nexthop share one
   * bundle, while bundles interned by transient or egress objects that never
   * become a stored BgpPath have no L1 entry. Either can exceed the other.
   *
   * Each field is named after the deduplicator it reports, so the name says
   * which level it belongs to. NOTE for anyone correlating with fb303: the L2
   * bundle count is published there as
   * `bgpcpp.deduplicated_attributes.total`. That counter name is kept for
   * continuity, but "total" is a misnomer -- it is the bundle count, never a
   * sum -- so it is deliberately NOT reproduced in this API.
   *
   * CAVEAT on dedup_bgp_path: `AdjRibEntry::setPreIn` and `setPostAttr` route
   * through DeDuplicatedBgpPath; `setPreOut` stores its path verbatim. In the
   * announce path that is not a gap -- preOut is handed the RIB best-entry
   * path, which reached the RIB as an already-interned postAttr -- but an
   * egress path that minted its own BgpPath would go uncounted here. See
   * AdjRibEntryTest for the pinned behaviour.
   */
  // L1
  9: optional i64 dedup_bgp_path;
  // L2
  10: optional i64 dedup_bgp_attributes;
  // L3
  11: optional i64 dedup_as_path;
  12: optional i64 dedup_communities;
  13: optional i64 dedup_cluster_list;
  14: optional i64 dedup_ext_communities;

  /**
   * Identifies which mutually exclusive payload is populated. The CLI sets
   * this after selecting an RPC path, so a legacy server does not need to
   * understand this field for the fallback path to be identified.
   */
  15: TAttributeStatsPayloadKind payload_kind = TAttributeStatsPayloadKind.UNKNOWN;
}

/**
 * Request wrapper for getDeduplicatorStats().
 *
 * The initial API always returns every deduplicator. Future filters or
 * snapshot options can be added here without changing the method signature.
 */
struct TGetDeduplicatorStatsRequest {}

/**
 * Statistics for one deduplicated collection.
 *
 * For example, entry_count = 42 means that the collection currently holds 42
 * distinct values; it is a count, not a byte size or reference count.
 */
struct TDeduplicatorCollectionStats {
  1: i64 entry_count;
}

/**
 * O(1) snapshot of the six BGP attribute deduplicators.
 *
 * Each collection is sampled independently, so the response is not an atomic
 * point-in-time snapshot across all six collections. A successful response
 * always contains every collection, including collections with zero entries.
 */
struct TGetDeduplicatorStatsResponse {
  /* L1: BgpPathC = attribute bundle + nexthop + topologyInfo. */
  1: TDeduplicatorCollectionStats bgp_path;

  /* L2: BgpAttributesC bundle. This is not a total of the other fields. */
  2: TDeduplicatorCollectionStats bgp_attributes;

  /* L3: sub-attributes held by BgpAttributesC. */
  3: TDeduplicatorCollectionStats as_path;
  4: TDeduplicatorCollectionStats communities;
  5: TDeduplicatorCollectionStats cluster_list;
  6: TDeduplicatorCollectionStats ext_communities;
}

/** Stable identity for one peer Adj-RIB. */
struct TAdjRibPeerKey {
  1: string peer_address;
  2: i64 remote_bgp_id;
}

/** Identity of the physical Adj-RIB-OUT group backing a peer. */
struct TAdjRibGroupKey {
  1: string egress_policy_name;
  2: i64 group_id;
}

/** Effective Adj-RIB-IN view for one peer. */
struct TAdjRibInPeerStats {
  1: TAdjRibPeerKey peer_key;
  2: string peer_name;
  3: i64 pre_policy_path_count;
  4: i64 post_policy_path_count;
  5: i64 active_prefixes;
  6: i64 active_paths;
  7: i64 stale_prefixes;
  8: i64 stale_paths;
}

/** Effective Adj-RIB-OUT view for one peer. */
struct TAdjRibOutPeerStats {
  1: TAdjRibPeerKey peer_key;
  2: TAdjRibGroupKey group_key;
  3: string peer_name;
  4: string peer_state;
  5: i64 active_prefixes;
  6: i64 active_paths;
  /**
   * Number of buckets in the packing list responsible for producing this
   * peer's updates. This is peer-local when update groups are disabled or the
   * peer is detached, and shared when the peer is in sync with an update
   * group.
   */
  7: i64 packing_list_size;
  /** Pending advertisement key size due to out-delay. */
  8: i64 out_delay_pending_keys;
}

struct TAdjRibInStats {
  1: list<TAdjRibInPeerStats> peers;
}

struct TAdjRibOutStats {
  1: list<TAdjRibOutPeerStats> peers;
}

struct TGetAdjRibStatsRequest {
  1: TDirectionFilter direction = TDirectionFilter.BOTH;
}

/**
 * O(peers) Adj-RIB statistics with no route-scale tree walk.
 * Peer snapshots have no ordering guarantee.
 */
struct TGetAdjRibStatsResponse {
  1: TAdjRibInStats rib_in;
  2: TAdjRibOutStats rib_out;
}

/**
* Filter parameters for attribute statistics
*/
struct TAttributeStatsFilter {
  1: TDirectionFilter direction = TDirectionFilter.BOTH;
  2: TPolicyStageFilter policyStage = TPolicyStageFilter.BOTH;
}

/**
* Get Entry memory statistics
*/
struct TEntryStats {
  1: i64 total_ucast_routes;
  2: i64 total_rib_paths;
  3: i64 total_adj_ribs;
  4: i64 total_originated_routes;
  5: i64 total_shadow_rib_entries;
  6: i64 total_netlink_wrapper_interfaces;
  /**
   * The number of interfaces that have a link-up hold right now
   * (BgpSettingConfig.enable_netlink_dampening). The value is zero when the
   * feature is off. This is a device total. To find which interface has a
   * hold, read the [LinkHold] log lines.
   */
  7: i64 total_netlink_wrapper_holds_active;
}

/**
 * The link-up hold times (link-flap dampening).
 *
 * initial_ms is the length of the first hold after a link-down. max_ms is the
 * longest hold, and it is also the decay window: a link that stays quiet for
 * this time returns to the initial hold.
 */
struct TNetlinkLinkUpHold {
  1: i32 initial_ms;
  2: i32 max_ms;
  // False when BgpSettingConfig.enable_netlink_dampening is off. The times are
  // then set but no link-up is held.
  3: bool enabled;
}

/**
 * Compact, server-computed summary of the BGP++ RIB for one address family,
 * analogous to Arista's "show ipv6 route summary". Returned by getRibSummary so
 * operators can see RIB scale without dumping the entire table.
 */
struct TRibSummary {
  // The address family this summary covers.
  1: bgp_attr.TBgpAfi afi;
  // Total number of prefixes in the RIB for this address family.
  2: i64 total_prefixes;
  // Histogram of prefix counts keyed by prefix (mask) length. Example: an entry
  // {64: 3751} means 3751 /64 prefixes. Only non-zero lengths are present.
  3: map<i16, i64> prefix_length_counts;
  // Best-path source breakdown: number of prefixes whose selected best path is
  // external (eBGP), internal (iBGP), confederation-external, or locally
  // originated. Analogous to Arista's "bgp External:/Internal:" split.
  4: i64 ebgp_prefixes;
  5: i64 ibgp_prefixes;
  6: i64 confed_ebgp_prefixes;
  7: i64 local_prefixes;
  // Number of unresolvable next-hops tracked in the RIB. This is a RIB-wide
  // (not per-address-family) count, so it is identical across the IPv4 and IPv6
  // responses; the CLI renders it once.
  8: i64 unresolvable_nexthops_count;
  // Number of prefixes (routes) in this address family that have no best path
  // because every candidate path's next-hop is unresolvable. Per-AFI, unlike
  // unresolvable_nexthops_count above: one unresolvable next-hop can back many
  // prefixes, and a prefix can have several next-hops. A null/drop route (0
  // next-hops) counts as a local best path, not as unresolved.
  9: i64 routes_with_unresolved_nexthops;
  // Total number of paths (route advertisements) held in the RIB for this
  // address family, summed across peers and including every add-path ID: one
  // peer advertising N add-path routes for a prefix contributes N. Same
  // semantic as TEntryStats.total_rib_paths, split per address family.
  10: i64 total_paths;
  // Subset of total_paths that best-path selection excluded as candidates --
  // today, paths whose next-hop is unresolvable. The remainder
  // (total_paths - inactive_paths) are the paths that entered selection, of
  // which the winners form each prefix's best/ECMP set. Mirrors the
  // bgpcpp.rib.inactive_path.count ODS gauge.
  //
  // Optional so a newer client can tell "server did not report this" apart from
  // a genuine zero: an older bgpd predating this field would otherwise
  // deserialize as 0 and the CLI would confidently render a fully-active RIB.
  // Consumers must omit the active/inactive split when this is unset rather
  // than substituting a default.
  11: optional i64 inactive_paths;
}

/**
 * BGP Profiler Statistics
 *
 * Lightweight coroutine profiler stats for performance monitoring.
 * Tracks execution time of critical BGP operations.
 */
struct TBgpProfilerStat {
  1: string name;
  2: i64 count;
  3: i64 p50_ms;
  4: i64 p90_ms;
  5: i64 p99_ms;
  6: i64 max_ms;
  7: i64 total_ms;
}

/**
 * Support debug level for BGP.
 */
enum TBgpDebugLevel {
  DEBUG0 = 0,
  DEBUG1 = 1,
  DEBUG2 = 2,
  DEBUG3 = 3,
  DEBUG4 = 4,
  DEBUG5 = 5,
  INFO = 6,
}

/**
 * Result codes for BGP policy change operations
 */
enum BgpPolicyChangeResult {
  POLICIES_APPLIED = 0,
  INTERNAL_ERROR = 1,
  INPUT_ERROR = 2,
  NOT_IMPLEMENTED = 3,
}

/**
 * Result codes for BGP config change operations
 */
enum BgpConfigChangeResult {
  CONFIG_APPLIED = 0,
  INTERNAL_ERROR = 1,
  INPUT_ERROR = 2,
  NOT_IMPLEMENTED = 3,
}

/**
* Standard Return Value with success and error message
*/
struct TResult {
  1: bool success = false;
  2: string err = "";
}

// Disk persistence format for BGP Rib to store Rib Policy
struct TRibPolicyStore {
  // Time in seconds at when the data was stored
  1: i64 storedTime;
  // rib policy data
  3: rib_policy.TRibPolicy policy;
  // File termination string to ensure proper truncation of file
  // Sanity check whether data storing completed properly
  100: string fileTermination;
}

/**
 * Drain state struct from BGP which includes
 *  - Node drain state
 *  - Interface drain state
 */
struct TBgpDrainState {
  1: optional bgp_policy.DrainState drain_state;
  2: optional list<string> drained_interfaces;
}

/**
 * Hold timer information for a BGP peer.
 * Exposes the remaining hold time before the session would be torn down.
 */
struct THoldTimerInfo {
  /** Peer IP address */
  1: string peer_address;
  /** Remaining hold time in milliseconds before the timer expires.
   *  Returns 0 for peers without an active hold timer or overdue timers. */
  2: i32 hold_time_remaining_ms;
}

/**
/**
 * Outcome of a single health check.
 * Used for per-check status, per-module status, and overall report status.
 * UNKNOWN=0: thrift defaults enum fields to 0 in newly constructed structs.
 *
 * PASS:    Check ran successfully; measured value is within healthy range.
 * WARN:    Check ran; value is degraded but not immediately traffic-impacting.
 * FAIL:    Check is in unhealthy state — either value is outside healthy
 *          range OR the check could not execute (e.g. counter missing).
 * SKIPPED: Check was intentionally not run (e.g. EBB-only check on DC).
 *
 * Precedence (worst wins): FAIL > WARN > PASS > SKIPPED
 */
enum HealthCheckStatus {
  UNKNOWN = 0,
  PASS = 1,
  FAIL = 2,
  SKIPPED = 3,
  WARN = 5,
}

/**
 * Health check category.
 * GLOBAL_* categories are subcategories under the "General BGP Health"
 * section in the RFC doc. Other categories map to individual BGP++ modules.
 * See: https://docs.google.com/document/d/1toZUF79Nx7erIU6RrrOJ12EXeQy7GrYHx-5YX7aIeOk
 */
enum HealthCheckCategory {
  UNKNOWN = 0,
  GLOBAL_SYSTEM = 1,
  GLOBAL_TASK_THREAD = 2,
  GLOBAL_CONVERGENCE = 3,
  SESSION_MANAGER = 4,
  PEER_MANAGER = 5,
  RIB = 6,
  NETLINK_WRAPPER = 7,
  NEXTHOP_TRACKER = 8,
  FIB_AGENT = 9,
  THRIFT_ENDPOINT = 10,
}

/**
 * Unique identifier for each health check.
 * Each category is allocated a range of 100 values for future expansion.
 * See: https://docs.google.com/document/d/1toZUF79Nx7erIU6RrrOJ12EXeQy7GrYHx-5YX7aIeOk
 */
enum HealthCheckId {
  HEALTH_CHECK_UNKNOWN = 0,

  /* GLOBAL_SYSTEM */
  GLOBAL_SYSTEM_THRIFT_REACHABLE = 101,
  GLOBAL_SYSTEM_RSS_MEMORY = 102,
  GLOBAL_SYSTEM_CPU_USAGE = 103,
  GLOBAL_SYSTEM_QUEUE_SIZES = 104,
  GLOBAL_SYSTEM_MEMORY_LEAK = 105,
  GLOBAL_SYSTEM_FD_COUNT = 106,
  GLOBAL_SYSTEM_THREAD_COUNT = 107,
  GLOBAL_SYSTEM_ATTR_DEDUP = 108,
  GLOBAL_SYSTEM_PLANNED_EXIT = 109,

  /* GLOBAL_TASK_THREAD_STATUS */
  GLOBAL_TASK_HEARTBEATS = 201,
  GLOBAL_TASK_UPTIME = 202,
  GLOBAL_TASK_STUCK_TASKS = 203,
  GLOBAL_TASK_WATCHDOG = 204,

  /* GLOBAL_CONVERGENCE */
  GLOBAL_CONVERGENCE_INITIALIZED = 301,
  GLOBAL_CONVERGENCE_TIME = 302,
  GLOBAL_CONVERGENCE_INIT_TIMEOUT = 303,
  GLOBAL_CONVERGENCE_EOR_RECEIVED = 304,
  GLOBAL_CONVERGENCE_INIT_PHASES = 305,
  GLOBAL_CONVERGENCE_SAFE_MODE = 306,
  GLOBAL_CONVERGENCE_STALE_PATHS = 307,
  GLOBAL_CONVERGENCE_STUCK_HANDSHAKE = 308,

  /* SESSION_MANAGER */
  SESSION_PORT_179 = 401,
  SESSION_ESTABLISHED = 402,
  SESSION_ACTIVE_ADJRIBS = 403,
  SESSION_FLAPS = 404,
  SESSION_HOLD_TIMER_EXPIRY = 405,
  SESSION_PEER_QUEUE = 406,
  SESSION_COLLISIONS = 407,
  SESSION_NOTIFICATIONS = 408,
  SESSION_SOCKET_BYTES = 409,
  SESSION_KEEPALIVES = 410,
  SESSION_SOCKET_ERRORS = 411,
  SESSION_TCP_WRITE_BLOCKS = 412,

  /* PEER_MANAGER */
  PEER_ZERO_ROUTES = 501,
  PEER_SLOW_DETACHED = 502,
  PEER_DROPPED_PREFIXES = 503,
  PEER_THRIFT_REJECTS = 504,
  PEER_PREFIX_COUNT = 505,
  PEER_POLICY_CACHE = 506,
  PEER_RIB_VERSION = 507,
  PEER_CHANGELIST_SIZE = 508,
  PEER_CONSUMER_LAG = 509,
  PEER_STUCK_BATONS = 510,
  PEER_PREFIX_LIMIT_DROPS = 511,

  /* RIB */
  RIB_ORIGINATED_ROUTES = 601,
  RIB_PATH_SELECTION_DURATION = 602,
  RIB_RECEIVED_ROUTES = 603,
  RIB_TABLE_VERSION = 604,
  RIB_ROUTE_CHURN = 605,
  RIB_OVERLOAD_MODE = 606,
  RIB_SHADOW_CONSISTENT = 607,
  RIB_PAUSE_TIME = 608,
  RIB_MODULE_RESPONSIVENESS = 609,

  /* NETLINK_WRAPPER */
  NETLINK_TRACKED_INTERFACES = 701,
  NETLINK_FIB_QUEUE = 702,
  NETLINK_SYNC_STATUS = 703,
  NETLINK_INTERFACE_STABLE = 704,
  NETLINK_SOCKET_CONNECTED = 705,

  /* NEXTHOP_TRACKER */
  NHT_CONFIG_CONSISTENT = 801,
  NHT_STREAM_CONNECTED = 802,
  NHT_UNREACHABLE_NEXTHOPS = 803,
  NHT_FSDB_SUBSCRIPTIONS = 804,
  NHT_CACHE_NOT_EMPTY = 805,

  /* FIB_AGENT */
  FIB_AGENT_CONNECTED = 901,
  FIB_AGENT_SYNCED = 902,
  FIB_AGENT_UPDATE_FAILURES = 903,
  FIB_AGENT_STATUS_FAILURES = 904,
  FIB_AGENT_ROUTES_EXIST = 905,
  FIB_AGENT_LAST_UPDATE_TIME = 906,
  FIB_AGENT_HOLDDOWN = 907,
  FIB_AGENT_FLUSH_EVENTS = 908,

  /* THRIFT_ENDPOINT */
  THRIFT_ALIVE = 1001,
  THRIFT_STREAMING_REJECTS = 1002,
  THRIFT_DRAIN_STATE = 1003,
  THRIFT_ACTIVE_CONNECTIONS = 1004,
}

/**
 * Result of a single health check.
 * Each check is identified by its HealthCheckId enum value.
 */
struct THealthCheckResult {
  /** Unique check identifier (use enumNameSafe() to get readable name) */
  1: HealthCheckId checkId;

  /** Health check category */
  2: HealthCheckCategory category;

  /** Pass/Warning/Fail/Skipped/Error */
  3: HealthCheckStatus status;

  /* Field 4 (severity) removed — ID reserved to prevent reuse */

  /** Human-readable detail with observed values */
  5: string message;

  /** Observed numeric value (when applicable) */
  6: optional double observedValue;

  /** Threshold compared against (when applicable) */
  7: optional double threshold;

  /** Unix epoch ms when the check ran */
  8: i64 timestampMs;
}

/**
 * Health report for a single category.
 */
struct TModuleHealthReport {
  /** Which category */
  1: HealthCheckCategory category;

  /** All check results for this category */
  2: list<THealthCheckResult> checks;

  /** PASS if all checks passed, FAIL if any failed, SKIPPED if all skipped */
  3: HealthCheckStatus overallStatus;
}

/**
 * Top-level health report aggregating all categories.
 * Returned by getHealthReport().
 */
struct THealthReport {
  /** Per-category reports, one per HealthCheckCategory */
  1: list<TModuleHealthReport> modules;

  /** PASS if all checks passed, FAIL if any failed */
  2: HealthCheckStatus overallStatus;

  /** Unix epoch ms when report was generated */
  3: i64 timestampMs;

  /** Summary counts */
  4: i32 passCount;
  5: i32 failCount;
  6: i32 skipCount;
  8: i32 warnCount;
}

/**
 * Direction for clearing a BGP neighbor session.
 * Used by clearBgpNeighbor() to specify the type of clear operation.
 */
enum ClearBgpNeighborDirection {
  UNKNOWN = 0,
  HARD_RESET = 1,
  ROUTE_REFRESH_IN = 2,
  ROUTE_REFRESH_OUT = 3,
}

/**
 * Filters for the getRejectedNetworks* calls. Every filter is optional; an
 * empty/unset filter matches everything. `direction` is the one exception: it
 * defaults to EGRESS rather than to "both", because rejection resolves against
 * a single Adj-RIB per walk and BOTH cannot be served.
 */
struct TRejectedNetworksRequest {
  /**
   * EGRESS reports routes we would send to the peer (pre-out present, post-out
   * denied); INGRESS reports routes we received from the peer. BOTH is not
   * supported -- rejection is resolved against one Adj-RIB per walk -- and is
   * refused rather than served as one side.
   *
   * EGRESS caveats on a peer in an update group (see getRejectedNetworksSummary
   * for why these are properties of the RIB-OUT, not of this API):
   *   - CRF rejections are not reported. The group-level egress path applies
   *     no route filter, so "Denied by CRF" never appears on a group entry.
   *     Egress *policy* rejections are reported normally.
   *   - For a peer detached from its group, only rejections recorded at or
   *     below its detach version are visible.
   */
  1: TDirectionFilter direction = TDirectionFilter.EGRESS;

  /**
   * RE2 pattern, partial match against the BGP session description (the
   * neighbor name). Empty matches every neighbor.
   * Example: "eb.*" matches description "eb001.f01.atn2".
   */
  2: string neighbor_name_regex = "";

  /**
   * Explicit peer addresses. Empty means "every neighbor matching
   * neighbor_name_regex". Example: ["fc00::1", "10.0.0.1"].
   */
  3: list<string> peers = [];

  /**
   * RE2 pattern, partial match against the name of the policy that rejected
   * the route. Empty matches every rejection reason. This is the server-side
   * equivalent of the CLI's "crf" subcommand.
   * Example: "^Denied by CRF$".
   */
  4: string policy_name_regex = "";

  /** Restrict to these prefixes. Empty means every rejected prefix. */
  5: list<bgp_attr.TIpPrefix> prefixes = [];

  /** Restrict to one address family. Unset means both. */
  6: optional bgp_attr.TBgpAfi afi;

  /**
   * Cap on the number of returned `groups`, used only by
   * getRejectedNetworksDetails. Capping here trims the returned list only; the
   * by_policy rollup and total_rejected_paths still cover everything walked.
   * 0 means unlimited, subject to a server-side hard cap that bounds handler
   * memory -- that one does bound prefix_count, see TRejectedPolicyStats.
   */
  7: i32 max_groups = 0;
}

/**
 * A neighbor that rejected a given prefix.
 */
struct TRejectedNeighbor {
  1: string peer_addr;
  /** BGP session description, e.g. "fsw001.p001.f01". */
  2: string description;
}

/**
 * One (prefix, rejecting policy) pair and the neighbors it applies to.
 */
struct TRejectedPrefixGroup {
  1: bgp_attr.TIpPrefix prefix;

  /**
   * Name of the policy that denied the route, verbatim from the policy
   * engine, e.g. "Denied by CRF" or "Denied by PROPAGATE_FSW_SSW_OUT term 2".
   * Empty when the Adj-RIB entry records no policy term for the rejection;
   * such rejections roll up under the "" key in by_policy.
   */
  2: string policy_name;

  /**
   * May be incomplete when the response's `truncated` is set: the device caps
   * the total neighbor entries across all groups, because each group can
   * otherwise name every walked neighbor.
   */
  3: list<TRejectedNeighbor> neighbors;
}

/**
 * Per-policy rollup, computed on the device.
 */
struct TRejectedPolicyStats {
  /**
   * Distinct prefixes this policy rejected. Exact while the response's
   * `truncated` is false. Once it is true the device has stopped tracking new
   * (prefix, policy) keys -- holding them just to keep this exact would grow
   * with RIB size, which is the allocation the cap exists to bound -- so this
   * becomes a lower bound. path_count and neighbor_count need no per-key
   * state and are exact either way.
   */
  1: i64 prefix_count;
  /** Individual (peer, prefix, path) rejections attributed to this policy. */
  2: i64 path_count;
  /** Distinct neighbors with at least one rejection by this policy. */
  3: i64 neighbor_count;
}

/** Rollup only, sized by the number of rejecting policies. */
struct TRejectedNetworksSummaryResponse {
  /** Policy name -> rollup. Covers every rejection walked. */
  1: map<string, TRejectedPolicyStats> by_policy;

  /** Total rejections walked. */
  2: i64 total_rejected_paths;

  /**
   * Neighbors the request filters selected, i.e. the neighbors this walk was
   * supposed to cover. A neighbor the filters did not select is not counted
   * here at all, so 0 means "the filters matched no neighbor", not "no
   * neighbor rejected anything".
   */
  3: i32 neighbors_matched;

  /**
   * True when the walk hit the server-side cap on tracked (prefix, policy)
   * keys, either overall or within a single neighbor's walk. Only
   * prefix_count is affected by it; total_rejected_paths, path_count and
   * neighbor_count still cover everything walked.
   */
  4: bool truncated;

  /**
   * Of the `neighbors_matched` above, how many the device could not finish
   * walking, and so contributed nothing to `by_policy` or
   * `total_rejected_paths`.
   *
   * Counts exactly one thing: the per-neighbor Adj-RIB walk raised or hit its
   * internal event-base timeout -- e.g. the PeerManager event base is wedged
   * under load, or the session was torn down between peer resolution and the
   * walk. A timeout also abandons the neighbors not yet walked, since work
   * already queued on a wedged event base cannot be cancelled and queuing
   * more behind it would only prolong the stall; those are counted here too.
   * It is NOT a filter miss (those never reach `neighbors_matched`) and it is
   * NOT a transport or connection error between client and device -- those
   * fail the RPC itself and you get no response at all, so they can never
   * show up here.
   *
   * Reading a result with neighbors_matched = X and peers_failed = Y: the
   * rollup describes exactly X - Y neighbors.
   *   - Y == 0: the answer is complete for all X.
   *   - Y > 0 and rejections found: the counts are a lower bound.
   *   - Y > 0 and no rejections found: inconclusive, NOT "nothing is being
   *     rejected". A fleet sweep should retry or narrow the request rather
   *     than record a clean result. Without this field that case is
   *     indistinguishable from a genuine zero -- the only evidence is an XLOG
   *     on the device, which a sweep never sees.
   */
  5: i32 peers_failed;
}

/** The same rollup plus the per-prefix breakdown, sized by rejection count. */
struct TRejectedNetworksDetailsResponse {
  /** One entry per (prefix, policy) pair. */
  1: list<TRejectedPrefixGroup> groups;

  /** Policy name -> rollup. Covers every rejection walked. */
  2: map<string, TRejectedPolicyStats> by_policy;

  /** Total rejections walked. Exact even when `groups` is truncated. */
  3: i64 total_rejected_paths;

  /**
   * Neighbors the request filters selected. Same meaning as on
   * TRejectedNetworksSummaryResponse.
   */
  4: i32 neighbors_matched;

  /**
   * True when `groups` is incomplete. Either the list was trimmed to
   * max_groups, the walk hit the server-side cap on tracked keys, or some
   * group's `neighbors` hit the server-side cap on neighbor entries.
   * total_rejected_paths, path_count and neighbor_count are exact in every
   * case; prefix_count is a lower bound only when the tracked-key cap was
   * hit, see TRejectedPolicyStats.
   */
  5: bool truncated;

  /**
   * Of the `neighbors_matched` above, how many the device could not finish
   * walking. Same meaning as on TRejectedNetworksSummaryResponse: a per-
   * neighbor Adj-RIB walk that raised or timed out on the device, or was
   * abandoned after an earlier one timed out -- never a filter miss and never
   * a client/device transport error. Non-zero means
   * `groups` and the rollup are both missing those neighbors' rejections, so a
   * zero result is inconclusive rather than "nothing rejected".
   */
  6: i32 peers_failed;
}

// @lint-ignore THRIFTCHECKS facebook-service-deprecated existing service inheritance is out of scope for this API addition
service TBgpService extends fb303.FacebookService {
  /**
   * Return build metadata for the running BGP process.
   */
  map<string, string> getBuildInfo();

  /**
   * [Logging]
   *
   * Dynamically set log level for BGP
   *
   * @param levelString: string representation of log level
   *                     (e.g. ".=DBG1,foo.bar=INFO")
   */
  void setLogLevel(1: string levelString);

  /**
   * [Config]
   *
   * Get running config
   */
  string getRunningConfig();

  /**
   * [Config]
   *
   * Same as getRunningConfig, but returns a Thrift struct
   */
  bgp_config.BgpConfig getRunningConfigStruct();

  /**
   * Has policy config artifact
   */
  bool hasPolicySymlink();

  /**
   * [Config]
   *
   * Get policy config
   */
  string getPolicyConfig();

  /**
   * [Config]
   *
   * Validate given config
   * @return <true, ''> on validate config
   *         <false, 'Error Msg'> on invalidate config
   */
  TResult validateConfig(1: string file_name);

  /**
   * [Config]
   *
   * Validate given config and policy as separate artifacts
   * @return <true, ''> on validate config
   *         <false, 'Error Msg'> on invalidate config
   */
  TResult validateConfigAndPolicy(
    1: string config_file_name,
    2: string policy_file_name,
  );

  /**
   * [Config]
   *
   * Fetch and return drain state inside config
   * @return DrainState defined inside bgp_policy.thrift
   */
  TBgpDrainState getDrainState();

  /**
   * [Initialization]
   *
   * Check if BGP++ mark itself converged.
   * @return true when BGP++ marked itself INITIALIZED;
   *         false when BGP++ yet finished initialization sequence;
   */
  bool initializationConverged();

  /**
   * [Initialization]
   *
   * Fetch the initialization event and its corresponding time duration.
   * @return a map of BgpInitializationEvent -> timeDuration mapping.
   */
  map_bgp_thriftBgpInitializationEvent_i64_cpptemplate_stdunordered_map_895 getInitializationEvents();

  /**
   * [Initialization]
   *
   * Return the delta between now and the timestamp of last programmed routes.
   * @return the delta in SECONDS.
   * ATTN: if NO fib programmed routes, return a negative number instead.
   */
  i64 getTimeElapsedSinceLastFibUpdate();

  /**
   * Get the current RIB version. This is a monotonically increasing counter
   * that increments whenever a material routing change occurs (best path
   * or multipath changes). Used for tracking how caught up each peer is
   * with RIB state (backpressure visibility).
   */
  i64 getRibVersion();

  /**
   * Get the total number of prefixes currently installed in the loc-RIB
   * (i.e. the number of entries in RibBase::ribEntries_). This is a
   * device-wide count across all address families.
   */
  i64 getNumPrefixes();

  /**
   * Get the BGP++ process uptime in seconds (time since the daemon started).
   * Sourced from the Watchdog process start time, mirroring the
   * bgpd.process.uptime.seconds ODS counter.
   */
  i64 getProcessUptimeSeconds();

  /*
   * Get locally originated routes
   */
  list<TOriginatedRoute> getOriginatedRoutes();

  /*
   * Get route distribution table
   */
  map<string, map<string, bool>> getRouteDistributionTable();

  /**
   * Get a list of active sessions
   */
  list<TBgpSession> getBgpSessions();

  /**
   * Get a list of active stream sessions
   */
  list<TBgpStreamSession> getBgpStreamSessions();

  /**
   * Get deatiled info on list of sessions by peer
   */
  list<TBgpSession> getBgpNeighbors(1: list<string> peer);

  /**
   * Get detailed info on peer egress backpressure statistics.
   */
  list<TPeerEgressStats> getPeerEgressStats();

  /**
   * Get detailed information about all active update groups.
   */
  TGetUpdateGroupInfoResponse getUpdateGroupInfo(
    1: TGetUpdateGroupInfoRequest request,
  );

  /**
   * Get lightweight summary information for all active update groups.
   */
  TGetUpdateGroupSummariesResponse getUpdateGroupSummaries();

  /**
   * Get local config information
   */
  TBgpLocalConfig getBgpLocalConfig();

  /**
   * Routes we receive from peer, before policy application
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPrefilterReceivedNetworks(1: string peer);

  /**
   * Routes we receive from peer, before policy application with add path
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPrefilterReceivedNetworks2(1: string peer);

  /**
   * Routes we receive from one bgp session of a peer, before policy application
   * @param peer: IP address of the peer
   *        sessionBgpId: BGP ID of the session in IPv4 format
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPrefilterReceivedNetworksFromSession(
    1: string peer,
    2: string sessionBgpId,
  );

  /**
   * Routes we receive from one bgp session of a peer, before policy application with add path
   * @param peer: IP address of the peer
   *        sessionBgpId: BGP ID of the session in IPv4 format
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPrefilterReceivedNetworksFromSession2(
    1: string peer,
    2: string sessionBgpId,
  );

  /**
   * Routes we receive from peer, after policy application
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPostfilterReceivedNetworks(1: string peer);

  /**
   * Routes we receive from peer, after policy application with add path
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPostfilterReceivedNetworks2(1: string peer);

  /**
   * Routes we receive from one bgp session of a peer, after policy application
   * @param peer: IP address of the peer
   *        sessionBgpId: BGP ID of the session in IPv4 format
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPostfilterReceivedNetworksFromSession(
    1: string peer,
    2: string sessionBgpId,
  );

  /**
   * Routes we receive from one bgp session of a peer, after policy application with add path
   * @param peer: IP address of the peer
   *        sessionBgpId: BGP ID of the session in IPv4 format
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPostfilterReceivedNetworksFromSession2(
    1: string peer,
    2: string sessionBgpId,
  );

  /**
   * Get stuff we send to peer after policy
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPostfilterAdvertisedNetworks(1: string peer);

  /**
   * Get stuff we send to peer after policy with add path
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPostfilterAdvertisedNetworks2(1: string peer);

  /**
   * Get stuff we send to peer prior to policy
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    bgp_route_types.TBgpPath
  > getPrefilterAdvertisedNetworks(1: string peer);

  /**
   * Get stuff we send to peer prior to policy with add path
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getPrefilterAdvertisedNetworks2(1: string peer);

  /**
   * Routes the ingress/egress policy denied, aggregated on the device.
   *
   * Server-side equivalent of
   *   "show bgp neighbors-by-name <regex> {advertised,received} rejected [crf]".
   *
   * The prefilter/postfilter pair above returns both full pre- and
   * post-policy tables and leaves the caller to diff them, so the payload
   * scales with RIB size. This walks each matching neighbor's Adj-RIB once and
   * emits only the denied routes plus the name of the policy that denied them,
   * so the response scales with the number of rejections instead. Intended for
   * fleet-wide collection.
   *
   * This returns counts only: one by_policy entry per rejecting policy, so the
   * payload is the same whether a policy denied two prefixes or two million.
   * Use getRejectedNetworksDetails when you need to know which prefixes.
   *
   * Note: rejection is evaluated per Adj-RIB entry, so with add-path a prefix
   * with one accepted and one denied path IS reported. The CLI diffs
   * prefix-keyed maps and reports nothing in that case.
   *
   * Note: with update groups enabled, a peer's RIB-OUT entries live under the
   * group owner key and are shared across the group's members, so an EGRESS
   * rejection recorded once for the group is reported once per member. Two
   * consequences, both properties of how the RIB-OUT is built rather than of
   * this walk: the group egress path applies no route filter, so CRF
   * rejections do not exist to report; and a detached peer only shares group
   * entries at or below its detach version. See the `direction` field.
   */
  TRejectedNetworksSummaryResponse getRejectedNetworksSummary(
    1: TRejectedNetworksRequest request,
  );

  /**
   * Same walk, filters and rollup as getRejectedNetworksSummary, plus one
   * `groups` entry per (prefix, policy) pair naming the neighbors it applies
   * to. A superset of the summary, so there is no need to call both.
   *
   * The payload grows with the number of rejections rather than the number of
   * policies, so prefer the summary for wide sweeps and use this once the scope
   * is narrowed. Bound it with max_groups; `truncated` reports completeness.
   */
  TRejectedNetworksDetailsResponse getRejectedNetworksDetails(
    1: TRejectedNetworksRequest request,
  );

  /**
   * Get post-policy network information for stream subscribers
   *
   * @param peerID - integer ID of BGP stream subscriber
   * @param policy-type - must be either "pre-policy" or "post-policy"
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<
    bgp_attr.TIpPrefix,
    list<bgp_route_types.TBgpPath>
  > getSubscriberNetworkInfo(1: i32 peerID, 2: string policy_type);

  /**
   * Announce network, afi inferred from prefix. This does not create FIB entry.
   *
   * @param prefix - The prefix to announce
   * @param communities - The list of communities to attach to prefix
   */
  void addNetwork(
    1: bgp_attr.TIpPrefix prefix,
    2: list<bgp_attr.TBgpCommunity> communities,
  );

  /**
   * Remove a previously advertised network. This does not clear FIB entry.
   *
   * @param prefix - The prefix to remove
   */
  void delNetwork(1: bgp_attr.TIpPrefix prefix);

  /**
   * Announce networks, afi inferred from prefix.
   * This does not create FIB entry.
   *
   * @param networks (prefix, attributes) to announce
   */
  @hack.SkipCodegen{reason = "Invalid argument"}
  void addNetworks(
    @thrift.AllowUnsafeNonSealedKeyType
    1: map<bgp_attr.TIpPrefix, TBgpAttributes> networks,
  );

  /**
   * Remove a previously advertised network.
   * This does not clear FIB entry.
   *
   * @param prefixes - The prefixes to remove
   */
  @hack.SkipCodegen{reason = "Invalid argument"}
  void delNetworks(
    @thrift.AllowUnsafeNonSealedKeyType
    1: set<bgp_attr.TIpPrefix> prefixes,
  );

  /**
   * Shutdown a peer session
   *
   * @param peer - the peer ip address
   */
  void shutdownSession(1: string peer);

  /**
   * Restart a peer session
   *
   * @param peer - the peer ip address
   */
  void restartSession(1: string peer);

  /**
   * Start a peer session
   *
   * @param peer - the peer ip address
   */
  void startSession(1: string peer);

  /**
   * Clear a BGP neighbor session
   *
   * @param peer - the peer ip address (ROUTE_REFRESH_IN/ROUTE_REFRESH_OUT
   *   require a static IP, not CIDR)
   * @param direction - the clear operation type:
   *   HARD_RESET: tear down and re-establish the session
   *   ROUTE_REFRESH_IN: send Route Refresh request to the peer (requires
   *     Route Refresh capability)
   *   ROUTE_REFRESH_OUT: re-send our routes to the peer (triggers AFI-scoped
   *     RIB re-dump)
   * @param afi - address family filter (AFI_IPV4, AFI_IPV6, or AFI_ALL for both)
   */
  void clearBgpNeighbor(
    1: string peer,
    2: ClearBgpNeighborDirection direction,
    3: bgp_attr.TBgpAfi afi,
  );

  /**
   * Dump the current BGP RIB (prefixes learned from others)
   *
   * @param afi - The afi to dump RIB for
   */
  list<bgp_route_types.TRibEntry> getRibEntries(1: bgp_attr.TBgpAfi afi);

  /**
   * Get compact FIB-out state for one exact prefix.
   *
   * The response omits BGP paths and other RIB attributes. Exact-prefix
   * lookup bounds RIB event-base work independently of total RIB size.
   *
   * @param request - Exact-prefix query parameters
   */
  bgp_route_types.TFibOutTable getFibOutPrefix(
    1: bgp_route_types.TFibOutPrefixRequest request,
  );

  /**
   * Get a compact summary of the BGP RIB (total prefixes + per-prefix-length
   * histogram) for one address family, without dumping the full table.
   *
   * @param afi - The afi to summarize the RIB for
   */
  TRibSummary getRibSummary(1: bgp_attr.TBgpAfi afi);

  /** Get canonical nexthop sets in current BGP FIB-out state. */
  bgp_route_types.TFibNexthopDatabase getFibNexthopDatabase();

  /**
   * Dump the current BGP RIB in canonical (deduplicated) form -- the same
   * content as getRibEntries(), encoded as a single TCanonicalRibState (shared
   * attr / path / peer pools + per-prefix entries) for a far smaller payload.
   *
   * @param afi - The afi to dump RIB for
   */
  bgp_route_types.TCanonicalRibState getRibEntriesCanonical(
    1: bgp_attr.TBgpAfi afi,
  );

  /**
   * Get a single prefix from the RIB in canonical (deduplicated) form.
   *
   * @param prefix - The string representation of the prefix to get
   */
  bgp_route_types.TCanonicalRibState getRibPrefixCanonical(1: string prefix);

  /**
   * Fetch routes in the RIB matching communities in canonical form.
   * Only paths matching at least one community are returned (match-any logic).
   *
   * @param afi - ipv4 or ipv6
   * @param community_ids - List of community strings (ASN:NN or integer or well-known)
   */
  bgp_route_types.TCanonicalRibState getRibEntriesForCommunitiesCanonical(
    1: bgp_attr.TBgpAfi afi,
    2: list<string> community_ids,
  );

  /**
   * Fetch routes in the RIB matching a single community in canonical form.
   *
   * @param afi - ipv4 or ipv6
   * @param community_id - Community string (ASN:NN or integer or well-known)
   */
  bgp_route_types.TCanonicalRibState getRibEntriesForCommunityCanonical(
    1: bgp_attr.TBgpAfi afi,
    2: string community_id,
  );

  /**
   * Get RIB entries for subprefixes in canonical (deduplicated) form.
   *
   * @param prefix - The string representation of the parent prefix
   */
  bgp_route_types.TCanonicalRibState getRibSubprefixesCanonical(
    1: string prefix,
  );

  /**
   * Dump the current Shadow RIB (prefixes learned from the RIB)
   *
   * Though both getRibEntries and getShadowRibEntries provide so
   * called list of TRibEntry. This list differs in interpreation
   *
   * getRibEntries provides a prefix with a set of paths that
   * include both a group of best equal cost paths and a group
   * of other higher cost (not in the group of best). One of
   * the paths from the group of "best" is marked as selected
   * the best path for non-add-path peers
   *
   * getShadowRibEntries contains only group of best equal cost
   * paths to be advertised. And the representation of it when
   * returned through this API will show selected "best" path
   * differetiating from the group of all the best equal cost
   * paths ("multiPaths")
   *
   * @param afi - The afi to dump Shadow RIB for
   */
  list<bgp_route_types.TRibEntry> getShadowRibEntries(1: bgp_attr.TBgpAfi afi);

  /**
   * Dump the current Shadow RIB in canonical (deduplicated) form.
   *
   * @param afi - The afi to dump Shadow RIB for
   */
  bgp_route_types.TCanonicalRibState getShadowRibEntriesCanonical(
    1: bgp_attr.TBgpAfi afi,
  );

  list<bgp_route_types.TRibEntry> getChangeListEntries(1: bgp_attr.TBgpAfi afi);

  /**
   * Dump the current ChangeList in canonical (deduplicated) form.
   *
   * @param afi - The afi to dump ChangeList for
   */
  bgp_route_types.TCanonicalRibState getChangeListEntriesCanonical(
    1: bgp_attr.TBgpAfi afi,
  );

  /**
   * Dump the current BGP RIB (prefixes learned from others)
   *
   * @param string - The string representation of the prefix to get
   */
  list<bgp_route_types.TRibEntry> getRibPrefix(1: string prefix);

  /**
   * Fetch the routes in bgp-local-rib matching the passed in commuinity.
   * Note that *ONLY* paths matching community of a route-entry in
   * the local-rib are returned, i.e. route's paths which does not match
   * passed community are filtered out (even though those might be part of
   * bestpath or ecmp/ucmp paths).
   *
   * @param afi - ipv4 or ipv6
   *
   * @param string - The string represents ASN:NN (both 16bits) value or
   *                 an integer (32bits) representing community.
   *                 Param can also represnet a well-known community from:
   *                 internet | no-advertise | no-export | no-export-subconfed
   */
  list<bgp_route_types.TRibEntry> getRibEntriesForCommunity(
    1: bgp_attr.TBgpAfi afi,
    2: string community_id,
  );

  /**
   * Fetch the routes in bgp-local-rib matching the passed in commuinity.
   * Note that *ONLY* paths matching community of a route-entry in
   * the local-rib are returned, i.e. route's paths which does not match
   * passed community are filtered out (even though those might be part of
   * bestpath or ecmp/ucmp paths).
   * @param afi - ipv4 or ipv6
   *
   * @param list<string> - The string represents ASN:NN (both 16bits) value or
   *                       an integer (32bits) representing community.
   *                       Param can also represnet a well-known community from:
   *                       internet | no-advertise | no-export | no-export-subconfed
   *
   * community_ids applies match-any logic (in contrast of match-all)
   * e.g. rib entry in returned list should match at least 1 item in community_ids.
   * when community_ids is emtpy, no rib entry should be returned.
   */
  list<bgp_route_types.TRibEntry> getRibEntriesForCommunities(
    1: bgp_attr.TBgpAfi afi,
    2: list<string> community_ids,
  );

  /*
   * Dump the current BGP RIB whose peer address is in the subnet indicated
   * by a given prefix
   *
   * @param prefix - The string representation of the prefix
   */
  list<bgp_route_types.TRibEntry> getRibSubprefixes(1: string prefix);

  /**
   * Get RibPolicy.
   *
   * @return thrift struct of TRibPolicy
   */
  rib_policy.TRibPolicy getRibPolicy();

  /**
   * Clear RibPolicy.
   *
   */
  void clearRibPolicy();

  /**
   * [Route Attribute Policy]
   */
  /**
   * Set RouteAttributePolicy.
   *
   * @param policy - The route attribute policy user wants to set with
   * @return <true, ''> on route attribute policy setting success
   *         <false, 'Error Msg'> on route attribute policy setting failure
   */
  TResult setRouteAttributePolicy(1: rib_policy.TRouteAttributePolicy policy);

  /**
   * Get RouteAttributePolicy.
   *
   * @return thrift struct of TRouteAttributePolicy
   */
  rib_policy.TRouteAttributePolicy getRouteAttributePolicy();

  /**
   * Clear RouteAttributePolicy.
   */
  void clearRouteAttributePolicy();

  /**
   * [Path Selection Policy]
   */
  /**
   * Set PathSelectionPolicy.
   *
   * @param policy - The path selection policy user wants to set with
   * @return <true, ''> on path selection policy setting success
   *         <false, 'Error Msg'> on path selection policy setting failure
   */
  TResult setPathSelectionPolicy(1: rib_policy.TPathSelectionPolicy policy);

  /**
   * Get PathSelectionPolicy.
   *
   * @return thrift struct of TPathSelectionPolicy
   */
  rib_policy.TPathSelectionPolicy getPathSelectionPolicy();

  /**
   * Clear PathSelectionPolicy.
   * Note: When CPS FILE_MODE is active, this operation is silently skipped.
   */
  void clearPathSelectionPolicy();

  /**
   * [Path Selection Policy - File Mode]
   * Refresh CPS policy from the local artifact file.
   * Reads CpsPolicyArtifact, syncs dryrun mode, and applies policy if
   * dryrun=false (FILE_MODE).
   */
  TResult setCpsPolicyFromFile();

  /**
   * Get the active path selection criteria for the given prefixes.
   *
   * @return a list of TPathSelector which contains the path selection
   *   criteria that is active for the corresponding prefix, i.e., it has
   *   at most one TPathSelectionCriteria in criteria_list. If it is empty,
   *   and bgp_native_path_selection_min_nexthop is not set, the prefix
   *   either does not exist or applies default multipathSelector
   */
  list<rib_policy.TPathSelector> getActivePathSelectionCriteria(
    1: list<string> prefixes,
  );

  /**
   * [Route Filter Policy]
   */
  /**
   * Set RouteFilterPolicy.
   *
   * @param policy - The route filter policy user wants to set with
   * @return <true, ''> on route filter policy setting success
   *         <false, 'Error Msg'> on route filter policy setting failure
   */
  TResult setRouteFilterPolicy(1: rib_policy.TRouteFilterPolicy policy);

  /**
   * [Link-up hold]
   */
  /**
   * Set the link-up hold times without a restart.
   *
   * bgpd reads bgp_netlink_link_up_hold_initial_ms and
   * bgp_netlink_link_up_hold_max_ms one time at start-up, and a gflag cannot
   * change while bgpd runs. Each restart is a graceful restart event, so a
   * qualification test must change these times another way.
   *
   * The netlink fiber reads the new values at the next link-down. A hold that
   * already started keeps its length. To set max_ms to a small value therefore
   * collapses the ladder within one hold.
   *
   * @param initial_ms - the first hold, in milliseconds. Must be more than 0.
   * @param max_ms - the longest hold, in milliseconds. Must be at least
   *                 initial_ms.
   * @return <true, ''> when bgpd accepted the times
   *         <false, 'Error Msg'> when a value is out of range, or when this
   *         build has no NetlinkWrapper
   */
  TResult setNetlinkLinkUpHold(1: i32 initial_ms, 2: i32 max_ms);

  /**
   * [Link-up hold]
   */
  /**
   * Show the link-up hold times that bgpd uses now.
   */
  TNetlinkLinkUpHold getNetlinkLinkUpHold();

  /**
   * [Route Filter Policy]
   */
  /**
   * Clear Ingress and Egress Route Filter Policy.
   *
   * @param
   * @return <true> on policy clearing success
   *         <false> on policy clearing failure
   */
  void clearIngressEgressRouteFiltersPolicy();

  /**
   * [Route Filter Policy]
   */
  /**
   * Clear Golden Prefixes Policy.
   *
   * @param
   * @return <true, ''> on policy clearing success
   *         <false, 'Error Msg'> on policy clearing failure
   */
  void clearGoldenPrefixesPolicy();

  /**
   * [Route Filter Policy]
   */
  /**
   * Get if device is in safemode.
   *
   * @param
   * @return <true> device is in safe mode
   *         <false> device is not in safe mode
   */
  bool getIsSafeModeOn();

  /**
   * Get total golden VIPs count.
   *
   * @param
   * @return total golden VIPs count
   */
  i64 getGoldenVipsCount();

  /**
   * [Route Filter Policy]
   */
  /**
   * Remove safe mode file.
   *
   * @param
   * @return
   */
  void removeSafeModeFile();

  /**
   * Return the golden prefixes policy and it's status (active or inactive).
   */
  TGoldenPrefixesPolicyStatus getGoldenPrefixesPolicyStatus();

  /**
   * Return a mapping from golden prefix to the number of its unique subnets.
   */
  map<string, i32> getGoldenPrefixSubnetCounts();

  /**
   * Get RouteFilterPolicy.
   *
   * @return thrift struct of TRouteFilterPolicy
   */
  rib_policy.TRouteFilterPolicy getRouteFilterPolicy();

  /**
   * Clear RouteFilterPolicy.
   * Note: When CRF FILE_MODE is active, this operation is silently skipped.
   */
  void clearRouteFilterPolicy();

  /**
   * [Route Filter Policy - File Mode]
   * Refresh CRF policy from the local artifact file.
   * Reads CrfPolicyArtifact, syncs dryrun mode, and applies policy if
   * dryrun=false (FILE_MODE).
   */
  TResult setCrfPolicyFromFile();

  /**
   * [Watchdog]
   *
   * @param A list of path to the monitored queues/modules
   *    e.g., peer_manager.queue1 returnes the queue sizes of the specific queue
   *    while peer_manager returns all queue sizes monitored in the module
   *    ab empty list would get all of the modules
   * @return a map from monitored paths to the queue sizes, such as
   *    {'peer_manager.queue1', 3}
   */
  monitored_queue_size_map getMonitoredQueueSizes(1: list<string> paths);

  /**
   * Deprecated wire-compatibility placeholder. Returns an empty response
   * without scanning the Adj-RIB. Use getDeduplicatorStats instead.
   */
  TAttributeStats getAttributeStats();

  /**
   * Get an O(1) snapshot of the BGP attribute deduplicators.
   */
  TGetDeduplicatorStatsResponse getDeduplicatorStats(
    1: TGetDeduplicatorStatsRequest request,
  );

  /**
   * Get cached per-peer Adj-RIB-IN and effective Adj-RIB-OUT statistics without
   * traversing a radix tree.
   */
  TGetAdjRibStatsResponse getAdjRibStats(1: TGetAdjRibStatsRequest request);

  /**
   * Deprecated wire-compatibility placeholder. Returns an empty response
   * without scanning the Adj-RIB.
   */
  TAttributeStats getAttributeStatsFiltered(1: TAttributeStatsFilter filter);

  /**
   * Get entry memory statistics
   */
  TEntryStats getEntryStats();

  /**
   * Get bgp (ingress/egress) policy statistics.
   *
   * @return thrift struct of TPolicyStats
   */
  policy_thrift.TPolicyStats getPolicyStats();

  /**
   * Change log level dynamicaly.
   * For now we support only root-category (recursive) log-level setting.
   * TODO: Allow for per-category log level alteration in future.
   */
  void setDebugLevel(1: TBgpDebugLevel level);

  /**
   * Get routes announced to a peer for specific prefixes  (post-policy)
   *
   * @param peerId: peer that we are asking about
   * @param pfxs: prefixes that we care about
   * @returns: map of prefix -> networks for each of the given prefixes
   * that are announced to this peer.
   */
  @hack.SkipCodegen{reason = "Invalid return type"}
  map<bgp_attr.TIpPrefix, TBgpNetwork> getAdvertisedNetworksFiltered(
    1: string peerId,
    2: list<bgp_attr.TIpPrefix> pfxs,
  );

  /**
   * Get network we receive from peer (pre-policy)
   *
   * @param peerId: peer that we are asking about
   * @returns: map of "route sources" to list of networks for each source
   */
  list<TBgpNetwork> getReceivedNetworks(1: string peerId);

  /**
   * Get network we announce to peer (post-policy)
   *
   * @param peerId: peer that we are asking about
   * @returns: list ot networks
   */
  list<TBgpNetwork> getAdvertisedNetworks(1: string peerId);

  /**
   * Get sessions details for a particular peer and established session
   *
   * @param peerID: peer that we are asking about
   * @param sessionBgpID: specific session for the peer that we are asking about
   * @returns: list of active sessions
  */
  list<TBgpSession> getBgpNeighborsFromSession(
    1: string peerId,
    2: string sessionBgpId,
  );

  /**
   * Set BGP policy for specific peers
   *
   * @param peersPolicy: Map of peer IDs to direction-specific policy names
   * @returns: Result indicating success/failure status
   */
  BgpPolicyChangeResult setPeersPolicy(
    1: map<string, map<bgp_policy.DIRECTION, string>> peersPolicy,
  );

  /**
   * Set BGP policy for peer groups
   *
   * @param peerGroupsPolicy: Map of peer group IDs to direction-specific policy names
   * @returns: Result indicating success/failure status
   */
  BgpPolicyChangeResult setPeerGroupsPolicy(
    1: map<string, map<bgp_policy.DIRECTION, string>> peerGroupsPolicy,
  );

  /**
   * Unset BGP policy for specific peers, causing them to fall back to their
   * peer group's policy via the policy resolution hierarchy.
   *
   * @param peersToUnset: Map of peer IDs to set of directions to unset
   * @returns: Result indicating success/failure status
   */
  BgpPolicyChangeResult unsetPeersPolicy(
    1: map<string, set<bgp_policy.DIRECTION>> peersToUnset,
  );

  /**
   * Incorporate a collection of BGP peers into the operational configuration.
   * Each peer config may optionally reference an existing peer_group_name
   * to facilitate configuration inheritance from the designated peer group.
   *
   * Currently accepted BgpPeer fields:
   *   - peer_addr
   *   - local_addr
   *   - next_hop4
   *   - next_hop6
   *   - description
   *   - peer_id
   *   - ingress_policy_name
   *   - egress_policy_name
   *   - remote_as_4_byte
   *   - peer_group_name
   * Setting any other field will return BgpConfigChangeResult::INPUT_ERROR.
   *
   * @param peers - A list of BgpPeer configurations to be added
   *                (definition - https://fburl.com/code/ha28hjuf)
   * @returns: A BgpConfigChangeResult object indicating the outcome
   */
  BgpConfigChangeResult addPeers(1: list<bgp_config.BgpPeer> peers);

  /**
   * Remove BGP peers from the operational configuration.
   * Only static peers (non-CIDR) are accepted. Dynamic peers are rejected.
   * Non-existent peer addresses are treated as no-ops.
   *
   * @param peerAddrs - A list of peer address strings (IPv4 or IPv6) to remove
   * @returns: A BgpConfigChangeResult indicating the outcome
   */
  BgpConfigChangeResult delPeers(1: list<string> peerAddrs);

  /**
   * Get nexthop information for a specific prefix
   *
   * @param prefix: The prefix to query nexthop information for
   * @returns: NexthopInfo containing nexthop details for the prefix
   */
  TNexthopInfo getNexthopInfoForNexthop(1: string prefix);

  /**
   * Get nexthop information for the given nexthops. If the list is empty,
   * returns nexthop info for ALL entries currently in the nexthop cache.
   *
   * @param nexthops: nexthop IP addresses to query; empty = all cache entries
   * @returns: list of TNexthopInfo, one per matching nexthop-cache entry
   */
  list<TNexthopInfo> getNexthopInfos(1: list<string> nexthops);

  /**
   * [Profiler]
   *
   * Start/Stop BGP Profiler
   * @param enable - true to start, false to stop
   */
  void startProfiler(1: bool enable);

  /**
   * [Profiler]
   *
   * Set regex filter for BGP Profiler
   * Only functions matching the regex will be profiled.
   * Pass empty string to clear the filter.
   * @param regex - regex string to filter function names
   */
  void setProfilerFilter(1: string regex);

  /**
   * [Profiler]
   *
   * Get BGP Profiler Stats
   * @returns: List of profiler statistics for each tracked function
   */
  list<TBgpProfilerStat> getProfilerStats();

  /**
   * [Profiler]
   *
   * Clear BGP Profiler Stats
   * Resets all counters and histograms.
   */
  void clearProfilerStats();

  /**
   * [Debug] Reset per-peer cumulative BGP message counters in BOTH directions:
   * socket tx/rx counts (SessionManager), AdjRib sent/recv message counts,
   * update-group sent counts, and their fb303 keys. Live prefix gauges are not
   * affected.
   *
   * `peers` selects which peers to clear, by IP address; an empty list clears
   * every peer.
   */
  void clearCounters(1: list<string> peers);

  /**
   * [Telemetry]
   *
   * Get the remaining hold timer value for BGP peers.
   * Returns per-peer hold timer info including the remaining time
   * before the hold timer expires.
   *
   * @param peers: list of peer IP addresses to query. If empty, returns
   *               hold timer info for all peers.
   * @returns: list of THoldTimerInfo with remaining hold time per peer.
   */
  list<THoldTimerInfo> getHoldTimers(1: list<string> peers);

  /**
   * [Health]
   *
   * Run all registered health checks and return a comprehensive
   * health report for the BGP++ daemon.
   * Covers internal checks (system resources, task health, convergence,
   * session/peer/rib state) and external checks (FIB agent, thrift
   * endpoint). Platform-aware: EBB-only checks are skipped on DC.
   */
  THealthReport getHealthReport();

  /**
   * [Partial Drain]
   *
   * Get device-level partial-drain status
   */
  bgp_route_types.TPartialDrainStatus getPartialDrainStatus();

  /**
   * [Partial Drain]
   *
   * Get full partial-drain state including affected prefix list.
   */
  bgp_route_types.TPartialDrainState getPartialDrainState();

  /**
   * [Partial Drain]
   *
   * Get list of prefixes currently in partial-drain state.
   */
  list<bgp_route_types.TPartiallyDrainedPrefix> getPartiallyDrainedPrefixes();
}

// The following were automatically generated and may benefit from renaming.
typedef bgp_attr.TIpPrefix bgp_attrTIpPrefix_cpptemplate_stdmap_895

@cpp.Type{template = "std::unordered_map"}
typedef map<
  bgp_thrift.BgpInitializationEvent,
  i64
> map_bgp_thriftBgpInitializationEvent_i64_cpptemplate_stdunordered_map_895

@cpp.Type{template = "folly::F14FastMap"}
typedef map<string, i32> monitored_queue_size_map
