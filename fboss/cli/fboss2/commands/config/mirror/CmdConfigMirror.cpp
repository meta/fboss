/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/mirror/CmdConfigMirror.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/IPAddress.h>
#include <folly/String.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {

constexpr std::string_view kEgressPort = "egress-port";
constexpr std::string_view kDestinationIp = "destination-ip";
constexpr std::string_view kSourceIp = "source-ip";
constexpr std::string_view kUdpSrcPort = "udp-src-port";
constexpr std::string_view kUdpDstPort = "udp-dst-port";
constexpr std::string_view kDscp = "dscp";
constexpr std::string_view kTruncate = "truncate";

const std::unordered_set<std::string> kAttrs = {
    std::string(kEgressPort),
    std::string(kDestinationIp),
    std::string(kSourceIp),
    std::string(kUdpSrcPort),
    std::string(kUdpDstPort),
    std::string(kDscp),
    std::string(kTruncate),
};

int32_t parseInteger(
    const std::string& attr,
    const std::string& value,
    int32_t minimum,
    int32_t maximum) {
  int32_t parsed;
  try {
    parsed = folly::to<int32_t>(value);
  } catch (...) {
    throw std::invalid_argument(
        fmt::format("{} must be an integer, got '{}'", attr, value));
  }
  if (parsed < minimum || parsed > maximum) {
    throw std::invalid_argument(
        fmt::format(
            "{} value {} is outside the supported range [{}, {}]",
            attr,
            parsed,
            minimum,
            maximum));
  }
  return parsed;
}

bool parseBool(const std::string& value) {
  std::string normalized = value;
  folly::toLowerAscii(normalized);
  if (normalized == "true") {
    return true;
  }
  if (normalized == "false") {
    return false;
  }
  throw std::invalid_argument(
      fmt::format("truncate must be true or false, got '{}'", value));
}

void validateIp(const std::string& attr, const std::string& value) {
  if (!folly::IPAddress::tryFromString(value).hasValue()) {
    throw std::invalid_argument(
        fmt::format(
            "{} must be a valid IPv4 or IPv6 address, got '{}'", attr, value));
  }
}

bool hasSflowAttrs(const std::map<std::string, std::string>& attrs) {
  return attrs.count(std::string(kDestinationIp)) ||
      attrs.count(std::string(kSourceIp)) ||
      attrs.count(std::string(kUdpSrcPort)) ||
      attrs.count(std::string(kUdpDstPort));
}

bool isSflowMirror(const cfg::Mirror& mirror) {
  const auto& tunnel = mirror.destination()->tunnel();
  return tunnel.has_value() && tunnel->sflowTunnel().has_value();
}

void validateEgressPortReference(
    const cfg::Port& port,
    const std::string& mirrorName) {
  if ((port.ingressMirror().has_value() &&
       *port.ingressMirror() == mirrorName) ||
      (port.egressMirror().has_value() && *port.egressMirror() == mirrorName)) {
    throw std::invalid_argument(
        fmt::format(
            "Egress port for mirror '{}' cannot reference the same mirror",
            mirrorName));
  }
}

cfg::MirrorEgressPort resolveEgressPort(
    const cfg::SwitchConfig& config,
    const std::string& mirrorName,
    const std::string& portValue) {
  const auto namedPort = std::find_if(
      config.ports()->begin(), config.ports()->end(), [&](const auto& cfgPort) {
        return cfgPort.name().has_value() && *cfgPort.name() == portValue;
      });
  if (namedPort != config.ports()->end()) {
    validateEgressPortReference(*namedPort, mirrorName);
    cfg::MirrorEgressPort egressPort;
    egressPort.name() = portValue;
    return egressPort;
  }

  const auto logicalID = folly::tryTo<int32_t>(portValue);
  if (logicalID.hasValue()) {
    const auto logicalPort = std::find_if(
        config.ports()->begin(),
        config.ports()->end(),
        [&](const auto& cfgPort) {
          return *cfgPort.logicalID() == logicalID.value();
        });
    if (logicalPort != config.ports()->end()) {
      validateEgressPortReference(*logicalPort, mirrorName);
      cfg::MirrorEgressPort egressPort;
      egressPort.logicalID() = logicalID.value();
      return egressPort;
    }
  }

  throw std::invalid_argument(
      fmt::format(
          "Egress port '{}' for mirror '{}' was not found in staged config",
          portValue,
          mirrorName));
}

void applyAttrs(
    cfg::SwitchConfig& config,
    cfg::Mirror& mirror,
    const std::map<std::string, std::string>& attrs) {
  auto& destination = *mirror.destination();

  if (auto it = attrs.find(std::string(kEgressPort)); it != attrs.end()) {
    destination.egressPort() =
        resolveEgressPort(config, *mirror.name(), it->second);
  }

  if (hasSflowAttrs(attrs)) {
    if (!destination.tunnel().has_value()) {
      destination.tunnel() = cfg::MirrorTunnel{};
    }
    auto& tunnel = *destination.tunnel();
    if (!tunnel.sflowTunnel().has_value()) {
      tunnel.sflowTunnel() = cfg::SflowTunnel{};
    }
    auto& sflow = *tunnel.sflowTunnel();

    if (auto it = attrs.find(std::string(kDestinationIp)); it != attrs.end()) {
      sflow.ip() = it->second;
    }
    if (auto it = attrs.find(std::string(kSourceIp)); it != attrs.end()) {
      tunnel.srcIp() = it->second;
    }
    if (auto it = attrs.find(std::string(kUdpSrcPort)); it != attrs.end()) {
      sflow.udpSrcPort() = parseInteger(it->first, it->second, 1, 65535);
    }
    if (auto it = attrs.find(std::string(kUdpDstPort)); it != attrs.end()) {
      sflow.udpDstPort() = parseInteger(it->first, it->second, 1, 65535);
    }
  }

  if (auto it = attrs.find(std::string(kDscp)); it != attrs.end()) {
    mirror.dscp() =
        static_cast<int8_t>(parseInteger(it->first, it->second, 0, 63));
  }
  if (auto it = attrs.find(std::string(kTruncate)); it != attrs.end()) {
    mirror.truncate() = parseBool(it->second);
  }
}

void validateCompleteMirror(const cfg::Mirror& mirror) {
  const auto& destination = *mirror.destination();
  if (isSflowMirror(mirror)) {
    const auto& sflow = *destination.tunnel()->sflowTunnel();
    if (sflow.ip()->empty()) {
      throw std::invalid_argument(
          fmt::format(
              "sFlow mirror '{}' requires destination-ip", *mirror.name()));
    }
    if (!sflow.udpSrcPort().has_value() || !sflow.udpDstPort().has_value()) {
      throw std::invalid_argument(
          fmt::format(
              "sFlow mirror '{}' requires udp-src-port and udp-dst-port",
              *mirror.name()));
    }
    if (const auto& srcIp = destination.tunnel()->srcIp()) {
      const auto source = folly::IPAddress(*srcIp);
      const auto destinationIp = folly::IPAddress(*sflow.ip());
      if (source.isV4() != destinationIp.isV4()) {
        throw std::invalid_argument(
            fmt::format(
                "sFlow mirror '{}' requires source-ip and destination-ip to use the same address family",
                *mirror.name()));
      }
    }
    return;
  }
  if (!destination.egressPort().has_value()) {
    throw std::invalid_argument(
        fmt::format("SPAN mirror '{}' requires egress-port", *mirror.name()));
  }
}

} // namespace

MirrorConfigArg::MirrorConfigArg(std::vector<std::string> v) {
  if (v.empty() || v[0].empty()) {
    throw std::invalid_argument("Mirror name is required");
  }
  if (v.size() == 1) {
    throw std::invalid_argument("At least one mirror attribute is required");
  }
  if ((v.size() - 1) % 2 != 0) {
    throw std::invalid_argument(
        fmt::format("Missing value for mirror attribute '{}'", v.back()));
  }

  name_ = v[0];
  for (size_t i = 1; i < v.size(); i += 2) {
    std::string attr = v[i];
    folly::toLowerAscii(attr);
    if (!kAttrs.count(attr)) {
      throw std::invalid_argument(
          fmt::format(
              "Unknown mirror attribute '{}'. Valid attributes: {}",
              v[i],
              folly::join(", ", kAttrs)));
    }
    if (!attrs_.emplace(attr, v[i + 1]).second) {
      throw std::invalid_argument(
          fmt::format(
              "Mirror attribute '{}' was specified more than once", attr));
    }
    if (v[i + 1].empty()) {
      throw std::invalid_argument(
          fmt::format(
              "Mirror attribute '{}' requires a non-empty value", attr));
    }
  }

  if (auto it = attrs_.find(std::string(kDestinationIp)); it != attrs_.end()) {
    validateIp(it->first, it->second);
  }
  if (auto it = attrs_.find(std::string(kSourceIp)); it != attrs_.end()) {
    validateIp(it->first, it->second);
  }
  if (auto it = attrs_.find(std::string(kUdpSrcPort)); it != attrs_.end()) {
    parseInteger(it->first, it->second, 1, 65535);
  }
  if (auto it = attrs_.find(std::string(kUdpDstPort)); it != attrs_.end()) {
    parseInteger(it->first, it->second, 1, 65535);
  }
  if (auto it = attrs_.find(std::string(kDscp)); it != attrs_.end()) {
    parseInteger(it->first, it->second, 0, 63);
  }
  if (auto it = attrs_.find(std::string(kTruncate)); it != attrs_.end()) {
    parseBool(it->second);
  }
  data_ = std::move(v);
}

CmdConfigMirrorTraits::RetType CmdConfigMirror::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& mirrorConfig) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto& mirrors = *swConfig.mirrors();

  auto it = std::find_if(
      mirrors.begin(), mirrors.end(), [&](const cfg::Mirror& mirror) {
        return *mirror.name() == mirrorConfig.getName();
      });
  const bool creating = it == mirrors.end();

  cfg::Mirror candidate;
  if (creating) {
    candidate.name() = mirrorConfig.getName();
    candidate.destination() = cfg::MirrorDestination{};
  } else {
    candidate = *it;
    if (hasSflowAttrs(mirrorConfig.getAttrs()) && !isSflowMirror(candidate)) {
      throw std::invalid_argument(
          fmt::format(
              "Mirror '{}' already exists as SPAN; delete it before changing it to sFlow",
              mirrorConfig.getName()));
    }
  }

  applyAttrs(swConfig, candidate, mirrorConfig.getAttrs());
  validateCompleteMirror(candidate);

  if (!creating && candidate == *it) {
    return fmt::format(
        "Mirror '{}' is already configured with the requested values",
        mirrorConfig.getName());
  }
  if (creating) {
    mirrors.push_back(std::move(candidate));
  } else {
    *it = std::move(candidate);
  }

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully {} mirror '{}'",
      creating ? "created" : "updated",
      mirrorConfig.getName());
}

void CmdConfigMirror::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void CmdHandler<CmdConfigMirror, CmdConfigMirrorTraits>::run();

} // namespace facebook::fboss
