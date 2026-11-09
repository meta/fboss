/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/delete/traffic_counter/CmdDeleteTrafficCounter.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/String.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

namespace {

// Collects the matchers (ACL names) of every match action in `policy` that
// references the counter, so the refusal message can name the referrers.
void collectCounterReferrers(
    const cfg::TrafficPolicyConfig& policy,
    const std::string& counterName,
    const std::string& policyLabel,
    std::vector<std::string>& referrers) {
  for (const auto& mta : *policy.matchToAction()) {
    const auto& counter = mta.action()->counter();
    if (counter.has_value() && *counter == counterName) {
      referrers.push_back(
          fmt::format("{} matcher '{}'", policyLabel, *mta.matcher()));
    }
  }
}

} // namespace

TrafficCounterNameArg::TrafficCounterNameArg(std::vector<std::string> v) {
  if (v.size() != 1 || v[0].empty()) {
    throw std::invalid_argument(
        "Expected exactly one non-empty argument: <counter-name>");
  }
  name_ = v[0];
  data_ = std::move(v);
}

CmdDeleteTrafficCounterTraits::RetType CmdDeleteTrafficCounter::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& arg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();

  const std::string& name = arg.getName();

  auto& counters = *swConfig.trafficCounters();
  auto it = std::find_if(
      counters.begin(), counters.end(), [&name](const auto& counter) {
        return *counter.name() == name;
      });
  if (it == counters.end()) {
    return fmt::format(
        "Warning: traffic counter '{}' not found, nothing to delete", name);
  }

  // Refuse while a traffic-policy match action still attaches this counter:
  // deleting it would leave a dangling counter reference and the agent would
  // reject the config.
  std::vector<std::string> referrers;
  if (const auto& dataPlanePolicy = swConfig.dataPlaneTrafficPolicy()) {
    collectCounterReferrers(
        *dataPlanePolicy, name, "dataPlaneTrafficPolicy", referrers);
  }
  if (const auto& cpuPolicy = swConfig.cpuTrafficPolicy()) {
    if (const auto& trafficPolicy = cpuPolicy->trafficPolicy()) {
      collectCounterReferrers(
          *trafficPolicy, name, "cpuTrafficPolicy", referrers);
    }
  }
  if (!referrers.empty()) {
    throw FbossError(
        "Traffic counter '",
        name,
        "' is still referenced by: ",
        folly::join("; ", referrers),
        ". Remove the referencing match action(s) first.");
  }

  counters.erase(it);

  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);

  return fmt::format("Successfully deleted traffic counter '{}'", name);
}

void CmdDeleteTrafficCounter::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

// Explicit template instantiation
template void
CmdHandler<CmdDeleteTrafficCounter, CmdDeleteTrafficCounterTraits>::run();

} // namespace facebook::fboss
