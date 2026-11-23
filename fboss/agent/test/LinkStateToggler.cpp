/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/test/LinkStateToggler.h"

#include "fboss/agent/ApplyThriftConfig.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/hw/switch_asics/HwAsic.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/test/TestEnsembleIf.h"
#include "fboss/lib/CommonUtils.h"

#include <boost/container/flat_map.hpp>
#include <folly/gen/Base.h>
#include <gtest/gtest.h>

#include <algorithm>

namespace {

using namespace facebook::fboss;

// Instead of bringing up ports 1-by-1, bring them up in batches such that
// 1) Similar to prod environment where links could come up in batches, and
// 2) More efficient during testing.
constexpr auto kBatchSize = 32;

std::string getMyHostName(const cfg::SwitchConfig& cfg) {
  CHECK_GE(cfg.switchSettings()->switchIdToSwitchInfo()->size(), 1);
  auto [switchID, switchInfo] =
      *cfg.switchSettings()->switchIdToSwitchInfo()->begin();

  if (switchInfo.switchType() == cfg::SwitchType::VOQ ||
      switchInfo.switchType() == cfg::SwitchType::FABRIC) {
    // A host may have multiple switchIDs, but all those switchIDs will carry
    // the same hostname. Thus, get any switchID
    auto dsfNodeIter = cfg.dsfNodes()->find(switchID);
    if (dsfNodeIter == cfg.dsfNodes()->end()) {
      throw FbossError("SwitchID:", switchID, " Not found in DSF Node map");
    }
    return *dsfNodeIter->second.name();
  } else {
    return getLocalHostnameUqdn();
  }
}

bool skipTogglingPort(const cfg::Port& port, const std::string& myHostName) {
  switch (*port.portType()) {
    case cfg::PortType::INTERFACE_PORT: {
      // TODO: Migrate LLDP neighbor implementation from using
      // expecetedLLDPValues to more generic expectedNeighborReachability.
      // At that time, case FABRIC_PORT implementation will work for
      // INTERFACE_PORT as well.

      // Toggle ports that have empty LLDP neighbor
      if (port.expectedLLDPValues()->size() == 0) {
        return false;
      }

      auto lldpTagToValue = port.expectedLLDPValues();

      auto sysIter = lldpTagToValue->find(cfg::LLDPTag::SYSTEM_NAME);
      if (sysIter == lldpTagToValue->end()) {
        return false;
      }
      auto remoteSystem = sysIter->second;

      auto portIter = lldpTagToValue->find(cfg::LLDPTag::PORT);
      if (portIter == lldpTagToValue->end()) {
        return false;
      }
      auto remotePort = portIter->second;

      // Toggle, if expected neighbor is same as self.
      return !(
          myHostName == remoteSystem && port.name().value_or("") == remotePort);
    }
    case cfg::PortType::FABRIC_PORT: {
      // Toggle ports that have empty neighbor
      if (port.expectedNeighborReachability()->size() == 0) {
        return false;
      }

      CHECK_EQ(port.expectedNeighborReachability()->size(), 1);
      auto expectedNeighbor = port.expectedNeighborReachability()->front();
      auto remoteSystem = *expectedNeighbor.remoteSystem();
      auto remotePort = *expectedNeighbor.remotePort();

      // Toggle, if expected neighbor is same as self.
      return !(
          myHostName == remoteSystem && port.name().value_or("") == remotePort);
    }
    case cfg::PortType::CPU_PORT:
    case cfg::PortType::RECYCLE_PORT:
    case cfg::PortType::EVENTOR_PORT:
    case cfg::PortType::HYPER_PORT:
    case cfg::PortType::HYPER_PORT_MEMBER:
      return true;
    case cfg::PortType::MANAGEMENT_PORT:
      return false;
  }

  throw FbossError(
      "invalid port type: ",
      apache::thrift::util::enumNameSafe(*port.portType()));
}

} // namespace

namespace facebook::fboss {

void LinkStateToggler::linkStateChanged(PortID port, bool up) noexcept {
  {
    std::lock_guard<std::mutex> lk(linkEventMutex_);
    if (portIdToWaitFor_.empty() ||
        (portIdToWaitFor_.find(port) == portIdToWaitFor_.end()) ||
        up != waitForPortUp_) {
      return;
    }
    portIdToWaitFor_.erase(port);
    XLOG(DBG2) << " Got port " << (up ? "up" : "down")
               << " event on : " << port;
    if (portIdToWaitFor_.empty()) {
      desiredPortEventsOccurred_ = true;
      linkEventCV_.notify_one();
    }
  }
}

void LinkStateToggler::setPortIDsAndStateToWaitFor(
    const std::set<PortID>& ports,
    bool waitForPortUp) {
  for (const auto& port : ports) {
    XLOG(DBG2) << " Wait for port " << (waitForPortUp ? "up" : "down")
               << " event on : " << port;
  }
  // In the case of multi_switch, link state toggler does not receive callbacks
  // directly from hardware (it will reach SwSwitch through IPC). Therefore, we
  // skip waiting synchronously and directly wait for switch state to be
  // updated.
  if (!FLAGS_multi_switch && !ports.empty()) {
    std::lock_guard<std::mutex> lk(linkEventMutex_);
    portIdToWaitFor_ = std::set<PortID>(ports);
    waitForPortUp_ = waitForPortUp;
    desiredPortEventsOccurred_ = false;
  }
}

cfg::PortLoopbackMode LinkStateToggler::findDesiredLoopbackMode(
    const std::shared_ptr<SwitchState>& newState,
    PortID port,
    bool up) const {
  if (!up) {
    return cfg::PortLoopbackMode::NONE;
  }
  // For bringing up port - prefer loopback mode from config
  // If not available fall back to loopback mode from ASIC
  auto config = ensemble_->getCurrentConfig();
  auto configPort = std::find_if(
      config.ports()->begin(), config.ports()->end(), [port](const auto& p) {
        return PortID(*p.logicalID()) == port;
      });
  if (configPort != config.ports()->end() &&
      *configPort->loopbackMode() != cfg::PortLoopbackMode::NONE) {
    return *configPort->loopbackMode();
  }
  XLOG(WARN) << " Falling back to picking loopback mode from ASIC";
  auto currPort = newState->getPorts()->getNodeIf(port);
  auto switchId = ensemble_->scopeResolver().scope(currPort).switchId();
  auto asic = ensemble_->getHwAsicTable()->getHwAsic(switchId);
  return asic->getDesiredLoopbackMode(currPort->getPortType());
}

void LinkStateToggler::portStateChangeImpl(
    const std::vector<PortID>& ports,
    bool up) {
  for (auto i = 0; i < ports.size(); i += kBatchSize) {
    auto newState = ensemble_->getProgrammedState()->clone();
    auto batchedPorts = std::vector<PortID>(
        ports.begin() + i,
        ports.begin() + std::min(i + kBatchSize, int(ports.size())));
    std::set<PortID> portsToWaitFor;
    std::unordered_map<PortID, cfg::PortLoopbackMode> port2LoopbackMode;
    for (auto port : batchedPorts) {
      auto desiredLbMode = findDesiredLoopbackMode(newState, port, up);
      if (desiredLbMode !=
          newState->getPorts()->getNodeIf(port)->getLoopbackMode()) {
        portsToWaitFor.insert(port);
        port2LoopbackMode[port] = desiredLbMode;
      }
    }
    setPortIDsAndStateToWaitFor(portsToWaitFor, up);
    auto updateLoopbackMode = [portsToWaitFor, port2LoopbackMode](
                                  const std::shared_ptr<SwitchState>& in) {
      auto switchState = in->clone();
      for (const auto& port : portsToWaitFor) {
        auto newPort =
            switchState->getPorts()->getNodeIf(port)->modify(&switchState);
        newPort->setLoopbackMode(port2LoopbackMode.at(port));
      }
      return switchState;
    };
    ensemble_->applyNewState(updateLoopbackMode);
    for (const auto& port : portsToWaitFor) {
      invokeLinkScanIfNeeded(port, up);
    }
    waitForPortEventsOrSkipIfAlreadyInState(portsToWaitFor, up);
  }
}

bool LinkStateToggler::waitForPortEventsOrSkipIfAlreadyInState(
    const std::set<PortID>& ports,
    bool up) {
  if (!FLAGS_multi_switch) {
    auto desiredOperState =
        up ? PortFields::OperState::UP : PortFields::OperState::DOWN;
    std::unique_lock<std::mutex> lock{linkEventMutex_};

    // Drop ports that are already in the desired OperState from the wait set.
    auto currentState = ensemble_->getProgrammedState();
    for (const auto& portId : ports) {
      auto port = currentState->getPorts()->getNodeIf(portId);
      if (port->getOperState() == desiredOperState) {
        portIdToWaitFor_.erase(portId);
      }
    }

    if (portIdToWaitFor_.empty()) {
      // No port is pending a link event (all are already in the desired state),
      XLOG(DBG2) << "No ports pending a link event for desired "
                 << (up ? "UP" : "DOWN") << " state, skipping event wait";
      desiredPortEventsOccurred_ = true;
    } else {
      // At least one port still needs to transition, wait for its event(s).
      linkEventCV_.wait(lock, [this] { return desiredPortEventsOccurred_; });
      CHECK(portIdToWaitFor_.empty());
    }
    // A port can be dropped from the wait set by an early linkscan callback
    // before its switch-state OperState is updated, so sync OperState for all
    // ports here whether we waited or skipped.
    auto updateOperState = [this, ports, up](
                               const std::shared_ptr<SwitchState>& in) {
      auto newState = in->clone();
      for (const auto& port : ports) {
        auto newPort = newState->getPorts()->getNodeIf(port)->modify(&newState);
        newPort->setOperState(up);
      }
      return newState;
    };
    ensemble_->applyNewState(updateOperState);
  }

  for (const auto& portId : ports) {
    WITH_RETRIES_N_TIMED(60, std::chrono::milliseconds(1000), {
      EXPECT_EVENTUALLY_EQ(
          ensemble_->getProgrammedState()
              ->getPorts()
              ->getNodeIf(portId)
              ->getOperState(),
          (up ? PortFields::OperState::UP : PortFields::OperState::DOWN));
    });
  }
  return true;
}

void LinkStateToggler::waitForPortDown(PortID port) {
  WITH_RETRIES({
    EXPECT_EVENTUALLY_EQ(
        ensemble_->getProgrammedState()
            ->getPorts()
            ->getNodeIf(port)
            ->getOperState(),
        PortFields::OperState::DOWN);
  });
}

void LinkStateToggler::applyInitialConfig(const cfg::SwitchConfig& initCfg) {
  applyInitialConfigWithPortsDown(initCfg);
  bringUpPorts(initCfg);
}

std::shared_ptr<SwitchState> LinkStateToggler::applyInitialConfigWithPortsDown(
    const cfg::SwitchConfig& initCfg) {
  // Goal of this function is twofold
  // - Apply initial config.
  // - Set preemphasis on all ports to 0
  // We do this in 2 steps
  // i)  Apply initial config, but with ports disabled. This is done to cater
  // for platforms where ports only show up after first config application
  // ii) Set preempahsis for all ports to 0
  // iii) Apply initial config with the correct port state.
  // Coupled with preempahsis set to 0 and lbmode=NONE, this will keep ports
  // down. We will then set the enabled ports to desired loopback mode in
  // bringUpPorts API
  auto cfg = initCfg;
  boost::container::flat_map<int, cfg::PortState> portId2DesiredState;
  auto myHostName = getMyHostName(initCfg);
  for (auto& port : *cfg.ports()) {
    if (skipTogglingPort(port, myHostName)) {
      continue;
    }
    portId2DesiredState[*port.logicalID()] = *port.state();
    // Keep ports down by disabling them and setting loopback mode to NONE
    *port.state() = cfg::PortState::DISABLED;
    *port.loopbackMode() = cfg::PortLoopbackMode::NONE;
  }
  // i) Set preempahsis to 0, so ports state can be manipulated by just setting
  // loopback mode (lopbackMode::NONE == down), loopbackMode::{MAC, PHY} == up)
  // ii) Apply first config with all ports set to loopback mode as NONE
  // iii) Synchronously bring ports up. By doing this we are guaranteed to have
  // tided over, over the first set of linkscan events that come as a result of
  // init (since there are no portup events in init + initial config
  // application). iii) Start tests.
  ensemble_->applyNewConfig(cfg);

  // Wait for port state to be disabled in switch state
  for (auto& port : *cfg.ports()) {
    if (skipTogglingPort(port, myHostName)) {
      continue;
    }
    waitForPortDown(PortID(*port.logicalID()));
  }
  auto updateLoopbacks = [&](const std::shared_ptr<SwitchState>& in) {
    auto switchState = in->clone();
    for (auto& cfgPort : *cfg.ports()) {
      if (portId2DesiredState.find(*cfgPort.logicalID()) ==
          portId2DesiredState.end()) {
        continue;
      }
      // Set all port preemphasis values to 0 so that we can bring ports up and
      // down by setting their loopback mode to PHY and NONE respectively.
      // TODO: use sw port's pinConfigs to set this
      auto port = switchState->getPorts()
                      ->getNodeIf(PortID(*cfgPort.logicalID()))
                      ->modify(&switchState);
      port->setZeroPreemphasis(true);
      *cfgPort.state() = portId2DesiredState[*cfgPort.logicalID()];
    }
    return switchState;
  };
  // Update txSetting first and then enable admin state
  ensemble_->applyNewState(updateLoopbacks);
  ensemble_->applyNewConfig(cfg);

  auto updateSquelch = [&](const std::shared_ptr<SwitchState>& in) {
    // Some platforms silently undo squelch setting on admin enable. Prevent it
    // by setting squelch after admin enable.
    if (!ensemble_->getHwAsicTable()->isFeatureSupportedOnAnyAsic(
            HwAsic::Feature::RX_LANE_SQUELCH_ENABLE)) {
      return std::shared_ptr<SwitchState>();
    }

    auto switchState = in->clone();
    for (auto& cfgPort : *cfg.ports()) {
      auto port = switchState->getPorts()
                      ->getNodeIf(PortID(*cfgPort.logicalID()))
                      ->modify(&switchState);
      port->setRxLaneSquelch(true);
    }
    return switchState;
  };
  ensemble_->applyNewState(updateSquelch);
  ensemble_->switchRunStateChanged(SwitchRunState::CONFIGURED);
  return ensemble_->getProgrammedState();
}

void LinkStateToggler::bringUpPorts(const cfg::SwitchConfig& initCfg) {
  std::vector<PortID> portsToBringUp;
  auto myHostName = getMyHostName(initCfg);
  folly::gen::from(*initCfg.ports()) |
      folly::gen::filter([&myHostName](const auto& port) {
        return *port.state() == cfg::PortState::ENABLED &&
            !skipTogglingPort(port, myHostName);
      }) |
      folly::gen::map(
          [](const auto& port) { return PortID(*port.logicalID()); }) |
      folly::gen::appendTo(portsToBringUp);
  bringUpPorts(portsToBringUp);
}

void LinkStateToggler::invokeLinkScanIfNeeded(
    PortID /* port */,
    bool /* isUp */) {}

} // namespace facebook::fboss
