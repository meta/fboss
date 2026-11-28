// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/cli/fboss2/session/AgentServiceController.h"

#include <fmt/format.h>
#include <folly/Conv.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <chrono>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrlAsyncClient.h"
#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/SystemdInterface.h"
#include "fboss/cli/fboss2/session/SystemdSequencer.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/lib/CommonFileUtils.h"

namespace {
constexpr std::string_view kWedgeAgent = "wedge_agent";
constexpr std::string_view kSwAgent = "fboss_sw_agent";
constexpr std::string_view kHwAgentPrefix = "fboss_hw_agent@";
constexpr std::string_view kNetosSwAgent = "netos.service.fboss_sw_agent";
constexpr std::string_view kNetosHwAgentPrefix =
    "netos.service.fboss_wedge_agent_";

// Whether a systemd unit is the sw_agent, under its classic or NetOS name.
bool isSwAgentUnit(std::string_view service) {
  return service == kSwAgent || service == kNetosSwAgent;
}

// The switch index of a hw_agent unit, under its classic
// (fboss_hw_agent@<N>) or NetOS (netos.service.fboss_wedge_agent_<vendor>_<N>)
// name; nullopt for any other unit.
std::optional<int> hwAgentSwitchIndex(std::string_view service) {
  if (service.starts_with(kHwAgentPrefix)) {
    return folly::to<int>(service.substr(kHwAgentPrefix.size()));
  }
  if (service.starts_with(kNetosHwAgentPrefix)) {
    return folly::to<int>(service.substr(service.rfind('_') + 1));
  }
  return std::nullopt;
}
} // namespace

namespace facebook::fboss {

AgentServiceController::AgentServiceController(
    std::vector<int> switchIndexes,
    bool multiSwitch)
    : switchIndexes_(std::move(switchIndexes)), multiSwitch_(multiSwitch) {}

AgentServiceController::AgentServiceController(
    std::vector<int> switchIndexes,
    bool multiSwitch,
    std::unique_ptr<SystemdInterface> systemd)
    : sequencer_(std::move(systemd)),
      switchIndexes_(std::move(switchIndexes)),
      multiSwitch_(multiSwitch) {}

bool AgentServiceController::isSplitMode() const {
  return multiSwitch_;
}

std::string AgentServiceController::getColdbootFileForService(
    const std::string& service) {
  AgentDirectoryUtil dirUtil;

  if (isSwAgentUnit(service)) {
    return dirUtil.getSwColdBootOnceFile();
  }
  if (const auto switchIndex = hwAgentSwitchIndex(service)) {
    return dirUtil.getHwColdBootOnceFile(*switchIndex);
  }
  if (service == kWedgeAgent) {
    return dirUtil.getColdBootOnceFile();
  }
  throw std::runtime_error(
      fmt::format("Unknown service type for coldboot: {}", service));
}

void AgentServiceController::createColdbootMarkerFile(
    const std::string& coldbootFile) {
  createDir(parentDirectoryTree(coldbootFile));
  touchFile(coldbootFile);
}

void AgentServiceController::performColdboot(
    const std::vector<std::string>& services) {
  for (const auto& service : services) {
    XLOG(DBG1) << "Performing coldboot for service: " << service;
    createColdbootMarkerFile(getColdbootFileForService(service));
    sequencer_.restartAndWait(service);
    XLOG(DBG1) << "Coldboot completed for service: " << service;
  }
}

void AgentServiceController::performWarmboot(
    const std::vector<std::string>& services) {
  for (const auto& service : services) {
    XLOG(DBG1) << "Performing warmboot for service: " << service;
    sequencer_.restartAndWait(service);
    XLOG(DBG1) << "Warmboot completed for service: " << service;
  }
}

std::vector<std::string> AgentServiceController::getServicesToRestart() const {
  if (!isSplitMode()) {
    XLOG(DBG1)
        << "Detected monolithic mode (multi-switch not enabled on running agent)";
    return {std::string(kWedgeAgent)};
  }
  XLOG(DBG1) << "Detected split mode (multi-switch enabled on running agent)";

  std::vector<std::string> services;
  services.reserve(switchIndexes_.size() + 1);
  for (const auto& switchIndex : switchIndexes_) {
    // Native HW-agent units include a platform-specific vendor component
    // (for example, brcm or csco). Discover it from systemd so this OSS code
    // does not duplicate or depend on NetOS vendor metadata.
    services.emplace_back(sequencer_.resolveUnit(
        fmt::format("{}{}", kHwAgentPrefix, switchIndex),
        fmt::format("{}*_{}", kNetosHwAgentPrefix, switchIndex)));
  }
  XLOG(DBG1) << "Found " << services.size() << " hw_agent instances";

  // Add sw_agent last so hw_agent restarts first
  services.emplace_back(sequencer_.resolveUnit(
      std::string(kSwAgent), std::string(kNetosSwAgent)));
  return services;
}

std::vector<std::string> AgentServiceController::reloadConfig(
    const HostInfo& hostInfo) {
  std::string serviceName(isSplitMode() ? kSwAgent : kWedgeAgent);

  XLOG(DBG1) << "Reloading config for " << serviceName;

  auto client =
      utils::createClient<apache::thrift::Client<facebook::fboss::FbossCtrl>>(
          hostInfo);
  client->sync_reloadConfig();

  XLOG(DBG1) << "Config reloaded for " << serviceName;
  return {serviceName};
}

std::string AgentServiceController::restartTypeName(
    cli::ConfigActionLevel level) {
  switch (level) {
    case cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART:
      return "coldboot";
    case cli::ConfigActionLevel::SERVICE_RESTART:
      return "warmboot";
    case cli::ConfigActionLevel::HITLESS:
      return "reload";
  }
  return "restart";
}

// Same check as SwSwitch::isFullyConfigured().
bool AgentServiceController::isAgentConfigured(const HostInfo& hostInfo) {
  try {
    auto client =
        utils::createClient<apache::thrift::Client<FbossCtrl>>(hostInfo);
    auto runState = client->sync_getSwitchRunState();
    return runState >= SwitchRunState::CONFIGURED &&
        runState != SwitchRunState::EXITING;
  } catch (const std::exception&) {
    // Expected while the agent is still starting up.
    return false;
  }
}

void AgentServiceController::waitForAgentConfigured(
    const HostInfo& hostInfo,
    int maxWaitSeconds,
    int pollIntervalMs) {
  int waitedMs = 0;
  while (waitedMs < maxWaitSeconds * 1000) {
    if (isAgentConfigured(hostInfo)) {
      return;
    }
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    waitedMs += pollIntervalMs;
  }
  throw std::runtime_error(
      fmt::format(
          "Agent did not become configured within {} seconds", maxWaitSeconds));
}

std::vector<std::string> AgentServiceController::restart(
    cli::ConfigActionLevel level) {
  auto services = getServicesToRestart();

  XLOG(DBG1) << "Restarting " << folly::join(", ", services) << " ("
             << restartTypeName(level) << ")...";

  if (level == cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART) {
    performColdboot(services);
  } else {
    performWarmboot(services);
  }
  return services;
}

} // namespace facebook::fboss
