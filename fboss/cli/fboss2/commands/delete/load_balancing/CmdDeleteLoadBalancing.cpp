/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/load_balancing/CmdDeleteLoadBalancing.h"

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <algorithm>
#include <iostream>
#include <string>
#include "fboss/cli/fboss2/commands/config/load_balancing/CmdConfigLoadBalancing.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

// Shared implementation for both ECMP and LAG handlers. Removing a
// load-balancer is applied hitlessly by the SAI layer: SaiSwitch dispatches
// LoadBalancersDelta removals to SaiSwitchManager::removeLoadBalancer
// without any *ChangeProhibited guard, the same delta path the config
// subcommands rely on.
std::string runLoadBalancerDelete(cfg::LoadBalancerID id) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  const auto before = swConfig.loadBalancers()->size();
  auto msg = removeLoadBalancer(swConfig, id);
  if (swConfig.loadBalancers()->size() != before) {
    session.saveConfig(
        cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  }
  return msg;
}

} // namespace

std::string removeLoadBalancer(
    cfg::SwitchConfig& swConfig,
    cfg::LoadBalancerID id) {
  auto& loadBalancers = *swConfig.loadBalancers();
  auto it = std::find_if(
      loadBalancers.begin(), loadBalancers.end(), [id](const auto& lb) {
        return *lb.id() == id;
      });
  if (it == loadBalancers.end()) {
    return fmt::format(
        "Warning: no {} load-balancer configured, nothing to delete",
        lbIdToString(id));
  }
  loadBalancers.erase(it);
  return fmt::format("Deleted {} load-balancer", lbIdToString(id));
}

CmdDeleteLoadBalancingEcmpTraits::RetType
CmdDeleteLoadBalancingEcmp::queryClient(const HostInfo& /* hostInfo */) {
  return runLoadBalancerDelete(cfg::LoadBalancerID::ECMP);
}

void CmdDeleteLoadBalancingEcmp::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

CmdDeleteLoadBalancingLagTraits::RetType CmdDeleteLoadBalancingLag::queryClient(
    const HostInfo& /* hostInfo */) {
  return runLoadBalancerDelete(cfg::LoadBalancerID::AGGREGATE_PORT);
}

void CmdDeleteLoadBalancingLag::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiations
template void
CmdHandler<CmdDeleteLoadBalancing, CmdDeleteLoadBalancingTraits>::run();
template void
CmdHandler<CmdDeleteLoadBalancingEcmp, CmdDeleteLoadBalancingEcmpTraits>::run();
template void
CmdHandler<CmdDeleteLoadBalancingLag, CmdDeleteLoadBalancingLagTraits>::run();

} // namespace facebook::fboss
