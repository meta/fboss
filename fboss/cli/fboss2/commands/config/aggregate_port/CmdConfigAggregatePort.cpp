// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/commands/config/aggregate_port/CmdConfigAggregatePort.h"

#include <fmt/format.h>
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/aggregate_port/AggregatePortConfigUtils.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"

namespace facebook::fboss {
namespace {

std::map<std::string, std::string> parseAttributes(
    const std::vector<std::string>& tokens,
    size_t offset,
    bool (*isKnownAttribute)(const std::string&),
    const std::string& objectName) {
  if ((tokens.size() - offset) % 2 != 0) {
    throw std::invalid_argument(
        fmt::format(
            "{} attributes must be supplied as <attribute> <value> pairs",
            objectName));
  }
  std::map<std::string, std::string> attributes;
  for (size_t i = offset; i < tokens.size(); i += 2) {
    if (!isKnownAttribute(tokens[i])) {
      throw std::invalid_argument(
          fmt::format("Unknown {} attribute '{}'", objectName, tokens[i]));
    }
    if (!attributes.emplace(tokens[i], tokens[i + 1]).second) {
      throw std::invalid_argument(
          fmt::format("Duplicate {} attribute '{}'", objectName, tokens[i]));
    }
  }
  return attributes;
}

} // namespace

AggregatePortConfigArg::AggregatePortConfigArg(std::vector<std::string> v) {
  if (v.empty()) {
    throw std::invalid_argument("Aggregate-port name is required");
  }
  name_ = v.front();
  if (name_.empty()) {
    throw std::invalid_argument("Aggregate-port name must not be empty");
  }
  attributes_ = parseAttributes(
      v, 1, aggregate_port_utils::isAggregatePortAttribute, "aggregate-port");
  data_ = std::move(v);
}

AggregatePortMemberConfigArg::AggregatePortMemberConfigArg(
    std::vector<std::string> v) {
  auto attribute =
      std::find_if(v.begin(), v.end(), aggregate_port_utils::isMemberAttribute);
  ports_.assign(v.begin(), attribute);
  if (ports_.empty()) {
    throw std::invalid_argument(
        "At least one aggregate-port member is required");
  }
  auto offset = static_cast<size_t>(std::distance(v.begin(), attribute));
  attributes_ = parseAttributes(
      v,
      offset,
      aggregate_port_utils::isMemberAttribute,
      "aggregate-port member");
  data_ = std::move(v);
}

CmdConfigAggregatePortTraits::RetType CmdConfigAggregatePort::queryClient(
    const HostInfo& /* hostInfo */,
    const ObjectArgType& aggregatePortArg) {
  auto& session = ConfigSession::getInstance();
  auto& swConfig = *session.getAgentConfig().sw();
  auto aggregatePorts = *swConfig.aggregatePorts();
  auto* existing = aggregate_port_utils::findAggregatePort(
      aggregatePorts, aggregatePortArg.getName());
  const bool creating = existing == nullptr;

  cfg::AggregatePort candidate;
  if (creating) {
    if (!aggregatePortArg.getAttributes().count("key")) {
      throw std::invalid_argument(
          "The key attribute is required when creating an aggregate port");
    }
    candidate.name() = aggregatePortArg.getName();
    candidate.description() = "";
    candidate.memberPorts() = {};
    candidate.minimumCapacity() = cfg::switch_config_constants::ALL_LINKS();
    candidate.aggregatePortType() = cfg::AggregatePortType::LAG_PORT;
    candidate.extendedKey() = 0;
  } else {
    candidate = *existing;
  }

  aggregate_port_utils::applyAggregatePortAttributes(
      candidate, aggregatePortArg.getAttributes());
  if (!creating && *candidate.key() != *existing->key()) {
    throw std::invalid_argument(
        "The aggregate-port key cannot be changed; delete and recreate the aggregate port");
  }
  if (creating) {
    aggregatePorts.push_back(candidate);
  } else {
    *existing = candidate;
  }
  aggregate_port_utils::validateAggregatePorts(swConfig, aggregatePorts);

  if (!creating &&
      candidate ==
          *aggregate_port_utils::findAggregatePort(
              *swConfig.aggregatePorts(), aggregatePortArg.getName())) {
    return fmt::format(
        "Aggregate-port '{}' is already configured with the requested values",
        aggregatePortArg.getName());
  }

  swConfig.aggregatePorts() = std::move(aggregatePorts);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully {} aggregate-port '{}'",
      creating ? "created" : "updated",
      aggregatePortArg.getName());
}

void CmdConfigAggregatePort::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

CmdConfigAggregatePortMemberTraits::RetType
CmdConfigAggregatePortMember::queryClient(
    const HostInfo& /* hostInfo */,
    const AggregatePortConfigArg& aggregatePortArg,
    const ObjectArgType& memberArgs) {
  if (!aggregatePortArg.getAttributes().empty()) {
    throw std::invalid_argument(
        "Aggregate-port attributes cannot precede the member subcommand");
  }
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

  std::set<int32_t> requestedPortIds;
  for (const auto& portToken : memberArgs.getPorts()) {
    auto resolved = aggregate_port_utils::resolvePort(swConfig, portToken);
    if (!requestedPortIds.insert(resolved.id).second) {
      throw std::invalid_argument(
          fmt::format("Port '{}' was specified more than once", portToken));
    }

    auto member = std::find_if(
        aggregatePort->memberPorts()->begin(),
        aggregatePort->memberPorts()->end(),
        [&](const auto& configuredMember) {
          return *configuredMember.memberPortID() == resolved.id;
        });
    if (member == aggregatePort->memberPorts()->end()) {
      cfg::AggregatePortMember newMember;
      newMember.memberPortID() = resolved.id;
      newMember.priority() = 32768;
      newMember.rate() = cfg::LacpPortRate::FAST;
      newMember.activity() = cfg::LacpPortActivity::ACTIVE;
      newMember.holdTimerMultiplier() =
          cfg::switch_config_constants::DEFAULT_LACP_HOLD_TIMER_MULTIPLIER();
      aggregate_port_utils::applyMemberAttributes(
          newMember, memberArgs.getAttributes());
      aggregatePort->memberPorts()->push_back(std::move(newMember));
    } else {
      aggregate_port_utils::applyMemberAttributes(
          *member, memberArgs.getAttributes());
    }
  }

  aggregate_port_utils::validateAggregatePorts(swConfig, aggregatePorts);
  if (aggregatePorts == *swConfig.aggregatePorts()) {
    return fmt::format(
        "Aggregate-port '{}' members are already configured with the requested values",
        aggregatePortArg.getName());
  }

  swConfig.aggregatePorts() = std::move(aggregatePorts);
  session.saveConfig(cli::ServiceType::AGENT, cli::ConfigActionLevel::HITLESS);
  return fmt::format(
      "Successfully configured {} member(s) on aggregate-port '{}'",
      memberArgs.getPorts().size(),
      aggregatePortArg.getName());
}

void CmdConfigAggregatePortMember::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void
CmdHandler<CmdConfigAggregatePort, CmdConfigAggregatePortTraits>::run();
template void CmdHandler<
    CmdConfigAggregatePortMember,
    CmdConfigAggregatePortMemberTraits>::run();

} // namespace facebook::fboss
