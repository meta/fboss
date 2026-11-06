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

FbossServiceUtil::FbossServiceUtil(
    std::vector<int> switchIndexes,
    bool multiSwitch)
    : systemd_(std::make_unique<SystemdInterface>()),
      switchIndexes_(std::move(switchIndexes)),
      multiSwitch_(multiSwitch) {}

FbossServiceUtil::FbossServiceUtil(
    std::vector<int> switchIndexes,
    bool multiSwitch,
    std::unique_ptr<SystemdInterface> systemd)
    : systemd_(std::move(systemd)),
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
    const std::string& service) {
  AgentDirectoryUtil dirUtil;

  if (service == kSwAgent || service == kNetosSwAgent) {
    return dirUtil.getSwColdBootOnceFile();
  } else if (service.find(kHwAgentPrefix) == 0) {
    std::string indexStr = service.substr(kHwAgentPrefix.size());
    int switchIndex = folly::to<int>(indexStr);
    return dirUtil.getHwColdBootOnceFile(switchIndex);
  }
  if (service.find(kNetosHwAgentPrefix) == 0) {
    const auto switchIndex =
        folly::to<int>(service.substr(service.rfind('_') + 1));
    return dirUtil.getHwColdBootOnceFile(switchIndex);
  }
  if (service == kWedgeAgent) {
    return dirUtil.getColdBootOnceFile();
  }
  throw std::runtime_error(
      fmt::format("Unknown service type for coldboot: {}", service));
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

void FbossServiceUtil::performColdboot(
    const std::vector<std::string>& services) {
  for (const auto& service : services) {
    LOG(INFO) << "Performing coldboot for service: " << service;
    createColdbootMarkerFile(getColdbootFileForService(service));
    performRestartAndWait(service);
    LOG(INFO) << "Coldboot completed for service: " << service;
  }
}

void FbossServiceUtil::performWarmboot(
    const std::vector<std::string>& services) {
  for (const auto& service : services) {
    LOG(INFO) << "Performing warmboot for service: " << service;
    performRestartAndWait(service);
    LOG(INFO) << "Warmboot completed for service: " << service;
  }
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

        // Add sw_agent last so hw_agent restarts first
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

  // Only an agent coldboot needs the coldboot marker files; every other
  // (service, level) pair is the same plain restart-and-wait sequence.
  if (service == cli::ServiceType::AGENT &&
      level == cli::ConfigActionLevel::DISRUPTIVE_SERVICE_RESTART) {
    performColdboot(services);
  } else {
    performWarmboot(services);
  }

  return services;
}

} // namespace facebook::fboss
