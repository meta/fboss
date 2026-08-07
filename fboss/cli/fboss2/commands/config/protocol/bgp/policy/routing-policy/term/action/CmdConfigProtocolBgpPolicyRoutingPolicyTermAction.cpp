/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/action/CmdConfigProtocolBgpPolicyRoutingPolicyTermAction.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/IPAddress.h>
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/BgpRoutingPolicyCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

namespace {

// The `set <attribute>` dispatch keys, exactly as documented. Kept here so
// the valid-attribute set and the handler table stay in sync.
constexpr std::string_view kAsPathPrepend = "as-path prepend";
constexpr std::string_view kCommunity = "community";
constexpr std::string_view kLocalPref = "local-pref";
constexpr std::string_view kMed = "med";
constexpr std::string_view kNextHop = "next-hop";
constexpr std::string_view kOrigin = "origin";
constexpr std::string_view kWeight = "weight";

// result values, mapped onto bgp_policy.FlowControlAction. GOTO-TERM (the
// sheet's fourth value) is deferred: FlowControlAction has no GOTO arm, and
// the thrift models "go to term X" as a per-action-entry next_term_id whose
// target the documented grammar does not carry.
constexpr std::string_view kResultAccept = "ACCEPT";
constexpr std::string_view kResultReject = "REJECT";
constexpr std::string_view kResultContinue = "CONTINUE";

// origin values (bgp_policy.Origin names).
constexpr std::string_view kOriginIgp = "IGP";
constexpr std::string_view kOriginEgp = "EGP";
constexpr std::string_view kOriginIncomplete = "INCOMPLETE";

// next-hop's non-address spelling. bgpd's SetNexthop validator explicitly
// rejects set_self ("Unsupported nexthop config"), so the CLI rejects it
// up front instead of staging a config the daemon aborts on.
constexpr std::string_view kNextHopSelf = "self";
// `additive` switches community from overwrite (SET) to append (ADD).
constexpr std::string_view kCommunityAdditive = "additive";

constexpr int64_t kUint32Max = std::numeric_limits<uint32_t>::max();
constexpr int32_t kUint16Max = std::numeric_limits<uint16_t>::max();

using BgpPolicyAction = bgp::bgp_policy::BgpPolicyAction;
using BgpPolicyActionType = bgp::bgp_policy::BgpPolicyActionType;
using CommunityActionType = bgp::bgp_policy::CommunityActionType;
using BgpPolicyTerm = bgp::bgp_policy::BgpPolicyTerm;
using FlowControlAction = bgp::bgp_policy::FlowControlAction;
using MedActionType = bgp::bgp_policy::MedActionType;
using Origin = bgp::bgp_policy::Origin;
using WeightActionType = bgp::bgp_policy::WeightActionType;
using bgpcli::AttrHandler;
using bgpcli::enumAttr;
using bgpcli::err;
using bgpcli::ok;
using bgpcli::Result;
using bgpcli::Tokens;
using bgpcli::uintAttr;

// Find the action entry this attribute kind lives in, creating it if absent.
// The CLI keeps one BgpPolicyAction entry per action kind (the thrift models
// policy_action_entries as a list of single-purpose actions), keyed by the
// BgpPolicyActionType; re-issuing a kind updates its entry. The `type` field
// is marked deprecated in the thrift, but bgpd's
// createPolicyAttributesActionItem still dispatches on it and aborts on the
// unset default (0) — every entry MUST carry it.
BgpPolicyAction& findOrCreateActionEntry(
    BgpPolicyTerm& term,
    BgpPolicyActionType type) {
  auto& actions = *term.policy_action_entries();
  for (auto& action : actions) {
    if (*action.type() == type) {
      return action;
    }
  }
  actions.emplace_back();
  auto& action = actions.back();
  action.type() = type;
  return action;
}

// The shared "find-or-create the policy and term, dispatch, roll back
// phantoms on failure, persist on success" flow of both action leaves.
template <typename Handler>
std::string runTermActionCommand(
    const BgpRoutingPolicyConfig& policyArgs,
    const BgpRoutingPolicyTermConfig& termArgs,
    const std::string& leafName,
    Handler&& handler) {
  // Ancestor-level attributes mixed with an action command parse, but only
  // the leaf runs — reject instead of silently dropping them.
  if (!policyArgs.attr().empty() || !termArgs.attr().empty()) {
    return fmt::format(
        "Error: configure routing-policy/term attributes and action in "
        "separate commands (got attribute '{}' alongside action {})",
        !policyArgs.attr().empty() ? policyArgs.attr() : termArgs.attr(),
        leafName);
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

  Result result = handler(term);

  if (result.ok) {
    result.message += fmt::format(
        " for routing-policy {} term {}",
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

// ---- result handler --------------------------------------------------------

std::optional<FlowControlAction> parseResult(const std::string& value) {
  if (value == kResultAccept) {
    return FlowControlAction::ACCEPT;
  }
  if (value == kResultReject) {
    return FlowControlAction::DENY;
  }
  if (value == kResultContinue) {
    return FlowControlAction::NEXT_TERM;
  }
  return std::nullopt;
}

void setResult(BgpPolicyTerm& term, FlowControlAction result) {
  term.term_miss_action() = result;
}

const AttrHandler<BgpPolicyTerm>& resultHandler() {
  static const std::string kResultValues =
      fmt::format("{}|{}|{}", kResultAccept, kResultReject, kResultContinue);
  static const AttrHandler<BgpPolicyTerm> kHandler =
      enumAttr<BgpPolicyTerm, FlowControlAction>(
          "result", kResultValues, parseResult, setResult);
  return kHandler;
}

// ---- `set <attribute>` setters ---------------------------------------------
// Each writes one already-parsed, already-validated value into the term's
// action entry for its kind. Parsing, bounds and message text belong to the
// shared factories in BgpCliAttrHandlers.h; the three below that remain full
// handlers do so because their value shape has no factory: a uniform ASN
// list, a value plus an optional trailing flag, and an address with a
// rejected spelling.

void setLocalPref(BgpPolicyTerm& term, int64_t localPref) {
  findOrCreateActionEntry(term, BgpPolicyActionType::SET_LOCAL_PREF)
      .set_local_pref()
      .ensure()
      .local_pref() = localPref;
}

void setMed(BgpPolicyTerm& term, int64_t med) {
  auto& medAction = findOrCreateActionEntry(term, BgpPolicyActionType::MED)
                        .med_action()
                        .ensure();
  medAction.med_value() = med;
  medAction.med_action_type() = MedActionType::SET;
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

void setOrigin(BgpPolicyTerm& term, Origin origin) {
  findOrCreateActionEntry(term, BgpPolicyActionType::ORIGIN).set_origin() =
      origin;
}

void setWeight(BgpPolicyTerm& term, int64_t weight) {
  auto& weightAction =
      findOrCreateActionEntry(term, BgpPolicyActionType::WEIGHT)
          .weight_action()
          .ensure();
  weightAction.weight_value() = static_cast<int32_t>(weight);
  weightAction.weight_action_type() = WeightActionType::SET;
}

// SetAsPathPrepend models ONE ASN repeated N times, so the documented
// <asn-list> must be uniform: `65000 65000` becomes {asn=65000,
// repeat_times=2}.
Result actionAsPathPrepend(BgpPolicyTerm& term, const Tokens& values) {
  if (values.empty()) {
    return err("Error: as-path prepend requires <asn> [<asn> ...]");
  }
  auto first = bgpcli::parseAsn4Byte(values[0]);
  for (const auto& v : values) {
    auto asn = bgpcli::parseAsn4Byte(v);
    if (!asn) {
      return err(
          fmt::format(
              "Error: Invalid as-path prepend ASN '{}'; expected a 4-byte ASN",
              v));
    }
    if (*asn != *first) {
      return err(
          "Error: as-path prepend supports a single ASN repeated N times "
          "(e.g. `65000 65000`); mixed ASNs are not supported");
    }
  }
  auto& prepend =
      findOrCreateActionEntry(term, BgpPolicyActionType::AS_PATH_PREPEND)
          .set_as_path_prepend()
          .ensure();
  prepend.asn() = *first;
  prepend.repeat_times() = static_cast<int32_t>(values.size());
  return ok(
      fmt::format(
          "Successfully set as-path prepend to: {} x{}",
          *first,
          values.size()));
}

// <community-string> [additive]: additive appends (ADD), the default
// overwrites (SET). bgpd's CommunityAction reads the thrift-deprecated but
// still load-bearing community_action.communities string list — the sheet's
// JSON field path agrees.
Result actionCommunity(BgpPolicyTerm& term, const Tokens& values) {
  if (values.empty() || values.size() > 2 ||
      (values.size() == 2 && values[1] != kCommunityAdditive)) {
    return err("Error: community requires <community-string> [additive]");
  }
  const bool additive = values.size() == 2;
  auto& communityAction =
      findOrCreateActionEntry(term, BgpPolicyActionType::COMMUNITY_LIST)
          .community_action()
          .ensure();
  communityAction.name() = values[0];
  communityAction.communities().ensure() = {values[0]};
  communityAction.action_type() =
      additive ? CommunityActionType::ADD : CommunityActionType::SET;
  return ok(
      fmt::format(
          "Successfully set community to: {}{}",
          values[0],
          additive ? " (additive)" : ""));
}

// <ip-address>. The sheet also documents `self` and `peer-address`, but
// bgpd's SetNexthop validator rejects set_self and the thrift has no peer
// arm — both are deferred rather than staging a config the daemon aborts on.
Result actionNextHop(BgpPolicyTerm& term, const Tokens& values) {
  if (values.size() != 1) {
    return err("Error: next-hop requires <ip-address>");
  }
  if (values[0] == kNextHopSelf) {
    return err(
        "Error: next-hop `self` is not supported by bgpd yet; expected an "
        "IP address");
  }
  auto addr = folly::IPAddress::tryFromString(values[0]);
  if (!addr) {
    return err(
        fmt::format(
            "Error: Invalid next-hop value '{}'; expected an IP address",
            values[0]));
  }
  auto& nexthop = findOrCreateActionEntry(term, BgpPolicyActionType::NEXT_HOP)
                      .set_nexthop()
                      .ensure();
  nexthop.set_self() = false;
  auto& hop = nexthop.next_hop().ensure();
  hop.version() = addr->isV6() ? 6 : 4;
  hop.next_hop_prefix() = values[0];
  return ok(fmt::format("Successfully set next-hop to: {}", values[0]));
}

// ---- `set <attribute>` registry --------------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter or handler that stores it.
const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>&
setAttrHandlers() {
  static const std::string kOriginValues =
      fmt::format("{}|{}|{}", kOriginIgp, kOriginEgp, kOriginIncomplete);
  static const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>
      kHandlers = {
          {std::string(kAsPathPrepend), actionAsPathPrepend},
          {std::string(kCommunity), actionCommunity},
          {std::string(kLocalPref),
           uintAttr<BgpPolicyTerm>(kLocalPref, kUint32Max, setLocalPref)},
          {std::string(kMed),
           uintAttr<BgpPolicyTerm>(kMed, kUint32Max, setMed)},
          {std::string(kNextHop), actionNextHop},
          {std::string(kOrigin),
           enumAttr<BgpPolicyTerm, Origin>(
               kOrigin, kOriginValues, parseOrigin, setOrigin)},
          {std::string(kWeight),
           uintAttr<BgpPolicyTerm>(kWeight, kUint16Max, setWeight)},
      };
  return kHandlers;
}

std::string validSetAttrList() {
  std::string out;
  for (const auto& [name, _] : setAttrHandlers()) {
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
// errors (same mechanism as BgpRoutingPolicyTermConfig).
BgpRoutingPolicyTermActionResultConfig::BgpRoutingPolicyTermActionResultConfig(
    std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: action result requires <ACCEPT|REJECT|CONTINUE>");
  }
  values_ = std::move(v);
}

BgpRoutingPolicyTermActionSetConfig::BgpRoutingPolicyTermActionSetConfig(
    std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: action set requires an <attribute>. Valid attributes: {}",
            validSetAttrList()));
  }
  // `as-path` only exists composed with `prepend`; anything else is a
  // single-token attribute name.
  size_t valueStart = 1;
  if (v[0] == "as-path") {
    if (v.size() < 2 || v[1] != "prepend") {
      throw std::invalid_argument(
          "Error: `as-path` must be followed by `prepend` "
          "(usage: set as-path prepend <asn> [<asn> ...])");
    }
    attr_ = std::string(kAsPathPrepend);
    valueStart = 2;
  } else {
    attr_ = v[0];
  }
  values_.assign(v.begin() + valueStart, v.end());

  if (setAttrHandlers().find(attr_) == setAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown action set attribute '{}'. Valid attributes: {}",
            attr_,
            validSetAttrList()));
  }
}

CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits::RetType
CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpRoutingPolicyConfig& policyArgs,
    const BgpRoutingPolicyTermConfig& termArgs,
    const ObjectArgType& args) {
  return runTermActionCommand(
      policyArgs, termArgs, "result", [&](BgpPolicyTerm& term) {
        return resultHandler()(term, args.values());
      });
}

void CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits::RetType
CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpRoutingPolicyConfig& policyArgs,
    const BgpRoutingPolicyTermConfig& termArgs,
    const ObjectArgType& args) {
  return runTermActionCommand(
      policyArgs, termArgs, "set", [&](BgpPolicyTerm& term) {
        // The attribute is guaranteed valid: the constructor rejects an
        // unknown attribute before we get here.
        return setAttrHandlers().find(args.attr())->second(term, args.values());
      });
}

void CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiations
template void CmdHandler<
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult,
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResultTraits>::run();
template void CmdHandler<
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet,
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSetTraits>::run();

} // namespace facebook::fboss
