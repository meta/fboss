/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/sai/switch/SaiHandler.h"

#include "fboss/agent/ThriftHandlerUtils.h"
#include "fboss/agent/hw/sai/switch/SaiSwitch.h"

#include <folly/logging/xlog.h>
#include "fboss/lib/LogThriftCall.h"

using std::chrono::seconds;
using std::chrono::system_clock;

namespace facebook::fboss {

void SaiHandler::getCurrentHwStateJSON(
    std::string& ret,
    std::unique_ptr<std::string> path) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);

  if (path) {
    ret = utility::getCurrentStateJSONForPathHelper(
        *path, hw_->getProgrammedState());
  }
}

void SaiHandler::getCurrentHwStateJSONForPaths(
    std::map<std::string, std::string>& pathToState,
    std::unique_ptr<std::vector<std::string>> paths) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);

  for (auto& path : *paths) {
    pathToState[path] = utility::getCurrentStateJSONForPathHelper(
        path, hw_->getProgrammedState());
  }
}

SaiHandler::SaiHandler(SaiSwitch* hw)
    : hw_(hw), diagShell_(hw), diagCmdServer_(hw, &diagShell_) {}

apache::thrift::ResponseAndServerStream<std::string, std::string>
SaiHandler::startDiagShell() {
  XLOG(DBG2) << "New diag shell session connecting";
  if (!hw_->isFullyInitialized()) {
    throw FbossError("switch is still initializing or is exiting ");
  }
  diagShell_.tryConnect();
  auto streamAndPublisher =
      apache::thrift::ServerStream<std::string>::createPublisher([this]() {
        diagShell_.markResetPublisher();
        XLOG(DBG2) << "Diag shell session disconnected";
      });

  std::string firstPrompt =
      diagShell_.start(std::move(streamAndPublisher.second));
  return {firstPrompt, std::move(streamAndPublisher.first)};
}

void SaiHandler::produceDiagShellInput(
    std::unique_ptr<std::string> input,
    std::unique_ptr<ClientInformation> client) {
  diagShell_.consumeInput(std::move(input), std::move(client));
}

void SaiHandler::diagCmd(
    fbstring& result,
    std::unique_ptr<fbstring> cmd,
    std::unique_ptr<ClientInformation> client,
    int16_t /* unused */,
    bool /* unused */) {
  auto log = LOG_THRIFT_CALL(WARN, *cmd);
  hw_->ensureConfigured(__func__);
  {
    const std::lock_guard<std::mutex> lock(diagCmdLock_);
    result = diagCmdServer_.diagCmd(std::move(cmd), std::move(client));
  }
}

SwitchRunState SaiHandler::getHwSwitchRunState() {
  auto log = LOG_THRIFT_CALL(DBG1);
  return hw_->getRunState();
}

void SaiHandler::getHwFabricReachability(
    std::map<::std::int64_t, ::facebook::fboss::FabricEndpoint>& reachability) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureVoqOrFabric(__func__);
  auto reachabilityInfo = hw_->getFabricConnectivity();
  for (auto&& entry : reachabilityInfo) {
    reachability.insert(std::move(entry));
  }
}

void SaiHandler::getHwFabricConnectivity(
    std::map<::std::string, ::facebook::fboss::FabricEndpoint>& connectivity) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureVoqOrFabric(__func__);
  auto connectivityInfo = hw_->getFabricConnectivity();
  for (auto& entry : connectivityInfo) {
    auto port = hw_->getProgrammedState()->getPorts()->getNodeIf(entry.first);
    if (port) {
      connectivity.insert({port->getName(), entry.second});
    }
  }
}

void SaiHandler::getHwSwitchReachability(
    std::map<::std::string, std::vector<::std::string>>& reachability,
    std::unique_ptr<::std::vector<::std::string>> switchNames) {
  auto log = LOG_THRIFT_CALL(DBG1, *switchNames);
  hw_->ensureVoqOrFabric(__func__);
  if (switchNames->empty()) {
    throw FbossError("Empty switch name list input for getSwitchReachability.");
  }
  std::unordered_set<std::string> switchNameSet{
      switchNames->begin(), switchNames->end()};

  auto state = hw_->getProgrammedState();
  for (const auto& [_, dsfNodes] : std::as_const(*state->getDsfNodes())) {
    for (const auto& [_, node] : std::as_const(*dsfNodes)) {
      if (std::find(
              switchNameSet.begin(), switchNameSet.end(), node->getName()) !=
          switchNameSet.end()) {
        std::vector<std::string> reachablePorts;
        for (const auto& portId :
             hw_->getSwitchReachability(node->getSwitchId())) {
          auto port = state->getPorts()->getNodeIf(portId);
          if (port) {
            reachablePorts.push_back(port->getName());
          } else {
            XLOG(ERR) << "Port " << portId
                      << " in reachability matrix is not found in state";
          }
        }
        reachability.insert({node->getName(), std::move(reachablePorts)});
      }
    }
  }
  return;
}

void SaiHandler::clearHwPortStats(std::unique_ptr<std::vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL(DBG1, *ports);
  hw_->ensureConfigured(__func__);
  utility::clearPortStats(hw_, std::move(ports), hw_->getProgrammedState());
}

void SaiHandler::clearAllHwPortStats() {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  auto allPorts = std::make_unique<std::vector<int32_t>>();
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      allPorts->push_back(port.second->getID());
    }
  }
  clearHwPortStats(std::move(allPorts));
}

void SaiHandler::clearInterfacePhyCounters(
    std::unique_ptr<std::vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL(DBG1, *ports);
  hw_->ensureConfigured(__func__);
  hw_->clearInterfacePhyCounters(ports);
}

void SaiHandler::getHwL2Table(std::vector<L2EntryThrift>& l2Table) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  hw_->fetchL2Table(&l2Table);
}

void SaiHandler::getVirtualDeviceToConnectionGroups(
    std::map<
        int64_t,
        std::map<int64_t, std::vector<facebook::fboss::RemoteEndpoint>>>&
        virtualDevice2ConnectionGroups) {
  auto log = LOG_THRIFT_CALL(DBG1);
  auto deviceToConnectionGroups =
      hw_->getVirtualDeviceToRemoteConnectionGroups();
  // Convert to format required by thrift handler
  for (const auto& [vid, connGroups] : deviceToConnectionGroups) {
    auto& outVidConnGroups = virtualDevice2ConnectionGroups[vid];
    for (const auto& [groupSize, group] : connGroups) {
      auto& outVidGroup = outVidConnGroups[groupSize];
      std::for_each(
          group.begin(),
          group.end(),
          [&outVidGroup](const auto& remoteEndpoint) {
            outVidGroup.push_back(remoteEndpoint);
          });
    }
  }
}

void SaiHandler::listHwObjects(
    std::string& out,
    std::unique_ptr<std::vector<HwObjectType>> hwObjects,
    bool cached) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  out = hw_->listObjects(*hwObjects, cached);
}

BootType SaiHandler::getBootType() {
  auto log = LOG_THRIFT_CALL(DBG1);
  // Known from init, and SwSwitch asks for it before the HwSwitch is
  // configured.
  return hw_->getBootType();
}

void SaiHandler::getInterfacePrbsState(
    prbs::InterfacePrbsState& prbsState,
    std::unique_ptr<std::string> interface,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  auto port = swState->getPorts()->getPort(*interface);
  if (port->getPortType() == cfg::PortType::INTERFACE_PORT ||
      port->getPortType() == cfg::PortType::FABRIC_PORT ||
      port->getPortType() == cfg::PortType::MANAGEMENT_PORT ||
      port->getPortType() == cfg::PortType::HYPER_PORT_MEMBER) {
    prbsState = hw_->getPortPrbsState(port->getID());
  } else {
    throw FbossError(
        "Cannot get PRBS state for interface " + *interface +
        " with unsupported port type " +
        apache::thrift::util::enumNameSafe(port->getPortType()));
  }
}

void SaiHandler::getAllInterfacePrbsStates(
    std::map<std::string, prbs::InterfacePrbsState>& prbsStates,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
          port.second->getPortType() == cfg::PortType::FABRIC_PORT ||
          port.second->getPortType() == cfg::PortType::MANAGEMENT_PORT ||
          port.second->getPortType() == cfg::PortType::HYPER_PORT_MEMBER) {
        prbsStates[port.second->getName()] =
            hw_->getPortPrbsState(port.second->getID());
      }
    }
  }
}

void SaiHandler::getInterfacePrbsStats(
    phy::PrbsStats& prbsStats,
    std::unique_ptr<std::string> interface,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  auto port = swState->getPorts()->getPort(*interface);
  if (port->getPortType() == cfg::PortType::INTERFACE_PORT ||
      port->getPortType() == cfg::PortType::FABRIC_PORT ||
      port->getPortType() == cfg::PortType::MANAGEMENT_PORT ||
      port->getPortType() == cfg::PortType::HYPER_PORT_MEMBER) {
    auto prbsState = hw_->getPortPrbsState(port->getID());
    auto generatorEnabled = prbsState.generatorEnabled();
    auto checkerEnabled = prbsState.checkerEnabled();
    if (generatorEnabled && checkerEnabled) {
      auto prbsEnabled = (generatorEnabled.value() && checkerEnabled.value());
      if (prbsEnabled) {
        auto asicPrbsStats = hw_->getPortAsicPrbsStats(port->getID());
        prbsStats.portId() = port->getID();
        prbsStats.component() = phy::PortComponent::ASIC;
        for (const auto& lane : asicPrbsStats) {
          prbsStats.laneStats()->push_back(lane);
          auto timeCollected = lane.timeCollected().value();
          // Store most recent timeCollected across all lane stats
          if (timeCollected > prbsStats.timeCollected()) {
            prbsStats.timeCollected() = timeCollected;
          }
        }
      } else {
        throw FbossError(
            "Cannot get PRBS stats for interface " + *interface +
            " with PRBS disabled");
      }
    } else {
      throw FbossError(
          "Cannot get PRBS stats for interface " + *interface +
          " with no PRBS state");
    }
  } else {
    throw FbossError(
        "Cannot get PRBS stats for interface " + *interface +
        " with unsupported port type " +
        apache::thrift::util::enumNameSafe(port->getPortType()));
  }
}

void SaiHandler::getAllInterfacePrbsStats(
    std::map<std::string, phy::PrbsStats>& prbsStats,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  for (const auto& portMap : std::as_const(*(swState->getPorts()))) {
    for (const auto& port : std::as_const(*portMap.second)) {
      if (port.second->getPortType() == cfg::PortType::INTERFACE_PORT ||
          port.second->getPortType() == cfg::PortType::FABRIC_PORT ||
          port.second->getPortType() == cfg::PortType::MANAGEMENT_PORT ||
          port.second->getPortType() == cfg::PortType::HYPER_PORT_MEMBER) {
        auto prbsState = hw_->getPortPrbsState(port.second->getID());
        auto generatorEnabled = prbsState.generatorEnabled();
        auto checkerEnabled = prbsState.checkerEnabled();
        if (generatorEnabled && checkerEnabled) {
          auto prbsEnabled =
              (generatorEnabled.value() && checkerEnabled.value());
          if (prbsEnabled) {
            phy::PrbsStats prbsStatsEntry;
            auto asicPrbsStats =
                hw_->getPortAsicPrbsStats(port.second->getID());
            prbsStatsEntry.portId() = port.second->getID();
            prbsStatsEntry.component() = phy::PortComponent::ASIC;
            for (const auto& lane : asicPrbsStats) {
              prbsStatsEntry.laneStats()->push_back(lane);
              auto timeCollected = lane.timeCollected().value();
              // Store most recent timeCollected across all lane stats
              if (timeCollected > prbsStatsEntry.timeCollected()) {
                prbsStatsEntry.timeCollected() = timeCollected;
              }
            }
            prbsStats[port.second->getName()] = prbsStatsEntry;
          }
        }
      }
    }
  }
}

void SaiHandler::clearInterfacePrbsStats(
    std::unique_ptr<std::string> interface,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  auto port = swState->getPorts()->getPort(*interface);
  auto prbsState = hw_->getPortPrbsState(port->getID());
  auto generatorEnabled = prbsState.generatorEnabled();
  auto checkerEnabled = prbsState.checkerEnabled();
  if (generatorEnabled && checkerEnabled) {
    auto prbsEnabled = (generatorEnabled.value() && checkerEnabled.value());
    if (!prbsEnabled) {
      throw FbossError(
          "Cannot clear PRBS stats for interface: " + *interface +
          " with PRBS disabled");
    }
  } else {
    throw FbossError(
        "Cannot clear PRBS stats for interface: " + *interface +
        " with no PRBS state");
  }
  hw_->clearPortAsicPrbsStats(port->getID());
}

void SaiHandler::bulkClearInterfacePrbsStats(
    std::unique_ptr<std::vector<std::string>> interfaces,
    phy::PortComponent component) {
  auto log = LOG_THRIFT_CALL(DBG1);
  if (component != phy::PortComponent::ASIC) {
    throw FbossError("Unsupported component");
  }
  hw_->ensureConfigured(__func__);
  std::shared_ptr<SwitchState> swState = hw_->getProgrammedState();
  for (const auto& interface : *interfaces) {
    auto port = swState->getPorts()->getPort(interface);
    auto prbsState = hw_->getPortPrbsState(port->getID());
    auto generatorEnabled = prbsState.generatorEnabled();
    auto checkerEnabled = prbsState.checkerEnabled();
    if (generatorEnabled && checkerEnabled) {
      auto prbsEnabled = (generatorEnabled.value() && checkerEnabled.value());
      if (!prbsEnabled) {
        throw FbossError(
            "Cannot clear PRBS stats for interface: " + interface +
            " with PRBS disabled");
      }
    }
  }
  for (const auto& interface : *interfaces) {
    auto port = swState->getPorts()->getPort(interface);
    hw_->clearPortAsicPrbsStats(port->getID());
  }
}

void SaiHandler::getAllHwFirmwareInfo(
    std::vector<FirmwareInfo>& firmwareInfoList) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);

  firmwareInfoList = hw_->getAllFirmwareInfo();
}

void SaiHandler::getHwDebugDump(std::string& out) {
  out = hw_->getDebugDump();
}

void SaiHandler::setSdkRegDumpEnabled(bool enabled) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  hw_->setSdkRegDumpEnabled(enabled);
}

void SaiHandler::triggerCableLengthMeasurement(
    std::unique_ptr<std::vector<int32_t>> ports) {
  auto log = LOG_THRIFT_CALL(DBG1, *ports);
  hw_->ensureConfigured(__func__);
  hw_->triggerCableLengthMeasurement(ports);
}

void SaiHandler::getPortPrbsState(
    prbs::InterfacePrbsState& prbsState,
    int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  prbsState = hw_->getPortPrbsState(PortID(portId));
}

void SaiHandler::getPortAsicPrbsStats(
    std::vector<phy::PrbsLaneStats>& prbsStats,
    int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  prbsStats = hw_->getPortAsicPrbsStats(PortID(portId));
}

void SaiHandler::clearPortAsicPrbsStats(int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  hw_->clearPortAsicPrbsStats(PortID(portId));
}
void SaiHandler::getPortPrbsPolynomials(
    std::vector<prbs::PrbsPolynomial>& prbsPolynomials,
    int32_t portId) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  prbsPolynomials = hw_->getPortPrbsPolynomials(PortID(portId));
}

void SaiHandler::getProgrammedState(state::SwitchState& switchState) {
  auto log = LOG_THRIFT_CALL(DBG1);
  hw_->ensureConfigured(__func__);
  auto state = hw_->getProgrammedState();
  CHECK(state);
  switchState = state->toThrift();
}

} // namespace facebook::fboss
