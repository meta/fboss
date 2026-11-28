// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/AgentConfigDomain.h"

#include <fmt/format.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/AgentServiceController.h"
// Needed by the explicit instantiation below, which include-cleaner does not
// count as a use.
#include "fboss/cli/fboss2/session/TypedConfigDomain-defs.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtils.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

template class TypedConfigDomain<cfg::AgentConfig>;

AgentConfigDomain::AgentConfigDomain(
    const std::string& sessionConfigDir,
    const std::string& systemConfigDir)
    : TypedConfigDomain(
          ConfigDomainInfo{
              cli::ServiceType::AGENT,
              "Agent",
              fmt::format("{}/agent.conf", sessionConfigDir),
              "cli/agent.conf",
              fmt::format("{}/cli/agent.conf", systemConfigDir),
              // A rollback reloads the agent, unless a commit being undone
              // recorded a higher level.
              cli::ConfigActionLevel::HITLESS,
          }) {}

AgentConfigDomain::~AgentConfigDomain() = default;

std::string AgentConfigDomain::queryCurrentPath(
    const HostInfo& hostInfo) const {
  std::string configPath;
  auto client =
      utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
  client->sync_getOption(configPath, "config");
  return configPath;
}

cfg::AgentConfig AgentConfigDomain::parse(std::string_view json) const {
  cfg::AgentConfig config;
  apache::thrift::SimpleJSONSerializer::deserialize<cfg::AgentConfig>(
      json, config);
  if (*config.sw() == cfg::SwitchConfig()) {
    apache::thrift::SimpleJSONSerializer::deserialize<cfg::SwitchConfig>(
        json, *config.sw());
  }
  return config;
}

std::unique_ptr<AgentServiceController> AgentConfigDomain::makeController(
    const HostInfo& hostInfo) const {
  // Ask the agent rather than read the config file: the config may omit
  // multi_switch when it is passed on the command line.
  MultiSwitchRunState runState;
  try {
    runState = utils::getMultiSwitchRunState(hostInfo);
  } catch (const std::exception& e) {
    throw std::runtime_error(
        fmt::format(
            "Failed to query agent run state from {}. "
            "Is the agent running? Error: {}",
            hostInfo.getName(),
            e.what()));
  }

  std::vector<int> switchIndexes;
  for (const auto& [idx, _] : *runState.hwIndexToRunState()) {
    switchIndexes.push_back(idx);
  }
  std::sort(switchIndexes.begin(), switchIndexes.end());

  return std::make_unique<AgentServiceController>(
      std::move(switchIndexes), *runState.multiSwitchEnabled());
}

void AgentConfigDomain::prepare(
    cli::ConfigActionLevel /*level*/,
    const HostInfo& hostInfo) {
  if (!controller_) {
    controller_ = makeController(hostInfo);
  }
}

std::vector<std::string> AgentConfigDomain::apply(
    cli::ConfigActionLevel level,
    const HostInfo& hostInfo) {
  prepare(level, hostInfo);
  if (level == cli::ConfigActionLevel::HITLESS) {
    return controller_->reloadConfig(hostInfo);
  }
  auto units = controller_->restart(level);
  controller_->waitForAgentConfigured(hostInfo);
  return units;
}

std::string AgentConfigDomain::actionName(cli::ConfigActionLevel level) const {
  return AgentServiceController::restartTypeName(level);
}

} // namespace facebook::fboss
