/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/CmdDeleteProtocolBgpPolicyRoutingPolicy.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/core.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/CmdUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fmt/format.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

namespace facebook::fboss {

namespace {
constexpr std::string_view kObjectName = "routing-policy";

// Everything that names a routing-policy and that bgpd resolves at config
// load: a neighbor's or peer-group's ingress/egress policy and a network6
// prefix's policy. Labelled the way the CLI addresses each referrer.
std::vector<std::string> findReferencesToRoutingPolicy(
    const bgp::thrift::BgpConfig& cfg,
    const std::string& policyName) {
  std::vector<std::string> referrers;
  auto matches = [&](const auto& field) {
    return field.has_value() && *field == policyName;
  };
  // `peers` (field 5) and `networks6` (field 4) are required in BgpConfig.
  // `peer_groups` (field 16) is optional, hence the presence check below.
  for (const auto& peer : *cfg.peers()) {
    if (matches(peer.ingress_policy_name())) {
      referrers.push_back(
          fmt::format("neighbor {} ingress", *peer.peer_addr()));
    }
    if (matches(peer.egress_policy_name())) {
      referrers.push_back(fmt::format("neighbor {} egress", *peer.peer_addr()));
    }
  }
  if (cfg.peer_groups().has_value()) {
    for (const auto& group : *cfg.peer_groups()) {
      if (matches(group.ingress_policy_name())) {
        referrers.push_back(
            fmt::format("peer-group {} ingress", *group.name()));
      }
      if (matches(group.egress_policy_name())) {
        referrers.push_back(fmt::format("peer-group {} egress", *group.name()));
      }
    }
  }
  for (const auto& network : *cfg.networks6()) {
    if (matches(network.policy_name())) {
      referrers.push_back(fmt::format("network6 {}", *network.prefix()));
    }
  }
  return referrers;
}
} // namespace

// Parse + validate at construction so queryClient stays a thin dispatch.
BgpRoutingPolicyRef::BgpRoutingPolicyRef(std::vector<std::string> v)
    : utils::BaseObjectArgType<std::string>(v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "Error: delete protocol bgp policy routing-policy requires <name>");
  }
  if (v[0].empty()) {
    throw std::invalid_argument(
        fmt::format("Error: {} name must not be empty", kObjectName));
  }
  // Unlike the config grammar, nothing may follow the name: there are no
  // attributes to delete through this command.
  if (v.size() > 1) {
    throw std::invalid_argument(
        fmt::format(
            "Error: unexpected token '{}'. Usage: delete protocol bgp "
            "policy routing-policy <name>",
            v[1]));
  }
  policyName_ = v[0];
}

CmdDeleteProtocolBgpPolicyRoutingPolicyTraits::RetType
CmdDeleteProtocolBgpPolicyRoutingPolicy::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& args) {
  auto& session = ConfigSession::getInstance();
  auto& cfg = session.getBgpConfig();
  // Delete mirrors add: an absent target is already the requested end state,
  // so this is a success with a warning. Nothing is saved, so a typo'd delete
  // can't stage an unrelated session change.
  auto absent = fmt::format(
      "Warning: BGP routing-policy {} does not exist; nothing to delete",
      args.policyName());
  if (!cfg.policies().has_value()) {
    return absent;
  }
  auto& policies = *cfg.policies()->bgp_policy_statements();
  auto it =
      std::find_if(policies.begin(), policies.end(), [&](const auto& policy) {
        return *policy.name() == args.policyName();
      });
  if (it == policies.end()) {
    return absent;
  }
  // ingress/egress_policy_name and network6 policy_name resolve against this
  // policy by name at daemon load; erasing it while something still names it
  // would commit a dangling reference and fail the load. Refuse and name the
  // referrers instead.
  auto referrers = findReferencesToRoutingPolicy(cfg, args.policyName());
  if (!referrers.empty()) {
    return fmt::format(
        "Error: BGP routing-policy {} is still referenced by {}; re-point or "
        "remove those references first",
        args.policyName(),
        folly::join(", ", referrers));
  }
  policies.erase(it);
  session.saveBgpConfig();
  return fmt::format(
      "Successfully deleted BGP routing-policy {}\nConfig saved to: {}",
      args.policyName(),
      session.getBgpSessionConfigPath());
}

void CmdDeleteProtocolBgpPolicyRoutingPolicy::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdDeleteProtocolBgpPolicyRoutingPolicy,
    CmdDeleteProtocolBgpPolicyRoutingPolicyTraits>::run();

} // namespace facebook::fboss
