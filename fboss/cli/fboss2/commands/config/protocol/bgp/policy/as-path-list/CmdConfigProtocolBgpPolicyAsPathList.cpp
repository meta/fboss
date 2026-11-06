/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/CmdConfigProtocolBgpPolicyAsPathList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

// BGP++ evaluates AS-path expressions with boost::regex, so validate with the
// same parser and accept exactly the expressions the daemon accepts.
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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/BgpAsPathListCliUtils.h"
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
// valid-attribute set and the handler table stay in sync. Patterns are flat
// `regex` attributes because bgpd stores them as a flat string list; nested
// entries are reserved for families where the daemon stores objects
// (prefix-list, routing-policy).
constexpr std::string_view kBooleanOperator = "boolean-operator";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kRegex = "regex";

constexpr std::string_view kBooleanOperatorAnd = "AND";
constexpr std::string_view kBooleanOperatorOr = "OR";

using AsPathList = bgp::bgp_policy::AsPathList;
using BooleanOperator = bgp::routing_policy::BooleanOperator;
using bgpcli::AttrHandler;
using bgpcli::enumAttr;
using bgpcli::err;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;
using bgpcli::Tokens;

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

// ---- setters ----------------------------------------------------------------
// Each writes one already-parsed, already-validated value. Parsing and message
// text belong to the shared factories in BgpCliAttrHandlers.h.

void setListBooleanOperator(AsPathList& list, BooleanOperator op) {
  list.boolean_operator() = op;
}

void setListDescription(AsPathList& list, const std::string& description) {
  list.description() = description;
}

// Hand-written because no factory covers an accumulating, validated set:
// each `regex` appends one boost::regex to as_paths (the field bgpd matches
// against the `_`-joined AS path), compiled here so a pattern bgpd would
// reject at load never reaches the session. Idempotent on repeats.
Result regex(AsPathList& list, const Tokens& values) {
  if (values.size() != 1 || values[0].empty()) {
    return err(
        fmt::format(
            "Error: {} requires <regex> (one token; join AS numbers with `_`, "
            "e.g. ^65000_65001$)",
            kRegex));
  }
  const auto& pattern = values[0];
  if (std::any_of(pattern.begin(), pattern.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
      })) {
    return err(
        fmt::format(
            "Error: {} '{}' must not contain whitespace; bgpd renders the AS "
            "path with `_` between AS numbers",
            kRegex,
            pattern));
  }
  try {
    // NOLINTNEXTLINE(facebook-hte-BoostRegexRisky)
    boost::regex compiled(pattern);
  } catch (const boost::regex_error& e) {
    return err(
        fmt::format("Error: Malformed {} '{}': {}", kRegex, pattern, e.what()));
  }
  auto& paths = list.as_paths().ensure();
  if (std::find(paths.begin(), paths.end(), pattern) != paths.end()) {
    return ok(fmt::format("{} {} already present", kRegex, pattern));
  }
  paths.push_back(pattern);
  return ok(fmt::format("Successfully added {} {}", kRegex, pattern));
}

// ---- list-level attribute registry ------------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<AsPathList>, std::less<>>&
listAttrHandlers() {
  static const std::string kBooleanOperatorValues =
      fmt::format("{}|{}", kBooleanOperatorAnd, kBooleanOperatorOr);
  static const std::map<std::string, AttrHandler<AsPathList>, std::less<>>
      kHandlers = {
          {std::string(kBooleanOperator),
           enumAttr<AsPathList, BooleanOperator>(
               kBooleanOperator,
               kBooleanOperatorValues,
               lookupBooleanOperator,
               setListBooleanOperator)},
          {std::string(kDescription),
           joinedStringAttr<AsPathList>(kDescription, setListDescription)},
          {std::string(kRegex), regex},
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
// errors (same mechanism as BgpPeerGroupConfig).
BgpAsPathListConfig::BgpAsPathListConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: as-path-list <name> is required, optionally followed by an "
        "<attribute> <value>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: as-path-list name must not be empty");
  }
  listName_ = v[0];

  if (v.size() == 1) {
    return; // bare `as-path-list <name>`: create the list
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (listAttrHandlers().find(attr_) == listAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown as-path-list attribute '{}'. Valid attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyAsPathListTraits::RetType
CmdConfigProtocolBgpPolicyAsPathList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool listCreated = !bgpcli::asPathListExists(cfg, args.listName());
  auto& list = bgpcli::findOrCreateAsPathList(cfg, args.listName());
  auto& lists = *cfg.policies().ensure().aspath_lists();

  Result result = args.attr().empty()
      ? ok(listCreated
               ? fmt::format(
                     "Successfully created BGP as-path-list {}",
                     args.listName())
               : fmt::format(
                     "BGP as-path-list {} already exists", args.listName()))
      // The attribute is guaranteed valid: BgpAsPathListConfig's constructor
      // rejects an unknown attribute before we get here.
      : listAttrHandlers().find(args.attr())->second(list, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message += fmt::format(" for as-path-list {}", args.listName());
    }
    if (args.attr() == kBooleanOperator) {
      // bgpd rejects a term whose inline copy of the list carries a different
      // operator ("Conflicting boolean_operator"), so keep every referencing
      // match in step with the list.
      for (auto& ref :
           bgpcli::findTermsReferencingAsPathList(cfg, args.listName())) {
        ref.filter->boolean_operator() = *list.boolean_operator();
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

void CmdConfigProtocolBgpPolicyAsPathList::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyAsPathList,
    CmdConfigProtocolBgpPolicyAsPathListTraits>::run();

} // namespace facebook::fboss
