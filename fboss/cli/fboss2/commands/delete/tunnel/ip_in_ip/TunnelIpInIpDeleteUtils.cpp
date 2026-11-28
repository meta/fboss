/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/tunnel/ip_in_ip/TunnelIpInIpDeleteUtils.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/TunnelIpInIpConfigUtils.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentConfigDomain.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss::tunnel_delete_utils {

using tunnel_utils::directionName;
using tunnel_utils::kAttrDscpMode;
using tunnel_utils::kAttrEcnMode;
using tunnel_utils::kAttrSource;
using tunnel_utils::kAttrTerminationType;
using tunnel_utils::kAttrTtlMode;
using tunnel_utils::toLower;

namespace {

bool contains(const std::vector<std::string>& v, std::string_view s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

void parseTunnelDeleteArgs(
    const std::vector<std::string>& v,
    const std::unordered_set<std::string>& resettableAttrs,
    const std::unordered_set<std::string>& requiredAttrs,
    const std::vector<std::string>& resettableDisplay,
    std::string& tunnelId,
    std::vector<std::string>& attributes) {
  if (v.empty()) {
    throw std::invalid_argument("Tunnel ID is required");
  }

  // First token is the tunnel ID; reject if it looks like an attribute name to
  // catch the common mistake of forgetting the ID.
  const std::string firstLower = toLower(v[0]);
  if (resettableAttrs.count(firstLower) > 0 ||
      requiredAttrs.count(firstLower) > 0) {
    throw std::invalid_argument(
        fmt::format("Expected tunnel ID, got attribute name '{}'", v[0]));
  }
  tunnelId = v[0];

  for (size_t i = 1; i < v.size(); ++i) {
    const std::string attrLower = toLower(v[i]);

    if (requiredAttrs.count(attrLower) > 0) {
      throw std::invalid_argument(
          fmt::format(
              "Cannot reset required field '{}'. To remove the tunnel, run "
              "'delete tunnel ip-in-ip <encap|decap> <tunnel-id>' with no "
              "attributes.",
              v[i]));
    }

    if (resettableAttrs.count(attrLower) == 0) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown or unsupported tunnel attribute '{}'. Valid "
              "attributes: {}",
              v[i],
              folly::join(", ", resettableDisplay)));
    }

    attributes.push_back(attrLower);
  }
}

std::string applyTunnelDelete(
    cfg::SwitchConfig& swConfig,
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::vector<std::string>& attributes,
    bool& changed) {
  changed = false;
  if (!swConfig.ipInIpTunnels().has_value() ||
      swConfig.ipInIpTunnels()->empty()) {
    // Idempotent: nothing to delete.
    return "No IP-in-IP tunnels configured; nothing to delete";
  }

  auto& tunnels = *swConfig.ipInIpTunnels();
  auto it = std::find_if(tunnels.begin(), tunnels.end(), [&](const auto& t) {
    return *t.ipInIpTunnelId() == tunnelId;
  });
  if (it == tunnels.end()) {
    // Idempotent: tunnel doesn't exist.
    return fmt::format("Tunnel '{}' not found; nothing to delete", tunnelId);
  }

  // An unset tunnelType is treated as decap by the agent.
  const TunnelType existingType =
      it->tunnelType().value_or(TunnelType::IP_IN_IP_DECAP);
  if (existingType != tunnelType) {
    throw std::invalid_argument(
        fmt::format(
            "Tunnel '{}' is an {} tunnel; use "
            "'delete tunnel ip-in-ip {} {}' instead.",
            tunnelId,
            directionName(existingType),
            directionName(existingType),
            tunnelId));
  }

  if (attributes.empty()) {
    tunnels.erase(it);
    changed = true;
    return fmt::format("Successfully deleted tunnel '{}'", tunnelId);
  }

  const bool resetsSource = contains(attributes, kAttrSource);

  // 'source' is a hard requirement for encap tunnels (the agent reads it
  // unconditionally when programming the encap). The parse layer already
  // rejects it via the encap required set; this guards any other caller.
  if (tunnelType == TunnelType::IP_IN_IP_ENCAP && resetsSource) {
    throw std::invalid_argument(
        fmt::format(
            "Cannot reset 'source' on encap tunnel '{}': encap requires an "
            "outer source. Configure a new source instead.",
            tunnelId));
  }

  // A P2P decap termination matches on the outer source: the agent reads it
  // from 'source' and aborts at commit when it is unset. Resetting source is
  // therefore only allowed if termination-type is reset in the same command
  // (the agent then falls back to its P2MP default).
  if (tunnelType == TunnelType::IP_IN_IP_DECAP && resetsSource &&
      !contains(attributes, kAttrTerminationType) &&
      it->tunnelTermType().has_value() &&
      *it->tunnelTermType() == cfg::TunnelTerminationType::P2P) {
    throw std::invalid_argument(
        fmt::format(
            "Cannot reset 'source' on tunnel '{}': its p2p termination "
            "matches on the outer source. Reset 'termination-type' in the "
            "same command or configure a new source instead.",
            tunnelId));
  }

  changed = true;
  for (const auto& attr : attributes) {
    if (attr == kAttrSource) {
      it->srcIp().reset();
      it->srcIpMask().reset();
    } else if (attr == kAttrTtlMode) {
      it->ttlMode().reset();
    } else if (attr == kAttrDscpMode) {
      it->dscpMode().reset();
    } else if (attr == kAttrEcnMode) {
      it->ecnMode().reset();
    } else if (attr == kAttrTerminationType) {
      it->tunnelTermType().reset();
    }
  }

  return fmt::format(
      "Successfully reset tunnel '{}': {}",
      tunnelId,
      folly::join(", ", attributes));
}

std::string deleteTunnel(
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::vector<std::string>& attributes) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.config<AgentConfigDomain>().sw();

  bool changed = false;
  auto result =
      applyTunnelDelete(swConfig, tunnelType, tunnelId, attributes, changed);

  if (changed) {
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  }
  return result;
}

} // namespace facebook::fboss::tunnel_delete_utils
