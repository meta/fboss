/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/sflow_collector/CmdConfigSflowCollector.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <algorithm>
#include <iostream>
#include <limits>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {

bool collectorMatches(
    const cfg::SflowCollector& configured,
    const SflowCollectorArg& requested) {
  auto configuredIp = folly::IPAddress::tryFromString(*configured.ip());
  return configuredIp.hasValue() &&
      configuredIp.value() == requested.getIpAddress() &&
      *configured.port() == requested.getPort();
}

} // namespace

SflowCollectorArg::SflowCollectorArg(std::vector<std::string> v) {
  if (v.size() != 2) {
    throw std::invalid_argument(
        "Expected exactly two arguments: <collector-ip> <udp-port>");
  }

  auto parsedIp = folly::IPAddress::tryFromString(v[0]);
  if (!parsedIp.hasValue()) {
    throw std::invalid_argument(
        fmt::format("Invalid sFlow collector IP address '{}'", v[0]));
  }
  ipAddress_ = parsedIp.value();
  canonicalIp_ = ipAddress_.str();

  int64_t parsedPort;
  try {
    parsedPort = folly::to<int64_t>(v[1]);
  } catch (const std::exception&) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid sFlow collector port '{}': must be an integer from 1 to {}",
            v[1],
            std::numeric_limits<int16_t>::max()));
  }
  if (parsedPort < 1 || parsedPort > std::numeric_limits<int16_t>::max()) {
    throw std::invalid_argument(
        fmt::format(
            "Invalid sFlow collector port '{}': must be from 1 to {}",
            v[1],
            std::numeric_limits<int16_t>::max()));
  }
  port_ = static_cast<int16_t>(parsedPort);

  data_ = {canonicalIp_, std::to_string(port_)};
}

CmdConfigSflowCollectorTraits::RetType CmdConfigSflowCollector::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& collector) {
  auto& session = ConfigSession::getInstance();
  auto& collectors = *session.getAgentConfig().sw()->sFlowCollectors();

  auto existing = std::find_if(
      collectors.begin(), collectors.end(), [&](const auto& configured) {
        return collectorMatches(configured, collector);
      });
  if (existing != collectors.end()) {
    return fmt::format(
        "sFlow collector {} port {} is already configured",
        collector.getCanonicalIp(),
        collector.getPort());
  }

  cfg::SflowCollector configured;
  configured.ip() = collector.getCanonicalIp();
  configured.port() = collector.getPort();
  collectors.push_back(std::move(configured));

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully added sFlow collector {} port {}",
      collector.getCanonicalIp(),
      collector.getPort());
}

void CmdConfigSflowCollector::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdConfigSflowCollector, CmdConfigSflowCollectorTraits>::run();

} // namespace facebook::fboss
