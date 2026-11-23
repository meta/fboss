// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_types.h"

namespace facebook::fboss::aggregate_port_utils {

struct ResolvedPort {
  int32_t id;
  std::string name;
};

bool isAggregatePortAttribute(const std::string& token);
bool isMemberAttribute(const std::string& token);

cfg::AggregatePort* findAggregatePort(
    std::vector<cfg::AggregatePort>& aggregatePorts,
    const std::string& name);
const cfg::AggregatePort* findAggregatePort(
    const std::vector<cfg::AggregatePort>& aggregatePorts,
    const std::string& name);

ResolvedPort resolvePort(
    const cfg::SwitchConfig& swConfig,
    const std::string& token);

void applyAggregatePortAttributes(
    cfg::AggregatePort& aggregatePort,
    const std::map<std::string, std::string>& attributes);
void applyMemberAttributes(
    cfg::AggregatePortMember& member,
    const std::map<std::string, std::string>& attributes);

void validateAggregatePorts(
    const cfg::SwitchConfig& swConfig,
    const std::vector<cfg::AggregatePort>& aggregatePorts);

} // namespace facebook::fboss::aggregate_port_utils
