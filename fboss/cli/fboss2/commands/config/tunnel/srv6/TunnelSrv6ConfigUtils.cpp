/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 */

#include "fboss/cli/fboss2/commands/config/tunnel/srv6/TunnelSrv6ConfigUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss::srv6_tunnel_utils {

namespace {

const cfg::Srv6Tunnel* FOLLY_NULLABLE
findTunnel(const cfg::SwitchConfig& config, const std::string& tunnelId) {
  if (!config.srv6Tunnels().has_value()) {
    return nullptr;
  }
  auto it = std::find_if(
      config.srv6Tunnels()->begin(),
      config.srv6Tunnels()->end(),
      [&](const auto& tunnel) { return *tunnel.srv6TunnelId() == tunnelId; });
  return it == config.srv6Tunnels()->end() ? nullptr : &*it;
}

bool interfaceExists(const cfg::SwitchConfig& config, int32_t interfaceId) {
  return std::any_of(
      config.interfaces()->begin(),
      config.interfaces()->end(),
      [&](const auto& intf) { return *intf.intfID() == interfaceId; });
}

} // namespace

std::string toLower(const std::string& value) {
  std::string result = value;
  std::transform(
      result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
  return result;
}

std::string directionName(TunnelType type) {
  switch (type) {
    case TunnelType::SRV6_ENCAP:
      return "encap";
    case TunnelType::SRV6_DECAP:
      return "decap";
    case TunnelType::IP_IN_IP_ENCAP:
    case TunnelType::IP_IN_IP_DECAP:
      throw std::invalid_argument("Expected an SRv6 tunnel type");
  }
  throw std::invalid_argument("Unknown tunnel type");
}

cfg::TunnelMode parseTunnelMode(const std::string& value) {
  const auto lower = toLower(value);
  if (lower == "uniform") {
    return cfg::TunnelMode::UNIFORM;
  }
  if (lower == "pipe") {
    return cfg::TunnelMode::PIPE;
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid tunnel mode '{}'. Valid values: uniform, pipe", value));
}

cfg::TunnelTerminationType parseTerminationType(const std::string& value) {
  const auto lower = toLower(value);
  if (lower == "p2p") {
    return cfg::TunnelTerminationType::P2P;
  }
  if (lower == "p2mp") {
    return cfg::TunnelTerminationType::P2MP;
  }
  if (lower == "mp2p") {
    return cfg::TunnelTerminationType::MP2P;
  }
  if (lower == "mp2mp") {
    return cfg::TunnelTerminationType::MP2MP;
  }
  throw std::invalid_argument(
      fmt::format(
          "Invalid termination type '{}'. Valid values: p2p, p2mp, mp2p, mp2mp",
          value));
}

void parseTunnelConfigArgs(
    const std::vector<std::string>& values,
    const std::unordered_set<std::string>& allowedAttrs,
    std::string& tunnelId,
    std::map<std::string, std::string>& attrs) {
  if (values.empty() || values[0].empty()) {
    throw std::invalid_argument("SRv6 tunnel ID is required");
  }

  const auto first = toLower(values[0]);
  if (allowedAttrs.count(first)) {
    throw std::invalid_argument(
        fmt::format("Expected SRv6 tunnel ID, got attribute '{}'", values[0]));
  }
  tunnelId = values[0];

  for (size_t index = 1; index < values.size();) {
    const auto attr = toLower(values[index++]);
    if (!allowedAttrs.count(attr)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown or unsupported SRv6 tunnel attribute '{}'", attr));
    }
    if (attrs.count(attr)) {
      throw std::invalid_argument(
          fmt::format("Duplicate SRv6 tunnel attribute '{}'", attr));
    }
    if (index >= values.size() || allowedAttrs.count(toLower(values[index]))) {
      throw std::invalid_argument(
          fmt::format("Missing value for attribute '{}'", attr));
    }

    const auto& value = values[index++];
    if (attr == kAttrSource) {
      try {
        auto address = folly::IPAddress(value);
        if (!address.isV6()) {
          throw std::invalid_argument("not IPv6");
        }
        attrs[attr] = address.str();
      } catch (const std::exception&) {
        throw std::invalid_argument(
            fmt::format("Invalid IPv6 source address '{}'", value));
      }
    } else if (
        attr == kAttrTtlMode || attr == kAttrDscpMode || attr == kAttrEcnMode) {
      parseTunnelMode(value);
      attrs[attr] = toLower(value);
    } else if (attr == kAttrTerminationType) {
      parseTerminationType(value);
      attrs[attr] = toLower(value);
    } else if (attr == kAttrUnderlayIntfId) {
      int32_t interfaceId;
      try {
        interfaceId = folly::to<int32_t>(value);
      } catch (const folly::ConversionError&) {
        throw std::invalid_argument(
            fmt::format(
                "underlay-intf-id must be an integer, got '{}'", value));
      }
      if (interfaceId <= 0) {
        throw std::invalid_argument("underlay-intf-id must be positive");
      }
      attrs[attr] = folly::to<std::string>(interfaceId);
    }
  }
}

std::vector<std::string> applyTunnelConfig(
    cfg::SwitchConfig& swConfig,
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs) {
  if (tunnelType != TunnelType::SRV6_ENCAP &&
      tunnelType != TunnelType::SRV6_DECAP) {
    throw std::invalid_argument(
        "Only SRv6 encap and decap types are supported");
  }

  const auto* existing = findTunnel(swConfig, tunnelId);
  if (existing && *existing->tunnelType() != tunnelType) {
    throw std::invalid_argument(
        fmt::format(
            "SRv6 tunnel '{}' already exists as {}; delete it before configuring it as {}",
            tunnelId,
            directionName(*existing->tunnelType()),
            directionName(tunnelType)));
  }
  if (tunnelType == TunnelType::SRV6_DECAP &&
      swConfig.srv6Tunnels().has_value()) {
    const auto otherDecap = std::find_if(
        swConfig.srv6Tunnels()->begin(),
        swConfig.srv6Tunnels()->end(),
        [&](const auto& tunnel) {
          return *tunnel.srv6TunnelId() != tunnelId &&
              *tunnel.tunnelType() == TunnelType::SRV6_DECAP;
        });
    if (otherDecap != swConfig.srv6Tunnels()->end()) {
      throw std::invalid_argument(
          fmt::format(
              "Only one SRv6 decap tunnel is supported; '{}' already exists",
              *otherDecap->srv6TunnelId()));
    }
  }

  cfg::Srv6Tunnel candidate;
  if (existing) {
    candidate = *existing;
  } else {
    candidate.srv6TunnelId() = tunnelId;
    candidate.tunnelType() = tunnelType;
  }

  std::vector<std::string> results;
  for (const auto& [attr, value] : attrs) {
    if (attr == kAttrSource) {
      candidate.srcIp() = value;
    } else if (attr == kAttrUnderlayIntfId) {
      candidate.underlayIntfID() = folly::to<int32_t>(value);
    } else if (attr == kAttrTtlMode) {
      candidate.ttlMode() = parseTunnelMode(value);
    } else if (attr == kAttrDscpMode) {
      candidate.dscpMode() = parseTunnelMode(value);
    } else if (attr == kAttrEcnMode) {
      candidate.ecnMode() = parseTunnelMode(value);
    } else if (attr == kAttrTerminationType) {
      candidate.tunnelTermType() = parseTerminationType(value);
    }
    results.push_back(fmt::format("{}={}", attr, value));
  }

  if (tunnelType == TunnelType::SRV6_ENCAP) {
    if (*candidate.underlayIntfID() <= 0) {
      throw std::invalid_argument(
          fmt::format(
              "SRv6 encap tunnel '{}' requires underlay-intf-id", tunnelId));
    }
    if (!interfaceExists(swConfig, *candidate.underlayIntfID())) {
      throw std::invalid_argument(
          fmt::format(
              "Interface ID {} not found in staged config",
              *candidate.underlayIntfID()));
    }
    if (!candidate.srcIp().has_value()) {
      throw std::invalid_argument(
          fmt::format("SRv6 encap tunnel '{}' requires source", tunnelId));
    }
  }
  if (tunnelType == TunnelType::SRV6_DECAP &&
      (candidate.srcIp().has_value() || candidate.dstIp().has_value())) {
    throw std::invalid_argument(
        "SRv6 decap tunnels cannot have source or destination addresses");
  }

  if (!swConfig.srv6Tunnels().has_value()) {
    swConfig.srv6Tunnels() = std::vector<cfg::Srv6Tunnel>{};
  }
  auto& tunnels = *swConfig.srv6Tunnels();
  auto it =
      std::find_if(tunnels.begin(), tunnels.end(), [&](const auto& tunnel) {
        return *tunnel.srv6TunnelId() == tunnelId;
      });
  if (it == tunnels.end()) {
    tunnels.push_back(std::move(candidate));
  } else {
    *it = std::move(candidate);
  }
  return results;
}

std::string configureTunnel(
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto results = applyTunnelConfig(swConfig, tunnelType, tunnelId, attrs);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully configured SRv6 {} tunnel '{}': {}",
      directionName(tunnelType),
      tunnelId,
      results.empty() ? "no attribute changes" : folly::join(", ", results));
}

} // namespace facebook::fboss::srv6_tunnel_utils
