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

package "facebook.com/neteng/fboss/bgp/public_tld/configerator/structs/neteng/bgp_policy/rib_policy"

include "configerator/structs/neteng/bgp_policy/thrift/bgp_policy.thrift"
include "configerator/structs/neteng/bgp_policy/thrift/routing_policy.thrift"
include "configerator/structs/neteng/fboss/bgp/if/bgp_attr.thrift"
include "configerator/structs/neteng/fboss/thrift/common.thrift" as fboss_common

namespace py neteng.bgp_policy.thrift.rib_policy
namespace py3 neteng.bgp_policy.thrift
namespace cpp2 facebook.bgp.rib_policy
namespace go neteng.bgp_policy.rib_policy

enum KeyType {
  DEVICE_REGEX = 0,
  PEER_GROUP_NAME = 1,
}

// The structs are sorted in dependency order:
// The leaf structs are defined before their roots

// TBgpCommunityMatch carries additional match_type flag which allows negative
// match on the community
struct TBgpCommunityMatch {
  // match_type: EQUAL/NOT_EQUAL
  1: routing_policy.MatchValueLogicOperator match_type = routing_policy.MatchValueLogicOperator.EQUAL;

  2: bgp_attr.TBgpCommunity community;
}

// Similar to bgp_policy.CommunityList, given the communities,
// TCommunityListMatch applies boolean_operator to determine if a route matches
// For instance, suppose the operator is OR, a route matches if it has a path
// that carries any communities in the list. On the other hand, when the operator
// is AND, a route matches if it has a path that carries all listed communities.
struct TCommunityListMatch {
  // We only support OR and AND operations here
  // All operator other than OR will be treated as AND
  1: routing_policy.BooleanOperator boolean_operator = routing_policy.BooleanOperator.OR;

  2: list<TBgpCommunityMatch> communities;
}

// BgpPathMatcher maps to one type of BGP path
// AND operation is used among all specified fields.
struct TBgpPathMatcher {
  // a list of communities to match on (see TCommunityListMatch for details)
  5: optional TCommunityListMatch community_list;

  // exact match on origin
  2: optional bgp_policy.Origin origin;

  // exact match on as-path-length
  3: optional i32 as_path_length;

  // regex match on as-path
  4: optional string as_path_regex;

  // match paths with link_bandwidth (bits/s) >= min_lbw_bps
  6: optional i64 min_lbw_bps;

  // Only valid with at least one match field above. Among paths satisfying
  // those matches, retain the lowest IGP cost; ties are retained and unknown
  // costs rank last. Unset or false leaves path matching unchanged.
  7: optional bool prefer_lowest_igp_cost;
}

// TPathSelectionCriteria maps to one type of route
// AND operation between all specified fields
struct TPathSelectionCriteria {
  // a list of BGP path matchers
  // any path has to match one of path_matchers,
  // i.e., it's an "or" match among the path_matches
  1: list<TBgpPathMatcher> path_matchers;

  // minimum nexthop required.
  //
  // If no nexthop is selected by this criteria, then this criteria is considered not met,
  // regardless of whether min_nexthop is set or what it is set to.
  2: optional i32 min_nexthop;
}

struct TPathSelector {
  // An *ordered* list of custom TPathSelectionCriteria(s), highest priority at front
  // e.g list[criteria0, criteria1, ...]
  //
  // BGP tries to select path that meet the criteria from highest priority to lower
  // ones, if no criteria can be met: BGP falls back to native BGP-based path selection.
  1: list<TPathSelectionCriteria> criteria_list;

  // NOTE: the below criterias are only evaluated iff none of the criteria_list is met (aka fallback to native BGP path selection)
  // If native BGP path selection yields no nexthop -> drop route (do not program in FIB

  //
  // ECMP (mututally exclusive to UCMP)
  //

  // additional BGP-based path selection criteria
  // withdraws the best path if multipath size < min_nexthop
  // FIB entry can be purged or kept depending on drain_on_min_nexthop_violation
  2: optional i32 bgp_native_path_selection_min_nexthop;

  // only evaluated when bgp_native_path_selection_min_nexthop is specified
  // if native BGP path selector yields fewer nexthops than bgp_native_path_selection_min_nexthop,
  // instead of withdrawing the prefix, retain the bestpath and attach drain community 65446:10
  // (removing live community 65446:30), allowing peers to deprioritize the path gracefully
  3: optional bool drain_on_min_nexthop_violation;

  //
  // UCMP (mututally exclusive to ECMP)
  //

  // additional BGP-based path selection criteria
  // withdraws the best path if aggregate link-bandwidth bps of all selected paths < min_aggregate_lbw_bps
  // FIB entry can be purged or kept depending on relax_bgp_min_aggregate_lbw_bps
  4: optional i64 bgp_min_aggregate_lbw_bps;

  // only evaluated when bgp_min_aggregate_lbw_bps is specified
  // if native BGP path selector yields paths with aggregate link-bandwidth bps less than bgp_min_aggregate_lbw_bps,
  // instead of dropping the route completely (not programming in FIB),
  // we keep the FIB warm with the selected nexthops, but we will withdraw the route from neighbors
  5: optional bool relax_bgp_min_aggregate_lbw_bps;
}

/**
 * Matcher selects the routes.
 *
 * At least one criteria must be specified for selection
 * OR operator is assumed among all attributes, i.e., when all lists are specified,
 * we consider a route matched if it matches any one of the lists
 * Each attribute has its matching criteria
 */
struct TRibRouteMatcher {
  // When prefixes are specified, match one of the prefixes
  1: optional list<bgp_attr.TIpPrefix> prefixes;

  // When community_list is specified, follow its rule to determine the match
  2: optional TCommunityListMatch community_list;
}

struct TPathSelectionStatement {
  // Select routes to be transformed
  1: TRibRouteMatcher matcher;

  2: TPathSelector multi_path_selector;
}

// override path selection criteria to compute routes
@fboss_common.AllowSkipThriftCow
struct TPathSelectionPolicy {
  // maps of statement name -> statement, e.g.,
  //   map<"cps-job-1-userA-vip", ...> statements;
  //   map<"cps-job-2-userA-vip", ...> statements;
  // each statement applies actions to a matched routes
  1: map<string, TPathSelectionStatement> statements;
  // monoincreasing version number of all Centralized Path Selection configs as a bundle
  2: i64 version;
}

struct TRouteAttributeLbwAction {
  // set aggregated link-bandwidth per rib entry (unit: bps)
  // no-op: value 0 will set rib entry lbw as None.
  1: i64 lbw;
}

struct TNextHopWeightAction {
  // a list of BGP path matchers
  // any path has to match one of path_matchers,
  // i.e., it's an "or" match among the path_matches
  1: list<TBgpPathMatcher> path_matchers;

  // applied nexthop weight
  2: i32 weight;
}

/**
 * Override nexthop weights for a given prefix
 * we use TBgpPathMatcher to identify nexthop path, we can support other identifer
 * like peer-ip-addr in future
 *
 * e.g
 * nexthopWeightActions[
 *   (as-regex=[".*64981"], weight=100), // eb1
 *   (as-regex=[".*64982"], weight=200), // eb2
 *   ...
 * ]
 */
struct TRouteAttributeUcmpAction {
  // TODO: deprecate this in favor of nexthop_weight_actions
  1: map<string, i32> nexthop_weight_map;

  2: list<TNextHopWeightAction> nexthop_weight_actions;

  // Assuming following:
  // S1: nexthops set discovered by BGP
  // S2: nexthops set discovered by CTE
  // if S1 > S2: CTE provides a partial view, BGP should fallback to ECMP
  // if S2 > S1: CTE provides more nexthop weights than what's discovered in BGP
  //    1) apply_all_actions_or_fallback_to_ecmp(true)
  //       weights are only valid iff all weight actions can be applied, otherwise
  //       BGP should fallback to ECMP (this is most likely applicable to DC CTE)
  //    2) apply_all_actions_or_fallback_to_ecmp(false)
  //       weights are valid even though some nexthops may not be discovered by
  //       BGP. (this is applicable to DC-BB UCMP case)
  3: bool apply_all_actions_or_fallback_to_ecmp;

  // Behavior to apply when a route has multiple paths matching the same action.
  // - false: weights are assigned as-is to each path. The total weight for the
  //   matching path set is equal to the product of
  //   (configured weight) * (number of paths in the set).
  // - true: weights for each path are divided by the number of paths in the
  //   set, such that the total weight of the path set is equal to the
  //   configured weight.
  4: optional bool divide_weights_by_matching_path_count;
}

/**
 * Captures information for route transform.
 *
 * At least one `set_` action must be specified
 * All provided `set_` actions are applied at once
 */
struct TRouteAttributeActions {
  1: optional TRouteAttributeLbwAction set_lbw;
  2: optional TRouteAttributeUcmpAction set_ucmp_weights;
}

struct TRouteAttributeStatement {
  // Select routes to be transformed
  1: TRibRouteMatcher matcher;

  // The actions to take for selected routes
  2: TRouteAttributeActions actions;

  // Number of seconds this statement is valid for.
  // The statements must be refreshed or updated within `ttl_secs`.
  // If not then policy will become in-effective.
  // Policy is preserved across restarts.
  // no-op: the statement will never retire
  3: optional i32 ttl_secs; // TO BE DEPRECATED

  // The version ID that helps trigger NSDB publication to bgpd
  // When any content in RibPolicy gets changed, bgpd will receive
  // a new FULL snapshot, and hence bumping version ID will trigger
  // TTL refreshment at bgpd.
  // TO BE DEPRECATED
  4: i32 version_id = 0; // TO BE DEPRECATED

  // expiration timestamp in epoch seconds
  // to replace ttl_secs field
  // statements must be refreshed or updated by this timestamp
  // no-op: statement is valid forever
  // policy is preserved across restarts
  5: optional i64 expiration_time_s;
}

// overwrite route attributes of computed routes
@fboss_common.AllowSkipThriftCow
struct TRouteAttributePolicy {
  // maps of statement name -> statement, e.g.,
  //   map<"cte-job-1-prefix-type1", ...> statements;
  // each statement applies actions to a matched routes
  1: map<string, TRouteAttributeStatement> statements;
}

struct TRouteFilter {
  // prefix ALLOWLIST applied to post-policy routes
  1: optional routing_policy.PrefixList prefix_list;
  // if explicitly set to true, log filter violations but not enforce
  2: optional bool permissive_mode;
}

struct TRouteFilterStatement {
  // applied to post-ingress-policy routes
  1: TRouteFilter ingress_filter;
  // applied to post-egress-policy routes
  2: TRouteFilter egress_filter;
}

// A policy containing golden prefixes.
// This policy will only be applied when BGP is in safe mode. See [redacted]
struct TGoldenPrefixPolicy {
  // A prefix must match at least one prefix in the allow list to be considered golden.
  1: optional routing_policy.PrefixList allowed_prefixes;
}

@fboss_common.AllowSkipThriftCow
struct TRouteFilterPolicy {
  // maps of device regex -> statement, e.g.,
  // {{".*eb.*", TRouteFilterStatement}}
  // routes from peers matching a regex will be subject to
  // filtering by that statement
  1: map<string, TRouteFilterStatement> statements;
  // monoincreasing version number of all Centralized Route Filtering configs as a bundle
  2: i64 version;
  // The golden prefix policy applies equally to all peers.
  3: optional TGoldenPrefixPolicy golden_prefix_policy;
  // The key type of the statement, e.g., DEVICE_REGEX or PEER_GROUP_NAME
  4: optional KeyType key_type;
}

/**
 * Represents a RibPolicy, which influences BGP route computation in multiple aspects:
 *   - route attribute overwrite (through route_attribute_policy)
 *   - path selection override (through path_selection_policy)
 *   - route filter override (through route_filter_policy)
 * Together, RibPolicy allows an external source (i.e., Centralium) to manipulate
 * BGP forwarding decisions. With a centralized view, the external source can achieve
 * better routing efficiency or safer traffic migration.
 *
 * `TRibPolicy` is intended to be set via RPC API and is not supported via Config
 * primarily because of its dynamic nature.
 *
 * Each policy component can and must be set individually
 */
struct TRibPolicy {
  4: optional TRouteAttributePolicy route_attribute_policy;

  5: optional TPathSelectionPolicy path_selection_policy;

  6: optional TRouteFilterPolicy route_filter_policy;
}

/**
 * CRF Policy Artifact generated by COOP.
 *
 * This struct is used for file-based CRF policy delivery from COOP to BGP++.
 * It wraps TRouteFilterPolicy with a mode flag.
 *
 * dryrun field controls the delivery mode:
 *   - false: FILE_MODE (BGP++ reads and applies policy from artifact file)
 *   - true:  DRYRUN_MODE (BGP++ reads policy but does not apply it)
 *
 * Consumers:
 *   - COOP (Python): Generates and writes this artifact to file
 *   - BGP++ (C++): Reads artifact at startup and via refreshCrfPolicyFromFile() RPC
 */
struct CrfPolicyArtifact {
  // The CRF policy to apply
  1: TRouteFilterPolicy policy;

  // Mode flag: true = DRYRUN_MODE (default), false = FILE_MODE
  2: bool dryrun = true;
}

/**
 * CPS Policy Artifact generated by COOP.
 *
 * This struct is used for file-based CPS policy delivery from COOP to BGP++.
 * It wraps TPathSelectionPolicy with a mode flag.
 *
 * dryrun field controls the delivery mode:
 *   - false: FILE_MODE (BGP++ reads and applies policy from artifact file)
 *   - true:  DRYRUN_MODE (BGP++ reads policy but does not apply it)
 *
 * Consumers:
 *   - COOP (Python): Generates and writes this artifact to file
 *   - BGP++ (C++): Reads artifact at startup and via refreshCpsPolicyFromFile() RPC
 */
struct CpsPolicyArtifact {
  // The CPS policy to apply
  1: TPathSelectionPolicy policy;

  // Mode flag: true = DRYRUN_MODE (default), false = FILE_MODE
  2: bool dryrun = true;
}
