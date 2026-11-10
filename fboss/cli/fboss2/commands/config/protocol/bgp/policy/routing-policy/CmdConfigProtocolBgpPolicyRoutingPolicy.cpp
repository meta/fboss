/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/BgpRoutingPolicyCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {

// The attribute names, exactly as documented. Kept here so the valid-attribute
// set and the handler table stay in sync.
constexpr std::string_view kObjectName = "routing-policy";
constexpr std::string_view kDescription = "description";

using BgpPolicyStatement = bgp::bgp_policy::BgpPolicyStatement;
using bgpcli::AttrHandler;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;

// ---- policy-level setters ---------------------------------------------------
// Each writes one already-parsed, already-validated value. Parsing and message
// text belong to the shared factories in BgpCliAttrHandlers.h.

void setDescription(
    BgpPolicyStatement& policy,
    const std::string& description) {
  policy.description() = description;
}

// ---- policy-level attribute registry ---------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<BgpPolicyStatement>, std::less<>>&
policyAttrHandlers() {
  static const std::
      map<std::string, AttrHandler<BgpPolicyStatement>, std::less<>>
          kHandlers = {
              {std::string(kDescription),
               joinedStringAttr<BgpPolicyStatement>(
                   kDescription, setDescription)},
          };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : policyAttrHandlers()) {
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
// errors (same mechanism as BgpCommunityListConfig).
BgpRoutingPolicyConfig::BgpRoutingPolicyConfig(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: routing-policy <name> is required, optionally followed by "
        "an <attribute> <value>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument(
        fmt::format("Error: {} name must not be empty", kObjectName));
  }
  policyName_ = v[0];
  if (v.size() == 1) {
    return; // bare `routing-policy <name>`: create it
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (policyAttrHandlers().find(attr_) == policyAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown routing-policy attribute '{}'. Valid "
            "attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyRoutingPolicyTraits::RetType
CmdConfigProtocolBgpPolicyRoutingPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool policyCreated =
      !bgpcli::routingPolicyExists(cfg, args.policyName());
  auto& policy = bgpcli::findOrCreateRoutingPolicy(cfg, args.policyName());

  Result result = args.attr().empty()
      ? ok(policyCreated
               ? fmt::format(
                     "Successfully created BGP routing-policy {}",
                     args.policyName())
               : fmt::format(
                     "BGP routing-policy {} already exists", args.policyName()))
      // The attribute is guaranteed valid: BgpRoutingPolicyConfig's
      // constructor rejects an unknown attribute before we get here.
      : policyAttrHandlers().find(args.attr())->second(policy, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message +=
          fmt::format(" for routing-policy {}", args.policyName());
    }
    session.saveBgpConfig();
    result.message +=
        fmt::format("\nConfig saved to: {}", session.getBgpSessionConfigPath());
  } else if (policyCreated) {
    // Drop the phantom policy so a rejected value is not visible to later
    // lookups in the same process.
    cfg.policies().ensure().bgp_policy_statements()->pop_back();
  }
  return result.message;
}

void CmdConfigProtocolBgpPolicyRoutingPolicy::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyRoutingPolicy,
    CmdConfigProtocolBgpPolicyRoutingPolicyTraits>::run();

} // namespace facebook::fboss
