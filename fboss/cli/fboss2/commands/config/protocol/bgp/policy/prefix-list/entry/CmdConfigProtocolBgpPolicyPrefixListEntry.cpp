/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/entry/CmdConfigProtocolBgpPolicyPrefixListEntry.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/BgpPrefixListCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {

// The attribute names, exactly as documented. Kept here so the
// valid-attribute set and the handler table stay in sync. base-prefix is not
// an attribute: it is the entry's key, given as the `prefix <prefix/len>`
// selector.
constexpr std::string_view kCommunities = "communities";
constexpr std::string_view kCompareOperator = "compare-operator";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kMatchLogic = "match-logic";
constexpr std::string_view kMaxAllowedSubnetCount = "max-allowed-subnet-count";
constexpr std::string_view kPrefixLenRange = "prefix-len-range";
constexpr std::string_view kRegex = "regex";
constexpr std::string_view kValue = "value";

// prefix-len-range compare-operator values
// (routing_policy.ComparisonOperator names). RG is not offered: bgpd's
// toPolicyComparisonOperator() throws on it.
constexpr std::string_view kCompareOperatorEq = "EQ";
constexpr std::string_view kCompareOperatorGe = "GE";
constexpr std::string_view kCompareOperatorLe = "LE";
constexpr std::string_view kCompareOperatorNe = "NE";
constexpr std::string_view kCompareOperatorGt = "GT";
constexpr std::string_view kCompareOperatorLt = "LT";

// match-logic values (routing_policy.MatchValueLogicOperator names). Only
// EQUAL is offered: bgpd throws "Unsupported Prefix configuration:
// match_logic" for anything else once a policy references the list.
constexpr std::string_view kMatchLogicEqual = "EQUAL";

// prefix-len-range value bounds: a prefix length (v6 caps it at 128).
constexpr int32_t kPrefixLenMax = 128;

using ComparisonOperator = bgp::routing_policy::ComparisonOperator;
using CompareNumericValue = bgp::routing_policy::CompareNumericValue;
using MatchValueLogicOperator = bgp::routing_policy::MatchValueLogicOperator;
using PrefixListEntry = bgp::routing_policy::PrefixListEntry;
using bgpcli::AttrHandler;
using bgpcli::enumAttr;
using bgpcli::err;
using bgpcli::intAttr;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;
using bgpcli::Tokens;

// ---- value lookups ----------------------------------------------------------

std::optional<MatchValueLogicOperator> lookupMatchLogic(const std::string& s) {
  if (s == kMatchLogicEqual) {
    return MatchValueLogicOperator::EQUAL;
  }
  return std::nullopt;
}

std::optional<ComparisonOperator> lookupPrefixLenRangeOperator(
    const std::string& s) {
  if (s == kCompareOperatorEq) {
    return ComparisonOperator::EQ;
  }
  if (s == kCompareOperatorGe) {
    return ComparisonOperator::GE;
  }
  if (s == kCompareOperatorLe) {
    return ComparisonOperator::LE;
  }
  if (s == kCompareOperatorNe) {
    return ComparisonOperator::NE;
  }
  if (s == kCompareOperatorGt) {
    return ComparisonOperator::GT;
  }
  if (s == kCompareOperatorLt) {
    return ComparisonOperator::LT;
  }
  return std::nullopt;
}

// The prefix-len-range sub-attributes report themselves with the two-token
// name the user typed, so the message names the command rather than the
// sub-key alone. Hoisted so the string_view the factory captures outlives it.
const std::string& prefixLenRangeCompareOperatorName() {
  static const std::string kName =
      fmt::format("{} {}", kPrefixLenRange, kCompareOperator);
  return kName;
}

const std::string& prefixLenRangeValueName() {
  static const std::string kName =
      fmt::format("{} {}", kPrefixLenRange, kValue);
  return kName;
}

// ---- prefix-len-range setters -----------------------------------------------

void setRangeCompareOperator(
    CompareNumericValue& range,
    ComparisonOperator op) {
  range.compare_operator() = op;
}

void setRangeValue(CompareNumericValue& range, int32_t value) {
  range.value() = value;
}

// ---- prefix-len-range sub-attribute registry --------------------------------
// The thrift models a list of accepted length ranges (prefix_len_ranges); the
// documented CLI exposes a single range, so these target the one
// CompareNumericValue the entry handler maintains at prefix_len_ranges[0].
const std::map<std::string, AttrHandler<CompareNumericValue>, std::less<>>&
prefixLenRangeAttrHandlers() {
  static const std::string kRangeOperatorValues = fmt::format(
      "{}|{}|{}|{}|{}|{}",
      kCompareOperatorEq,
      kCompareOperatorGe,
      kCompareOperatorLe,
      kCompareOperatorNe,
      kCompareOperatorGt,
      kCompareOperatorLt);
  static const std::string kValueRange = fmt::format("0-{}", kPrefixLenMax);
  static const std::
      map<std::string, AttrHandler<CompareNumericValue>, std::less<>>
          kHandlers = {
              {std::string(kCompareOperator),
               enumAttr<CompareNumericValue, ComparisonOperator>(
                   prefixLenRangeCompareOperatorName(),
                   kRangeOperatorValues,
                   lookupPrefixLenRangeOperator,
                   setRangeCompareOperator)},
              {std::string(kValue),
               intAttr<CompareNumericValue>(
                   prefixLenRangeValueName(),
                   kValueRange,
                   0,
                   kPrefixLenMax,
                   setRangeValue)},
          };
  return kHandlers;
}

const std::string& prefixLenRangeUsage() {
  static const std::string kUsage = fmt::format(
      "Error: {} requires <{}|{}> <value>",
      kPrefixLenRange,
      kCompareOperator,
      kValue);
  return kUsage;
}

// ---- entry-level setters ----------------------------------------------------

void setDescription(PrefixListEntry& entry, const std::string& description) {
  entry.description() = description;
}

void setMatchLogic(PrefixListEntry& entry, MatchValueLogicOperator op) {
  entry.match_logic() = op;
}

// bgpd only honours this for golden-prefix policies; elsewhere it is stored
// and ignored.
void setMaxAllowedSubnetCount(PrefixListEntry& entry, int32_t count) {
  entry.max_allowed_golden_prefix_subnet_count() = count;
}

// ---- hand-written handlers --------------------------------------------------
// These three remain full handlers because their value shape has no factory:
// an accumulating set, a composed sub-attribute, and a stored-verbatim regex.

Result communities(PrefixListEntry& entry, const Tokens& values) {
  if (values.size() != 1) {
    return err(
        fmt::format("Error: {} requires <community-string>", kCommunities));
  }
  // The accepted communities are a set; repeated invocations accumulate
  // members rather than replacing the set.
  const bool inserted = entry.communities().ensure().insert(values[0]).second;
  return ok(
      inserted
          ? fmt::format("Successfully added {} to {}", values[0], kCommunities)
          : fmt::format("{} already contains {}", kCommunities, values[0]));
}

Result prefixLenRange(PrefixListEntry& entry, const Tokens& values) {
  if (values.empty()) {
    return err(prefixLenRangeUsage());
  }
  auto handler = prefixLenRangeAttrHandlers().find(values[0]);
  if (handler == prefixLenRangeAttrHandlers().end()) {
    return err(prefixLenRangeUsage());
  }
  auto& ranges = *entry.prefix_len_ranges();
  const bool rangeCreated = ranges.empty();
  if (rangeCreated) {
    ranges.emplace_back();
  }
  auto result =
      handler->second(ranges.at(0), Tokens(values.begin() + 1, values.end()));
  // A rejected value must not leave a phantom range behind.
  if (!result.ok && rangeCreated) {
    ranges.clear();
  }
  return result;
}

Result regex(PrefixListEntry& entry, const Tokens& values) {
  if (values.size() != 1) {
    return err(fmt::format("Error: {} requires <string>", kRegex));
  }
  // The thrift documents regex as an alternative to prefix_len_ranges; the CLI
  // stores what is given and leaves precedence to the daemon.
  entry.regex() = values[0];
  return ok(fmt::format("Successfully set {} to: {}", kRegex, values[0]));
}

// ---- entry-level attribute registry ----------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter or handler that stores it.
const std::map<std::string, AttrHandler<PrefixListEntry>, std::less<>>&
entryAttrHandlers() {
  static const std::string kMatchLogicValues = std::string(kMatchLogicEqual);
  static const std::map<std::string, AttrHandler<PrefixListEntry>, std::less<>>
      kHandlers = {
          {std::string(kCommunities), communities},
          {std::string(kDescription),
           joinedStringAttr<PrefixListEntry>(kDescription, setDescription)},
          {std::string(kMatchLogic),
           enumAttr<PrefixListEntry, MatchValueLogicOperator>(
               kMatchLogic,
               kMatchLogicValues,
               lookupMatchLogic,
               setMatchLogic)},
          {std::string(kMaxAllowedSubnetCount),
           intAttr<PrefixListEntry>(
               kMaxAllowedSubnetCount,
               "non-negative integer",
               0,
               std::numeric_limits<int32_t>::max(),
               setMaxAllowedSubnetCount)},
          {std::string(kPrefixLenRange), prefixLenRange},
          {std::string(kRegex), regex},
      };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : entryAttrHandlers()) {
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
// errors (same mechanism as BgpPrefixListConfig).
BgpPrefixListEntryConfig::BgpPrefixListEntryConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: prefix <prefix/len> is required, optionally followed by an "
        "<attribute> <value>");
  }
  const auto normalizedPrefix = bgpcli::normalizePrefixWithLength(v[0]);
  if (!normalizedPrefix) {
    throw std::invalid_argument(
        fmt::format("Error: Invalid prefix '{}'; expected <prefix/len>", v[0]));
  }
  basePrefix_ = *normalizedPrefix;
  if (v.size() == 1) {
    return; // bare `prefix <prefix/len>`: create it
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (entryAttrHandlers().find(attr_) == entryAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown prefix-list prefix attribute '{}'. Valid "
            "attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyPrefixListEntryTraits::RetType
CmdConfigProtocolBgpPolicyPrefixListEntry::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpPrefixListConfig& listArgs,
    const ObjectArgType& args) {
  // The parent parse accepts `prefix-list <name> <attr> <value> ... prefix
  // ...`, but only the leaf (this command) runs — silently dropping the
  // list-level attribute would look like it was staged. Reject the mix.
  if (!listArgs.attr().empty()) {
    return fmt::format(
        "Error: configure prefix-list attributes and prefix in separate "
        "commands (got prefix-list attribute '{}' alongside prefix {})",
        listArgs.attr(),
        args.basePrefix());
  }

  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool listCreated = !bgpcli::prefixListExists(cfg, listArgs.listName());
  auto& list = bgpcli::findOrCreatePrefixList(cfg, listArgs.listName());
  const bool entryCreated =
      !bgpcli::prefixListEntryExists(list, args.basePrefix());
  auto& entry = bgpcli::findOrCreatePrefixListEntry(list, args.basePrefix());

  Result result = args.attr().empty()
      ? ok(entryCreated
               ? fmt::format(
                     "Successfully created BGP prefix-list {} prefix {}",
                     listArgs.listName(),
                     args.basePrefix())
               : fmt::format(
                     "BGP prefix-list {} prefix {} already exists",
                     listArgs.listName(),
                     args.basePrefix()))
      // The attribute is guaranteed valid: BgpPrefixListEntryConfig's
      // constructor rejects an unknown attribute before we get here.
      : entryAttrHandlers().find(args.attr())->second(entry, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message += fmt::format(
          " for prefix-list {} prefix {}",
          listArgs.listName(),
          args.basePrefix());
    }
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  } else {
    // A rejected value must not leave a phantom entry (or a phantom list
    // implicitly created for it) visible to later lookups in this process.
    if (entryCreated) {
      list.prefixes()->pop_back();
    }
    if (listCreated) {
      cfg.policies().ensure().prefix_lists()->pop_back();
    }
  }
  return result.message;
}

void CmdConfigProtocolBgpPolicyPrefixListEntry::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyPrefixListEntry,
    CmdConfigProtocolBgpPolicyPrefixListEntryTraits>::run();

} // namespace facebook::fboss
