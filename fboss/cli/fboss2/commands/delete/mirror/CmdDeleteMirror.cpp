/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/mirror/CmdDeleteMirror.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {

void collectPolicyReferrers(
    const cfg::TrafficPolicyConfig& policy,
    const std::string& mirrorName,
    const std::string& policyName,
    std::vector<std::string>& referrers) {
  for (const auto& matchToAction : *policy.matchToAction()) {
    const auto& action = *matchToAction.action();
    if ((action.ingressMirror().has_value() &&
         *action.ingressMirror() == mirrorName) ||
        (action.egressMirror().has_value() &&
         *action.egressMirror() == mirrorName)) {
      referrers.push_back(
          fmt::format("{} matcher '{}'", policyName, *matchToAction.matcher()));
    }
  }
}

std::vector<std::string> findReferrers(
    const cfg::SwitchConfig& config,
    const std::string& mirrorName) {
  std::vector<std::string> referrers;
  for (const auto& port : *config.ports()) {
    if ((port.ingressMirror().has_value() &&
         *port.ingressMirror() == mirrorName) ||
        (port.egressMirror().has_value() &&
         *port.egressMirror() == mirrorName)) {
      if (port.name().has_value()) {
        referrers.push_back(fmt::format("port '{}'", *port.name()));
      } else {
        referrers.push_back(
            fmt::format("port with logical ID {}", *port.logicalID()));
      }
    }
  }
  if (const auto& policy = config.dataPlaneTrafficPolicy()) {
    collectPolicyReferrers(
        *policy, mirrorName, "dataPlaneTrafficPolicy", referrers);
  }
  if (const auto& policy = config.globalEgressTrafficPolicy_DEPRECATED()) {
    collectPolicyReferrers(
        *policy, mirrorName, "globalEgressTrafficPolicy_DEPRECATED", referrers);
  }
  if (const auto& cpuPolicy = config.cpuTrafficPolicy()) {
    if (const auto& policy = cpuPolicy->trafficPolicy()) {
      collectPolicyReferrers(
          *policy, mirrorName, "cpuTrafficPolicy", referrers);
    }
  }
  return referrers;
}

} // namespace

MirrorNameArg::MirrorNameArg(std::vector<std::string> v) {
  if (v.size() != 1 || v[0].empty()) {
    throw std::invalid_argument(
        "Expected exactly one non-empty argument: <mirror-name>");
  }
  name_ = v[0];
  data_ = std::move(v);
}

CmdDeleteMirrorTraits::RetType CmdDeleteMirror::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& mirror) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto& mirrors = *swConfig.mirrors();

  auto it = std::find_if(
      mirrors.begin(), mirrors.end(), [&](const cfg::Mirror& config) {
        return *config.name() == mirror.getName();
      });
  if (it == mirrors.end()) {
    throw FbossError("No mirror named '", mirror.getName(), "'");
  }

  auto referrers = findReferrers(swConfig, mirror.getName());
  if (!referrers.empty()) {
    throw FbossError(
        "Mirror '",
        mirror.getName(),
        "' is still referenced by: ",
        folly::join("; ", referrers),
        ". Remove the references first.");
  }

  mirrors.erase(it);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format("Successfully deleted mirror '{}'", mirror.getName());
}

void CmdDeleteMirror::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void CmdHandler<CmdDeleteMirror, CmdDeleteMirrorTraits>::run();

} // namespace facebook::fboss
