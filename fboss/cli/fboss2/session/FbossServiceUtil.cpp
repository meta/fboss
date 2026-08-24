/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/session/FbossServiceUtil.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <glog/logging.h>
#include <chrono>
#include <stdexcept>
#include <thread>
#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/SwitchInfoUtils.h"
#include "fboss/agent/gen-cpp2/agent_config_types.h"
#include "fboss/agent/if/gen-cpp2/FbossCtrl.h"
#include "fboss/cli/fboss2/session/SystemdInterface.h"
#include "fboss/cli/fboss2/utils/CmdClientUtilsCommon.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"
#include "fboss/lib/CommonFileUtils.h"

namespace {
constexpr std::string_view kWedgeAgent = "wedge_agent";
constexpr std::string_view kSwAgent = "fboss_sw_agent";
constexpr std::string_view kHwAgentPrefix = "fboss_hw_agent@";
constexpr std::string_view kBgpd = "bgpd";
constexpr std::string_view kNetosSwAgent = "netos.service.fboss_sw_agent";
constexpr std::string_view kNetosBgpd = "netos.service.fboss_bgp";
constexpr std::string_view kNetosHwAgentPrefix =
    "netos.service.fboss_wedge_agent_";
constexpr std::string_view kSystemdServiceSuffix = ".service";
} // namespace

namespace facebook::fboss {

FbossServiceUtil::FbossServiceUtil(const cfg::AgentConfig& agentConfig)
    : systemd_(std::make_unique<SystemdInterface>()) {
  const auto& args = *agentConfig.defaultCommandLineArgs();
  // Parse multi_switch from the on-disk config rather than calling
  // utils::isMultiSwitchEnabled(hostInfo), so this works independently of
  // the agent's state without requiring a live thrift connection.
  multiSwitch_ =
      args.count("multi_switch") && args.at("multi_switch") == "true";
  for (const auto& [_, info] : getSwitchInfoFromConfig(&(*agentConfig.sw()))) {
    switchIndexes_.push_back(*info.switchIndex());
  }
  std::sort(switchIndexes_.begin(), switchIndexes_.end());
}

FbossServiceUtil::FbossServiceUtil(
    std::vector<int> switchIndexes,
    bool multiSwitch)
    : systemd_(std::make_unique<SystemdInterface>()),
      switchIndexes_(std::move(switchIndexes)),
      multiSwitch_(multiSwitch) {}

FbossServiceUtil::FbossServiceUtil(
    std::vector<int> switchIndexes,
    bool multiSwitch,
    std::unique_ptr<SystemdInterface> systemd,
    AgentDirectoryUtil dirUtil)
    : systemd_(std::move(systemd)),
      dirUtil_(std::move(dirUtil)),
      switchIndexes_(std::move(switchIndexes)),
      multiSwitch_(multiSwitch) {}

std::string FbossServiceUtil::getServiceName(cli::ServiceType service) {
  switch (service) {
    case cli::ServiceType::AGENT:
      return std::string(kWedgeAgent);
    case cli::ServiceType::BGP:
      return std::string(kBgpd);
  }
  throw std::runtime_error("Unknown service type");
}

bool FbossServiceUtil::isSplitMode() const {
  return multiSwitch_;
}

std::string FbossServiceUtil::getColdbootFileForService(
    const std::string& service) const {
  if (service == kSwAgent || service == kNetosSwAgent) {
    return dirUtil_.getSwColdBootOnceFile();
  } else if (service.find(kHwAgentPrefix) == 0) {
    std::string indexStr = service.substr(kHwAgentPrefix.size());
    int switchIndex = folly::to<int>(indexStr);
    return dirUtil_.getHwColdBootOnceFile(switchIndex);
  }
  if (service.find(kNetosHwAgentPrefix) == 0) {
    const auto switchIndex =
        folly::to<int>(service.substr(service.rfind('_') + 1));
    return dirUtil_.getHwColdBootOnceFile(switchIndex);
  }
  if (service == kWedgeAgent) {
    return dirUtil_.getColdBootOnceFile();
  }
  throw std::runtime_error(
      fmt::format("Unknown service type for coldboot: {}", service));
}

std::optional<std::string> FbossServiceUtil::getCanWarmBootFileForService(
    const std::string& service) const {
  if (service == kSwAgent || service == kNetosSwAgent ||
      service == kWedgeAgent) {
    return dirUtil_.getSwSwitchCanWarmBootFile();
  } else if (service.find(kHwAgentPrefix) == 0) {
    std::string indexStr = service.substr(kHwAgentPrefix.size());
    return dirUtil_.getHwSwitchCanWarmBootFile(folly::to<int>(indexStr));
  }
  if (service.find(kNetosHwAgentPrefix) == 0) {
    const auto switchIndex =
        folly::to<int>(service.substr(service.rfind('_') + 1));
    return dirUtil_.getHwSwitchCanWarmBootFile(switchIndex);
  }
  // bgpd and anything else outside the agent do not warm boot.
  return std::nullopt;
}

void FbossServiceUtil::createColdbootMarkerFile(
    const std::string& coldbootFile) {
  createDir(parentDirectoryTree(coldbootFile));
  touchFile(coldbootFile);
}

void FbossServiceUtil::performRestartAndWait(const std::string& service) {
  systemd_->restartService(service);
  systemd_->waitForServiceActive(service);
}

void FbossServiceUtil::performStopAllThenStart(
    const std::vector<std::string>& services,
    const std::function<void()>& beforeStart) {
  bool beforeStartDone = false;
  try {
    // Stop order is the reverse of start order: sw_agent first, hw_agents
    // after. Stopping a hw_agent while the sw_agent is still up looks to the
    // sw_agent like a hw_agent crash: HwSwitchConnectionStatusTable::
    // disconnected() writes sw_cold_boot_once and hw_cold_boot_once_<idx>
    // markers and forces both sides cold on the way back up.
    const std::vector<std::string> stopOrder(
        services.rbegin(), services.rend());
    for (const auto& service : stopOrder) {
      LOG(INFO) << "Stopping service: " << service;
      systemd_->stopService(service);
      systemd_->waitForServiceInactive(service);
    }

    beforeStart();
    beforeStartDone = true;

    // Start in forward order: hw_agents first, sw_agent last.
    for (const auto& service : services) {
      LOG(INFO) << "Starting service: " << service;
      systemd_->startService(service);
      systemd_->waitForServiceActive(service);
    }
  } catch (const std::exception& ex) {
    // systemd does not restart a unit we stopped explicitly, so propagating
    // straight out of here would leave the box with no agents at all and
    // nothing to recover them. Best effort: start everything back up first.
    LOG(ERROR) << "Restart sequence failed: " << ex.what()
               << ". Attempting to start all services back up";
    if (!beforeStartDone) {
      // Coldboot writes its marker files here, so a failure part way through
      // leaves some services marked and some not. Whatever comes back up may
      // not be the boot type that was asked for.
      LOG(ERROR)
          << "The pre-start step did not complete, so the services below may "
          << "not come up with the requested boot type. Verify the boot type "
          << "before relying on this switch.";
    }
    for (const auto& service : services) {
      try {
        systemd_->startService(service);
        systemd_->waitForServiceActive(service);
      } catch (const std::exception& recoveryEx) {
        LOG(ERROR) << "Failed to bring " << service
                   << " back up while recovering: " << recoveryEx.what();
      }
    }
    throw;
  }
}

std::vector<std::string> FbossServiceUtil::findServicesMissingWarmBootState(
    const std::vector<std::string>& services) const {
  std::vector<std::string> missing;
  for (const auto& service : services) {
    auto canWarmBootFile = getCanWarmBootFileForService(service);
    if (canWarmBootFile.has_value() && !checkFileExists(*canWarmBootFile)) {
      missing.push_back(service);
    }
  }
  return missing;
}

void FbossServiceUtil::logServicesMissingWarmBootState(
    const std::vector<std::string>& services) const {
  auto missing = findServicesMissingWarmBootState(services);
  if (!missing.empty()) {
    LOG(ERROR)
        << "No warm boot state was saved by: " << folly::join(", ", missing)
        << ". These services will cold boot on start. The usual cause is "
        << "systemd SIGKILLing them for exceeding TimeoutStopSec before their "
        << "graceful exit finished.";
  }
}

void FbossServiceUtil::performColdboot(
    const std::vector<std::string>& services) {
  auto markColdboot = [this, &services]() {
    for (const auto& service : services) {
      LOG(INFO) << "Marking coldboot for service: " << service;
      createColdbootMarkerFile(getColdbootFileForService(service));
    }
  };

  if (services.size() == 1) {
    // Nothing to order against, so keep the smaller restart window.
    markColdboot();
    performRestartAndWait(services[0]);
    return;
  }
  // Markers are written between the stop and start phases so a service's own
  // shutdown cannot clobber them.
  performStopAllThenStart(services, markColdboot);
}

void FbossServiceUtil::performWarmboot(
    const std::vector<std::string>& services) {
  if (services.size() == 1) {
    // Nothing to order against, so keep the smaller restart window.
    performRestartAndWait(services[0]);
    return;
  }
  performStopAllThenStart(services, [this, &services]() {
    logServicesMissingWarmBootState(services);
  });
}

std::string FbossServiceUtil::resolveSystemdServiceName(
    const std::string& service) const {
  if (!systemd_
           ->getMatchingServices(
               fmt::format("{}{}", service, kSystemdServiceSuffix))
           .empty()) {
    return service;
  }

  std::string netosService;
  if (service == kSwAgent) {
    netosService = kNetosSwAgent;
  } else if (service == kBgpd) {
    netosService = kNetosBgpd;
  }
  if (service.find(kHwAgentPrefix) == 0) {
    const auto switchIndex = service.substr(kHwAgentPrefix.size());
    // Native HW-agent units include a platform-specific vendor component
    // (for example, brcm or csco). Discover it from systemd so this OSS code
    // does not duplicate or depend on NetOS vendor metadata.
    netosService = fmt::format("{}*_{}", kNetosHwAgentPrefix, switchIndex);
  }
  if (netosService.empty()) {
    throw std::runtime_error(fmt::format("Unknown FBOSS service: {}", service));
  }

  auto matches = systemd_->getMatchingServices(
      fmt::format("{}{}", netosService, kSystemdServiceSuffix));
  if (matches.size() > 1) {
    throw std::runtime_error(
        fmt::format(
            "Found multiple systemd services matching {}: {}",
            netosService,
            folly::join(", ", matches)));
  }
  if (matches.size() == 1) {
    auto resolvedService = std::move(matches.front());
    if (folly::StringPiece(resolvedService).endsWith(kSystemdServiceSuffix)) {
      resolvedService.resize(
          resolvedService.size() - kSystemdServiceSuffix.size());
    }
    return resolvedService;
  }
  throw std::runtime_error(
      fmt::format(
          "No systemd service found for {} and {}", service, netosService));
}

std::vector<std::string> FbossServiceUtil::getServicesToRestart(
    cli::ServiceType service) const {
  switch (service) {
    case cli::ServiceType::AGENT: {
      std::vector<std::string> services;
      if (isSplitMode()) {
        LOG(INFO)
            << "Detected split mode (multi-switch enabled on running agent)";

        for (const auto& switchIndex : switchIndexes_) {
          services.emplace_back(resolveSystemdServiceName(
              fmt::format("{}{}", kHwAgentPrefix, switchIndex)));
        }
        LOG(INFO) << "Found " << services.size() << " hw_agent instances";

        // Add sw_agent last: it is started last, and stopped first.
        services.emplace_back(resolveSystemdServiceName(std::string(kSwAgent)));
      } else {
        LOG(INFO)
            << "Detected monolithic mode (multi-switch not enabled on running agent)";
        services.emplace_back(getServiceName(service));
      }
      return services;
    }
    case cli::ServiceType::BGP:
      // BGP++ is a single, mode-independent service.
      return {resolveSystemdServiceName(std::string(kBgpd))};
  }
  throw std::runtime_error("Unknown service type");
}

std::vector<std::string> FbossServiceUtil::reloadConfig(
    cli::ServiceType service,
    const HostInfo& hostInfo) {
  std::vector<std::string> reloadedServices;
  switch (service) {
    case cli::ServiceType::AGENT: {
      std::string serviceName =
          isSplitMode() ? std::string(kSwAgent) : getServiceName(service);

      LOG(INFO) << "Reloading config for " << serviceName;

      auto client = utils::createClient<
          apache::thrift::Client<facebook::fboss::FbossCtrl>>(hostInfo);
      client->sync_reloadConfig();

      LOG(INFO) << "Config reloaded for " << serviceName;
      reloadedServices.emplace_back(serviceName);
      break;
    }
    case cli::ServiceType::BGP:
      // bgpd has no hitless reloadConfig() RPC; config changes are applied by
      // restarting the service (SERVICE_RESTART), so this path is never taken.
      throw std::runtime_error(
          "bgpd does not support config reload; it must be restarted");
  }
  return reloadedServices;
}

std::string FbossServiceUtil::restartTypeName(
    cli::ServiceType service,
    cli::ConfigActionLevel level) {
  // The action level is generic; what it means is decided per service. Only
  // the agent distinguishes a warmboot from a coldboot -- bgpd has neither, so
  // every restart level is a plain restart for it.
  switch (level) {
    case cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART:
      return service == cli::ServiceType::AGENT ? "coldboot" : "restart";
    case cli::ConfigActionLevel::SERVICE_RESTART:
      return service == cli::ServiceType::AGENT ? "warmboot" : "restart";
    case cli::ConfigActionLevel::HITLESS:
      // Not expected: HITLESS is applied via reloadConfig(), not restart.
      return "reload";
  }
  return "restart";
}

// Same check as SwSwitch::isFullyConfigured().
bool FbossServiceUtil::isAgentConfigured(const HostInfo& hostInfo) {
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

void FbossServiceUtil::waitForAgentConfigured(
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

std::vector<std::string> FbossServiceUtil::restartService(
    cli::ServiceType service,
    cli::ConfigActionLevel level) {
  const std::string restartType = restartTypeName(service, level);

  auto services = getServicesToRestart(service);

  LOG(INFO) << "Restarting " << getServiceName(service) << " (" << restartType
            << ")...";

  // Only an agent coldboot needs the coldboot marker files; both paths
  // otherwise share the same stop-all-then-start-all sequence.
  if (service == cli::ServiceType::AGENT &&
      level == cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART) {
    performColdboot(services);
  } else {
    performWarmboot(services);
  }

  return services;
}

} // namespace facebook::fboss
