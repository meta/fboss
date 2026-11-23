/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/TestThriftHandler.h"

#include "fboss/agent/AgentConfig.h"
#include "fboss/agent/NdpCache.h"
#include "fboss/agent/NeighborUpdater.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/Utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>

using facebook::network::toIPAddress;

namespace {

// Systemd service names for test binaries.
const std::string kSwAgentTestService = "fboss_sw_agent_test";
const std::string kWedgeAgentTestService = "wedge_agent-test";
// hw_agent: prefix; append switchIndex for the full systemd unit name
// (e.g. "fboss_hw_agent_for_testing_0").
const std::string kHwAgentTestServicePrefix = "fboss_hw_agent_for_testing_";

const std::set<std::string> kServicesSupportingRestart() {
  static const std::set<std::string> servicesSupportingRestart = {
      kSwAgentTestService,
      kWedgeAgentTestService,
      "fsdb_service_for_testing",
      "qsfp_service_for_testing",
      "bgp",
  };

  return servicesSupportingRestart;
}

// The systemd service name differs from the actual binary/process name.
// pkill matches against the process name, so we need to map service names
// to the actual binary names.
const std::map<std::string, std::string>& kServiceToProcessName() {
  static const std::map<std::string, std::string> serviceToProcessName = {
      {kSwAgentTestService, kSwAgentTestService},
      {kWedgeAgentTestService, kWedgeAgentTestService},
      {"fsdb_service_for_testing", "fsdb"},
      {"qsfp_service_for_testing", "qsfp_service"},
      {"bgp", "bgp"},
  };

  return serviceToProcessName;
}

std::string trimWhitespace(std::string s) {
  auto first = std::find_if_not(
      s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); });
  auto last = std::find_if_not(
      s.rbegin(), s.rend(), [](unsigned char c) { return std::isspace(c); });
  if (first >= last.base()) {
    return "";
  }
  return std::string(first, last.base());
}

bool systemdUnitExists(const std::string& serviceName) {
  auto loadState = trimWhitespace(
      facebook::fboss::runShellCmd(
          folly::to<std::string>(
              "systemctl show -p LoadState --value ",
              serviceName,
              " 2>/dev/null || true")));
  return loadState == "loaded" || loadState == "masked";
}

std::string resolveServiceName(const std::string& logicalServiceName) {
  if (logicalServiceName != kSwAgentTestService) {
    return logicalServiceName;
  }
  if (systemdUnitExists(kSwAgentTestService)) {
    return kSwAgentTestService;
  }
  if (systemdUnitExists(kWedgeAgentTestService)) {
    XLOG(INFO) << "Resolved logical service " << logicalServiceName
               << " to installed unit " << kWedgeAgentTestService;
    return kWedgeAgentTestService;
  }
  return logicalServiceName;
}

std::set<int16_t> getSwitchIndicesImpl(facebook::fboss::SwSwitch* sw) {
  std::set<int16_t> switchIndices;
  for (const auto& [switchId, switchInfo] :
       sw->getSwitchInfoTable().getSwitchIdToSwitchInfo()) {
    switchIndices.insert(*switchInfo.switchIndex());
  }
  return switchIndices;
}

std::string makeAsyncSystemdUnitName(
    const std::string& serviceName,
    const std::string& action) {
  return folly::to<std::string>(
      serviceName,
      "_",
      action,
      "_",
      std::chrono::steady_clock::now().time_since_epoch().count());
}

void scheduleAsyncSystemdCommand(
    const std::string& unitName,
    const std::string& command,
    int32_t delayInSeconds = 0) {
  auto cmd = folly::to<std::string>(
      "systemd-run --collect --unit=",
      unitName,
      " --on-active=",
      delayInSeconds,
      "s /bin/bash -lc \"",
      command,
      "\"");
  auto output = facebook::fboss::runShellCmd(cmd);
  XLOG(INFO) << "Scheduled async systemd command: " << cmd
             << ", output: " << output;
}

} // namespace

namespace facebook::fboss {

TestThriftHandler::TestThriftHandler(SwSwitch* sw) : ThriftHandler(sw) {}

void TestThriftHandler::gracefullyRestartService(
    std::unique_ptr<std::string> serviceName,
    int32_t delayInSeconds) {
  XLOG(INFO) << __func__;

  if (delayInSeconds < 0) {
    throw std::invalid_argument("Restart delay cannot be negative");
  }

  if (kServicesSupportingRestart().find(*serviceName) ==
      kServicesSupportingRestart().end()) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Failed to restart gracefully. Unsupported service: ",
            *serviceName));
  }

  auto actualServiceName = resolveServiceName(*serviceName);

  // In multi-switch mode, match production ordering (AgentExecutor):
  //   Stop:  sw_agent first, then hw_agent(s)
  //   Start: hw_agent(s) first, then sw_agent
  // This handler runs inside sw_agent, so "systemctl stop sw_agent" kills
  // the process that spawned the shell. The systemd unit uses KillMode=process,
  // so only the main PID is killed — the child shell from popen() survives
  // and continues executing the remaining stop/start commands.
  if (FLAGS_multi_switch && actualServiceName == kSwAgentTestService) {
    auto switchIndices = getSwitchIndicesImpl(getSw());
    CHECK(!switchIndices.empty())
        << "No switch indices found in multi-switch mode";

    std::string cmd =
        folly::to<std::string>("systemctl stop ", actualServiceName);
    for (auto index : switchIndices) {
      cmd += folly::to<std::string>(
          " ; systemctl stop ", kHwAgentTestServicePrefix, index);
    }
    if (delayInSeconds > 0) {
      cmd += folly::to<std::string>(" ; sleep ", delayInSeconds);
    }
    for (auto index : switchIndices) {
      cmd += folly::to<std::string>(
          " ; systemctl start ", kHwAgentTestServicePrefix, index);
    }
    cmd += folly::to<std::string>(" ; systemctl start ", actualServiceName);
    auto unitName = makeAsyncSystemdUnitName(actualServiceName, "restart");
    scheduleAsyncSystemdCommand(unitName, cmd);
    return;
  }

  auto cmd = delayInSeconds == 0
      ? folly::to<std::string>("systemctl restart ", actualServiceName)
      : folly::to<std::string>(
            "systemctl stop ",
            actualServiceName,
            " ; sleep ",
            delayInSeconds,
            " ; systemctl start ",
            actualServiceName);
  auto unitName = makeAsyncSystemdUnitName(actualServiceName, "restart");
  scheduleAsyncSystemdCommand(unitName, cmd);
}

void TestThriftHandler::ungracefullyRestartService(
    std::unique_ptr<std::string> serviceName) {
  XLOG(INFO) << __func__;

  // QSFP ungraceful restart can be implemented by:
  // (A) pkill -9 qsfp
  // (B) touch /dev/shm/fboss/qsfp_service/cold_boot_once_qsfp_service,
  //     restart qsfp
  //
  // QSFP does not save any state for transceivers (non xphy platforms).
  // Thus a process crash (simulated by pkill) is able to warmboot and thus does
  // not flap ports.
  //
  // From the testing standpoint, simulating process crash is of interest, and
  // thus this handler is implemented using pkill -9.
  //
  // (B) is a planned cold boot restart and will flap ports, but is not
  // implemented here.
  if (kServicesSupportingRestart().find(*serviceName) ==
      kServicesSupportingRestart().end()) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Failed to restart ungracefully. Unsupported service: ",
            *serviceName));
  }

  auto actualServiceName = resolveServiceName(*serviceName);

  auto it = kServiceToProcessName().find(actualServiceName);
  if (it == kServiceToProcessName().end()) {
    throw std::runtime_error(
        folly::to<std::string>(
            "No process name mapping for service: ", actualServiceName));
  }

  // pkill -9 followed by systemctl restart: pkill alone won't restart the
  // service since the systemd unit has Restart=no.
  // Use ';' (not '&&') so that if pkill kills the current process
  // (e.g. agent), the orphaned shell still executes systemctl restart.
  std::string cmd;
  if (actualServiceName == kSwAgentTestService && FLAGS_multi_switch) {
    // In multi-switch mode, kill all hw_agents first, then restart all,
    // to simulate an atomic crash. No explicit warm boot cleanup needed.
    // Agents cold boot naturally after SIGKILL since they don't save warm boot
    // state on crash.
    auto switchIndices = getSwitchIndicesImpl(getSw());
    CHECK(!switchIndices.empty())
        << "No switch indices found in multi-switch mode";

    // Use "systemctl kill --signal=KILL" instead of pkill because the
    // binary name (e.g. fboss_hw_agent-sai_impl) differs from the service
    // name. Killing hw_agent causes sw_agent to exit on its own via
    // exit_for_any_hw_disconnect; we then restart hw_agent(s) first, then
    // sw_agent.
    for (auto index : switchIndices) {
      if (!cmd.empty()) {
        cmd += " ; ";
      }
      cmd += folly::to<std::string>(
          "systemctl kill --signal=KILL ", kHwAgentTestServicePrefix, index);
    }
    for (auto index : switchIndices) {
      cmd += folly::to<std::string>(
          " ; systemctl restart ", kHwAgentTestServicePrefix, index);
    }
    cmd += folly::to<std::string>(" ; systemctl restart ", actualServiceName);
  } else {
    cmd = folly::to<std::string>(
        "pkill -9 ", it->second, " ; systemctl restart ", actualServiceName);
  }
  XLOG(INFO) << "Ungraceful restart cmd: " << cmd;
  runShellCmd(cmd);
}

void TestThriftHandler::gracefullyRestartServiceWithDelay(
    std::unique_ptr<std::string> serviceName,
    int32_t delayInSeconds) {
  XLOG(INFO) << __func__;

  auto actualServiceName = resolveServiceName(*serviceName);
  const std::string& expected =
      FLAGS_multi_switch ? kSwAgentTestService : kWedgeAgentTestService;
  if (actualServiceName != expected) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Failed to restart with delay. Expected ",
            expected,
            " for current mode (multi_switch=",
            FLAGS_multi_switch,
            "), got: ",
            *serviceName,
            " (resolved to: ",
            actualServiceName,
            ")"));
  }

  // In multi-switch mode, unlike gracefullyRestartService /
  // ungracefullyRestartService we only stop sw_agent (not hw_agent). This is
  // sufficient to trigger DSF GR on remote subscribers: sw_agent owns the
  // FSDB publisher; when it dies, FSDB sends ALL_PUBLISHERS_GONE to
  // subscribers, which starts their dsf_gr_hold timer. hw_agent staying alive
  // is fine for this test since we only validate subscriber-side GR behavior,
  // not hw_agent warmboot/state-handoff. In mono mode, wedge_agent contains
  // both SwSwitch (FSDB publisher) and HwSwitch in one process — stopping it
  // achieves the same publisher-disconnect outcome.
  auto unitName = makeAsyncSystemdUnitName(actualServiceName, "delayed_start");
  // Schedule a delayed start in a separate transient unit, then return after
  // queueing a non-blocking stop for the current service.
  auto cmd = folly::to<std::string>("systemctl start ", actualServiceName);
  scheduleAsyncSystemdCommand(unitName, cmd, delayInSeconds);
  facebook::fboss::runShellCmd(
      folly::to<std::string>("systemctl --no-block stop ", actualServiceName));
}

void TestThriftHandler::addNeighbor(
    int32_t interfaceID,
    std::unique_ptr<BinaryAddress> ip,
    std::unique_ptr<std::string> mac,
    int32_t portID) {
  ensureConfigured(__func__);

  auto neighborIP = toIPAddress(*ip);

  if (!neighborIP.isV6()) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Thrift API addNeighbor supports IPv6 neighbors only. Neighbor to add:",
            neighborIP));
  }

  auto neighborMac = folly::MacAddress::tryFromString(*mac);
  if (!neighborMac.hasValue()) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Thrift API addNeighbor: invalid MAC address provided: ", *mac));
  }

  // To resolve a neighbor, mimic receiving NDP Response from neighbor
  getSw()->getNeighborUpdater()->receivedNdpMineForIntf(
      InterfaceID(interfaceID),
      neighborIP.asV6(),
      neighborMac.value(),
      PortDescriptor(PortID(portID)),
      ICMPv6Type::ICMPV6_TYPE_NDP_NEIGHBOR_ADVERTISEMENT,
      0 /* flags */);
}

void TestThriftHandler::setSwitchDrainState(
    cfg::SwitchDrainState switchDrainState) {}

void TestThriftHandler::setSelfHealingLagState(int32_t portId, bool enable) {
  ensureVoqOrFabric(__func__);
  const auto port = getSw()->getState()->getPorts()->getNodeIf(PortID(portId));
  if (!port) {
    throw FbossError("no such port ", portId);
  }
  if (port->getPortType() != cfg::PortType::INTERFACE_PORT) {
    throw FbossError(
        "Can set selfHealingLag on INTERFACE port only. Port: ",
        portId,
        " portType: ",
        apache::thrift::util::enumNameSafe(port->getPortType()));
  }

  if (port->getSelfHealingECMPLagEnable().has_value() &&
      port->getSelfHealingECMPLagEnable().value() == enable) {
    XLOG(DBG2) << __func__ << " port already in SelfHealingLagState"
               << (enable ? "ENABLED" : "DISABLED");
    return;
  }

  auto updateFn = [portId, enable](const std::shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getNodeIf(PortID(portId));
    std::shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setSelfHealingECMPLagEnable(enable);
    return newState;
  };
  getSw()->updateStateBlocking("set Port SelfHealingLagstate", updateFn);
}

void TestThriftHandler::setConditionalEntropyRehash(
    int32_t portId,
    bool enable) {
  ensureVoqOrFabric(__func__);
  const auto port = getSw()->getState()->getPorts()->getNodeIf(PortID(portId));
  if (!port) {
    throw FbossError("no such port ", portId);
  }
  if (port->getPortType() != cfg::PortType::INTERFACE_PORT) {
    throw FbossError(
        "Can set Conditional Entropy Rehash on INTERFACE port only. Port: ",
        portId,
        " portType: ",
        apache::thrift::util::enumNameSafe(port->getPortType()));
  }

  if (port->getConditionalEntropyRehash() == enable) {
    XLOG(DBG2) << __func__ << " port already in conditionalEntroRehash: "
               << (enable ? "ENABLED" : "DISABLED");
    return;
  }

  auto updateFn = [portId, enable](const std::shared_ptr<SwitchState>& state) {
    const auto oldPort = state->getPorts()->getNodeIf(PortID(portId));
    std::shared_ptr<SwitchState> newState{state};
    auto newPort = oldPort->modify(&newState);
    newPort->setConditionalEntropyRehash(enable);
    return newState;
  };
  getSw()->updateStateBlocking("set Port ConditionalEntropyHash", updateFn);
}

} // namespace facebook::fboss
