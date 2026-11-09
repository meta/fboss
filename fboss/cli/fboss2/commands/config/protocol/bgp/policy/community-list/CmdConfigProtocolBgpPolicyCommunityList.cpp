/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/community-list/CmdConfigProtocolBgpPolicyCommunityList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

// BGP++ evaluates community expressions with boost::regex, so validate with
// the same parser and accept exactly the expressions the daemon accepts.
// NOLINTNEXTLINE(misc-include-cleaner,facebook-hte-BadInclude-boost/regex.hpp)
#include <boost/regex.hpp>
#include <fmt/core.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/community-list/BgpCommunityListCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h>
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {

// The attribute names, exactly as documented. Kept here so the
// valid-attribute set and the handler table stay in sync. Values are flat
// `community` attributes because bgpd stores them as a flat string list;
// nested entries are reserved for families where the daemon stores objects
// (prefix-list, routing-policy).
constexpr std::string_view kBooleanOperator = "boolean-operator";
constexpr std::string_view kCommunity = "community";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kExactMatch = "exact-match";

// boolean-operator values (routing_policy.BooleanOperator names).
constexpr std::string_view kBooleanOperatorAnd = "AND";
constexpr std::string_view kBooleanOperatorOr = "OR";

using BooleanOperator = bgp::routing_policy::BooleanOperator;
using CommunityList = bgp::bgp_policy::CommunityList;
using bgpcli::AttrHandler;
using bgpcli::boolAttr;
using bgpcli::enumAttr;
using bgpcli::err;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;
using bgpcli::Tokens;

// ---- value lookups ----------------------------------------------------------

// NOT is deliberately not offered: bgpd treats every operator other than OR
// as "all must match", so NOT would silently behave as AND.
std::optional<BooleanOperator> lookupBooleanOperator(const std::string& s) {
  if (s == kBooleanOperatorAnd) {
    return BooleanOperator::AND;
  }
  if (s == kBooleanOperatorOr) {
    return BooleanOperator::OR;
  }
  return std::nullopt;
}

// ---- list-level setters -----------------------------------------------------
// Each writes one already-parsed, already-validated value. Parsing and message
// text belong to the shared factories in BgpCliAttrHandlers.h.

void setListBooleanOperator(CommunityList& list, BooleanOperator op) {
  list.boolean_operator() = op;
}

void setListDescription(CommunityList& list, const std::string& description) {
  list.description() = description;
}

void setListExactMatch(CommunityList& list, bool exactMatch) {
  list.exact_match() = exactMatch;
}

// Hand-written because no factory covers an accumulating, validated set:
// each `community` appends one value to communities (the field bgpd matches
// against). bgpd accepts either a literal `<asn>:<value>` or a regex, and
// compiles anything that is not a literal as a regex at load, so compile it
// here too and keep a pattern bgpd would reject out of the session.
// Idempotent on repeats.
Result community(CommunityList& list, const Tokens& values) {
  if (values.size() != 1 || values[0].empty()) {
    return err(
        fmt::format(
            "Error: {} requires <community> (one token; a literal such as "
            "65000:100 or a regex)",
            kCommunity));
  }
  const auto& value = values[0];
  if (std::any_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
      })) {
    return err(
        fmt::format(
            "Error: {} '{}' must not contain whitespace; repeat the command "
            "once per community",
            kCommunity,
            value));
  }
  try {
    // NOLINTNEXTLINE(facebook-hte-BoostRegexRisky)
    boost::regex compiled(value);
  } catch (const boost::regex_error& e) {
    return err(
        fmt::format(
            "Error: Malformed {} '{}': {}", kCommunity, value, e.what()));
  }
  auto& communities = list.communities().ensure();
  if (std::find(communities.begin(), communities.end(), value) !=
      communities.end()) {
    return ok(fmt::format("{} {} already present", kCommunity, value));
  }
  communities.push_back(value);
  return ok(fmt::format("Successfully added {} {}", kCommunity, value));
}

// ---- list-level attribute registry ------------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<CommunityList>, std::less<>>&
listAttrHandlers() {
  static const std::string kBooleanOperatorValues =
      fmt::format("{}|{}", kBooleanOperatorAnd, kBooleanOperatorOr);
  static const std::map<std::string, AttrHandler<CommunityList>, std::less<>>
      kHandlers = {
          {std::string(kBooleanOperator),
           enumAttr<CommunityList, BooleanOperator>(
               kBooleanOperator,
               kBooleanOperatorValues,
               lookupBooleanOperator,
               setListBooleanOperator)},
          {std::string(kCommunity), community},
          {std::string(kDescription),
           joinedStringAttr<CommunityList>(kDescription, setListDescription)},
          {std::string(kExactMatch),
           boolAttr<CommunityList>(kExactMatch, setListExactMatch)},
      };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : listAttrHandlers()) {
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
// errors (same mechanism as BgpAsPathListConfig).
BgpCommunityListConfig::BgpCommunityListConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: community-list <name> is required, optionally followed by an "
        "<attribute> <value>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: community-list name must not be empty");
  }
  listName_ = v[0];

  if (v.size() == 1) {
    return; // bare `community-list <name>`: create the list
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (listAttrHandlers().find(attr_) == listAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown community-list attribute '{}'. Valid attributes: "
            "{}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyCommunityListTraits::RetType
CmdConfigProtocolBgpPolicyCommunityList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool listCreated = !bgpcli::communityListExists(cfg, args.listName());
  auto& list = bgpcli::findOrCreateCommunityList(cfg, args.listName());
  auto& lists = *cfg.policies().ensure().community_lists();

  Result result = args.attr().empty()
      ? ok(listCreated
               ? fmt::format(
                     "Successfully created BGP community-list {}",
                     args.listName())
               : fmt::format(
                     "BGP community-list {} already exists", args.listName()))
      // The attribute is guaranteed valid: BgpCommunityListConfig's
      // constructor rejects an unknown attribute before we get here.
      : listAttrHandlers().find(args.attr())->second(list, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message += fmt::format(" for community-list {}", args.listName());
    }
    if (args.attr() == kBooleanOperator || args.attr() == kExactMatch) {
      // bgpd requires a term's inline copy of the list to carry the same
      // boolean_operator ("Conflicting boolean_operator from the reference")
      // and reads exact_match only from that copy. Keep both fields
      // synchronized on either update: besides applying the requested
      // attribute, this repairs a stale inline copy that bgpd would reject.
      for (auto& ref :
           bgpcli::findTermsReferencingCommunityList(cfg, args.listName())) {
        if (ref.filter == nullptr) {
          continue;
        }
        ref.filter->boolean_operator() = *list.boolean_operator();
        if (list.exact_match().has_value()) {
          ref.filter->exact_match() = *list.exact_match();
        }
      }
    }
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  } else if (listCreated && !lists.empty()) {
    // Drop the phantom list so a rejected value is not visible to later
    // lookups in the same process.
    lists.pop_back();
  }
  return result.message;
}

void CmdConfigProtocolBgpPolicyCommunityList::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyCommunityList,
    CmdConfigProtocolBgpPolicyCommunityListTraits>::run();

} // namespace facebook::fboss
