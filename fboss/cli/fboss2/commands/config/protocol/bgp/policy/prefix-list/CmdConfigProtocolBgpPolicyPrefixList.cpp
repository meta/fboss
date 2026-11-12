/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/CmdConfigProtocolBgpPolicyPrefixList.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
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
// valid-attribute set and the handler table stay in sync. The list's prefixes
// are their own subcommand, not attributes.
constexpr std::string_view kBooleanOperator = "boolean-operator";
constexpr std::string_view kDescription = "description";
constexpr std::string_view kIpVersion = "ip-version";

// boolean-operator values (routing_policy.BooleanOperator names). Only OR is
// offered: bgpd's PrefixTreeMatch rejects any other value ("PrefixList
// BooleanOperator can only be OR") once a policy references the list, and
// it rejects a list-level compare_operator outright, so that attribute is
// not offered at all.
constexpr std::string_view kBooleanOperatorOr = "OR";

// ip-version values, stored as the numeric IP version in PrefixList.version
// (the documented thrift field for this command).
constexpr std::string_view kIpVersionV4 = "v4";
constexpr std::string_view kIpVersionV6 = "v6";
constexpr int32_t kIpVersionV4Value = 4;
constexpr int32_t kIpVersionV6Value = 6;

using BooleanOperator = bgp::routing_policy::BooleanOperator;
using PrefixList = bgp::routing_policy::PrefixList;
using bgpcli::AttrHandler;
using bgpcli::enumAttr;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;

// ---- value lookups ----------------------------------------------------------

std::optional<BooleanOperator> lookupBooleanOperator(const std::string& s) {
  if (s == kBooleanOperatorOr) {
    return BooleanOperator::OR;
  }
  return std::nullopt;
}

std::optional<int32_t> lookupIpVersion(const std::string& s) {
  if (s == kIpVersionV4) {
    return kIpVersionV4Value;
  }
  if (s == kIpVersionV6) {
    return kIpVersionV6Value;
  }
  return std::nullopt;
}

// ---- list-level setters -----------------------------------------------------
// Each writes one already-parsed, already-validated value. Parsing and message
// text belong to the shared factories in BgpCliAttrHandlers.h.

void setListBooleanOperator(PrefixList& list, BooleanOperator op) {
  list.boolean_operator() = op;
}

void setListDescription(PrefixList& list, const std::string& description) {
  list.description() = description;
}

// ip-version is stored as the numeric PrefixList.version, the field the
// command is documented against.
void setListIpVersion(PrefixList& list, int32_t version) {
  list.version() = version;
}

// ---- list-level attribute registry ------------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<PrefixList>, std::less<>>&
listAttrHandlers() {
  static const std::string kBooleanOperatorValues =
      std::string(kBooleanOperatorOr);
  static const std::string kIpVersionValues =
      fmt::format("{}|{}", kIpVersionV4, kIpVersionV6);
  static const std::map<std::string, AttrHandler<PrefixList>, std::less<>>
      kHandlers = {
          {std::string(kBooleanOperator),
           enumAttr<PrefixList, BooleanOperator>(
               kBooleanOperator,
               kBooleanOperatorValues,
               lookupBooleanOperator,
               setListBooleanOperator)},
          {std::string(kDescription),
           joinedStringAttr<PrefixList>(kDescription, setListDescription)},
          {std::string(kIpVersion),
           enumAttr<PrefixList, int32_t>(
               kIpVersion,
               kIpVersionValues,
               lookupIpVersion,
               setListIpVersion)},
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
BgpPrefixListConfig::BgpPrefixListConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: prefix-list <name> is required, optionally followed by an "
        "<attribute> <value>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument("Error: prefix-list name must not be empty");
  }
  listName_ = v[0];

  if (v.size() == 1) {
    return; // bare `prefix-list <name>`: create the list
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (listAttrHandlers().find(attr_) == listAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown prefix-list attribute '{}'. Valid attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyPrefixListTraits::RetType
CmdConfigProtocolBgpPolicyPrefixList::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool listCreated = !bgpcli::prefixListExists(cfg, args.listName());
  auto& list = bgpcli::findOrCreatePrefixList(cfg, args.listName());
  auto& lists = *cfg.policies().ensure().prefix_lists();

  Result result = args.attr().empty()
      ? ok(listCreated
               ? fmt::format(
                     "Successfully created BGP prefix-list {}", args.listName())
               : fmt::format(
                     "BGP prefix-list {} already exists", args.listName()))
      // The attribute is guaranteed valid: BgpPrefixListConfig's constructor
      // rejects an unknown attribute before we get here.
      : listAttrHandlers().at(args.attr())(list, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message += fmt::format(" for prefix-list {}", args.listName());
    }
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  } else if (listCreated) {
    // Drop the phantom list so a rejected value is not visible to later
    // lookups in the same process.
    const auto created =
        std::find_if(lists.begin(), lists.end(), [&](const auto& candidate) {
          return *candidate.name() == args.listName();
        });
    if (created != lists.end()) {
      lists.erase(created);
    }
  }
  return result.message;
}

void CmdConfigProtocolBgpPolicyPrefixList::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyPrefixList,
    CmdConfigProtocolBgpPolicyPrefixListTraits>::run();

} // namespace facebook::fboss
