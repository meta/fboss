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

namespace cpp2 facebook.bgp.thrift
namespace py neteng.fboss.bgp_config
namespace py3 configerator.structs.neteng.fboss.bgp
namespace go neteng.fboss.bgp_config

include "configerator/structs/neteng/bgp_policy/thrift/bgp_policy.thrift"
include "configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/python.thrift"

package "facebook.com/neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/bgp_config"

struct ClassId {
  /*
   * classID is a way in agent to tag packets.
   * [redacted]
   */
  1: i32 value;
  /*
   * mininmum supporting paths for the prefix in the rib table
   * to allow route programming for the class id
   */
  2: optional i64 minimum_supporting_routes;
}

struct BgpPeerTimers {
  1: i32 hold_time_seconds;
  2: i32 keep_alive_seconds;
  3: i32 out_delay_seconds;
  4: i32 withdraw_unprog_delay_seconds;
  5: optional i32 graceful_restart_seconds;
  6: optional i32 graceful_restart_end_of_rib_seconds;
}

struct RouteLimit {
  // Maximum routes allow for this peer
  1: i64 max_routes = 12000; // default 12000

  // Only emit warning when max route limit exceeded
  2: bool warning_only = false; // default false

  // Number of routes at which warning is to be issued.
  3: i64 warning_limit = 0; // default 0, not using warning_limit
}

struct PeerGroup {
  1: string name;
  2: optional string description;
  /* Basic peer config */
  @thrift.Deprecated
  3: optional i32 remote_as; // To be deprecated, use 4 byte version
  4: optional string local_addr;
  5: optional string next_hop4;
  6: optional string next_hop6;
  7: optional bool enabled;
  8: optional i32 router_port_id;
  9: optional i64 remote_as_4_byte; // unsigned int 32, RFC 6793

  /* BgpPgFeatures in generic_switch_config.thrift */
  10: optional bool is_passive;
  11: optional bool is_confed_peer;
  12: optional bool is_rr_client;
  13: optional bool next_hop_self;
  14: optional bool remove_private_as;

  /* BgpPgMultiprotocolAttrs in generic_switch_config.thrift */
  20: optional bool disable_ipv4_afi;
  21: optional bool disable_ipv6_afi;
  /* Policy name for ingress and egress policies */
  22: optional string ingress_policy_name;
  23: optional string egress_policy_name;
  /* BgpPgTimers in generic_switch_config.thrift */
  24: optional BgpPeerTimers bgp_peer_timers;
  /* This is peer_tag/type in BgpPeer */
  25: optional string peer_tag;
  /* Local-AS# RFC-7705 */
  @thrift.Deprecated
  26: optional i32 local_as; // To be deprecated, use 4 byte version
  37: optional i64 local_as_4_byte; // unsigned int32, RFC 6793
  /* Route caps for pre & post filter prefixes */
  28: optional RouteLimit pre_filter;
  29: optional RouteLimit post_filter;
  /* Enable stateful HA to preserve preIn during restart */
  30: optional bool enable_stateful_ha;

  31: optional bgp_attr.AddPath add_path;
  /* Advertising ipv4 nlri over v6 nexthop # RFC-5549 */
  32: optional bool v4_over_v6_nexthop;

  /**
   * BGP Link-Bandwidth advertisement & receipt knobs
   */
  27: optional bgp_attr.AdvertiseLinkBandwidth advertise_link_bandwidth;
  35: optional bgp_attr.ReceiveLinkBandwidth receive_link_bandwidth;

  /*
   *  Link bandwidth to be advertised in bits (not bytes) per second.  Optional
   *  valid suffixes are K, M, G, for example, "100G"
   */
  33: optional string link_bandwidth_bps;

  /* For redistribute peer, do not program received routes */
  34: optional bool is_redistribute_peer;

  /* Enable enforce_first_as */
  36: optional bool enforce_first_as;

  /**
   * TTL Security / GTSM (Generalized TTL Security Mechanism, RFC 5082).
   * When set, outbound BGP packets use TTL=255 and inbound packets are
   * checked for TTL >= (256 - ttl_security_hops).
   */
  38: optional i32 ttl_security_hops;

  /* Enable Enhanced Route Refresh capability advertisement (RFC 7313, cap 70) */
  39: optional bool enhanced_route_refresh;

  /* Enable Route Refresh capability advertisement (RFC 2918, cap 2) */
  40: optional bool route_refresh;

  /* Additional remote ASN accepted during migration, e.g. 4200000000. */
  41: optional i64 additional_remote_as_4_byte; // unsigned int32, RFC 6793
}

/**
 * The configuration of a single BGP peer
 */
struct BgpPeer {
  /**
   * the peer's BGP ASN
   */
  @thrift.Deprecated
  1: optional i32 remote_as; // To be deprecated, use 4 byte version
  35: optional i64 remote_as_4_byte; // unsigned int32, RFC 6793
  /**
   * local address to connect/listen on
   */
  2: string local_addr;
  /**
   * remote address to connect to or accept connections from
   * this could be either address or prefix spec (e.g. x.x.x.x/24)
   * the latter only works for passive listening sessions
   */
  3: string peer_addr;
  /**
   * next-hop values to use in outgoing updates
   */
  4: string next_hop4;
  5: string next_hop6;
  /**
   * description for the peer, e.g. csw05a.frc1
   */
  6: optional string description;
  /**
   * passive peer will listen for incoming connections
   * the port to bind to is the listen_port from global config
   * default is to actively connect to the peer
   */
  7: optional bool is_passive;
  /**
   * remote confederation as number
   */
  8: optional bool is_confed_peer;

  /**
   * peer type
   */
  9: optional string type;
  10: optional string peer_id;
  /**
  * whether we should reflect this peer's routes
  */
  11: optional bool is_rr_client;
  /**
  * the tag to group similar peers
  */
  12: optional string peer_tag;
  /**
  * whether we should set next-hop to self to this peer
  * this works for all AFI's.
  */
  13: optional bool next_hop_self;
  /**
  * By default all AFI's are enabled, below allows to disable
  * them selectively. This is done for backward compat with
  * old configs.
  */
  14: optional bool disable_ipv4_afi;
  15: optional bool disable_ipv6_afi;

  /**
   * the port that this peer is found on. This is used to remove peers quickly
   * in case the port goes down.
   */
  18: optional i32 router_port_id;
  19: optional BgpPeerTimers bgp_peer_timers;

  /**
   * Whether we expect the peer to be present. (Not taken by BGP++)
   */
  20: optional bool enabled;

  /**
   * Remove private AS
   */
  21: optional bool remove_private_as;

  /**
   * Policy name for ingress and egress policies.
   * Bgp++ refers policy using name instead of BgpPolicy to avoid replication of
   * policy in the config.
   */
  22: optional string ingress_policy_name;
  23: optional string egress_policy_name;

  /**
   * Local-AS# for this peer. Only valid for eBGP peers.
   * Overrides the global config's local-as for this peer.
   * Reference: RFC-7705
   */
  @thrift.Deprecated
  25: optional i32 local_as; // To be deprecated, use 4 byte version
  37: optional i64 local_as_4_byte; // unsigned int32, RFC 6793

  /* Route caps for pre & post filter prefixes */
  27: optional RouteLimit pre_filter;
  28: optional RouteLimit post_filter;

  /* Enable stateful HA to preserve preIn during restart */
  29: optional bool enable_stateful_ha;

  /* Advertising ipv4 nlri over v6 nexthop # RFC-5549 */
  31: optional bool v4_over_v6_nexthop;

  /**
   * BGP Link-Bandwidth advertisement & receipt knobs
   */
  26: optional bgp_attr.AdvertiseLinkBandwidth advertise_link_bandwidth;
  34: optional bgp_attr.ReceiveLinkBandwidth receive_link_bandwidth;

  /*
   *  Link bandwidth to be advertised in bits (not bytes) per second.  Optional
   *  valid suffixes are K, M, G, for example, "100G"
   */
  32: optional string link_bandwidth_bps;

  /* For redistribute peer, do not program received routes */
  33: optional bool is_redistribute_peer;

  /**
   * Peer Group name. peer config overwrites peer group config
   */
  99: optional string peer_group_name;

  /**
   * Flag to control whether we want to enable add_path
   * functionality to this peer.
   */
  100: optional bgp_attr.AddPath add_path;

  /* Enable enforce_first_as */
  101: optional bool enforce_first_as;

  /**
   * TTL Security / GTSM (Generalized TTL Security Mechanism, RFC 5082).
   * When set, outbound BGP packets use TTL=255 and inbound packets are
   * checked for TTL >= (256 - ttl_security_hops).
   * Typical value: 1 for directly connected eBGP peers.
   */
  102: optional i32 ttl_security_hops;

  /* Enable Enhanced Route Refresh capability advertisement (RFC 7313, cap 70) */
  103: optional bool enhanced_route_refresh;

  /* Enable Route Refresh capability advertisement (RFC 2918, cap 2) */
  104: optional bool route_refresh;

  /* Additional remote ASN accepted during migration, e.g. 4200000000. */
  105: optional i64 additional_remote_as_4_byte; // unsigned int32, RFC 6793
}

/**
 * The spec of an advertised BGP network
 *
 * Note: When attributes are given along with policy_name,
 *       attributes are set first and then policy is applied
 */
struct BgpNetwork {
  1: string prefix;
  2: optional list<string> communities;
  3: optional i32 minimum_supporting_routes;
  4: optional bool install_to_fib;
  5: optional string policy_name; // policy to modify other attributes
  6: optional i32 local_pref;
  7: optional i32 origin;
  8: optional bgp_attr.TAsPath as_path;
  9: optional string nexthop;
  10: optional bool require_nexthop_resolution; // only originate if nexthop is resolved
}

struct BgpCommunity {
  1: string name;
  2: string description;
  3: list<string> communities;
}

struct BgpLocalPref {
  1: i32 localpref;
  2: string name;
  3: string description;
}

/**
 * BGP UCMP quantizer (non-uniform)
 * Qunatizer internal generates a map<input_bps, quantized_bps>, and for each input_bps
 * it will perform closet match to find its proper quantized-bps.
 * e.g
 * min_step_bps: 100G
 * error_pct_threshold: 0.1
 * fixed_quantized_bps_list: [2400G, 3600G]
 *
 * input-bps -> quantized_bps
 * 3600 -> 3600
 * 3500 -> 3600
 * 3400 -> 3600
 * 3300 -> 3600 (400G step)
 * 3200 -> 3200
 * 3100 -> 3200
 * 3000 -> 3200 (300G step)
 * 2900 -> 2900
 * ...
 * 2500 -> 2600
 * 2400 -> 2400
 * 2300 -> 2400
 * 2200 -> 2400 (300G step)
 * 2100 -> 2100
 * ...
 * 300 -> 300 (100G step)
 * 200 -> 200 (100G step)
 * 100 -> 100 (100G step)
 */
struct BgpUcmpQuantizerConfig {
  /**
   * minimum quantization step in bps, typically chosen as individual link speed
   * e.g "100G"
   */
  1: string min_step_bps;

  /**
   * quantization error = quantized-bps / input-bps - 1
   * error threshold sets upper bound threshold for quantization error for all inputs
   * e.g 0.1 (10%)
   */
  2: double error_pct_threshold;

  /**
   * This list contains bps that's ensured to be part of quantized-bps output, quantizer
   * internall will go through each of them and find rest of quantized-bps with top-down
   * approach
   * This list has to contain the maximum capacity we would like to support
   * e.g ["2400G", "3600G"]
   */
  3: list<string> fixed_quantized_bps_list;
}

/**
 * BGP Policy configuration thrift struct
 * Currently used only to help with rendering config output
 *
 * TODO: evaluate if the BgpPolicyConfig thrift struct is needed.
 * Only getPolicyConfig() used this and BgpConfig completely covers this struct.
 */
struct BgpPolicyConfig {
  /**
   * List of community names and descriptions
   */
  1: list<BgpCommunity> communities = [];

  /**
   * List of localpref values with their
   * mnemonic name and description
   */
  2: list<BgpLocalPref> localprefs = [];

  /**
   * Policies used by BGP++
   */
  3: optional bgp_policy.BgpPolicies policies;
}

// Settings for how BGP behaves if switch prefix limits are exceeded.
enum OverloadProtectionMode {
  // After the limit is exceeded, drop all further prefixes.
  DROP_EXCESS_PREFIXES = 1,

  // After the limit is exceeded, apply the golden prefix policy.
  // See [redacted]
  //
  // NOTE: This behavior requires the golden prefix policy.
  // If no golden prefix policy is available, BGP will fall back to
  // DROP_EXCESS_PREFIXES mode.
  APPLY_GOLDEN_PREFIX_POLICY = 2,
}

/**
 * The switch level limit to enforce to cap the max path/route BGP can support
 * after [redacted]. See [redacted] for details.
 *  1. total ingress path scale limit
 *  2. total unique prefix limit
 *
 * Consider different tier of devices have different scale limit in terms of:
 *  1) number of prefix received
 *  2) HW chip limitation
 *  3) etc.
 *
 * The network administrator must set the limit accordingly based on the least
 * performant platform. There is NO default value set for the switch limit.
 */
struct BgpSwitchLimitConfig {
  /**
   * This is the max-cap of the total received path count from all BGP sessions.
   * Attention: with add-path functionality enabled, multi-paths received for
   * the same prefix will be counted multiple times.
   */
  1: optional i64 ingress_path_limit;

  /**
   * This is the max-cap of the total unique prefixes received from BGP peers.
   * The same prefix with different path attribute will consider as 1. This
   * limits the possible max routes to be programmed to wedge_agent.
   */
  2: optional i64 prefix_limit;

  /**
   * This is the max-cap of the total path limit, that includes both ingress and egress paths.
   * The same prefix with different path attribute will consider as 1. This
   * limits the possible max routes to be programmed to wedge_agent.
   */
  3: optional i64 total_path_limit;

  4: OverloadProtectionMode overload_protection_mode = DROP_EXCESS_PREFIXES;
  /**
  * upperbound of allowed golden vips per device
  * The number needs to align with golden prefixes policy max_allowed_subnet_count, where vips are includes. ([redacted])
  * When there are more golden vips in the production, or hardware/software platfrom has upgraded, we can consider increase the number here and
  * in golden prefixes policy.([redacted])
  **/
  5: optional i32 max_golden_vips;
}

enum VerifyClientType {
  /**
   * Request a cert and verify it. Fail if verification fails or no cert is presented
   */
  ALWAYS = 0,
  /**
   * Request a cert from the peer and verify if one is presented. Will fail if verification fails. Do not fail if no cert is presented.
   */
  IF_PRESENTED = 1,
  /**
   * No verification is done and no cert is requested.
   */
  DO_NOT_REQUEST = 2,
}

struct ThriftServerConfig {
  /**
   * Flag to enable TLS for the thrift server
   */
  1: bool enable_tls = false;

  /**
   * CA path for verifying peers
   */
  2: optional string x509_ca_path;

  /**
   * Cert path
   */
  3: optional string x509_cert_path;

  /**
   * Key path
   */
  4: optional string x509_key_path;

  /**
   * ECC curve used, will mostly be using prime256v1
   */
  5: optional string ecc_curve_name;

  /**
   * Verification method to use for client certificates
   */
  6: optional VerifyClientType verify_client_type;
}

struct MemoryProfilingConfig {
  /**
  * Flag to enable memory profiling
  */
  1: bool enable_memory_profiling = false;
  2: i32 heap_dump_interval_s = 300;
}

/**
 * Update group configuration for slow peer detection and detachment.
 */
struct UpdateGroupConfig {
  /**
   * Allow slow peer detachment from update groups.
   * When false, slow peers will NOT be detached regardless of thresholds.
   */
  1: bool allowSlowPeerDetach = false;

  /**
   * Duration threshold (ms) for slow peer detection.
   * If a peer is continuously blocked longer than this, it is detached.
   */
  2: i32 slowPeerTimeThresholdMs = 50000;

  /**
   * Number of times a peer can become blocked within the rolling window
   * before being classified as slow and detached.
   */
  3: i32 slowPeerBlockCountThreshold = 10;

  /**
   * Rolling window duration (ms) for frequency-based slow peer detection.
   */
  4: i32 slowPeerBlockCountWindowMs = 1000;

  /**
   * Enable serializing group PDU for update groups.
   * When enabled, the group serializes BGP UPDATE once and distributes
   * to all peers. Each peer only mutates the nexthop.
   */
  5: bool enableSerializeGroupPdu = true;
}

struct BgpSettingConfig {
  @python.DeprecatedSortSetOnSerialize
  1: set<string> features;

  /**
   * Enable BGP Med comparison in path selection
   */
  2: optional bool enable_med_comparison;

  /**
   * Enable BGP Med missing as worst
   */
  3: optional bool enable_med_missing_as_worst;

  /**
   * Enable BGP Weight comparison in path selection
   */
  4: optional bool enable_weight_comparison;

  /**
   * Enable Next Hop Tracking and IGP cost comparision in path selection
   */
  5: optional bool enable_next_hop_tracking;

  /**
   * List of regex patterns for interface name matching in nexthop tracking.
   * Only interfaces whose names match at least one of these patterns will be
   * tracked for nexthop reachability. If empty or not set, no interfaces
   * will be tracked.
   */
  6: optional list<string> include_interface_regexes;

  /**
   * Enable Dynamic Policy Evaluation for prefix-list and routing policy
   */
  7: optional bool enable_dynamic_policy_evaluation;

  /**
   * Enable egress queue backpressure when queueing updates to TCP socket.
   *
   * DEPRECATED: bgp++ always uses egress queue backpressure and ignores this
   * field.
   */
  @thrift.Deprecated
  8: optional bool enable_egress_queue_backpressure;

  /**
   * Enable using path IDs allocated upon selection in Rib for outgoing updates, instead of using cached per-nexthop IDs in AdjRibOut.
   * This also includes constructing RibOut messages based on these path IDs instead of nexthops.
   */
  9: optional bool enable_rib_allocated_path_id;

  /**
   * Enable BGP Update Groups for shared update generation
   */
  10: optional bool enable_update_group;

  /**
   * Config to activate memory profiling on non-Meta device
   */
  11: optional MemoryProfilingConfig memory_profiling_config;

  /**
  * Enable BGP++ to use optimized GR logic
  */
  13: optional bool enable_optimized_GR;

  /**
  * Enable eiBGP multipath: equalize eBGP and iBGP paths by skipping the EXTERNAL_ROUTE preference filter in best path selection
  */
  14: optional bool enable_eibgp_multipath;

  /**
   * Configuration for update group slow peer detection and detachment.
   */
  15: optional UpdateGroupConfig update_group_config;

  /**
  * Enable BGP++ to use policy default action config
  */
  16: optional bool enable_policy_default_action;

  /**
   * When next-hop tracking is enabled, derive the IGP cost from the Open/R
   * client's nexthops (ClientID::OPENR) instead of from the resolved fwd
   * nexthops. The cost is taken solely from the Open/R entry: the minimum
   * cost over that client's nexthops. The resolved fwd nexthops are not
   * consulted -- there is no intersection between the Open/R and fwd nexthops.
   * If unset, the legacy behavior (min cost over the resolved fwd nexthops)
   * is used. In either case, if no relevant nexthop carries a cost, the IGP
   * cost is taken as unset (the nexthop stays reachable/eligible, just without
   * an IGP-cost preference).
   *
   * LIMITED-USE-CASE FLAG -- READ BEFORE EXTENDING.
   * This flag is intentionally narrow. It exists to satisfy the single,
   * concrete use-case understood today: FBOSS deployments that run exactly
   * one IGP (Open/R) and want BGP++ nexthop-tracking IGP cost to come from
   * that IGP's routes rather than from the resolved fwd nexthops. As such it
   * is a single hard-coded protocol (Open/R) toggle -- it is NOT a general
   * mechanism for selecting an IGP cost source.
   *
   * It deliberately does NOT model multiple IGPs/protocols, a protocol
   * selection policy, or awareness of which protocol contributed the
   * best-path. If/when there is a real need to scale this properly --
   * multiple protocols, an explicit selection policy, and the best-path
   * selected protocol in context -- a proper, well-specified solution
   * should be designed at that point with clear use-cases, and THIS
   * limited flag should be deprecated and removed in favor of it. Do not
   * grow this boolean into that general mechanism.
   */
  17: optional bool next_hop_tracking_use_openr_igp_cost;

  /** Enable add-path-receive reconciliation across a peer graceful restart. */
  18: optional bool enable_addpath_gr_reconcile;

  /**
   * Enable RFC 4271 classic IPv4-unicast NLRI encoding toward capability-less
   * peers. When true, a peer that advertised no MP-EXT capability
   * (capability-less / RFC 1771) receives IPv4-unicast announcements as classic
   * NLRI + NEXT_HOP (attr 3) instead of MP_REACH_NLRI (attr 14), and is keyed
   * into its own update group. Default (unset or false): every peer keeps
   * MP_REACH and existing update groups are unchanged, so the feature can be
   * turned off instantly for rollback.
   */
  19: optional bool enable_legacy_v4_nlri_encoding;

  /**
   * Enable bounded, backpressured egress for thrift stream subscribers (the
   * MP-BGP monitor). When true, a subscriber's AdjRib writes into a bounded
   * queue that is drained only under thrift stream credit, so a monitor that
   * stops consuming throttles the change-list consumer instead of growing an
   * unbounded buffer inside bgpd. Default (unset or false): the legacy
   * unbounded egress path, so the feature can be turned off instantly for
   * rollback.
   *
   * DEPRECATED: bgp++ always uses the bounded, backpressured egress path for
   * thrift stream subscribers and ignores this field.
   */
  @thrift.Deprecated
  20: optional bool enable_stream_subscriber_backpressure;

  /**
   * Enable link-flap dampening in nexthop tracking. A link-down publishes
   * immediately; a link-up is held until the interface is stable.
   */
  21: optional bool enable_netlink_dampening;

  /**
   * Maximum number of concurrent thrift stream subscribers (MP-BGP monitors).
   * Takes effect only when the "stream_subscriber_limit" feature string is
   * also present in `features`.
   */
  22: optional i32 stream_subscriber_limit;

  /**
   * Globally enable RFC 2918 Route Refresh capability advertisement. When
   * enabled, peers inherit an enabled default unless a peer or peer-group
   * explicitly disables route_refresh. Default (unset or false): Route
   * Refresh is disabled for every peer.
   */
  23: optional bool enable_route_refresh;

  /** Record the latest platform-normalized FIB request for each RIB entry. */
  24: optional bool enable_fib_out_tracking;
}

/**
 * NetServiceFramework configuration for EBB deployment
 */
struct BgpNetServiceThriftConfig {
  // Service identity for ACL checking
  1: optional string net_service_identity;

  // Path to static ACL JSON file
  2: optional string net_static_file_acl;

  // Kill switch file to disable ACL checking
  3: optional string net_auth_checker_kill_switch_file;

  // TLS/mTLS certificate paths
  4: optional string mtls_cert;
  5: optional string mtls_ca;

  // Network configuration
  6: optional i32 dscp;
  7: optional i32 vrf_creation_timeout_s;

  // Threading configuration
  8: optional i32 thrift_num_io_worker_threads;
  9: optional i32 thrift_num_cpu_worker_threads;

  // FB303 counter configuration
  10: optional bool export_counters_time_series;
  11: optional i32 counters_time_series_duration;

  // Thrift server behavior
  12: optional bool propagate_thrift_default_protocols;
}

/**
 * BGP configuration thrift struct for a switch
 */
struct BgpConfig {
  /**
   * this is normally the loopback IP
   */
  1: string router_id;

  /**
   * local ASN is same for all peers for simplicity
   */
  @thrift.Deprecated
  2: optional i32 local_as; // To be deprecated, use 4 byte version

  /**
   * v4 and v6 networks to announce
   */
  3: list<BgpNetwork> networks4;
  4: list<BgpNetwork> networks6;

  /**
   * the list of BGP peers for the switch
   */
  5: list<BgpPeer> peers;

  /**
   * the hold time for all BGP sessions
   */
  6: i32 hold_time = 30;

  /**
   * the TCP port to listen on for passive sessions
   */
  7: i32 listen_port = 179;

  /**
   * the confederation asn for this router, if any
   */
  @thrift.Deprecated
  8: optional i32 local_confed_as; // To be deprecated, use 4 byte version

  /**
   * Address to listen on, defaults to ::
   */
  10: string listen_addr = "::";

  /**
   * List of community names and descriptions
   */
  11: list<BgpCommunity> communities = [];

  /**
   * List of localpref values with their
   * mnemonic name and description
   */
  12: list<BgpLocalPref> localprefs = [];

  14: optional i32 graceful_restart_convergence_seconds;

  /**
   * Policies used by BGP++
   */
  15: optional bgp_policy.BgpPolicies policies;

  /**
   * the list of BGP peer groups used by peers
   */
  16: optional list<PeerGroup> peer_groups;

  /**
   * Use received link bandwidth extended community to compute UCMP paths
   */
  17: optional bool compute_ucmp_from_link_bandwidth_community;

  18: i32 eor_time_s = 120;

  /**
   * Quantization bucket size for UCMP weights (received link-bandwidth) for
   * programming. The HW may quantize again. We keep the bucket high enough by
   * default so that we can always fit the received weight (32 bit float) into
   * 32 bit integer without overflow error.
   */
  19: i32 ucmp_width = 65636;

  /**
   * BGP UCMP quantizer, applys to aggregateLocalWeights to reduce control
   * plane churns.
   */
  20: optional BgpUcmpQuantizerConfig ucmp_quantizer_config;

  /**
  * VipService related configs
  */
  21: bool enable_vip_service = false;
  // 22: reserved

  /**
   * 4-byte ASN definition for local ASN and local confed ASN
   *
   * RFC-6793: BGP Support for Four-Octet Autonomous System (AS) Number Space
   * https://datatracker.ietf.org/doc/html/rfc6793
   */
  23: optional i64 local_as_4_byte;
  24: optional i64 local_confed_as_4_byte;

  /**
   * Count confed as in as path length
   * RFC extension: https://datatracker.ietf.org/doc/draft-lapukhov-bgp-ecmp-considerations/
   */
  25: optional bool count_confeds_in_as_path_len;

  /**
   * This is used to override the default command line arguments we
   * pass to the agent.
   */
  26: map<string, string> defaultCommandLineArgs = {};

  /**
  * classID is a way in agent to tag packets.
  * ([redacted])
  *
  * bgp can instruct agent to tag traffic to certain destination prefix with
  * route programming:
  * > prefix1, via <nhg>, classid = 20
  *
  * In control plane we use a special community range to encode the class id,
  * so it comes as an attribute with route update. community_to_classid is a map from
  * community to class id translation
  *
  * i.e. community_to_classid = {"65520:100" : 20}
  * If an update come with 65520:100, it will be translate to class id 20. If update1
  * is choosen as the only best path, bgp programs class id 20 with nexthop group.
  *
  * Notice bgp will only program class id if all ecmp have the same attribute.
  * Otherwise class id is set to none.
  */
  27: optional map<string, ClassId> community_to_classid;

  /**
  * Not to advertise a prefix to a peer, if the prefix's AS-path contain that peer's AS
  *
  * Movitation:
  * [redacted]
  */
  28: optional bool sender_suppress_as_loop;

  /**
   * Indicates global drained state (i.e., whether UNDRAINED or in one of the drain states).
   * Note that this does not reflect the state of individual interfaces that may be drained
   * as part of orthogonal interface drain workflow
   */
  29: bgp_policy.DrainState drain_state = bgp_policy.DrainState.UNDRAINED;
  30: list<string> drained_interfaces = [];

  /**
   * Scuba category for logging certain policy evaluation result
   * empty meanings no logging will be done
   */
  31: optional string scuba_logging_category;

  /**
   * Switch-level limit handling prefix and path limit
   */
  32: optional BgpSwitchLimitConfig switch_limit_config;

  /**
   * Enable the VIPs limit control on vip server
   */
  35: optional bool enable_vip_server_limit;

  /**
   * Include feature knob and parameters configuration
   */
  36: optional BgpSettingConfig bgp_setting_config;

  /**
   * Config for BGP thrift server. Currently only tls settings are supported.
   * In the future the plan is to use this to support other thrift server configurables.
   */
  37: optional ThriftServerConfig thrift_server_config;

  /**
   * NetServiceFramework configuration for EBB deployments
   */
  38: optional BgpNetServiceThriftConfig net_service_config;

  /**
   * Route-reflector CLUSTER_ID (RFC 4456), as a dotted quad. It is an opaque
   * 4-octet identifier, so it need not be routable or match any configured
   * interface. Redundant reflectors serving one cluster must share this value
   * while keeping distinct router_ids, which is why it cannot be derived from
   * router_id. When unset the router_id is used, per RFC 4456 section 1.1.
   */
  39: optional string cluster_id;
}
