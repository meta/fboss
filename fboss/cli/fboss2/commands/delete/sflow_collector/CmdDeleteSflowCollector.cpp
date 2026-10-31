/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/sflow_collector/CmdDeleteSflowCollector.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/IPAddress.h>
#include <algorithm>
#include <iostream>

#include "fboss/agent/FbossError.h"
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

CmdDeleteSflowCollectorTraits::RetType CmdDeleteSflowCollector::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& collector) {
  auto& session = ConfigSession::getInstance();
  auto& collectors = *session.getAgentConfig().sw()->sFlowCollectors();

  auto firstMatch = std::find_if(
      collectors.begin(), collectors.end(), [&](const auto& configured) {
        return collectorMatches(configured, collector);
      });
  if (firstMatch == collectors.end()) {
    throw FbossError(
        "No sFlow collector ",
        collector.getCanonicalIp(),
        " port ",
        collector.getPort());
  }

  // Remove all semantically identical entries so legacy configs containing
  // equivalent IPv6 spellings cannot retain a duplicate collector.
  collectors.erase(
      std::remove_if(
          firstMatch,
          collectors.end(),
          [&](const auto& configured) {
            return collectorMatches(configured, collector);
          }),
      collectors.end());

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format(
      "Successfully deleted sFlow collector {} port {}",
      collector.getCanonicalIp(),
      collector.getPort());
}

void CmdDeleteSflowCollector::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteSflowCollector, CmdDeleteSflowCollectorTraits>::run();

} // namespace facebook::fboss
