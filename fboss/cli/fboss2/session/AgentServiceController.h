// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/gen-cpp2/cli_metadata_types_fwd.h"
#include "fboss/cli/fboss2/session/SystemdInterface.h"
#include "fboss/cli/fboss2/session/SystemdSequencer.h"

namespace facebook::fboss {

class HostInfo;

/**
 * Restarts and reloads the FBOSS agent.
 *
 * Encapsulates everything specific to the agent's deployment:
 * - Split mode (fboss_sw_agent + fboss_hw_agent@N) or monolithic (wedge_agent)
 * - Coldboot marker file creation
 * - Correct restart ordering (hw_agent before sw_agent)
 */
class AgentServiceController {
 public:
  // Production constructor: creates its own SystemdInterface.
  AgentServiceController(std::vector<int> switchIndexes, bool multiSwitch);

  // Test constructor: accepts an injected SystemdInterface mock.
  AgentServiceController(
      std::vector<int> switchIndexes,
      bool multiSwitch,
      std::unique_ptr<SystemdInterface> systemd);

  virtual ~AgentServiceController() = default;

  // Restarts the agent: a coldboot for DISRUPTIVE_SERVICE_RESTART, otherwise
  // a warmboot. Returns the agent units that were restarted.
  virtual std::vector<std::string> restart(cli::ConfigActionLevel level);

  // Waits until the agent at hostInfo can take config commands; systemd
  // reports it active well before that. Throws on timeout.
  void waitForAgentConfigured(
      const HostInfo& hostInfo,
      int maxWaitSeconds = 300,
      int pollIntervalMs = 1000);

  // Reload the config without a restart (for HITLESS changes).
  // Calls sync_reloadConfig() on the primary service (sw_agent in split mode,
  // wedge_agent in monolithic mode).
  // Returns the list of actual service names that were reloaded.
  virtual std::vector<std::string> reloadConfig(const HostInfo& hostInfo);

  // "coldboot", "warmboot" or "reload".
  static std::string restartTypeName(cli::ConfigActionLevel level);

 protected:
  // One poll of waitForAgentConfigured(); virtual so tests can stand in for
  // the agent.
  virtual bool isAgentConfigured(const HostInfo& hostInfo);

  // Returns true if running in split mode (multi_switch flag was set).
  virtual bool isSplitMode() const;

 private:
  SystemdSequencer sequencer_;
  std::vector<int> switchIndexes_;
  bool multiSwitch_;

  // Returns ordered list of services to restart (hw_agent first, sw_agent last)
  std::vector<std::string> getServicesToRestart() const;

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
