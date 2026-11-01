/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/tunnel/ip_in_ip/TunnelIpInIpConfigUtils.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddressV6.h>
#include <folly/String.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/ArgCompletion.h"

namespace facebook::fboss::tunnel_utils {

namespace {

inline constexpr std::string_view kTunnelModeUniform = "uniform";
inline constexpr std::string_view kTunnelModePipe = "pipe";

inline constexpr std::string_view kTermTypeP2P = "p2p";
inline constexpr std::string_view kTermTypeP2MP = "p2mp";

// Mask suffix appended to "destination"/"source" in the parsed attr map.
inline constexpr std::string_view kMaskSuffix = "-mask";

// The cfg::IpInIpTunnel mask fields hold an IPv6 mask *address* string
// (e.g. "ffff:ffff::"), not a prefix length: ThriftConfigApplier parses them
// with folly::IPAddressV6 and would throw on a bare integer at commit.
std::string prefixLenToMaskStr(int prefixLen) {
  return folly::IPAddressV6(
             folly::IPAddressV6::fetchMask(static_cast<size_t>(prefixLen)))
      .str();
}

const cfg::IpInIpTunnel* FOLLY_NULLABLE
findTunnel(const cfg::SwitchConfig& cfg, const std::string& id) {
  if (!cfg.ipInIpTunnels().has_value()) {
    return nullptr;
  }
  for (const auto& t : *cfg.ipInIpTunnels()) {
    if (*t.ipInIpTunnelId() == id) {
      return &t;
    }
  }
  return nullptr;
}

// Locate (or append) the tunnel and enforce that an existing tunnel is not
// silently switched between encap and decap.
cfg::IpInIpTunnel& findOrCreateTunnel(
    cfg::SwitchConfig& cfg,
    const std::string& id,
    TunnelType tunnelType) {
  if (!cfg.ipInIpTunnels().has_value()) {
    cfg.ipInIpTunnels() = std::vector<cfg::IpInIpTunnel>{};
  }
  auto it = std::find_if(
      cfg.ipInIpTunnels()->begin(),
      cfg.ipInIpTunnels()->end(),
      [&id](const auto& t) { return *t.ipInIpTunnelId() == id; });
  if (it != cfg.ipInIpTunnels()->end()) {
    // An unset tunnelType is treated as decap by the agent, so compare against
    // that default to catch a direction mismatch on a legacy tunnel.
    const TunnelType existingType =
        it->tunnelType().value_or(TunnelType::IP_IN_IP_DECAP);
    if (existingType != tunnelType) {
      throw std::invalid_argument(
          fmt::format(
              "Tunnel '{}' already exists as {}; cannot reconfigure it as {}. "
              "Delete it first to change direction.",
              id,
              directionName(existingType),
              directionName(tunnelType)));
    }
    return *it;
  }
  cfg::IpInIpTunnel t;
  t.ipInIpTunnelId() = id;
  t.tunnelType() = tunnelType;
  cfg.ipInIpTunnels()->push_back(std::move(t));
  return cfg.ipInIpTunnels()->back();
}

} // namespace

std::string toLower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string directionName(TunnelType type) {
  return type == TunnelType::IP_IN_IP_ENCAP ? "encap" : "decap";
}

cfg::TunnelMode parseTunnelMode(const std::string& s) {
  const std::string lower = toLower(s);
  if (lower == kTunnelModeUniform) {
    return cfg::TunnelMode::UNIFORM;
  }
  if (lower == kTunnelModePipe) {
    return cfg::TunnelMode::PIPE;
  }
  throw std::invalid_argument(
      fmt::format("Invalid mode '{}'. Valid values: uniform, pipe", s));
}

cfg::TunnelTerminationType parseTerminationType(const std::string& s) {
  const std::string lower = toLower(s);
  if (lower == kTermTypeP2P) {
    return cfg::TunnelTerminationType::P2P;
  }
  if (lower == kTermTypeP2MP) {
    return cfg::TunnelTerminationType::P2MP;
  }
  // Note: the thrift enum also defines MP2P/MP2MP, but the agent's
  // SaiTunnelManager only implements P2P and P2MP and aborts on anything
  // else at commit, so they are rejected here too.
  throw std::invalid_argument(
      fmt::format("Invalid termination-type '{}'. Valid values: p2p, p2mp", s));
}

void parseTunnelConfigArgs(
    const std::vector<std::string>& v,
    const std::unordered_set<std::string>& allowedAttrs,
    std::string& tunnelId,
    std::map<std::string, std::string>& attrs) {
  if (v.empty()) {
    throw std::invalid_argument("Tunnel ID is required");
  }

  const auto isAllowed = [&allowedAttrs](const std::string& s) {
    return allowedAttrs.count(s) > 0;
  };

  // Reject first-token-is-known-attr or the reserved 'mask' sub-keyword to
  // catch the common mistake of forgetting the tunnel ID. Match
  // case-insensitively.
  const std::string firstLower = toLower(v[0]);
  if (isAllowed(firstLower) || firstLower == kMaskKeyword) {
    throw std::invalid_argument(
        fmt::format("Expected tunnel ID, got attribute name '{}'", v[0]));
  }

  tunnelId = v[0];

  const std::string validList = folly::join(
      ", ",
      std::vector<std::string>{
          std::string(kAttrDestination),
          std::string(kAttrSource),
          std::string(kAttrTtlMode),
          std::string(kAttrDscpMode),
          std::string(kAttrEcnMode),
          std::string(kAttrTerminationType),
          std::string(kAttrUnderlayIntfId)});

  size_t i = 1;
  while (i < v.size()) {
    const std::string attr = toLower(v[i]);
    if (!isAllowed(attr)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown or unsupported attribute '{}'. Valid attributes: {}",
              v[i],
              validList));
    }
    ++i;

    if (i >= v.size() || isAllowed(toLower(v[i]))) {
      throw std::invalid_argument(
          fmt::format("Missing value for attribute '{}'", attr));
    }

    // destination / source: IPv6 addresses only, optional "mask <prefix-len>".
    if (attr == kAttrDestination || attr == kAttrSource) {
      const std::string& ip = v[i];
      if (!folly::IPAddressV6::validate(ip)) {
        throw std::invalid_argument(
            fmt::format(
                "Invalid IPv6 address '{}' (only IPv6 is supported)", ip));
      }
      attrs[attr] = ip;
      ++i;

      if (i < v.size() && toLower(v[i]) == kMaskKeyword) {
        ++i;
        if (i >= v.size()) {
          throw std::invalid_argument(
              fmt::format(
                  "Missing prefix length after 'mask' keyword for '{}'", attr));
        }
        const std::string& prefixLenStr = v[i];
        int prefixLen = -1;
        try {
          // folly::to rejects trailing garbage (unlike std::stoi).
          prefixLen = folly::to<int>(prefixLenStr);
        } catch (...) {
          throw std::invalid_argument(
              fmt::format(
                  "Invalid prefix length '{}': must be an integer in [0, 128]",
                  prefixLenStr));
        }
        if (prefixLen < 0 || prefixLen > 128) {
          throw std::invalid_argument(
              fmt::format("Prefix length {} out of range [0, 128]", prefixLen));
        }
        attrs[attr + std::string(kMaskSuffix)] = prefixLenStr;
        ++i;
      }
    } else if (
        attr == kAttrTtlMode || attr == kAttrDscpMode || attr == kAttrEcnMode) {
      // parseTunnelMode validates and lowercases; store the canonical lowercase
      // form so the apply step's string compare succeeds regardless of casing.
      attrs[attr] = toLower(v[i]);
      parseTunnelMode(attrs[attr]);
      ++i;
    } else if (attr == kAttrTerminationType) {
      attrs[attr] = toLower(v[i]);
      parseTerminationType(attrs[attr]);
      ++i;
    } else if (attr == kAttrUnderlayIntfId) {
      const std::string& intfId = v[i];
      try {
        // folly::to rejects trailing garbage (unlike std::stoi).
        folly::to<int>(intfId);
      } catch (...) {
        throw std::invalid_argument(
            fmt::format(
                "'underlay-intf-id' must be an integer, got '{}'", intfId));
      }
      attrs[attr] = intfId;
      ++i;
    }
  }
}

std::vector<std::string> applyTunnelConfig(
    cfg::SwitchConfig& swConfig,
    TunnelType tunnelType,
    const std::string& tunnelId,
    const std::map<std::string, std::string>& attrs) {
  // Validate the resulting (post-apply) tunnel state before mutating the
  // staged config: the agent hard-requires some fields and aborts — rather
  // than rejecting the config — when they are missing, so an incomplete
  // tunnel must never reach a commit. Validating the post-apply state (not
  // just the attrs given on this command line) keeps one-attribute-at-a-time
  // updates of existing tunnels working.
  //
  // Agent requirements being guarded:
  // - dstIp is an unqualified thrift field; ThriftConfigApplier does
  //   folly::IPAddressV6(*config.dstIp()) for both directions, which throws
  //   on the "" default.
  // - underlayIntfID defaults to 0; SaiTunnelManager::addTunnel throws
  //   FbossError when no router interface exists for it.
  // - For encap, SaiTunnelManager dereferences the optional srcIp.
  // - For decap with P2P termination, the term's source address comes from
  //   'source'; folly::IPAddress("") throws when it is unset.
  const bool isEncap = tunnelType == TunnelType::IP_IN_IP_ENCAP;
  const cfg::IpInIpTunnel* existing = findTunnel(swConfig, tunnelId);
  const bool willHaveDst = attrs.count(std::string(kAttrDestination)) > 0 ||
      (existing != nullptr && !existing->dstIp()->empty());
  const bool willHaveSrc = attrs.count(std::string(kAttrSource)) > 0 ||
      (existing != nullptr && existing->srcIp().has_value() &&
       !existing->srcIp()->empty());
  const bool willHaveUnderlay =
      attrs.count(std::string(kAttrUnderlayIntfId)) > 0 ||
      (existing != nullptr && *existing->underlayIntfID() != 0);

  if (!willHaveDst) {
    throw std::invalid_argument(
        fmt::format(
            "{} tunnel '{}' requires a 'destination' ({} IPv6)",
            isEncap ? "Encap" : "Decap",
            tunnelId,
            isEncap ? "outer-header destination" : "termination match"));
  }
  if (isEncap && !willHaveSrc) {
    throw std::invalid_argument(
        fmt::format(
            "Encap tunnel '{}' requires a 'source' (outer-header source "
            "IPv6)",
            tunnelId));
  }
  if (!willHaveUnderlay) {
    throw std::invalid_argument(
        fmt::format(
            "{} tunnel '{}' requires an 'underlay-intf-id' (underlay "
            "interface ID)",
            isEncap ? "Encap" : "Decap",
            tunnelId));
  }
  if (!isEncap) {
    std::optional<cfg::TunnelTerminationType> termType;
    auto termIt = attrs.find(std::string(kAttrTerminationType));
    if (termIt != attrs.end()) {
      termType = parseTerminationType(termIt->second);
    } else if (existing != nullptr && existing->tunnelTermType().has_value()) {
      termType = *existing->tunnelTermType();
    }
    // Unset termination-type falls back to the agent's P2MP default, which
    // does not need a source.
    if (termType == cfg::TunnelTerminationType::P2P && !willHaveSrc) {
      throw std::invalid_argument(
          fmt::format(
              "Decap tunnel '{}' with termination-type p2p requires a "
              "'source' (outer-header source IPv6 to match)",
              tunnelId));
    }
  }

  // Validate the underlay interface exists before mutating the config.
  auto underlayIt = attrs.find(std::string(kAttrUnderlayIntfId));
  if (underlayIt != attrs.end()) {
    const int intfId = folly::to<int>(underlayIt->second);
    bool found = false;
    for (const auto& intf : *swConfig.interfaces()) {
      if (*intf.intfID() == intfId) {
        found = true;
        break;
      }
    }
    if (!found) {
      throw std::invalid_argument(
          fmt::format(
              "Interface ID {} not found in config. Use 'fboss2-dev show "
              "interface' to see valid IDs.",
              intfId));
    }
  }

  cfg::IpInIpTunnel& tunnel =
      findOrCreateTunnel(swConfig, tunnelId, tunnelType);

  std::vector<std::string> results;

  for (const auto& [attr, value] : attrs) {
    if (attr == kAttrDestination) {
      tunnel.dstIp() = value;
      auto maskIt = attrs.find(std::string(kAttrDestination) + "-mask");
      if (maskIt != attrs.end()) {
        tunnel.dstIpMask() = prefixLenToMaskStr(folly::to<int>(maskIt->second));
        results.push_back(
            fmt::format("destination={} prefix-len={}", value, maskIt->second));
      } else {
        results.push_back(fmt::format("destination={}", value));
      }
    } else if (attr == kAttrSource) {
      tunnel.srcIp() = value;
      auto maskIt = attrs.find(std::string(kAttrSource) + "-mask");
      if (maskIt != attrs.end()) {
        tunnel.srcIpMask() = prefixLenToMaskStr(folly::to<int>(maskIt->second));
        results.push_back(
            fmt::format("source={} prefix-len={}", value, maskIt->second));
      } else {
        results.push_back(fmt::format("source={}", value));
      }
    } else if (
        attr == std::string(kAttrDestination) + "-mask" ||
        attr == std::string(kAttrSource) + "-mask") {
      // Already handled alongside destination/source.
    } else if (attr == kAttrTtlMode) {
      tunnel.ttlMode() = parseTunnelMode(value);
      results.push_back(fmt::format("ttl-mode={}", value));
    } else if (attr == kAttrDscpMode) {
      tunnel.dscpMode() = parseTunnelMode(value);
      results.push_back(fmt::format("dscp-mode={}", value));
    } else if (attr == kAttrEcnMode) {
      tunnel.ecnMode() = parseTunnelMode(value);
      results.push_back(fmt::format("ecn-mode={}", value));
    } else if (attr == kAttrTerminationType) {
      tunnel.tunnelTermType() = parseTerminationType(value);
      results.push_back(fmt::format("termination-type={}", value));
    } else if (attr == kAttrUnderlayIntfId) {
      // Existence already validated above, before the config was mutated.
      tunnel.underlayIntfID() = folly::to<int>(value);
      results.push_back(fmt::format("underlay-intf-id={}", value));
    }
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

  const std::string direction = directionName(tunnelType);
  if (results.empty()) {
    return fmt::format("{} tunnel '{}' configured", direction, tunnelId);
  }
  return fmt::format(
      "Successfully configured {} tunnel '{}': {}",
      direction,
      tunnelId,
      folly::join(", ", results));
}

std::vector<std::string> completeTunnelConfigArgs(
    const std::vector<std::string>& typed,
    const std::unordered_set<std::string>& allowedAttrs) {
  const std::vector<std::string> modes = {
      std::string(kTunnelModeUniform), std::string(kTunnelModePipe)};
  return utils::completion::completeAttrGrammar(
      typed,
      {.attrs = utils::completion::toWords(allowedAttrs),
       .values =
           {
               {std::string(kAttrTtlMode), modes},
               {std::string(kAttrDscpMode), modes},
               {std::string(kAttrEcnMode), modes},
               {std::string(kAttrTerminationType),
                {std::string(kTermTypeP2P), std::string(kTermTypeP2MP)}},
           },
       .minObjects = 1});
}

} // namespace facebook::fboss::tunnel_utils
