/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/match/CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/BgpRoutingPolicyCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

namespace {

// The composed `from <attribute>` dispatch keys, exactly as documented. Kept
// here so the valid-attribute set and the handler table stay in sync.
//
// Four of the seven documented `match from` attributes are NOT offered,
// because bgpd cannot apply them (each was proven by crash-looping bgpd on a
// device, then confirmed in private-BGP):
//
//  - local-pref, med, next-hop (NOS-6683/6684/6685):
//    createPolicyAttributeMatchItem has no LOCAL_PREFERENCE, MED or NEXT_HOP
//    case, so those atomic types hit `default:` and throw
//    "BgpPolicyAtomicMatch Config input error for type".
//  - community-list (NOS-6682): CommunityMatch's constructor throws
//    "The attribute \"communities\" is empty" unless the inline communities
//    list is non-empty, and CommunityMatch::PopulateReferences — the only
//    thing that resolves community_list_names — runs later, from
//    PolicyManager, once every Policy is already constructed. The by-name
//    reference path is therefore unreachable; only inlined community values
//    work, which is a snapshot rather than a reference.
//
// All four need a private-BGP change before the CLI can accept them.
constexpr std::string_view kFromKeyword = "from";
constexpr std::string_view kFromAsPathList = "from as-path-list";
constexpr std::string_view kFromOrigin = "from origin";
constexpr std::string_view kFromPrefixList = "from prefix-list";

// origin values (bgp_policy.Origin names).
constexpr std::string_view kOriginIgp = "IGP";
constexpr std::string_view kOriginEgp = "EGP";
constexpr std::string_view kOriginIncomplete = "INCOMPLETE";

using BgpPolicyAtomicMatch = bgp::bgp_policy::BgpPolicyAtomicMatch;
using BgpPolicyAtomicMatchType = bgp::bgp_policy::BgpPolicyAtomicMatchType;
using BgpPolicyTerm = bgp::bgp_policy::BgpPolicyTerm;
using Origin = bgp::bgp_policy::Origin;
using bgpcli::AttrHandler;
using bgpcli::enumAttr;
using bgpcli::Result;
using bgpcli::stringAttr;

// Find the atomic match of `type`, creating it (and the term's match object)
// if absent. Atomic entries are keyed by BgpPolicyAtomicMatchType, so
// re-issuing a match kind updates its entry rather than appending.
//
// The container is BgpPolicyTerm.policy_match_entries — a single
// BgpPolicyMatch, marked @thrift.Deprecated but the ONLY one bgpd reads:
// PolicyTerm.cpp guards its match loop on `if (term.policy_match_entries())`,
// and nothing in private-BGP reads the newer `policy_matches` list at all, so
// matches written there are silently ignored. Entries compose under
// BgpPolicyMatch's default AND, which is also the only operator bgpd accepts
// for more than one entry.
BgpPolicyAtomicMatch& findOrCreateAtomicMatch(
    BgpPolicyTerm& term,
    BgpPolicyAtomicMatchType type) {
  auto& entries = *term.policy_match_entries().ensure().match_entries();
  for (auto& entry : entries) {
    if (*entry.type() == type) {
      return entry;
    }
  }
  entries.emplace_back();
  auto& entry = entries.back();
  entry.type() = type;
  return entry;
}

// ---- match setters ---------------------------------------------------------
// Each writes one already-parsed, already-validated value into the term's
// atomic match of the matching type. Parsing and message text belong to the
// shared factories in BgpCliAttrHandlers.h.
//
// The list-typed matches take a full inline object rather than a name-only
// union arm, and a reference is expressed by its *_list_names field — NOT by
// the object's `name`, which is only a label. bgpd's match class keeps those
// names, then PolicyManager::PopulateReferences resolves each against its
// by-name map and merges in the referenced list's contents. Writing `name`
// instead leaves the match with nothing to compare against, so it silently
// matches nothing rather than failing loudly.

void setAsPathList(BgpPolicyTerm& term, const std::string& name) {
  findOrCreateAtomicMatch(term, BgpPolicyAtomicMatchType::AS_PATH)
      .as_path_filters()
      .ensure()
      .as_path_list_names()
      .ensure() = {name};
}

void setOrigin(BgpPolicyTerm& term, Origin origin) {
  findOrCreateAtomicMatch(term, BgpPolicyAtomicMatchType::ORIGIN).origin() =
      origin;
}

void setPrefixList(BgpPolicyTerm& term, const std::string& name) {
  findOrCreateAtomicMatch(term, BgpPolicyAtomicMatchType::PREFIX_LIST)
      .prefix_filters()
      .ensure()
      .prefix_list_names() = {name};
}

std::optional<Origin> parseOrigin(const std::string& value) {
  if (value == kOriginIgp) {
    return Origin::IGP;
  }
  if (value == kOriginEgp) {
    return Origin::EGP;
  }
  if (value == kOriginIncomplete) {
    return Origin::INCOMPLETE;
  }
  return std::nullopt;
}

// ---- match attribute registry ----------------------------------------------
// One line per supported attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>&
matchAttrHandlers() {
  static const std::string kOriginValues =
      fmt::format("{}|{}|{}", kOriginIgp, kOriginEgp, kOriginIncomplete);
  static const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>
      kHandlers = {
          {std::string(kFromAsPathList),
           stringAttr<BgpPolicyTerm>("as-path-list", "name", setAsPathList)},
          {std::string(kFromOrigin),
           enumAttr<BgpPolicyTerm, Origin>(
               "origin", kOriginValues, parseOrigin, setOrigin)},
          {std::string(kFromPrefixList),
           stringAttr<BgpPolicyTerm>("prefix-list", "name", setPrefixList)},
      };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : matchAttrHandlers()) {
    if (!out.empty()) {
      out += ", ";
    }
    out += name;
  }
  return out;
}

} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
// Throwing std::invalid_argument is how the framework surfaces arg parse
// errors (same mechanism as BgpRoutingPolicyTermActionSetConfig).
BgpRoutingPolicyTermMatchConfig::BgpRoutingPolicyTermMatchConfig(
    std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty() || v[0] != kFromKeyword) {
    throw std::invalid_argument(
        "Error: match requires `from <attribute> <value>`");
  }
  if (v.size() < 2) {
    throw std::invalid_argument(
        fmt::format(
            "Error: `from` requires an <attribute>. Valid attributes: {}",
            validAttrList()));
  }
  attr_ = fmt::format("{} {}", kFromKeyword, v[1]);
  values_.assign(v.begin() + 2, v.end());

  if (matchAttrHandlers().find(attr_) == matchAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown match attribute '{}'. Valid attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits::RetType
CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpRoutingPolicyConfig& policyArgs,
    const BgpRoutingPolicyTermConfig& termArgs,
    const ObjectArgType& args) {
  // Ancestor-level attributes mixed with a match command parse, but only
  // the leaf (this command) runs — reject instead of silently dropping them.
  if (!policyArgs.attr().empty() || !termArgs.attr().empty()) {
    return fmt::format(
        "Error: configure routing-policy/term attributes and match in "
        "separate commands (got attribute '{}' alongside match)",
        !policyArgs.attr().empty() ? policyArgs.attr() : termArgs.attr());
  }

  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool policyCreated =
      !bgpcli::routingPolicyExists(cfg, policyArgs.policyName());
  auto& policy =
      bgpcli::findOrCreateRoutingPolicy(cfg, policyArgs.policyName());
  const bool termCreated =
      !bgpcli::routingPolicyTermExists(policy, termArgs.seqNum());
  auto& term = bgpcli::findOrCreateRoutingPolicyTerm(policy, termArgs.seqNum());

  // The attribute is guaranteed valid: BgpRoutingPolicyTermMatchConfig's
  // constructor rejects an unknown attribute before we get here.
  Result result =
      matchAttrHandlers().find(args.attr())->second(term, args.values());

  if (result.ok) {
    result.message += fmt::format(
        " for routing-policy {} term {} match",
        policyArgs.policyName(),
        termArgs.seqNum());
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  } else {
    // A rejected value must not leave a phantom term (or a phantom policy
    // implicitly created for it) visible to later lookups in this process.
    if (termCreated) {
      policy.policy_entries()->pop_back();
    }
    if (policyCreated) {
      cfg.policies()->bgp_policy_statements()->pop_back();
    }
  }
  return result.message;
}

void CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch,
    CmdConfigProtocolBgpPolicyRoutingPolicyTermMatchTraits>::run();

} // namespace facebook::fboss
