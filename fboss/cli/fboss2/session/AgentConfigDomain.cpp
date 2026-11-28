// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/AgentConfigDomain.h"

#include <fmt/format.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <string_view>
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
// Needed by the explicit instantiation below, which include-cleaner does not
// count as a use.
#include "fboss/cli/fboss2/session/TypedConfigDomain-defs.h" // NOLINT(misc-include-cleaner)
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
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

} // namespace facebook::fboss
