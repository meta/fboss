/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/mnpu/SplitAgentThriftSyncer.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/mnpu/FdbEventSyncer.h"
#include "fboss/agent/mnpu/HwSwitchStatsSinkClient.h"
#include "fboss/agent/mnpu/LinkChangeEventSyncer.h"
#include "fboss/agent/mnpu/OperDeltaSyncer.h"
#include "fboss/agent/mnpu/RxPktEventSyncer.h"
#include "fboss/agent/mnpu/SwitchReachabilityChangeEventSyncer.h"
#include "fboss/agent/mnpu/TxPktEventSyncer.h"
namespace {
DEFINE_int32(
    hwagent_watchdog_interval_ms,
    10000,
    "interval in milliseconds for watchdog to check and update monitoring counters");
} // namespace

namespace facebook::fboss {

SplitAgentThriftSyncer::SplitAgentThriftSyncer(
    HwSwitch* hw,
    uint16_t serverPort,
    SwitchID switchId,
    uint16_t switchIndex,
    std::optional<std::string> multiSwitchStatsPrefix)
    : retryThread_(
          std::make_shared<folly::ScopedEventBaseThread>(
              "SplitAgentThriftRetryThread")),
      switchId_(switchId),
      hwSwitch_(hw),
      linkChangeEventSinkClient_(
          std::make_unique<LinkChangeEventSyncer>(
              serverPort,
              switchId_,
              retryThread_->getEventBase(),
              hw,
              multiSwitchStatsPrefix)),
      txPktEventStreamClient_(
          std::make_unique<TxPktEventSyncer>(
              serverPort,
              switchId_,
              retryThread_->getEventBase(),
              hw,
              multiSwitchStatsPrefix)),
      operDeltaClient_(
          std::make_unique<OperDeltaSyncer>(serverPort, switchId_, hw)),
      fdbEventSinkClient_(
          std::make_unique<FdbEventSyncer>(
              serverPort,
              switchId_,
              retryThread_->getEventBase(),
              multiSwitchStatsPrefix)),
      rxPktEventSinkClient_(
          std::make_unique<RxPktEventSyncer>(
              serverPort,
              switchId_,
              retryThread_->getEventBase(),
              multiSwitchStatsPrefix)),
      hwSwitchStatsSinkClient_(
          std::make_unique<HwSwitchStatsSinkClient>(
              serverPort,
              switchId_,
              switchIndex,
              retryThread_->getEventBase(),
              multiSwitchStatsPrefix)),
      switchReachabilityChangeEventSinkClient_(
          std::make_unique<SwitchReachabilityChangeEventSyncer>(
              serverPort,
              switchId_,
              retryThread_->getEventBase(),
              hw,
              multiSwitchStatsPrefix)) {
  multiSwitchStatsPrefix_ = std::move(multiSwitchStatsPrefix);
  updateWatchdogMissedCount();
  auto clients = {
      txPktEventStreamClient_->getThriftClientHeartbeat(),
      fdbEventSinkClient_->getThriftClientHeartbeat(),
      rxPktEventSinkClient_->getThriftClientHeartbeat(),
      hwSwitchStatsSinkClient_->getThriftClientHeartbeat(),
      switchReachabilityChangeEventSinkClient_->getThriftClientHeartbeat(),
      linkChangeEventSinkClient_->getThriftClientHeartbeat()};

  thriftClientWatchdog_ = std::make_unique<ThreadHeartbeatWatchdog>(
      std::chrono::milliseconds(FLAGS_hwagent_watchdog_interval_ms), [this]() {
        watchdogMissedCount_ += 1;
        updateWatchdogMissedCount();
      });

  for (const auto& client : clients) {
    thriftClientWatchdog_->startMonitoringHeartbeat(client);
  }

  thriftClientWatchdog_->start();
}

void SplitAgentThriftSyncer::packetReceived(
    std::unique_ptr<RxPacket> pkt) noexcept {
  multiswitch::RxPacket rxPkt;
  rxPkt.port() = pkt->getSrcPort();
  if (auto vlan = pkt->getSrcVlanIf()) {
    rxPkt.vlan() = *vlan;
  }
  if (pkt->getSrcAggregatePort()) {
    rxPkt.aggPort() = pkt->getSrcAggregatePort();
  }
  if (pkt->cosQueue()) {
    rxPkt.cosQueue() = hwQueueIdToCpuCosQueueId(
        *pkt->cosQueue(),
        hwSwitch_->getPlatform()->getAsic(),
        hwSwitch_->getSwitchStats());
  }
  if (pkt->packetType()) {
    rxPkt.packetType() = *pkt->packetType();
  }
  // coalesce the IOBuf before copy
  pkt->buf()->coalesce();
  rxPkt.data() = IOBuf::copyBuffer(pkt->buf()->data(), pkt->buf()->length());
  rxPkt.length() = pkt->buf()->computeChainDataLength();
  rxPktEventSinkClient_->enqueue(std::move(rxPkt));
}

void SplitAgentThriftSyncer::linkStateChanged(
    PortID port,
    bool up,
    cfg::PortType portType,
    std::optional<phy::LinkFaultStatus> iPhyFaultStatus,
    std::optional<AggregatePortID> aggPortId) {
  multiswitch::LinkEvent event;
  event.port() = port;
  event.up() = up;
  event.portType() = portType;
  if (iPhyFaultStatus) {
    event.iPhyLinkFaultStatus() = *iPhyFaultStatus;
  }
  if (aggPortId) {
    XLOG(DBG2) << "link state change for agg port " << aggPortId.value();
    event.aggPortId() = *aggPortId;
  }
  multiswitch::LinkChangeEvent changeEvent;
  changeEvent.linkStateEvent() = event;
  changeEvent.eventType() = multiswitch::LinkChangeEventType::LINK_STATE;
  linkChangeEventSinkClient_->enqueue(std::move(changeEvent));
}

void SplitAgentThriftSyncer::linkActiveStateChangedOrFwIsolated(
    const std::map<PortID, bool>& port2IsActive,
    bool fwIsolated,
    const std::optional<uint32_t>& numActiveFabricPortsAtFwIsolate) {
  multiswitch::LinkActiveEvent event;

  for (const auto& [portID, isActive] : port2IsActive) {
    event.port2IsActive()[portID] = isActive;
  }

  event.fwIsolated() = fwIsolated;
  if (numActiveFabricPortsAtFwIsolate) {
    event.numActiveFabricPortsAtFwIsolate() = *numActiveFabricPortsAtFwIsolate;
  }

  multiswitch::LinkChangeEvent changeEvent;
  changeEvent.linkActiveEvents() = event;
  changeEvent.eventType() = multiswitch::LinkChangeEventType::LINK_ACTIVE;
  linkChangeEventSinkClient_->enqueue(std::move(changeEvent));
}

void SplitAgentThriftSyncer::linkAdminStateChangedByFw(
    const std::vector<int32_t>& fwDisabledPortIds) {
  multiswitch::LinkFwDisableEvent event;
  event.portIds() = fwDisabledPortIds;

  multiswitch::LinkChangeEvent changeEvent;
  changeEvent.linkFwDisableEvent() = event;
  changeEvent.eventType() =
      multiswitch::LinkChangeEventType::LINK_DISABLED_BY_FIRMWARE;
  linkChangeEventSinkClient_->enqueue(std::move(changeEvent));
}

void SplitAgentThriftSyncer::switchReachabilityChanged(
    const SwitchID /*switchId*/,
    const std::map<SwitchID, std::set<PortID>>& switchReachabilityInfo) {
  multiswitch::SwitchReachabilityChangeEvent event;

  for (const auto& [switchId, portIds] : switchReachabilityInfo) {
    event.switchId2FabricPorts()[switchId] =
        std::set<int32_t>(portIds.begin(), portIds.end());
  }
  switchReachabilityChangeEventSinkClient_->enqueue(std::move(event));
}

void SplitAgentThriftSyncer::linkConnectivityChanged(
    const std::map<PortID, multiswitch::FabricConnectivityDelta>&
        port2ConnectivityDelta) {
  multiswitch::LinkConnectivityEvent event;

  for (const auto& [portID, connectivityDelta] : port2ConnectivityDelta) {
    event.port2ConnectivityDelta()[portID] = connectivityDelta;
  }

  multiswitch::LinkChangeEvent changeEvent;
  changeEvent.linkConnectivityEvents() = event;
  changeEvent.eventType() = multiswitch::LinkChangeEventType::LINK_CONNECTIVITY;
  linkChangeEventSinkClient_->enqueue(std::move(changeEvent));
}

void SplitAgentThriftSyncer::l2LearningUpdateReceived(
    L2Entry l2Entry,
    L2EntryUpdateType l2EntryUpdateType) {
  multiswitch::FdbEvent event;
  L2EntryThrift entry;
  entry.mac() = l2Entry.getMac().toString();
  entry.vlanID() = l2Entry.getVlanID();
  entry.l2EntryType() =
      (l2Entry.getType() == L2Entry::L2EntryType::L2_ENTRY_TYPE_PENDING)
      ? L2EntryType::L2_ENTRY_TYPE_PENDING
      : L2EntryType::L2_ENTRY_TYPE_VALIDATED;
  if (l2Entry.getPort().isAggregatePort()) {
    entry.trunk() = 1;
    entry.port() = l2Entry.getPort().aggPortID();
  } else {
    entry.port() = l2Entry.getPort().phyPortID();
  }
  if (l2Entry.getClassID()) {
    entry.classID() = static_cast<int>(l2Entry.getClassID().value());
  }
  event.entry() = std::move(entry);
  event.updateType() = l2EntryUpdateType;
  fdbEventSinkClient_->enqueue(std::move(event));
}

void SplitAgentThriftSyncer::updateHwSwitchStats(
    multiswitch::HwSwitchStats stats) {
  if (hwSwitchStatsSinkClient_->isConnectedToServer()) {
    hwSwitchStatsSinkClient_->enqueue(std::move(stats));
  }
}

void SplitAgentThriftSyncer::exitFatal() const noexcept {
  // TODO - Add handler
}

void SplitAgentThriftSyncer::pfcWatchdogStateChanged(
    const PortID& /* port */,
    const bool /* deadlock */) {
  // No need to sync PFC events to SwSwitch. We'll increment counters locally.
}

void SplitAgentThriftSyncer::registerStateObserver(
    StateObserver* /* observer */,
    const std::string& /* name */) {
  // TODO - Add handler
}

void SplitAgentThriftSyncer::unregisterStateObserver(
    StateObserver* /* observer */) {
  // TODO - Add handler
}

void SplitAgentThriftSyncer::start() {
  // Start any required services
  operDeltaClient_->startOperSync();
  isRunning_ = true;
}

void SplitAgentThriftSyncer::stop() {
  // Stop any started services
  if (thriftClientWatchdog_) {
    XLOG(DBG1) << "Stopping hwagent SplitAgentThriftClient watchdog";
    thriftClientWatchdog_->stop();
    thriftClientWatchdog_.reset();
  }

  linkChangeEventSinkClient_->cancel();
  txPktEventStreamClient_->cancel();
  fdbEventSinkClient_->cancel();
  rxPktEventSinkClient_->cancel();
  hwSwitchStatsSinkClient_->cancel();
  switchReachabilityChangeEventSinkClient_->cancel();

  isRunning_ = false;
}

void SplitAgentThriftSyncer::cancelPendingRxPktEnqueue() {
  rxPktEventSinkClient_->cancelPendingEnqueue();
}

void SplitAgentThriftSyncer::cancelPendingFdbEnqueue() {
  fdbEventSinkClient_->cancelPendingEnqueue();
}

void SplitAgentThriftSyncer::stopOperDeltaSync() {
  operDeltaClient_->stopOperSync();
}

SplitAgentThriftSyncer::~SplitAgentThriftSyncer() {
  if (isRunning_) {
    stop();
  }
}
void SplitAgentThriftSyncer::updateWatchdogMissedCount() {
  std::string counterName = "hwagent.watchdogMissCount";
  if (multiSwitchStatsPrefix_.has_value()) {
    counterName = multiSwitchStatsPrefix_.value() + "." + counterName;
  }
  fb303::fbData->addStatValue(counterName, watchdogMissedCount_);
}
} // namespace facebook::fboss
