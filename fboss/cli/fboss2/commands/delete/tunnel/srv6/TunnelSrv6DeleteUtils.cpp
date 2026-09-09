/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/cli/fboss2/commands/delete/tunnel/srv6/TunnelSrv6DeleteUtils.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <unordered_set>

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/TunnelSrv6ConfigUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss::srv6_tunnel_delete_utils {

namespace {
const std::unordered_set<std::string> kResettableAttrs = {
    std::string(srv6_tunnel_utils::kAttrTtlMode),
    std::string(srv6_tunnel_utils::kAttrDscpMode),
    std::string(srv6_tunnel_utils::kAttrEcnMode),
    std::string(srv6_tunnel_utils::kAttrTerminationType),
};

const std::unordered_set<std::string> kRequiredAttrs = {
    "source",
    "underlay-intf-id",
    "tunnel-type",
};
} // namespace

void parseTunnelDeleteArgs(
    const std::vector<std::string>& values,
    std::string& tunnelId,
    std::vector<std::string>& attrs) {
  if (values.empty() || values[0].empty()) {
    throw std::invalid_argument("SRv6 tunnel ID is required");
  }
  const auto first = srv6_tunnel_utils::toLower(values[0]);
  if (kResettableAttrs.count(first) || kRequiredAttrs.count(first)) {
    throw std::invalid_argument(
        fmt::format("Expected SRv6 tunnel ID, got attribute '{}'", values[0]));
  }
  tunnelId = values[0];
  std::unordered_set<std::string> seen;
  for (size_t index = 1; index < values.size(); ++index) {
    auto attr = srv6_tunnel_utils::toLower(values[index]);
    if (kRequiredAttrs.count(attr)) {
      throw std::invalid_argument(
          fmt::format(
              "Cannot reset required SRv6 tunnel attribute '{}'; reconfigure or delete the entire tunnel",
              attr));
    }
    if (!kResettableAttrs.count(attr)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown or non-resettable SRv6 tunnel attribute '{}'", attr));
    }
    if (!seen.insert(attr).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate SRv6 tunnel attribute '{}'", attr));
    }
    attrs.push_back(std::move(attr));
  }
}

std::string applyTunnelDelete(
    cfg::SwitchConfig& swConfig,
    TunnelType expectedType,
    const std::string& tunnelId,
    const std::vector<std::string>& attrs,
    bool& changed) {
  changed = false;
  if (!swConfig.srv6Tunnels().has_value()) {
    return fmt::format("SRv6 tunnel '{}' does not exist", tunnelId);
  }

  auto& tunnels = *swConfig.srv6Tunnels();
  auto it =
      std::find_if(tunnels.begin(), tunnels.end(), [&](const auto& tunnel) {
        return *tunnel.srv6TunnelId() == tunnelId;
      });
  if (it == tunnels.end()) {
    return fmt::format("SRv6 tunnel '{}' does not exist", tunnelId);
  }
  if (*it->tunnelType() != expectedType) {
    throw std::invalid_argument(
        fmt::format(
            "SRv6 tunnel '{}' is {}, not {}",
            tunnelId,
            srv6_tunnel_utils::directionName(*it->tunnelType()),
            srv6_tunnel_utils::directionName(expectedType)));
  }

  if (attrs.empty()) {
    tunnels.erase(it);
    changed = true;
    return fmt::format("Successfully deleted SRv6 tunnel '{}'", tunnelId);
  }

  std::vector<std::string> reset;
  for (const auto& attr : attrs) {
    bool hadValue = false;
    if (attr == srv6_tunnel_utils::kAttrTtlMode) {
      hadValue = it->ttlMode().has_value();
      it->ttlMode().reset();
    } else if (attr == srv6_tunnel_utils::kAttrDscpMode) {
      hadValue = it->dscpMode().has_value();
      it->dscpMode().reset();
    } else if (attr == srv6_tunnel_utils::kAttrEcnMode) {
      hadValue = it->ecnMode().has_value();
      it->ecnMode().reset();
    } else if (attr == srv6_tunnel_utils::kAttrTerminationType) {
      hadValue = it->tunnelTermType().has_value();
      it->tunnelTermType().reset();
    }
    if (hadValue) {
      changed = true;
      reset.push_back(attr);
    }
  }

  if (reset.empty()) {
    return fmt::format("No SRv6 tunnel attributes changed for '{}'", tunnelId);
  }
  return fmt::format(
      "Successfully reset SRv6 tunnel '{}': {}",
      tunnelId,
      folly::join(", ", reset));
}

std::string deleteTunnel(
    TunnelType expectedType,
    const std::string& tunnelId,
    const std::vector<std::string>& attrs) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  bool changed = false;
  auto result =
      applyTunnelDelete(swConfig, expectedType, tunnelId, attrs, changed);
  if (changed) {
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  }
  return result;
}

} // namespace facebook::fboss::srv6_tunnel_delete_utils
