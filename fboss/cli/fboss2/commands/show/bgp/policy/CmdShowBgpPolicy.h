/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/json/dynamic.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"

namespace facebook::fboss {

struct CmdShowBgpPolicyTraits : public ReadCommandTraits {
  static constexpr utils::ObjectArgTypeId ObjectArgTypeId =
      utils::ObjectArgTypeId::OBJECT_ARG_TYPE_ID_MESSAGE;
  using ObjectArgType = std::vector<std::string>;
  using RetType = folly::dynamic;

  // Human-authored guide prose for the CLI reference wiki. Superset of the
  // one-line help string registered in the command tree.
  static std::string_view description() {
    return "Shows the source of the BGP policy loaded by bgpd, followed by its content as JSON. Without a name, displays the complete policy document: community and local-preference definitions and all policy statements. With a name, displays that one statement from the same source. The source is BGP policy for a separate --policy artifact, or BGP config for policies embedded in the config. An unreadable active artifact never falls back to policies in the BGP config. An unknown name prints \"Policy '<name>' not found\" on stderr and no policy content. This is the BGP ingress/egress policy, not the separately pushed rib-policy.";
  }
};

class CmdShowBgpPolicy
    : public CmdHandler<CmdShowBgpPolicy, CmdShowBgpPolicyTraits> {
 public:
  using RetType = CmdShowBgpPolicyTraits::RetType;

  RetType queryClient(
      const HostInfo& hostInfo,
      const std::vector<std::string>& policyNames);
  void printOutput(const RetType& policyConfig, std::ostream& out = std::cout);

  /*
   * Synthetic standalone policy for the CLI reference wiki. Build it from
   * Thrift structs so the PORTABLE encoding matches the live RPC result.
   */
  static RetType sampleModel() {
    using facebook::bgp::bgp_policy::BgpPolicyAction;
    using facebook::bgp::bgp_policy::BgpPolicyActionType;
    using facebook::bgp::bgp_policy::BgpPolicyAtomicMatch;
    using facebook::bgp::bgp_policy::BgpPolicyAtomicMatchType;
    using facebook::bgp::bgp_policy::BgpPolicyMatch;
    using facebook::bgp::bgp_policy::BgpPolicyStatement;
    using facebook::bgp::bgp_policy::BgpPolicyTerm;
    using facebook::bgp::bgp_policy::CommunityListType;
    using facebook::bgp::bgp_policy::FlowControlAction;
    using facebook::bgp::bgp_policy::LocalPreference;

    // Match on a named community list, by reference rather than inline - which
    // is how a real policy is written, since the list is defined once and
    // reused.
    BgpPolicyAtomicMatch communityMatch;
    communityMatch.type() = BgpPolicyAtomicMatchType::COMMUNITY_LIST;
    CommunityListType communityListRef;
    communityListRef.community_list_name() = "CL_SAMPLE";
    communityMatch.community_list() = communityListRef;

    BgpPolicyMatch match;
    match.name() = "MATCH_SAMPLE_PREFIXES";
    match.match_entries() = {communityMatch};

    LocalPreference localPref;
    localPref.name() = "LP_SAMPLE";
    localPref.local_pref() = 100;

    BgpPolicyAction setLocalPref;
    setLocalPref.type() = BgpPolicyActionType::SET_LOCAL_PREF;
    setLocalPref.set_local_pref() = localPref;

    BgpPolicyAction permit;
    permit.type() = BgpPolicyActionType::PERMIT;

    BgpPolicyTerm term;
    term.name() = "SAMPLE_ACCEPT_TERM";
    term.description() =
        "Accept locally originated prefixes tagged with a sample community";
    term.policy_matches() = {match};
    term.policy_action_entries() = {setLocalPref, permit};
    term.term_miss_action() = FlowControlAction::NEXT_TERM;
    term.sequence_number() = 990;

    BgpPolicyStatement statement;
    statement.name() = "SAMPLE_UPLINK_OUT";
    statement.description() = "Egress policy applied to FSW uplinks";
    statement.policy_version() = "1";
    statement.policy_entries() = {term};
    // A term that misses falls through to the statement result, which denies -
    // the default-deny shape the prose points at.
    statement.result() = FlowControlAction::DENY;

    facebook::bgp::thrift::BgpCommunity community;
    community.name() = "SAMPLE_COMMUNITY";
    community.communities() = {"65108:337"};
    facebook::bgp::thrift::BgpLocalPref bgpLocalPref;
    bgpLocalPref.name() = "SAMPLE_LOCALPREF";
    bgpLocalPref.localpref() = 100;
    facebook::bgp::bgp_policy::BgpPolicies policies;
    policies.bgp_policy_statements() = {statement};

    facebook::bgp::thrift::BgpPolicyConfig policyConfig;
    policyConfig.communities() = {community};
    policyConfig.localprefs() = {bgpLocalPref};
    policyConfig.policies() = policies;
    return folly::dynamic::object("source", "BGP policy")(
        "policy",
        facebook::thrift::to_dynamic(
            policyConfig, facebook::thrift::dynamic_format::PORTABLE));
  }
};

} // namespace facebook::fboss
