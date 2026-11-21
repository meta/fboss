/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/LinkAggregationManager.h"

#include "fboss/agent/AggregatePortStats.h"
#include "fboss/agent/HwAsicTable.h"
#include "fboss/agent/LacpController.h"
#include "fboss/agent/LacpTypes-defs.h"
#include "fboss/agent/LacpTypes.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchIdScopeResolver.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/state/AggregatePort.h"
#include "fboss/agent/state/AggregatePortMap.h"
#include "fboss/agent/state/DeltaFunctions.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/PortMap.h"
#include "fboss/agent/state/SwitchState.h"

#include <folly/logging/xlog.h>

#include <algorithm>
#include <iterator>
#include <tuple>
#include <utility>

namespace facebook::fboss {

void LinkAggregationManager::recordStatistics(
    facebook::fboss::SwSwitch* sw,
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  bool wasUpPreviously = oldAggPort->isUp();
  bool isUpNow = newAggPort->isUp();

  if (wasUpPreviously == isUpNow) {
    return;
  }

  auto aggregatePortID = newAggPort->getID();

  auto aggregatePortStats = sw->stats()->aggregatePort(aggregatePortID);
  if (!aggregatePortStats) {
    // Because AggregatePorts are only created by configuration, we can be
    // sure that config application has already been completed at this
    // point. It follows that the first time this code is executed,
    // aggregatePortStats will be constructed with the desired name- that
    // specified in the config.
    aggregatePortStats = sw->stats()->createAggregatePortStats(
        aggregatePortID, newAggPort->getName());
  }

  aggregatePortStats->flapped();
}

ProgramForwardingAndPartnerState::ProgramForwardingAndPartnerState(
    PortID portID,
    AggregatePortID aggPortID,
    AggregatePort::Forwarding fwdState,
    AggregatePort::PartnerState partnerState)
    : portID_(portID),
      aggregatePortID_(aggPortID),
      forwardingState_(fwdState),
      partnerState_(partnerState) {}

std::shared_ptr<SwitchState> ProgramForwardingAndPartnerState::operator()(
    const std::shared_ptr<SwitchState>& state) {
  std::shared_ptr<SwitchState> nextState(state);
  auto* aggPort =
      nextState->getAggregatePorts()->getNodeIf(aggregatePortID_).get();
  if (!aggPort) {
    return nullptr;
  }

  XLOG(DBG2) << "Updating " << aggPort->getName() << ": ForwardingState["
             << nextState->getPorts()->getNodeIf(portID_)->getName() << "] --> "
             << (forwardingState_ == AggregatePort::Forwarding::ENABLED
                     ? "ENABLED"
                     : "DISABLED")
             << " PartnerState " << partnerState_.describe();

  aggPort = aggPort->modify(&nextState);
  aggPort->setForwardingState(portID_, forwardingState_);
  aggPort->setPartnerState(portID_, partnerState_);

  auto capacityResult = computeAggregatePortCapacityAndStatus(
      nextState->getAggregatePorts()->getNodeIf(aggregatePortID_), nextState);
  if (capacityResult.configuredCapacityMbps.has_value()) {
    aggPort->setConfiguredCapacityMbps(*capacityResult.configuredCapacityMbps);
  } else {
    aggPort->clearConfiguredCapacityMbps();
  }
  if (capacityResult.activeCapacityMbps.has_value()) {
    aggPort->setActiveCapacityMbps(*capacityResult.activeCapacityMbps);
  } else {
    aggPort->clearActiveCapacityMbps();
  }
  aggPort->setStatus(capacityResult.status);

  XLOG(DBG2) << "Updated " << aggPort->getName()
             << " forwardingSubportCount: " << aggPort->forwardingSubportCount()
             << ", minLinkCount: " << aggPort->getMinimumLinkCount()
             << ", minLinkCountToUp:"
             << aggPort->getMinimumLinkCountToUp().value_or(0)
             << ", up: " << static_cast<int>(aggPort->isUp());
  return nextState;
}

// Needed for CHECK_* macros to work with PortIDToController::iterator
std::ostream& operator<<(
    std::ostream& out,
    const LinkAggregationManager::PortIDToController::iterator& it) {
  return (out << it.get_ptr());
}

LinkAggregationManager::LinkAggregationManager(SwSwitch* sw)
    : portToController_(), sw_(sw) {
  sw_->registerStateObserver(this, "LinkAggregationManager");
}

void LinkAggregationManager::handlePacket(
    std::unique_ptr<RxPacket> pkt,
    folly::io::Cursor c) {
  // TODO(samank): check this is running in RX thread?
  std::shared_lock g(controllersLock_);
  auto ingressPort = pkt->getSrcPort();

  auto it = portToController_.find(ingressPort);
  if (it == portToController_.end()) {
    XLOG(ERR) << "No LACP controller found for port " << ingressPort;
    return;
  }

  auto lacpdu = LACPDU::from(&c);
  if (!lacpdu.isValid()) {
    XLOG(ERR) << "Invalid LACP data unit";
    return;
  }

  it->second->received(lacpdu);
}

void LinkAggregationManager::stateUpdated(const StateDelta& delta) {
  CHECK(sw_->getUpdateEvb()->inRunningEventBaseThread());

  std::unique_lock writeGuard(controllersLock_);

  DeltaFunctions::forEachChanged(
      delta.getAggregatePortsDelta(),
      &LinkAggregationManager::aggregatePortChanged,
      &LinkAggregationManager::aggregatePortAdded,
      &LinkAggregationManager::aggregatePortRemoved,
      this);

  // Downgrade to a reader lock
  std::shared_lock readGuard(
      folly::transition_lock<std::shared_lock>(writeGuard));

  DeltaFunctions::forEachChanged(
      delta.getPortsDelta(), &LinkAggregationManager::portChanged, this);

  DeltaFunctions::forEachChanged(
      delta.getAggregatePortsDelta(),
      &LinkAggregationManager::updateHyperPortState,
      this);
  DeltaFunctions::forEachAdded(
      delta.getAggregatePortsDelta(),
      [&](const std::shared_ptr<AggregatePort>& newAggPort) {
        updateHyperPortState(std::shared_ptr<AggregatePort>(), newAggPort);
      });
  DeltaFunctions::forEachRemoved(
      delta.getAggregatePortsDelta(),
      [&](const std::shared_ptr<AggregatePort>& oldAggPort) {
        updateHyperPortState(oldAggPort, std::shared_ptr<AggregatePort>());
      });
}

void LinkAggregationManager::aggregatePortAdded(
    const std::shared_ptr<AggregatePort>& aggPort) {
  PortIDToController::iterator it;
  bool inserted;

  for (const auto& subport : aggPort->sortedSubports()) {
    std::tie(it, inserted) = portToController_.emplace(
        subport.portID,
        std::make_shared<LacpController>(
            subport.portID,
            sw_->getLacpEvb(),
            subport.priority,
            subport.rate,
            subport.activity,
            subport.holdTimerMulitiplier,
            aggPort->getID(),
            aggPort->getSystemPriority(),
            aggPort->getSystemID(),
            aggPort->getMinimumLinkCount(),
            aggPort->getMinimumLinkCountToUp(),
            this));
    CHECK(inserted);
    // Warm boot LACP restore - restore state machines if member was in
    // fwding enabled state. Otherwise, perform normal LACP start.
    if (aggPort->getForwardingState(subport.portID) ==
        LegacyAggregatePortFields::Forwarding::ENABLED) {
      const auto pstate = aggPort->getPartnerState(subport.portID);
      XLOG(DBG2) << "Port [" << subport.portID
                 << "] Restoring Lacp state machine with partner state "
                 << pstate.describe();
      it->second->restoreMachines(pstate);
    } else {
      XLOG(DBG2) << "Port [" << subport.portID
                 << "] Starting Lacp state machines";
      it->second->startMachines();
      // LACP state machines listen to port events and set state accordingly
      // In warm boot case, there will not be a port up event if port was Up
      // during init. Check port state and call portUp if needed. If there are
      // duplicate port up calls, it will be ignored
      if (sw_->getState()->getPort(subport.portID)->isPortUp()) {
        XLOG(DBG2) << "Calling LACP port up for " << subport.portID;
        it->second->portUp();
      }
    }
  }
}

void LinkAggregationManager::aggregatePortRemoved(
    const std::shared_ptr<AggregatePort>& aggPort) {
  PortIDToController::iterator it;
  for (const auto& subport : aggPort->sortedSubports()) {
    it = portToController_.find(subport.portID);
    CHECK_NE(it, portToController_.end());
    it->second->stopMachines();
    portToController_.erase(it);
  }
}

void LinkAggregationManager::stopLacpOnSubPort(PortID subPort) {
  PortIDToController::iterator it;
  it = portToController_.find(subPort);
  CHECK_NE(it, portToController_.end());
  it->second->stopMachines();
}

void LinkAggregationManager::aggregatePortChanged(
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  CHECK_EQ(oldAggPort->getID(), newAggPort->getID());

  recordStatistics(sw_, oldAggPort, newAggPort);

  auto oldSubportRange = oldAggPort->sortedSubports();
  auto newSubport = newAggPort->sortedSubports();
  if (oldAggPort->getName() == newAggPort->getName() &&
      oldAggPort->getDescription() == newAggPort->getDescription() &&
      oldAggPort->getSystemPriority() == newAggPort->getSystemPriority() &&
      oldAggPort->getSystemID() == newAggPort->getSystemID() &&
      std::equal(
          oldSubportRange.begin(),
          oldSubportRange.end(),
          newSubport.begin(),
          newSubport.end())) {
    return;
  }

  aggregatePortRemoved(oldAggPort);
  aggregatePortAdded(newAggPort);

  updateAggregatePortStats(oldAggPort, newAggPort);
}

void LinkAggregationManager::updateAggregatePortStats(
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  if (oldAggPort->getName() == newAggPort->getName()) {
    return;
  }

  auto aggregatePortStats = sw_->stats()->aggregatePort(newAggPort->getID());
  if (!aggregatePortStats) {
    return;
  }

  aggregatePortStats->aggregatePortNameChanged(newAggPort->getName());
}

void LinkAggregationManager::updateHyperPortState(
    const std::shared_ptr<AggregatePort>& oldAggPort,
    const std::shared_ptr<AggregatePort>& newAggPort) {
  if (!FLAGS_hyper_port) {
    return;
  }
  cfg::PortState newPortState{cfg::PortState::DISABLED};
  std::optional<PortID> hyperPortId;
  if (!newAggPort) {
    // remove old aggregate port
    if (oldAggPort &&
        oldAggPort->getAggregatePortType() ==
            cfg::AggregatePortType::HYPER_PORT) {
      hyperPortId = PortID(oldAggPort->getID());
      newPortState = cfg::PortState::DISABLED;
    }
  } else {
    bool allNewSubPortsForwarding = true;
    for (const auto& subPort : newAggPort->sortedSubports()) {
      if (newAggPort->getForwardingState(subPort.portID) !=
          LegacyAggregatePortFields::Forwarding::ENABLED) {
        allNewSubPortsForwarding = false;
      }
    }
    bool allOldSubPortsForwarding = true;
    if (oldAggPort) {
      // existing aggregate port changed
      for (const auto& subPort : oldAggPort->sortedSubports()) {
        if (oldAggPort->getForwardingState(subPort.portID) !=
            LegacyAggregatePortFields::Forwarding::ENABLED) {
          allOldSubPortsForwarding = false;
        }
      }
    }
    if ((!oldAggPort || allOldSubPortsForwarding != allNewSubPortsForwarding) &&
        newAggPort->getAggregatePortType() ==
            cfg::AggregatePortType::HYPER_PORT) {
      hyperPortId = PortID(newAggPort->getID());
      newPortState = allNewSubPortsForwarding ? cfg::PortState::ENABLED
                                              : cfg::PortState::DISABLED;
    }
  }

  if (hyperPortId) {
    auto updateFn = [hyperPortId,
                     newPortState](const std::shared_ptr<SwitchState>& state) {
      const auto oldHyperPort =
          state->getPorts()->getNodeIf(hyperPortId.value());
      CHECK(oldHyperPort) << "hyper port " << (int)hyperPortId.value()
                          << " not found in state";
      std::shared_ptr<SwitchState> newState{state};
      auto newHyperPort = oldHyperPort->modify(&newState);
      XLOG(DBG2) << "set hyper port " << (int)hyperPortId.value()
                 << " to state "
                 << apache::thrift::util::enumNameSafe(newPortState);
      newHyperPort->setAdminState(newPortState);
      return newState;
    };
    sw_->updateStateNoCoalescing("set hyper-port state", std::move(updateFn));
  }
}

void LinkAggregationManager::portChanged(
    const std::shared_ptr<Port>& oldPort,
    const std::shared_ptr<Port>& newPort) {
  auto portId = newPort->getID();

  if (oldPort->getOperState() == Port::OperState::DOWN &&
      newPort->getOperState() == Port::OperState::UP) {
    auto it = portToController_.find(portId);
    if (it == portToController_.end()) {
      XLOG(ERR) << "Port " << portId << " not found";
      return;
    }
    it->second->portUp();
  } else if (
      oldPort->getOperState() == Port::OperState::UP &&
      newPort->getOperState() == Port::OperState::DOWN) {
    auto it = portToController_.find(portId);
    if (it == portToController_.end()) {
      XLOG(ERR) << "Port " << portId << " not found";
      return;
    }
    it->second->portDown();
  }
}

void LinkAggregationManager::populatePartnerPair(
    PortID portID,
    LacpPartnerPair& partnerPair) {
  // TODO(samank): CHECK in Thrift worker thread

  std::shared_ptr<LacpController> controller;
  {
    std::shared_lock g(controllersLock_);

    auto it = portToController_.find(portID);
    if (it == portToController_.end()) {
      throw LACPError("No LACP controller found for port ", portID);
    }

    controller = it->second;
  }

  controller->actorInfo().populate(*partnerPair.localEndpoint());
  controller->partnerInfo().populate(*partnerPair.remoteEndpoint());
}

void LinkAggregationManager::populatePartnerPairs(
    std::vector<LacpPartnerPair>& partnerPairs) {
  // TODO(samank): CHECK in Thrift worker thread

  partnerPairs.clear();

  std::shared_lock g(controllersLock_);

  partnerPairs.reserve(
      std::distance(portToController_.begin(), portToController_.end()));

  for (const auto& portAndController : portToController_) {
    auto controller = portAndController.second;

    partnerPairs.emplace_back();

    controller->actorInfo().populate(*partnerPairs.back().localEndpoint());
    controller->partnerInfo().populate(*partnerPairs.back().remoteEndpoint());
  }
}

bool LinkAggregationManager::transmit(LACPDU lacpdu, PortID portID) {
  CHECK(sw_->getLacpEvb()->inRunningEventBaseThread());

  auto pkt = sw_->allocatePacket(LACPDU::LENGTH);
  if (!pkt) {
    XLOG(DBG4) << "Failed to allocate tx packet for LACPDU transmission";
    return false;
  }

  folly::io::RWPrivateCursor writer(pkt->buf());

  auto switchId = sw_->getScopeResolver()->scope(portID).switchId();
  MacAddress cpuMac =
      sw_->getHwAsicTable()->getHwAsicIf(switchId)->getAsicMac();

  auto port = sw_->getState()->getPorts()->getNodeIf(portID);
  CHECK(port);

  TxPacket::writeEthHeader(
      &writer,
      LACPDU::kSlowProtocolsDstMac(),
      cpuMac,
      port->getIngressVlan(),
      LACPDU::EtherType::SLOW_PROTOCOLS);

  writer.writeBE<uint8_t>(static_cast<uint8_t>(LACPDU::EtherSubtype::LACP));

  lacpdu.to(&writer);

  // TODO(joseph5wu) Actually LACP should be multicast pkt, and using
  // OutOfPacket will actually send the packet to unicast queue.
  return sw_->sendNetworkControlPacketAsync(
      std::move(pkt), PortDescriptor(portID));
}

void LinkAggregationManager::enableForwardingAndSetPartnerState(
    PortID portID,
    AggregatePortID aggPortID,
    const AggregatePort::PartnerState& partnerState) {
  CHECK(sw_->getLacpEvb()->inRunningEventBaseThread());

  auto enableFwdStateFn = ProgramForwardingAndPartnerState(
      portID, aggPortID, AggregatePort::Forwarding::ENABLED, partnerState);

  sw_->updateStateNoCoalescing(
      "AggregatePort ForwardingAndPartnerState", std::move(enableFwdStateFn));
}

void LinkAggregationManager::disableForwardingAndSetPartnerState(
    PortID portID,
    AggregatePortID aggPortID,
    const ParticipantInfo& partnerState) {
  CHECK(sw_->getLacpEvb()->inRunningEventBaseThread());

  auto disableFwdStateFn = ProgramForwardingAndPartnerState(
      portID, aggPortID, AggregatePort::Forwarding::DISABLED, partnerState);

  sw_->updateStateNoCoalescing(
      "AggregatePort ForwardingAndPartnerState", std::move(disableFwdStateFn));
}

void LinkAggregationManager::recordLacpTimeout() {
  sw_->stats()->LacpRxTimeouts();
}

void LinkAggregationManager::recordLacpMismatchPduTeardown() {
  sw_->stats()->LacpMismatchPduTeardown();
}

std::vector<std::shared_ptr<LacpController>>
LinkAggregationManager::getControllersFor(
    folly::Range<std::vector<PortID>::const_iterator> ports) {
  // Although this method is thread-safe, it is only invoked from a Selector
  // object, which should always be executing over the LACP EVB
  CHECK(sw_->getLacpEvb()->inRunningEventBaseThread());

  std::vector<std::shared_ptr<LacpController>> controllers(
      std::distance(ports.begin(), ports.end()));

  std::shared_lock g(controllersLock_);

  // TODO(samank): Rerwite as an O(N + M) algorithm
  for (auto i = 0; i < controllers.size(); ++i) {
    auto it = portToController_.find(ports[i]);
    CHECK(it != portToController_.end());

    controllers[i] = it->second;
  }

  return controllers;
}

void LinkAggregationManager::stopProcessing() {
  if (stopped_) {
    return;
  }
  for (auto controller : portToController_) {
    controller.second->stopMachines();
  }
  sw_->unregisterStateObserver(this);
  stopped_ = true;
}

LinkAggregationManager::~LinkAggregationManager() {
  stopProcessing();
}

} // namespace facebook::fboss
