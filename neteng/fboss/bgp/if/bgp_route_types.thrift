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

include "configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/rib_policy.thrift"
include "configerator/structs/neteng/fboss/thrift/common.thrift" as fboss_common
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/thrift.thrift"

package "facebook.com/neteng/fboss/bgp/if/bgp_route_types"

namespace cpp2 facebook.neteng.fboss.bgp.thrift
namespace py neteng.fboss.bgp_route_types
namespace py.asyncio neteng.fboss.asyncio.bgp_route_types
namespace py3 neteng.fboss
namespace go neteng.fboss.bgp_route_types

/*
 * BGP extended community types.
 *
 * These represent extended communities as exchanged between applications,
 * not the wire format. Represented as a union for extensibility.
 */

enum TBgpTwoByteAsnExtCommType {
  LINK_BANDWIDTH_TYPE = 0x40,
}

enum TBgpTwoByteAsnExtCommSubType {
  LINK_BANDWIDTH_SUB_TYPE = 0x4,
  LINK_BANDWIDTH_SUB_TYPE_TRANSITIVE = 0x0,
}

struct TBgpTwoByteAsnExtComm {
  1: i16 type;
  2: i16 sub_type;
  3: i32 asn;
  4: i64 value;
}

struct TBgpRawExtComm {
  1: i64 value_low;
  2: i64 value_high;
}

union TBgpExtCommUnion {
  1: TBgpTwoByteAsnExtComm two_byte_asn;
  // Pending fix for D union implementation
  // 2: TBgpRawExtComm raw_values;
}

/**
 * This could theoretically be collapsed into a union, but leaving this as a
 * struct in case we want to add more fields here in the future.
*/

struct TBgpExtCommunity {
  1: TBgpExtCommUnion u;
}

struct TBgpAggregator {
  1: i32 asn;
  2: string ip;
}

/**
 * Single BGP path for a Rib entry, points to the peer who advertised it
 * This includes all fields in BgpAttributesC.
 */
struct TBgpPath {
  1: bgp_attr.TIpPrefix next_hop;
  2: bgp_attr.TAsPath as_path;
  3: optional list<bgp_attr.TBgpCommunity> communities;
  4: optional i64 originator_id;
  5: optional list<i64> cluster_list;
  6: optional i32 local_pref;
  7: optional i64 router_id;
  8: optional i32 origin;
  9: optional bgp_attr.TIpPrefix peer_id;
  /* BGP Path selection/rejection reason */
  10: optional string bestpath_filter_descr;
  11: optional list<TBgpExtCommunity> extCommunities;
  12: i64 last_modified_time; // time is in microseconds
  13: optional i64 path_id; // received path ID
  /*
   * Name of the policy (including policy and term name) which causes a
   * route (along with BGP attributes) to get accepted, rejected or modified
   * when applied to it.
   * Routes received from a peer may get rejected by a policy or accepted
   * to be programmed into RIB if a policy matches. Policy information
   * will be shown in the output of cli command:
   * fboss bgp postfilter-received <peer>.
   * Similarly, routes advertised to a peer may get accepted by a policy or
   * modified by it before it gets advertised to a peer. Policy information
   * will be shown in the output of cli command:
   * fboss bgp postfilter-advertised n<peer>.
   */
  14: optional string policy_name;
  // deprecated: use med (field 21) instead
  @thrift.DeprecatedUnvalidatedAnnotations{items = {"deprecated": "1"}}
  15: optional i32 multi_exit_disc;
  16: optional bool atomic_aggregate;
  17: optional TBgpAggregator aggregator;
  18: optional string peer_description;
  19: optional i64 next_hop_weight;
  // Used to indicate best path. This field is introduced so
  // fboss cli uses best path computed in bgp++ RIB instead of
  // using best_next_hop to deduce best path.
  20: optional bool is_best_path;
  21: optional i64 med;
  22: optional i32 weight;
  23: bool in_update = false;
  24: bool in_withdraw = true;
  // Used to encapsulate topology information for the control plane
  // to make routing decisions, e.g., NSF GAR
  @cpp.Type{template = "std::unordered_map"}
  25: optional map<string, i64> topologyInfo;
  26: optional i64 igp_cost;
  // path_id above would be received path ID. A speaker can also allocate
  // its own path ID to send to peers, which would go in this field
  27: optional i64 path_id_to_send;
  // Whether this path was excluded before best-path comparison. Unset/false
  // means it was an eligible candidate, whether or not it went on to win.
  // Set by the loc-RIB getters; left unset by shadow-RIB views, which only hold
  // selected paths.
  28: optional bool is_inactive;
  // Locally configured backup address. This is not a BGP wire attribute.
  29: optional bgp_attr.TIpPrefix backup_addr;
}

/**
 * RIB entry along with host information that we are querying against
 */
struct TRibEntryWithHost {
  1: list<TRibEntry> tRibEntries;
  2: string host;
  3: string ip;
  4: string oobName;
}

/** Operation in the last platform FIB request staged by BGP. */
enum TFibOutOperation {
  /** Zero intentionally represents a submitted route with no FIB contents. */
  NONE = 0,
  PROGRAM = 1,
}

/** Role assigned to a next hop in the platform FIB request. */
enum TFibOutNextHopRole {
  UNKNOWN = 0,
  PRIMARY = 1,
  BACKUP = 2,
}

/** Platform-normalized next hop in a FIB request staged by BGP. */
struct TFibOutNextHop {
  1: bgp_attr.TIpPrefix next_hop;
  2: i64 weight;
  3: TFibOutNextHopRole role;
  4: optional bool is_connected;
  5: optional string interface_name;
  /** Topology fields included in the FBOSS next-hop payload. */
  @cpp.Type{template = "std::unordered_map"}
  6: optional map<string, i64> topology_info;
}

/** Last platform-normalized FIB request staged by BGP for one prefix. */
struct TFibOutRoute {
  1: TFibOutOperation operation;
  2: list<TFibOutNextHop> next_hops;
  3: optional i32 admin_distance;
  4: optional i32 class_id;
  /** Number of live FIB-out routes that share this complete nexthop set. */
  5: optional i64 nexthop_set_ref_count;
}

/** Compact FIB-out state for one RIB prefix. */
struct TFibOutEntry {
  1: bgp_attr.TIpPrefix prefix;
  /** Absent until the prefix has entered a platform-adapter batch. */
  2: optional TFibOutRoute fib_out;
  /** Reserved for future FIB staging-status reporting. */
  3: optional bool fib_out_pending;
}

/** Compact response for a dedicated exact-prefix FIB-out query. */
struct TFibOutTable {
  /** True when FIB-out tracking is enabled in the running BGP configuration. */
  1: bool enabled;
  /** Zero or one compact row for the requested RIB prefix. */
  2: list<TFibOutEntry> entries;
}

/** Request for one exact-prefix FIB-out lookup. */
struct TFibOutPrefixRequest {
  /** Exact IPv4 or IPv6 prefix to query. */
  1: string prefix;
}

/** One canonical complete nexthop set referenced by current FIB-out state. */
struct TFibNexthopSet {
  /** All normalized members that define this canonical set. */
  1: list<TFibOutNextHop> next_hops;
  /** Number of live FIB-out routes that share this complete set. */
  2: i64 ref_count;
}

/** Knob state and contents of the BGP FIB nexthop database. */
struct TFibNexthopDatabase {
  1: bool enabled;
  2: list<TFibNexthopSet> nexthop_sets;
}

/**
 * RIB entry binds a prefix to multiple ECMP Bgp paths
 * Afi is inferred from prefix.afi
 */
struct TRibEntry {
  1: bgp_attr.TIpPrefix prefix;
  /** maps a group to the list of paths */
  2: map<string, list<TBgpPath>> paths;
  /**
   * Label of the `paths` group holding the selected best/ECMP paths (always
   * "best" when set). Empty when no best path is selected -- prefer `best_path`
   * (field 9) / per-path `is_best_path` as the explicit presence signal.
   */
  3: string best_group;
  4: bgp_attr.TIpPrefix best_next_hop;
  /*
   * If the path selection of the route is overridden by CPS,
   * the corresponding active criteria is set here
   * see getActivePathSelectionCriteria
   */
  5: optional rib_policy.TPathSelector active_cps_criteria;
  /*
   * Indicates path selection is pending for this entry.
   * When true, IGP cost may have changed but best-path hasn't been
   * recalculated yet. This helps identify transient states where
   * the displayed best-path may not reflect current IGP costs.
   */
  6: optional bool path_selection_pending;
  /*
   * RIB version when this entry was last modified. This is a monotonically
   * increasing counter that increments whenever a material routing change
   * occurs (best path or multipath changes). Used for tracking routing
   * table version per prefix.
   */
  7: optional i64 rib_version;
  /*
   * If the route's weights are overridden by CTE (route attribute policy),
   * the corresponding active UCMP action is set here
   */
  8: optional rib_policy.TRouteAttributeUcmpAction active_cte_ucmp_action;
  /*
   * Convenience copy of the selected best-path entry from `paths`.
   * Lets FSDB subscribers fetch only the best path (e.g. via
   * `ribMap/<prefix>/best_path`) without subscribing to the full `paths`
   * map, which is significantly larger.
   *
   * Unset when no path is currently "best" -- e.g. when CPS native criteria
   * (bgp_native_path_selection_min_nexthop / min_agg_lbw) is violated and
   * the entry is multipath-only with no bestpath, or when `bestpath` has
   * not yet been computed for this prefix.
   *
   * The `is_best_path` flag on this copy is always set to true.
   */
  9: optional TBgpPath best_path;
  /**
   * Submitted FIB-out state for this entry. Populated only by the exact-prefix
   * CLI path when FIB-out tracking is enabled.
   */
  10: optional TFibOutRoute fib_out;
  /** Reserved for future FIB staging-status reporting. */
  11: optional bool fib_out_pending;
}

/**
 * @deprecated Use TCapacity instead (surfaced via min_capacity / current_capacity
 * on TPartiallyDrainedPrefix). Retained for FSDB/NSDB wire compatibility because
 * field 4 min_capacity_threshold is already landed and published.
 *
 * The capacity-threshold criterion + value that triggered partial drain for a
 * single prefix. Exactly one variant is set per drained prefix — matches the
 * active criterion on the policy statement.
 */
union TMinCapacityThreshold {
  /** BGP native min-nexthop count the prefix violated */
  1: i32 mnh;
  /** BGP native aggregate LBW threshold (bps) the prefix violated */
  2: i64 agg_lbw_bps;
}

/**
 * A capacity value for a partially-drained prefix, by criterion. Exactly one
 * variant is set — the active criterion on the policy statement. Used for both
 * the violated threshold (min_capacity) and the current observed value
 * (current_capacity) on TPartiallyDrainedPrefix; the field name distinguishes
 * the two.
 */
union TCapacity {
  /**
   * nexthop count: the number of nexthops for a given prefix
   */
  1: i32 next_hop_count;
  /** Aggregate link bandwidth in bps: the total amount of bandwidth across all selected nexthops in bits per second */
  2: i64 agg_lbw_bps;
}

/**
 * A single prefix in partial-drain state. A prefix enters partial drain
 * when drain_on_min_nexthop_violation is configured on its path-selector
 * statement and the matched capacity threshold (min-nexthop or aggregate
 * LBW) is violated. The prefix is advertised with drain community 65446:10
 * instead of being withdrawn.
 */
struct TPartiallyDrainedPrefix {
  /** CIDR prefix currently in partial-drain state */
  1: bgp_attr.TIpPrefix prefix;
  /** @deprecated Use current_capacity (field 5) instead */
  2: i32 path_count;
  /** @deprecated Use min_capacity (field 6) instead */
  3: i32 mnh_threshold;
  /** @deprecated Use min_capacity (field 6) instead */
  4: TMinCapacityThreshold min_capacity_threshold;
  /** The current observed capacity, on the same criterion arm as min_capacity */
  5: TCapacity current_capacity;
  /** The capacity threshold that was violated to trigger drain */
  6: TCapacity min_capacity;
}

/**
 * Device-level partial-drain status
 */
struct TPartialDrainStatus {
  /** True if one or more prefixes are in partial-drain state */
  1: bool is_partially_drained;
  /** Number of prefixes currently violating MNH with relax=true */
  2: i32 num_affected_prefixes;
  /** Monotonic counter incremented each time the device enters or exits
   *  partial-drain state (is_partially_drained flips) in the current
   *  incarnation of BGP */
  3: i64 partial_drain_transition_count;
}

/**
 * Composite partial-drain state
 */
struct TPartialDrainState {
  /** Device-level summary — subscribe to this subpath for lightweight gating */
  1: TPartialDrainStatus partial_drain_state;
  /** Per-prefix detail — subscribe to full path when prefix-level visibility needed */
  2: list<TPartiallyDrainedPrefix> drained_prefixes;
}

/**
 * Dictionary of unique list-valued sub-attributes -- AS_PATH, communities,
 * extended-communities, and cluster-lists. Each TBgpDedupedPath references an
 * entry here by i64 ID instead of inlining the list, so a list shared by
 * many paths is stored once.
 *
 * Dedup unit is the WHOLE list (matching BGP++'s
 * DeDuplicatedAttribute<BgpAttrCommunitiesC etc.> infrastructure --
 * shared_ptr identity at list level is the natural cache key).
 */
struct TBgpAttrDict {
  /** Unique whole-list COMMUNITY values (RFC 1997). */
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<i64, list<bgp_attr.TBgpCommunity>> community_lists;
  /** Unique whole-list AS_PATH values (RFC 4271). */
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i64, list<bgp_attr.TAsPathSeg>> as_path_lists;
  /** Unique whole-list EXTENDED COMMUNITIES values (RFC 4360). */
  @cpp.Type{template = "folly::F14FastMap"}
  3: map<i64, list<TBgpExtCommunity>> ext_community_lists;
  /** Unique whole-list CLUSTER_LIST values (RFC 4456). */
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<i64, list<i64>> cluster_lists;
}

/**
 * A unique (deduplicated) BGP path, interned in
 * TCanonicalRibState.deduped_paths -- one entry per distinct path observed by
 * bgpd (the dedup unit is BGP++'s DeDuplicatedBgpPath). Cardinality is bounded
 * by the distinct (attributes, next_hop) combinations and is typically small
 * (hundreds to tens of thousands) even on devices with millions of paths,
 * because policy domains tag many prefixes with identical attributes.
 *
 * The list-valued sub-attributes (AS_PATH, communities, ext-communities,
 * cluster_list) are stored as indices into the shared TBgpAttrDict carried by
 * TCanonicalRibState.attr_dict; consumers resolve the actual values via one
 * dereference (deduped_paths[i].as_path_idx -> attr_dict.as_path_lists[idx]).
 * NEXT_HOP and the scalars are stored inline.
 *
 * Scalars are stored inline because they're already small (8 bytes or less
 * per field) and per-attribute dedup overhead would exceed the savings.
 *
 * Field-placement rule: a field lives here iff it is part of the path's dedup
 * identity -- identical for every prefix that received this path (NEXT_HOP, the
 * RFC attributes, topology_info, and the Cisco-local weight). Anything that
 * varies per (prefix, path) instance lives on TBgpPathCanonical instead; peer
 * attribution lives in TCanonicalPeer (referenced per path via
 * TBgpPathCanonical.peer_idx).
 */
struct TBgpDedupedPath {
  /**
   * RFC 4271 NEXT_HOP, stored inline. Unlike the list-valued sub-attributes
   * below, the next-hop is a plain value (not a DeDuplicatedAttribute), so it
   * has no stable pointer to intern on; it rides this entry, which is already
   * deduped per distinct whole-path.
   */
  1: bgp_attr.TIpPrefix next_hop;
  /**
   * RFC 4271 AS_PATH, as an index into TBgpAttrDict.as_path_lists. Unset when
   * the AS_PATH is empty (e.g. locally-originated routes) -- an empty list has
   * no deduplicated pointer to intern, so it carries no dict entry.
   */
  2: optional i64 as_path_idx;
  /**
   * RFC 1997 COMMUNITIES, as an index into TBgpAttrDict.community_lists.
   * Unset when the path has no communities (the converter skips the dict
   * lookup in that case for cheap empty-handling).
   */
  3: optional i64 communities_idx;
  /**
   * RFC 4360 EXTENDED COMMUNITIES, as an index into
   * TBgpAttrDict.ext_community_lists. Unset when none (typical for eBGP).
   */
  4: optional i64 ext_communities_idx;
  /**
   * RFC 4456 CLUSTER_LIST, as an index into TBgpAttrDict.cluster_lists.
   * Unset when empty (typical for eBGP; only iBGP routes through route
   * reflectors carry a non-empty cluster_list).
   */
  5: optional i64 cluster_list_idx;
  /** RFC 4271 ORIGIN. */
  6: optional i32 origin;
  /** RFC 4271 LOCAL_PREF. */
  7: optional i32 local_pref;
  /** RFC 4271 MULTI_EXIT_DISC. */
  8: optional i64 med;
  /** RFC 4271 ATOMIC_AGGREGATE. */
  9: optional bool atomic_aggregate;
  /** RFC 4456 ORIGINATOR_ID. */
  10: optional i64 originator_id;
  /**
   * RFC 4271 AGGREGATOR. Inline (rarely populated and the value-set is
   * bounded by aggregation points, so dict overhead isn't worth it).
   */
  11: optional TBgpAggregator aggregator;
  /**
   * Topology information for the control plane to make routing decisions
   * (e.g., NSF GAR). Mirrors TBgpPath.topologyInfo.
   */
  @cpp.Type{template = "std::unordered_map"}
  12: optional map<string, i64> topology_info;
  /** Cisco-style local BGP weight (path-selection attr). Mirrors TBgpPath.weight. */
  13: optional i32 weight;
  /** Locally configured backup address. Mirrors TBgpPath.backup_addr. */
  14: optional bgp_attr.TIpPrefix backup_addr;
}

/**
 * Per-prefix BGP path in the canonical RIB: an index into the shared
 * TCanonicalRibState.deduped_paths pool, plus the fields that vary per
 * (prefix, path) instance even when the underlying deduped path is identical,
 * so they cannot be deduplicated (the converse of TBgpDedupedPath's rule):
 * bestpath selection state, the UCMP next_hop_weight, Add-Path ids, igp_cost,
 * last_modified_time, and the advertising peer.
 *
 * Mirrors legacy TBgpPath semantically; the per-attribute payload is
 * retrieved by indirection through TCanonicalRibState.deduped_paths[path_idx].
 */
struct TBgpPathCanonical {
  /** Index into TCanonicalRibState.deduped_paths. Always set; consumers dereference it. */
  1: i64 path_idx;
  /**
   * Marks the selected best path within the "bestPath" group. Typically set
   * on at most one path per entry (or none, when no bestpath is currently
   * selected). Mirrors TBgpPath.is_best_path.
   */
  2: optional bool is_best_path;
  /**
   * UCMP nexthop weight for this path (Meta-specific, not RFC). Mirrors
   * TBgpPath.next_hop_weight; unset when the prefix is not UCMP-weighted.
   */
  3: optional i64 next_hop_weight;
  /** RFC 7911 received path ID. Disambiguates Add-Path entries from the same peer. */
  4: optional i64 path_id;
  /**
   * Index into TCanonicalRibState.peers identifying the peer that advertised
   * this path. Unset when peer attribution is not exported (e.g. best-path
   * views that don't need per-path peer info).
   */
  5: optional i64 peer_idx;
  /** IGP cost to the nexthop. Mirrors TBgpPath.igp_cost. */
  6: optional i64 igp_cost;
  /** Last modified time in microseconds. Mirrors TBgpPath.last_modified_time. */
  7: optional i64 last_modified_time;
  /** RFC 7911 allocated path ID to send to peers. Mirrors TBgpPath.path_id_to_send. */
  8: optional i64 path_id_to_send;
  /** BGP path selection/rejection reason. Mirrors TBgpPath.bestpath_filter_descr. */
  9: optional string bestpath_filter_descr;
  /**
   * Name of the policy (and term) that accepted / rejected / modified this path
   * in a given direction. Per (peer, direction); populated by the post-policy
   * export path (shown by `fboss bgp postfilter-received` / `-advertised`) and
   * left unset by the loc-RIB getters. Mirrors TBgpPath.policy_name.
   */
  10: optional string policy_name;
  /** Whether this path was excluded before comparison. Mirrors TBgpPath.is_inactive. */
  11: optional bool is_inactive;
}

/**
 * Canonical (compressed) RIB entry: a prefix + its set of paths. Holds the same
 * content as TRibEntry, but its paths reference the shared attr / path / peer
 * pools by index instead of inlining every attribute, so the encoded RIB is far
 * smaller on the wire. It is a compact ENCODING of TRibEntry, not a
 * reduced-attribute view -- prefer it over TRibEntry when payload size matters.
 */
struct TRibEntryCanonical {
  1: bgp_attr.TIpPrefix prefix;
  /**
   * Paths grouped by selection role, mirroring TRibEntry.paths.
   * Key "bestPath" holds the ECMP multipath set (the entry with
   * `is_best_path = true` is the selected bestpath); key "default" holds the
   * other paths (e.g. received paths that did not make the bestpath group).
   * Constants: facebook::bgp::kBestPathGroup, facebook::bgp::kDefaultPathGroup.
   */
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, list<TBgpPathCanonical>> paths;
  /**
   * RIB version when this entry was last modified. Monotonically increasing
   * per material routing change. Mirrors TRibEntry.rib_version.
   */
  3: optional i64 rib_version;
  /**
   * Indicates path selection is pending for this entry. Mirrors
   * TRibEntry.path_selection_pending.
   */
  4: optional bool path_selection_pending;
  /**
   * If the path selection of the route is overridden by CPS, the corresponding
   * active criteria is set here. Mirrors TRibEntry.active_cps_criteria.
   */
  5: optional rib_policy.TPathSelector active_cps_criteria;
  /**
   * If the route's weights are overridden by CTE (route attribute policy),
   * the corresponding active UCMP action is set here. Mirrors
   * TRibEntry.active_cte_ucmp_action.
   */
  6: optional rib_policy.TRouteAttributeUcmpAction active_cte_ucmp_action;
  /**
   * The selected best path, for consumers that read only the best path. A
   * self-contained TBgpDedupedPath: its sub-attrs resolve through attr_dict, so
   * a best-path-only subscriber need not pull the deduped_paths pool. Carries no
   * peer attribution. Left unset by the get-rib getters (fboss2 finds the best
   * path via is_best_path in `paths`). Mirrors TRibEntry.best_path.
   *
   * Annotated so best-path-only subscribers (e.g. HostReachTracker) can store
   * this subtree as a single hybrid thrift_cow node instead of a per-field COW
   * node chain. Inert unless a consumer instantiates its storage with
   * EnableHybridStorage=true; the wire format is unchanged (RuntimeAnnotation).
   */
  @fboss_common.AllowSkipThriftCow
  7: optional TBgpDedupedPath best_path;
}

/**
 * A BGP peer that advertised one or more paths, interned in
 * TCanonicalRibState.peers. Mirrors BGP++'s BgpPeerId: the dedup identity is
 * (peer_id, router_id), so all paths from the same peer share one entry.
 * Cardinality is bounded by the device's peer count (hundreds), so this pool
 * is negligible next to the path pool.
 */
struct TCanonicalPeer {
  /** Peer (BGP neighbor) address. Mirrors TBgpPath.peer_id. */
  1: bgp_attr.TIpPrefix peer_id;
  /** Peer's BGP router-id (remote BGP identifier). Mirrors TBgpPath.router_id. */
  2: optional i64 router_id;
  /**
   * Human-readable peer description. Carried for display; not part of the
   * dedup identity. Mirrors TBgpPath.peer_description.
   */
  3: optional string peer_description;
}

/**
 * Top-level wrapper for the canonical RIB -- a compact, deduplicated form of
 * BGP RIB entries (prefix -> paths). Holds the shared TBgpDedupedPath and
 * TCanonicalPeer pools (plus the TBgpAttrDict sub-attribute dictionary) and
 * the per-prefix entries that reference them by ID. IDs increase monotonically
 * for the lifetime of a bgpd process and are never reused. Retired values can
 * therefore be erased immediately without changing the meaning of any live
 * reference. IDs reset on restart, when consumers receive a full rewalk.
 *
 * CONSUMER CONTRACT: resolve values only by following the IDs on a live
 * TBgpPathCanonical (path_idx -> deduped_paths, peer_idx -> peers) and the
 * sub-attribute IDs on the paths so reached. A missing key is a stale or
 * malformed reference and must not be interpreted as another value.
 */
struct TCanonicalRibState {
  /**
   * Dictionary of unique list-valued sub-attributes. Each TBgpDedupedPath in
   * deduped_paths references entries here via ID.
   */
  1: TBgpAttrDict attr_dict;
  /**
   * Shared pool of unique (deduplicated) paths. Each TBgpPathCanonical in
   * rib_entries references one entry here via path_idx.
   */
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<i64, TBgpDedupedPath> deduped_paths;
  /**
   * Shared pool of unique peers. Each TBgpPathCanonical references the peer
   * that advertised it via peer_idx. Bounded by the device's peer count, so
   * negligible relative to deduped_paths.
   */
  @cpp.Type{template = "folly::F14FastMap"}
  3: map<i64, TCanonicalPeer> peers;
  /**
   * Per-prefix entries. Key is str() of folly::CIDRNetwork (matches the
   * prefix in TRibEntryCanonical.prefix).
   */
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<string, TRibEntryCanonical> rib_entries;
}
