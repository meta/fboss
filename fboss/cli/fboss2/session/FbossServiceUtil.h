/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types.h"
#include "fboss/cli/fboss2/session/SystemdInterface.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

namespace facebook::fboss {

namespace cfg {
class AgentConfig;
} // namespace cfg

/**
 * FbossServiceUtil handles systemd service orchestration for FBOSS agents.
 *
 * Encapsulates all logic for restarting/reloading fboss services, including:
 * - Split mode detection (multi_switch flag from agent config)
 * - Monolithic mode (wedge_agent)
 * - Coldboot marker file creation
 * - Correct restart ordering (hw_agent before sw_agent)
 */
class FbossServiceUtil {
 public:
  // Construct from an AgentConfig: infers multi_switch mode and switch indices.
  explicit FbossServiceUtil(const cfg::AgentConfig& agentConfig);

  // Production constructor: creates its own SystemdInterface.
  FbossServiceUtil(std::vector<int> switchIndexes, bool multiSwitch);

  // Test constructor: accepts an injected SystemdInterface mock.
  FbossServiceUtil(
      std::vector<int> switchIndexes,
      bool multiSwitch,
      std::unique_ptr<SystemdInterface> systemd);

  virtual ~FbossServiceUtil() = default;

  // Restart services for the given service type and action level.
  // Returns the list of actual systemd service names that were restarted.
  virtual std::vector<std::string> restartService(
      cli::ServiceType service,
      cli::ConfigActionLevel level);

  // Waits until the agent at hostInfo can take config commands; systemd
  // reports it active well before that. Throws on timeout.
  void waitForAgentConfigured(
      const HostInfo& hostInfo,
      int maxWaitSeconds = 300,
      int pollIntervalMs = 1000);

  virtual bool isAgentConfigured(const HostInfo& hostInfo);

  // Reload config for a service without restart (for HITLESS changes).
  // Calls sync_reloadConfig() on the primary service (sw_agent in split mode,
  // wedge_agent in monolithic mode).
  // Returns the list of actual service names that were reloaded.
  virtual std::vector<std::string> reloadConfig(
      cli::ServiceType service,
      const HostInfo& hostInfo);

  // Returns true if running in split mode (multi_switch flag was set).
  virtual bool isSplitMode() const;

  // Returns the systemd service name for a given service type.
  static std::string getServiceName(cli::ServiceType service);

  // Human-readable restart kind for a (service, level) pair, e.g. the agent
  // restarts by "warmboot" or "coldboot", bgpd (no warmboot) by "restart".
  static std::string restartTypeName(
      cli::ServiceType service,
      cli::ConfigActionLevel level);

 private:
  std::unique_ptr<SystemdInterface> systemd_;
  std::vector<int> switchIndexes_;
  bool multiSwitch_;

  // Returns ordered list of services to restart (hw_agent first, sw_agent last)
  std::vector<std::string> getServicesToRestart(cli::ServiceType service) const;

  // Prefers classic unit names and falls back to the installed NetOS unit.
  std::string resolveSystemdServiceName(const std::string& service) const;

  // Shared per-service helper: restart and wait for active.
  void performRestartAndWait(const std::string& service);

  // Coldboot: create marker file, then restart and wait for each service.
  void performColdboot(const std::vector<std::string>& services);

  // Warmboot: restart and wait for each service (no marker file).
  void performWarmboot(const std::vector<std::string>& services);

  // Returns the coldboot marker file path for a given service name.
  static std::string getColdbootFileForService(const std::string& service);

  // Creates the coldboot marker file, handling permissions via sudo if needed.
  static void createColdbootMarkerFile(const std::string& coldbootFile);
};

} // namespace facebook::fboss
