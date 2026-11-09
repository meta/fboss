/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/qos/priority_group_policy/CmdConfigQosPriorityGroupPolicyGroupId.h"

#include "fboss/cli/fboss2/CmdHandler.cpp"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <re2/re2.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/agent/gen-cpp2/switch_config_constants.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/cli/fboss2/commands/config/qos/priority_group_policy/CmdConfigQosPriorityGroupPolicy.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace {

std::string getValidScalingFactors() {
  std::vector<std::string> names;
  for (auto value :
       apache::thrift::TEnumTraits<cfg::MMUScalingFactor>::values) {
    names.push_back(apache::thrift::util::enumNameSafe(value));
  }
  return folly::join(", ", names);
}

} // namespace

PriorityGroupConfig::PriorityGroupConfig(std::vector<std::string> v) {
  // Minimum: <group-id> <attr> <value>
  if (v.empty()) {
    throw std::invalid_argument(
        "Expected: <group-id> <attr> <value> [<attr> <value> ...] where <attr> is one of: "
        "min-limit-bytes, headroom-limit-bytes, resume-offset-bytes, "
        "static-limit-bytes, scaling-factor, buffer-pool-name");
  }

  // Parse the group ID (first argument)
  const int16_t maxPgId = cfg::switch_config_constants::PORT_PG_VALUE_MAX();
  groupId_ = folly::to<int16_t>(v[0]);
  if (groupId_ < 0 || groupId_ > maxPgId) {
    throw std::invalid_argument(
        fmt::format(
            "Priority group ID must be between 0 and {}, got: {}",
            maxPgId,
            groupId_));
  }
  data_.push_back(v[0]);

  // Parse the remaining key-value pairs
  // After the group ID, we need pairs of <attr> <value>
  if ((v.size() - 1) % 2 != 0) {
    throw std::invalid_argument(
        "Attribute-value pairs must come in pairs. Got odd number of arguments after group ID.");
  }

  for (size_t i = 1; i < v.size(); i += 2) {
    attributes_.emplace_back(v[i], v[i + 1]);
    data_.push_back(v[i]);
    data_.push_back(v[i + 1]);
  }
}

CmdConfigQosPriorityGroupPolicyGroupIdTraits::RetType
CmdConfigQosPriorityGroupPolicyGroupId::queryClient(
    const HostInfo& /* hostInfo */,
    const PriorityGroupPolicyName& policyName,
    const ObjectArgType& config) {
  auto& session = ConfigSession::getInstance();
  auto& agentConfig = session.getAgentConfig();
  auto& switchConfig = *agentConfig.sw();

  // Get or create the portPgConfigs map
  if (!switchConfig.portPgConfigs()) {
    switchConfig.portPgConfigs() =
        std::map<std::string, std::vector<cfg::PortPgConfig>>{};
  }

  auto& portPgConfigs = *switchConfig.portPgConfigs();
  int16_t groupIdVal = config.getGroupId();

  // Get or create the policy entry (list of PortPgConfig)
  auto& configList = portPgConfigs[policyName.getName()];

  // Find the PortPgConfig with the matching group ID, or create a new one
  cfg::PortPgConfig* targetPgConfig = nullptr;
  for (auto& pgConfig : configList) {
    if (*pgConfig.id() == groupIdVal) {
      targetPgConfig = &pgConfig;
      break;
    }
  }

  if (targetPgConfig == nullptr) {
    // Create a new PortPgConfig with the given group ID
    cfg::PortPgConfig newConfig;
    newConfig.id() = groupIdVal;
    newConfig.name() = fmt::format("pg{}", groupIdVal);
    newConfig.minLimitBytes() = 0;
    newConfig.bufferPoolName() = "";
    configList.push_back(newConfig);
    targetPgConfig = &configList.back();
  }

  // Process each attribute-value pair
  static const re2::RE2 kValidPoolNamePattern("^[a-zA-Z][a-zA-Z0-9_-]{0,63}$");

  for (const auto& [attr, value] : config.getAttributes()) {
    if (attr == "min-limit-bytes") {
      int32_t bytes = folly::to<int32_t>(value);
      if (bytes < 0) {
        throw std::invalid_argument(
            "min-limit-bytes must be non-negative, got: " + value);
      }
      targetPgConfig->minLimitBytes() = bytes;
    } else if (attr == "headroom-limit-bytes") {
      int32_t bytes = folly::to<int32_t>(value);
      if (bytes < 0) {
        throw std::invalid_argument(
            "headroom-limit-bytes must be non-negative, got: " + value);
      }
      targetPgConfig->headroomLimitBytes() = bytes;
    } else if (attr == "resume-offset-bytes") {
      int32_t bytes = folly::to<int32_t>(value);
      if (bytes < 0) {
        throw std::invalid_argument(
            "resume-offset-bytes must be non-negative, got: " + value);
      }
      targetPgConfig->resumeOffsetBytes() = bytes;
    } else if (attr == "static-limit-bytes") {
      int32_t bytes = folly::to<int32_t>(value);
      if (bytes < 0) {
        throw std::invalid_argument(
            "static-limit-bytes must be non-negative, got: " + value);
      }
      targetPgConfig->staticLimitBytes() = bytes;
    } else if (attr == "scaling-factor") {
      cfg::MMUScalingFactor factor{};
      if (!apache::thrift::TEnumTraits<cfg::MMUScalingFactor>::findValue(
              value, &factor)) {
        throw std::invalid_argument(
            "Invalid scaling-factor: '" + value +
            "'. Valid values are: " + getValidScalingFactors());
      }
      targetPgConfig->sramScalingFactor() = factor;
    } else if (attr == "buffer-pool-name") {
      if (!re2::RE2::FullMatch(value, kValidPoolNamePattern)) {
        throw std::invalid_argument(
            "Invalid buffer pool name: '" + value +
            "'. Name must start with a letter, contain only alphanumeric "
            "characters, underscores, or hyphens, and be 1-64 characters long.");
      }
      targetPgConfig->bufferPoolName() = value;
    } else {
      throw std::invalid_argument(
          "Unknown attribute: '" + attr +
          "'. Valid attributes are: min-limit-bytes, headroom-limit-bytes, "
          "resume-offset-bytes, static-limit-bytes, scaling-factor, buffer-pool-name");
    }
  }

  // Save the updated config
  session.saveConfig(
      cli::ServiceType::AGENT, cli::ConfigActionLevel::SERVICE_RESTART);

  return fmt::format(
      "Successfully configured priority-group-policy '{}' group-id {}",
      policyName.getName(),
      groupIdVal);
}

void CmdConfigQosPriorityGroupPolicyGroupId::printOutput(
    const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<
    CmdConfigQosPriorityGroupPolicyGroupId,
    CmdConfigQosPriorityGroupPolicyGroupIdTraits>::run();

} // namespace facebook::fboss
