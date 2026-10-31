/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/CmdConfigReload.h"

#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/session/FbossServiceUtil.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/String.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace facebook::fboss {

BootTypeArg::BootTypeArg() : BaseObjectArgType() {}

BootTypeArg::BootTypeArg(std::vector<std::string> v) {
  if (v.empty()) {
    level_ = cli::ConfigActionLevel::HITLESS;
    return;
  }
  if (v.size() != 1) {
    throw std::invalid_argument(
        "Expected at most one boot type (hitless, warmboot, or coldboot)");
  }
  std::string mode = v[0];
  folly::toLowerAscii(mode);
  if (mode == "hitless") {
    level_ = cli::ConfigActionLevel::HITLESS;
  } else if (mode == "warmboot") {
    level_ = cli::ConfigActionLevel::SERVICE_RESTART;
  } else if (mode == "coldboot") {
    level_ = cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART;
  } else {
    throw std::invalid_argument(
        "Invalid boot type '" + v[0] +
        "'. Expected 'hitless', 'warmboot', or 'coldboot'");
  }
  data_.push_back(v[0]);
}

namespace {

// Build a comma-separated list of "<service> (<bootType>)" entries used in
// the success message for warmboot / coldboot reloads.
std::string formatRestartedServices(
    const std::vector<std::string>& services,
    folly::StringPiece bootType) {
  std::vector<std::string> labeled;
  labeled.reserve(services.size());
  for (const auto& service : services) {
    labeled.push_back(fmt::format("{} ({})", service, bootType));
  }
  return folly::join(", ", labeled);
}

// Restart agent services locally (warmboot or coldboot) by constructing a
// FbossServiceUtil directly from the on-disk agent config. We deserialize
// the JSON straight into cfg::AgentConfig (mirroring ConfigSession) rather
// than going through the AgentConfig wrapper, to avoid pulling
// load_agent_config into fboss2-config-lib. This also keeps the command free of
// session side effects.
std::string performLocalRestart(cli::ConfigActionLevel level) {
  // Derive /etc/coop/agent.conf via AgentDirectoryUtil so the
  // FBOSS_CONFIG_BASE_DIR test override works. getConfigDirectory() returns
  // <base>/agent; the agent reads its config from <base>/agent.conf (one level
  // up).
  AgentDirectoryUtil dirUtil;
  std::string configPath = std::filesystem::path(dirUtil.getConfigDirectory())
                               .parent_path()
                               .string() +
      "/agent.conf";

  std::string configJson;
  if (!folly::readFile(configPath.c_str(), configJson)) {
    throw std::runtime_error(
        fmt::format("Failed to read agent config file: {}", configPath));
  }
  cfg::AgentConfig agentConfig;
  apache::thrift::SimpleJSONSerializer::deserialize<cfg::AgentConfig>(
      configJson, agentConfig);

  // FbossServiceUtil infers multi_switch mode and switch indices from the
  // config; restartService() waits for each service to become active and
  // throws std::runtime_error on timeout.
  FbossServiceUtil serviceUtil(agentConfig);
  auto restarted = serviceUtil.restartService(cli::ServiceType::AGENT, level);

  folly::StringPiece bootType =
      (level == cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART)
      ? "coldboot"
      : "warmboot";
  return fmt::format(
      "Config reloaded successfully via {} restart.",
      formatRestartedServices(restarted, bootType));
}

} // namespace

CmdConfigReloadTraits::RetType CmdConfigReload::queryClient(
    const HostInfo& hostInfo,
    const BootTypeArg& bootType) {
  if (bootType.level() == cli::ConfigActionLevel::HITLESS) {
    auto client =
        utils::createClient<facebook::fboss::FbossCtrlAsyncClient>(hostInfo);
    client->sync_reloadConfig();
    return "Config reloaded successfully";
  }

  // warmboot / coldboot dispatch to local systemctl, so reject non-local
  // hosts to avoid restarting the wrong machine.
  if (!hostInfo.isLocalHost()) {
    throw std::invalid_argument(
        fmt::format(
            "config reload {} is local-only; remove --host or run on the switch.",
            (bootType.level() ==
             cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART)
                ? "coldboot"
                : "warmboot"));
  }

  // Warn (don't refuse) if the operator has an active session on disk; this
  // command operates on /etc/coop/agent.conf only and will not apply session
  // changes. Use the static path getter to avoid creating a singleton.
  std::error_code ec;
  if (std::filesystem::exists(
          ConfigSession::getSessionConfigPathStatic(), ec)) {
    std::cerr << "Warning: an active config session exists in ~/.fboss2/; this "
              << "command operates on /etc/coop/agent.conf only and will not "
              << "apply session changes." << std::endl;
  }

  return performLocalRestart(bootType.level());
}

void CmdConfigReload::printOutput(const RetType& logMsg) {
  std::cout << logMsg << std::endl;
}

// Explicit template instantiation
template void CmdHandler<CmdConfigReload, CmdConfigReloadTraits>::run();

} // namespace facebook::fboss
