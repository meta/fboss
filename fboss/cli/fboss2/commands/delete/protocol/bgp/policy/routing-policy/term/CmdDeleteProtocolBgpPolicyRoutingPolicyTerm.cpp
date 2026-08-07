/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/term/CmdDeleteProtocolBgpPolicyRoutingPolicyTerm.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <algorithm>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/cli/fboss2/commands/config/protocol/bgp/BgpCliValueParsers.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/BgpRoutingPolicyCliUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

namespace facebook::fboss {

// Parse + validate at construction so queryClient stays a thin dispatch.
BgpRoutingPolicyTermRef::BgpRoutingPolicyTermRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: delete protocol bgp policy routing-policy <name> term "
        "requires <seq-num>");
  }
  auto seq = bgpcli::parseNonNegInt64(v[0]);
  if (!seq) {
    throw std::invalid_argument(
        fmt::format(
            "Error: invalid term seq-num '{}'; expected a non-negative "
            "integer",
            v[0]));
  }
  // Nothing may follow the seq-num: there are no attributes to delete
  // through this command.
  if (v.size() > 1) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unexpected token '{}'. Usage: delete protocol bgp "
            "policy routing-policy <name> term <seq-num>",
            v[1]));
  }
  seqNum_ = *seq;
}

CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits::RetType
CmdDeleteProtocolBgpPolicyRoutingPolicyTerm::queryClient(
    const HostInfo& /* hostInfo */,
    const BgpRoutingPolicyRef& policyArgs,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Nothing is persisted for an unknown policy/term, so a typo'd delete
  // can't stage an unrelated session change.
  auto* policy = bgpcli::findRoutingPolicy(cfg, policyArgs.policyName());
  if (policy == nullptr) {
    return fmt::format(
        "Error: BGP routing-policy {} not found", policyArgs.policyName());
  }
  auto& terms = *policy->policy_entries();
  auto it = std::find_if(terms.begin(), terms.end(), [&](const auto& term) {
    return term.sequence_number().has_value() &&
        *term.sequence_number() == args.seqNum();
  });
  if (it == terms.end()) {
    return fmt::format(
        "Error: BGP routing-policy {} term {} not found",
        policyArgs.policyName(),
        args.seqNum());
  }
  terms.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP routing-policy {} term {}\n"
      "Config saved to: {}",
      policyArgs.policyName(),
      args.seqNum(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPolicyRoutingPolicyTerm::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPolicyRoutingPolicyTerm,
    CmdDeleteProtocolBgpPolicyRoutingPolicyTermTraits>::run();

} // namespace facebook::fboss
