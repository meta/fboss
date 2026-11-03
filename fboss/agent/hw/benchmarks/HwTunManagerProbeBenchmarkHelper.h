/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

/**
 * @file HwTunManagerProbeBenchmarkHelper.h
 * @brief Performance benchmark helper for TunManager kernel interface probing
 * and cleanup operations
 * @author Ashutosh Grewal (agrewal@meta.com)
 */

#pragma once

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/SwAgentInitializer.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/AgentEnsemble.h"
#include "fboss/agent/test/TestUtils.h"

#include <folly/Benchmark.h>

namespace facebook::fboss {

// Forward declaration for friend access
void runTunManagerProbeBenchmark();

} // namespace facebook::fboss

/**
 * @def HW_TUN_MANAGER_BENCHMARK_FRIEND_TESTS
 * @brief Macro defining friend declarations for TunManager benchmarks
 *
 * This macro contains friend class declarations needed to access private
 * members of TunManager for benchmarking.
 */
#define HW_TUN_MANAGER_BENCHMARK_FRIEND_TESTS \
  friend void facebook::fboss::runTunManagerProbeBenchmark();

#include "fboss/agent/TunManager.h"

namespace facebook::fboss {

/**
 * Print comprehensive network debugging information including kernel
 * interfaces, switch state interfaces, probed routes, and probed source rules.
 */
inline void printAllNetworkDebugInfo(
    AgentEnsemble* ensemble,
    TunManager* tunMgr) {
  // Print kernel network information
  XLOG(INFO) << "=== Kernel Network Information ===";

  // IPv4 addresses - Shows all interfaces, their status, and addresses
  XLOG(INFO) << "--- IPv4 Addresses ---";
  std::string cmd = "ip -4 a";
  auto output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  // IPv6 addresses - Shows all interfaces, their status, and addresses
  XLOG(INFO) << "--- IPv6 Addresses ---";
  cmd = "ip -6 a";
  output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  // IPv4 route table (all tables)
  XLOG(INFO) << "--- IPv4 Routes (All Tables) ---";
  cmd = "ip route show table all";
  output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  // IPv6 route table (all tables)
  XLOG(INFO) << "--- IPv6 Routes (All Tables) ---";
  cmd = "ip -6 route show table all";
  output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  // IPv4 rule table
  XLOG(INFO) << "--- IPv4 Rules ---";
  cmd = "ip rule";
  output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  // IPv6 rule table
  XLOG(INFO) << "--- IPv6 Rules ---";
  cmd = "ip -6 rule";
  output = runShellCmd(cmd);
  XLOG(INFO) << "Command: " << cmd;
  XLOG(INFO) << "Output:\n" << output;

  XLOG(INFO) << "=== End Kernel Network Information ===";
}

/**
 * Print switch state interface details.
 */
inline void printSwitchStateInterfaces(
    AgentEnsemble* ensemble,
    TunManager* tunMgr) {
  XLOG(INFO) << "=== Switch State Interface Details ===";

  auto config = ensemble->getCurrentConfig();
  for (int i = 0; i < config.interfaces()->size(); i++) {
    std::vector<std::string> intfIPv4s;
    std::vector<std::string> intfIPv6s;

    for (int j = 0; j < config.interfaces()[i].ipAddresses()->size(); j++) {
      std::string intfIP = folly::to<std::string>(
          folly::IPAddress::createNetwork(
              config.interfaces()[i].ipAddresses()[j], -1, false)
              .first);

      if (intfIP.find("::") != std::string::npos) {
        intfIPv6s.push_back(intfIP);
      } else {
        intfIPv4s.push_back(intfIP);
      }
    }

    auto status = tunMgr->getIntfStatus(
        ensemble->getProgrammedState(),
        (InterfaceID)config.interfaces()[i].intfID().value());

    // Convert vectors to comma-separated strings for logging
    std::string ipv4List = folly::join(", ", intfIPv4s);
    std::string ipv6List = folly::join(", ", intfIPv6s);

    XLOG(INFO) << "Interface ID: "
               << (InterfaceID)config.interfaces()[i].intfID().value()
               << ", Status: " << (status ? "UP" : "DOWN") << ", IPv4: ["
               << ipv4List << "]" << ", IPv6: [" << ipv6List << "]";
  }

  XLOG(INFO) << "=== End Switch State Interface Details ===";
}

/*
 * Benchmark probing and cleanup of TUN interface data.
 * This benchmarks the time it takes to:
 * 1. Probe existing kernel TUN interface state
 * 2. Perform initial cleanup of probed data
 */
inline void runTunManagerProbeBenchmark() {
  folly::BenchmarkSuspender suspender;
  std::unique_ptr<AgentEnsemble> ensemble{};

  // Set flags to enable TUN interface support
  // These must be set before creating the AgentEnsemble
  FLAGS_tun_intf = true;
  FLAGS_cleanup_probed_kernel_data = true;

  AgentEnsembleSwitchConfigFn initialConfigFn =
      [](const AgentEnsemble& ensemble) {
        auto allPorts = ensemble.masterLogicalPortIds();
        CHECK_GT(allPorts.size(), 0);

        XLOG(INFO) << "Using " << allPorts.size()
                   << " ports for TUN Manager benchmark";

        return utility::onePortPerInterfaceConfig(ensemble.getSw(), allPorts);
      };

  ensemble =
      createAgentEnsemble(initialConfigFn, false /*disableLinkStateToggler*/);

  // Apply the config and wait for state updates
  auto config = initialConfigFn(*ensemble);
  ensemble->applyNewConfig(config);
  waitForStateUpdates(ensemble->getSw());

  // Initial sync for TunManager is async and happens on the background event
  // base Wait for pending operations there to complete.
  waitForBackgroundThread(ensemble->getSw());
  waitForStateUpdates(ensemble->getSw());

  auto tunMgr = ensemble->getSw()->getTunManager();

  // Ensure we have a valid netlink socket for operations
  // There could be a race condition where the interface is up, but the
  // socket is not created. So, checking for the socket existence.
  if (!tunMgr->isValidNlSocket()) {
    XLOG(ERR) << "No valid netlink socket available for benchmark";
    return;
  }

  printAllNetworkDebugInfo(ensemble.get(), tunMgr);
  printSwitchStateInterfaces(ensemble.get(), tunMgr);

  // Start measuring only the critical probe and cleanup operations
  suspender.dismiss();

  const auto startTs = std::chrono::steady_clock::now();

  {
    // Hold mutex_ so a concurrent sync() can't walk intfs_ mid-cleanup.
    std::lock_guard<std::mutex> lock(tunMgr->mutex_);
    tunMgr->probeDone_ = false;
    tunMgr->doProbe(lock);
    tunMgr->deleteAllProbedData();
  }

  const auto endTs = std::chrono::steady_clock::now();
  auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTs - startTs);
  XLOG(INFO) << "TUN Manager probe and cleanup operations took "
             << elapsedMs.count() << "ms to complete";

  // Stop measuring
  suspender.rehire();
  printSwitchStateInterfaces(ensemble.get(), tunMgr);
  printAllNetworkDebugInfo(ensemble.get(), tunMgr);
}

} // namespace facebook::fboss
