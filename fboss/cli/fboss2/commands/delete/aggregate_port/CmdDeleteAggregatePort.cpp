// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/delete/aggregate_port/CmdDeleteAggregatePort.h"

#include <fmt/format.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/aggregate_port/AggregatePortConfigUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {

AggregatePortNameArg::AggregatePortNameArg(std::vector<std::string> v) {
  if (v.size() != 1 || v.front().empty()) {
    throw std::invalid_argument("Exactly one aggregate-port name is required");
  }
  name_ = v.front();
  data_ = std::move(v);
}

AggregatePortMemberDeleteArg::AggregatePortMemberDeleteArg(
    std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument(
        "At least one aggregate-port member is required");
  }
  ports_ = v;
  data_ = std::move(v);
}

CmdDeleteAggregatePortTraits::RetType CmdDeleteAggregatePort::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& aggregatePortArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto aggregatePorts = *swConfig.aggregatePorts();
  auto originalSize = aggregatePorts.size();
  std::erase_if(aggregatePorts, [&](const auto& aggregatePort) {
    return *aggregatePort.name() == aggregatePortArg.getName();
  });
  if (aggregatePorts.size() == originalSize) {
    throw std::invalid_argument(
        fmt::format(
            "Aggregate-port '{}' does not exist", aggregatePortArg.getName()));
  }

  aggregate_port_utils::validateAggregatePorts(swConfig, aggregatePorts);
  swConfig.aggregatePorts() = std::move(aggregatePorts);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully deleted aggregate-port '{}'", aggregatePortArg.getName());
}

void CmdDeleteAggregatePort::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

CmdDeleteAggregatePortMemberTraits::RetType
CmdDeleteAggregatePortMember::queryClient(
    const HostInfo& /* hostInfo */,
    const AggregatePortNameArg& aggregatePortArg,
    const ObjectArgType& memberArgs) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto aggregatePorts = *swConfig.aggregatePorts();
  auto* aggregatePort = aggregate_port_utils::findAggregatePort(
      aggregatePorts, aggregatePortArg.getName());
  if (!aggregatePort) {
    throw std::invalid_argument(
        fmt::format(
            "Aggregate-port '{}' does not exist", aggregatePortArg.getName()));
  }

  std::set<int32_t> portIds;
  for (const auto& token : memberArgs.getPorts()) {
    auto resolved = aggregate_port_utils::resolvePort(swConfig, token);
    if (!portIds.insert(resolved.id).second) {
      throw std::invalid_argument(
          fmt::format("Port '{}' was specified more than once", token));
    }
  }

  auto originalSize = aggregatePort->memberPorts()->size();
  std::erase_if(*aggregatePort->memberPorts(), [&](const auto& member) {
    return portIds.count(*member.memberPortID()) != 0;
  });
  if (aggregatePort->memberPorts()->size() == originalSize) {
    return fmt::format(
        "No requested members are configured on aggregate-port '{}'",
        aggregatePortArg.getName());
  }

  auto removedCount = originalSize - aggregatePort->memberPorts()->size();
  aggregate_port_utils::validateAggregatePorts(swConfig, aggregatePorts);
  swConfig.aggregatePorts() = std::move(aggregatePorts);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully removed {} member(s) from aggregate-port '{}'",
      removedCount,
      aggregatePortArg.getName());
}

void CmdDeleteAggregatePortMember::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

CmdDeleteAggregatePortMinimumCapacityToUpTraits::RetType
CmdDeleteAggregatePortMinimumCapacityToUp::queryClient(
    const HostInfo& /* hostInfo */,
    const AggregatePortNameArg& aggregatePortArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto aggregatePorts = *swConfig.aggregatePorts();
  auto* aggregatePort = aggregate_port_utils::findAggregatePort(
      aggregatePorts, aggregatePortArg.getName());
  if (!aggregatePort) {
    throw std::invalid_argument(
        fmt::format(
            "Aggregate-port '{}' does not exist", aggregatePortArg.getName()));
  }
  if (!aggregatePort->minimumCapacityToUp().has_value()) {
    return fmt::format(
        "Aggregate-port '{}' has no minimum-capacity-to-up setting",
        aggregatePortArg.getName());
  }

  aggregatePort->minimumCapacityToUp().reset();
  aggregate_port_utils::validateAggregatePorts(swConfig, aggregatePorts);
  swConfig.aggregatePorts() = std::move(aggregatePorts);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully removed minimum-capacity-to-up from aggregate-port '{}'",
      aggregatePortArg.getName());
}

void CmdDeleteAggregatePortMinimumCapacityToUp::printOutput(
    const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdDeleteAggregatePort, CmdDeleteAggregatePortTraits>::run();
template void CmdHandler<
    CmdDeleteAggregatePortMember,
    CmdDeleteAggregatePortMemberTraits>::run();
template void CmdHandler<
    CmdDeleteAggregatePortMinimumCapacityToUp,
    CmdDeleteAggregatePortMinimumCapacityToUpTraits>::run();

} // namespace facebook::fboss
