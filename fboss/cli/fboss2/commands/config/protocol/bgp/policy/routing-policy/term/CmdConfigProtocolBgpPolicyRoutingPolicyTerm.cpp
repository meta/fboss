/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"

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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliAttrHandlers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/BgpRoutingPolicyCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

namespace {

// The attribute names, exactly as documented. Kept here so the
// valid-attribute set and the handler table stay in sync. The term's action
// and match levels are their own subcommands (follow-ups), not attributes.
constexpr std::string_view kDescription = "description";

using BgpPolicyTerm = bgp::bgp_policy::BgpPolicyTerm;
using bgpcli::AttrHandler;
using bgpcli::joinedStringAttr;
using bgpcli::ok;
using bgpcli::Result;

// ---- term-level setters -----------------------------------------------------
// Each writes one already-parsed, already-validated value. Parsing and message
// text belong to the shared factories in BgpCliAttrHandlers.h.

void setDescription(BgpPolicyTerm& term, const std::string& description) {
  term.description() = description;
}

// ---- term-level attribute registry ------------------------------------------
// One line per documented attribute: its dispatch key, its value shape, and
// the setter that stores it.
const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>&
termAttrHandlers() {
  static const std::map<std::string, AttrHandler<BgpPolicyTerm>, std::less<>>
      kHandlers = {
          {std::string(kDescription),
           joinedStringAttr<BgpPolicyTerm>(kDescription, setDescription)},
      };
  return kHandlers;
}

std::string validAttrList() {
  std::string out;
  for (const auto& [name, _] : termAttrHandlers()) {
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
// errors (same mechanism as BgpRoutingPolicyConfig).
BgpRoutingPolicyTermConfig::BgpRoutingPolicyTermConfig(
    std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: term <seq-num> is required, optionally followed by an "
        "<attribute> <value>");
  }
  auto seq = bgpcli::parseNonNegInt64(v[0]);
  if (!seq) {
    throw std::invalid_argument(
        fmt::format(
            "Error: invalid term seq-num '{}'; expected a non-negative "
            "integer",
            v[0]));
  }
  seqNum_ = *seq;
  if (v.size() == 1) {
    return; // bare `term <seq-num>`: create it
  }

  attr_ = v[1];
  values_.assign(v.begin() + 2, v.end());

  if (termAttrHandlers().find(attr_) == termAttrHandlers().end()) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unknown routing-policy term attribute '{}'. Valid "
            "attributes: {}",
            attr_,
            validAttrList()));
  }
}

CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits::RetType
CmdConfigProtocolBgpPolicyRoutingPolicyTerm::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpRoutingPolicyConfig& policyArgs,
    const ObjectArgType& args) {
  // The parent parse accepts `routing-policy <name> <attr> <value> ... term
  // ...`, but only the leaf (this command) runs — silently dropping the
  // policy-level attribute would look like it was staged. Reject the mix.
  if (!policyArgs.attr().empty()) {
    return fmt::format(
        "Error: configure routing-policy attributes and term in separate "
        "commands (got routing-policy attribute '{}' alongside term {})",
        policyArgs.attr(),
        args.seqNum());
  }

  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  const bool policyCreated =
      !bgpcli::routingPolicyExists(cfg, policyArgs.policyName());
  auto& policy =
      bgpcli::findOrCreateRoutingPolicy(cfg, policyArgs.policyName());
  const bool termCreated =
      !bgpcli::routingPolicyTermExists(policy, args.seqNum());
  auto& term = bgpcli::findOrCreateRoutingPolicyTerm(policy, args.seqNum());

  Result result = args.attr().empty()
      ? ok(termCreated
               ? fmt::format(
                     "Successfully created BGP routing-policy {} term {}",
                     policyArgs.policyName(),
                     args.seqNum())
               : fmt::format(
                     "BGP routing-policy {} term {} already exists",
                     policyArgs.policyName(),
                     args.seqNum()))
      // The attribute is guaranteed valid: BgpRoutingPolicyTermConfig's
      // constructor rejects an unknown attribute before we get here.
      : termAttrHandlers().find(args.attr())->second(term, args.values());

  if (result.ok) {
    if (!args.attr().empty()) {
      result.message += fmt::format(
          " for routing-policy {} term {}",
          policyArgs.policyName(),
          args.seqNum());
    }
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

void CmdConfigProtocolBgpPolicyRoutingPolicyTerm::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigProtocolBgpPolicyRoutingPolicyTerm,
    CmdConfigProtocolBgpPolicyRoutingPolicyTermTraits>::run();

} // namespace facebook::fboss
