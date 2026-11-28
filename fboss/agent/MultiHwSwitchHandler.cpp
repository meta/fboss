// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/MultiHwSwitchHandler.h"
#include <folly/logging/xlog.h>
#include "fboss/agent/HwSwitchHandler.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/gen-cpp2/agent_stats_types.h"
#include "fboss/agent/state/StateDelta.h"

namespace facebook::fboss {

namespace {
auto makeHwSwitchSyncers(
    SwSwitch* sw,
    const std::map<int64_t, cfg::SwitchInfo>& switchInfoMap,
    HwSwitchHandlerInitFn hwSwitchHandlerInitFn) {
  std::map<SwitchID, std::unique_ptr<HwSwitchHandler>> syncers;
  for (auto entry : switchInfoMap) {
    syncers.emplace(
        SwitchID(entry.first),
        hwSwitchHandlerInitFn(SwitchID(entry.first), entry.second, sw));
  }
  return syncers;
}
} // namespace

MultiHwSwitchHandler::MultiHwSwitchHandler(
    const std::map<int64_t, cfg::SwitchInfo>& switchInfoMap,
    HwSwitchHandlerInitFn hwSwitchHandlerInitFn,
    SwSwitch* sw,
    std::optional<cfg::SdkVersion> sdkVersion)
    : sw_(sw),
      hwSwitchSyncers_(
          makeHwSwitchSyncers(sw, switchInfoMap, hwSwitchHandlerInitFn)),
      connectionStatusTable_{sw_},
      transactionsSupported_(transactionsSupported(sdkVersion)) {
  CHECK_LE(hwSwitchSyncers_.size(), kMaxLocalSwitchIds)
      << "Number of local switches (" << hwSwitchSyncers_.size()
      << ") exceeds kMaxLocalSwitchIds (" << kMaxLocalSwitchIds
      << "). Bump kMaxLocalSwitchIds in LocalSwitchIds.h.";
}

MultiHwSwitchHandler::~MultiHwSwitchHandler() {
  stop();
}

void MultiHwSwitchHandler::start() {
  if (!stopped_.load()) {
    return;
  }
  for (auto& entry : hwSwitchSyncers_) {
    entry.second->start();
  }
  stopped_.store(false);
}

void MultiHwSwitchHandler::stop() {
  if (stopped_.load()) {
    return;
  }
  // Cancel any pending waits for HwSwitch connect calls
  connectionStatusTable_.cancelWait();
  // set stop flag so that there are no more accesses to syncers
  stopped_.store(true);
  for (auto& entry : hwSwitchSyncers_) {
    entry.second->stop();
  }
}

std::shared_ptr<SwitchState> MultiHwSwitchHandler::stateChanged(
    const StateDelta& delta,
    bool transaction,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  std::vector<StateDelta> deltas;
  deltas.emplace_back(delta.oldState(), delta.newState());
  return stateChanged(
      deltas, transaction, hwWriteBehavior, deltaApplicationBehavior);
}

std::shared_ptr<SwitchState> MultiHwSwitchHandler::stateChanged(
    const std::vector<StateDelta>& deltas,
    bool transaction,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  std::map<SwitchID, const std::vector<StateDelta>&> deltasMap;
  std::shared_ptr<SwitchState> newState{nullptr};
  bool updateFailed{false};
  if (stopped_.load()) {
    throw FbossError("multi hw switch syncer not started");
  }
  for (const auto& entry : hwSwitchSyncers_) {
    auto switchId = entry.first;
    deltasMap.emplace(switchId, deltas);
  }
  auto results = stateChanged(
      deltasMap, transaction, hwWriteBehavior, deltaApplicationBehavior);
  for (const auto& result : results) {
    auto status = result.second.second;
    if (status == HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_SUCCEEDED) {
      newState = result.second.first;
    } else if (
        status == HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_FAILED) {
      updateFailed = true;
    }
  }
  if (updateFailed) {
    if (transactionsSupported()) {
      return rollbackStateChange(results, deltas, transaction);
    } else {
      /*
       * For deployments where we don't support transactions - e.g. legacy
       * BCMSwitch implementations. We never expect more than one
       * HwSwitch/HwSwitchSyncer
       */
      CHECK_EQ(hwSwitchSyncers_.size(), 1);
      return results[SwitchID(0)].first;
    }
  }
  /* none of the switches were updated */
  if (!newState) {
    return deltas.front().oldState();
  }
  return newState;
}

std::shared_ptr<SwitchState> MultiHwSwitchHandler::rollbackStateChange(
    const std::map<SwitchID, HwSwitchStateUpdateResult>& updateResults,
    const std::vector<StateDelta>& deltas,
    bool transaction) {
  std::map<SwitchID, const std::vector<StateDelta>> deltasMap;
  for (const auto& entry : updateResults) {
    auto switchId = entry.first;
    auto status = entry.second.second;
    auto currentState = entry.second.first;
    auto currentThriftState = currentState->toThrift();
    // For successful operation on a HwSwitch, reverting full vector
    // For the failed switch, already rolled back by HwSwitch, nop
    std::vector<StateDelta> reverseDeltas;
    for (const auto& delta : deltas) {
      reverseDeltas.emplace(
          reverseDeltas.begin(), delta.newState(), delta.oldState());
    }
    if (status != HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_CANCELLED) {
      deltasMap.emplace(switchId, std::move(reverseDeltas));
    }
  }
  // only uses the reference to above deltasMap. No new copies
  std::map<SwitchID, const std::vector<StateDelta>&> switchIdAndDeltas;
  for (const auto& entry : deltasMap) {
    switchIdAndDeltas.emplace(entry.first, entry.second);
  }
  auto results = stateChanged(switchIdAndDeltas, transaction);
  for (const auto& result : results) {
    if (result.second.second ==
        HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_FAILED) {
      throw FbossError(
          "Failed to rollback switch state on switch id ", result.first);
    }
  }
  return deltas.front().oldState();
}

std::map<SwitchID, HwSwitchStateUpdateResult>
MultiHwSwitchHandler::stateChanged(
    const std::map<SwitchID, const std::vector<StateDelta>&>& deltas,
    bool transaction,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  std::vector<SwitchID> switchIds;
  std::vector<folly::Future<HwSwitchStateUpdateResult>> futures;
  for (const auto& entry : deltas) {
    switchIds.push_back(entry.first);
    auto update = HwSwitchStateUpdate(entry.second, transaction);
    futures.emplace_back(stateChanged(
        entry.first, update, hwWriteBehavior, deltaApplicationBehavior));
  }
  return getStateUpdateResult(switchIds, futures);
}

folly::Future<HwSwitchStateUpdateResult> MultiHwSwitchHandler::stateChanged(
    SwitchID switchId,
    const HwSwitchStateUpdate& update,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  auto iter = hwSwitchSyncers_.find(switchId);
  if (iter == hwSwitchSyncers_.end()) {
    throw FbossError("hw switch syncer for switch id ", switchId, " not found");
  }
  return iter->second->stateChanged(
      update, hwWriteBehavior, deltaApplicationBehavior);
}

std::map<SwitchID, HwSwitchStateUpdateResult>
MultiHwSwitchHandler::getStateUpdateResult(
    const std::vector<SwitchID>& switchIds,
    std::vector<folly::Future<HwSwitchStateUpdateResult>>& futures) const {
  std::map<SwitchID, HwSwitchStateUpdateResult> hwUpdateResults;

  auto results = folly::collectAll(futures).wait();
  auto index{0};
  std::shared_ptr<SwitchState> newState;
  for (const auto& result : results.value()) {
    CHECK_LT(index, switchIds.size());
    auto switchId = switchIds[index];
    if (result.hasException()) {
      XLOG(ERR) << "Failed to get state update result for switch id "
                << switchId << ":" << result.exception().what();
      result.exception().throw_exception();
    }
    hwUpdateResults.emplace(switchId, result.value());
    index++;
  }
  return hwUpdateResults;
}

HwSwitchHandler* MultiHwSwitchHandler::getHwSwitchHandler(SwitchID switchId) {
  auto handler = hwSwitchSyncers_.find(switchId);
  if (handler == hwSwitchSyncers_.end()) {
    throw FbossError(
        "No hw switch handler for switch id: ", switchId, " found!");
  }
  return handler->second.get();
}

bool MultiHwSwitchHandler::transactionsSupported() const {
  return transactionsSupported_;
}

bool MultiHwSwitchHandler::transactionsSupported(
    std::optional<cfg::SdkVersion> sdkVersion) const {
  for (auto& entry : hwSwitchSyncers_) {
    if (!entry.second->transactionsSupported(sdkVersion)) {
      return false;
    }
  }
  return true;
}

std::map<PortID, FabricEndpoint> MultiHwSwitchHandler::getFabricConnectivity() {
  // TODO - retire this api after migrating clients to HwAgent api
  return hwSwitchSyncers_.begin()->second->getFabricConnectivity();
}

FabricReachabilityStats MultiHwSwitchHandler::getFabricReachabilityStats() {
  // TODO - support with multiple switches
  CHECK_EQ(hwSwitchSyncers_.size(), 1);
  return hwSwitchSyncers_.begin()->second->getFabricReachabilityStats();
}

std::vector<PortID> MultiHwSwitchHandler::getSwitchReachability(
    SwitchID switchId) {
  return hwSwitchSyncers_.begin()->second->getSwitchReachability(switchId);
}

bool MultiHwSwitchHandler::needL2EntryForNeighbor(
    const cfg::SwitchConfig* config) const {
  for (auto& entry : hwSwitchSyncers_) {
    if (entry.second->needL2EntryForNeighbor(config)) {
      return true;
    }
  }
  return false;
}

bool MultiHwSwitchHandler::isHwSwitchConnected(const SwitchID& switchId) {
  // for monolithic mode, we always return true, as we are not using
  // connectionStatusTable_ in this case
  if (sw_->isRunModeMonolithic()) {
    return true;
  }
  return connectionStatusTable_.getConnectionStatus(switchId) == 1;
}

std::unique_ptr<TxPacket> MultiHwSwitchHandler::allocatePacket(uint32_t size) {
  // TODO - support with multiple switches
  CHECK_GE(hwSwitchSyncers_.size(), 1);
  return hwSwitchSyncers_.begin()->second->allocatePacket(size);
}

bool MultiHwSwitchHandler::sendPacketOutOfPortAsync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) noexcept {
  auto switchId = sw_->getScopeResolver()->scope(portID).switchId();
  auto iter = hwSwitchSyncers_.find(switchId);
  if (iter == hwSwitchSyncers_.end()) {
    XLOG(ERR) << " hw switch syncer for switch id " << switchId << " not found";
    return false;
  }
  return iter->second->sendPacketOutOfPortAsync(std::move(pkt), portID, queue);
}

template <typename SendFn>
bool MultiHwSwitchHandler::sendPacketSwitchedImpl(
    std::unique_ptr<TxPacket> pkt,
    const LocalSwitchIDs& switchIds,
    SendFn sendFn) noexcept {
  if (hwSwitchSyncers_.empty()) {
    XLOG_EVERY_MS(WARNING, 5000) << "no hw switch syncers available";
    return false;
  }
  if (!switchIds.empty()) {
    bool sent = false;
    // Collect valid connected targets
    std::vector<HwSwitchHandler*> targets;
    for (auto switchId : switchIds) {
      auto iter = hwSwitchSyncers_.find(switchId);
      if (iter == hwSwitchSyncers_.end()) {
        XLOG_EVERY_MS(WARNING, 5000)
            << "hw switch syncer for switch id " << switchId << " not found";
        continue;
      }
      if (!isHwSwitchConnected(switchId)) {
        continue;
      }
      targets.push_back(iter->second.get());
    }
    // Send to each target, cloning packet for all but the last
    for (size_t i = 0; i + 1 < targets.size(); i++) {
      sent |= sendFn(targets[i], pkt->clone());
    }
    if (!targets.empty()) {
      sent |= sendFn(targets.back(), std::move(pkt));
    }
    return sent;
  }
  // Fallback: use first available connected switch to send pkt
  for (auto& hwSwitchHandler : hwSwitchSyncers_) {
    if (isHwSwitchConnected(hwSwitchHandler.first)) {
      return sendFn(hwSwitchHandler.second.get(), std::move(pkt));
    }
  }
  return false;
}

bool MultiHwSwitchHandler::sendPacketSwitchedSync(
    std::unique_ptr<TxPacket> pkt,
    const LocalSwitchIDs& switchIds) noexcept {
  return sendPacketSwitchedImpl(
      std::move(pkt),
      switchIds,
      [](HwSwitchHandler* handler, std::unique_ptr<TxPacket> p) {
        return handler->sendPacketSwitchedSync(std::move(p));
      });
}

bool MultiHwSwitchHandler::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt,
    const LocalSwitchIDs& switchIds) noexcept {
  return sendPacketSwitchedImpl(
      std::move(pkt),
      switchIds,
      [](HwSwitchHandler* handler, std::unique_ptr<TxPacket> p) {
        return handler->sendPacketSwitchedAsync(std::move(p));
      });
}

bool MultiHwSwitchHandler::sendPacketOutOfPortSyncForPktType(
    std::unique_ptr<TxPacket> pkt,
    const PortID& portID,
    PacketType packetType) noexcept {
  auto switchId = sw_->getScopeResolver()->scope(portID).switchId();
  auto iter = hwSwitchSyncers_.find(switchId);
  if (iter == hwSwitchSyncers_.end()) {
    XLOG_EVERY_MS(WARNING, 5000)
        << "hw switch syncer for switch id " << switchId << " not found";
    return false;
  }
  if (!isHwSwitchConnected(switchId)) {
    return false;
  }
  return iter->second->sendPacketOutOfPortSyncForPktType(
      std::move(pkt), portID, packetType);
}

std::map<SwitchID, HwSwitchHandler*> MultiHwSwitchHandler::getHwSwitchHandlers()
    const {
  std::map<SwitchID, HwSwitchHandler*> handlers;
  for (const auto& [switchId, syncer] : hwSwitchSyncers_) {
    auto handler = static_cast<HwSwitchHandler*>(syncer.get());
    handlers.emplace(switchId, handler);
  }
  return handlers;
}

multiswitch::StateOperDelta MultiHwSwitchHandler::getNextStateOperDelta(
    int64_t switchId,
    std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
    int64_t lastUpdateSeqNum) {
  if (!isRunning()) {
    throw FbossError("multi hw switch syncer not started");
  }
  auto iter = hwSwitchSyncers_.find(SwitchID(switchId));
  if (iter == hwSwitchSyncers_.end()) {
    throw FbossError("No hw switch syncer for switch id: ", switchId);
  }
  return iter->second->getNextStateOperDelta(
      std::move(prevOperResult), lastUpdateSeqNum);
}

void MultiHwSwitchHandler::notifyHwSwitchGracefulExit(int64_t switchId) {
  notifyHwSwitchDisconnected(switchId, true);
}

void MultiHwSwitchHandler::notifyHwSwitchDisconnected(
    int64_t switchId,
    bool gracefulExit) {
  if (!isRunning()) {
    throw FbossError("multi hw switch syncer not started");
  }
  auto iter = hwSwitchSyncers_.find(SwitchID(switchId));
  if (iter == hwSwitchSyncers_.end()) {
    throw FbossError("No hw switch syncer for switch id: ", switchId);
  }

  auto removed = gracefulExit
      ? connectionStatusTable_.gracefullyExited(SwitchID(switchId))
      : connectionStatusTable_.disconnected(SwitchID(switchId));
  if (removed) {
    // cancel any pending long poll request
    iter->second->notifyHwSwitchDisconnected();
    if (!gracefulExit) {
      sw_->setPortsDownForSwitch(SwitchID(switchId));
    }
  }
}

bool MultiHwSwitchHandler::waitUntilHwSwitchConnected() {
  return connectionStatusTable_.waitUntilHwSwitchConnected();
}

bool MultiHwSwitchHandler::waitUntilAllHwSwitchesConnected() {
  return connectionStatusTable_.waitUntilHwSwitchConnected(
      hwSwitchSyncers_.size());
}

std::map<int32_t, SwitchRunState> MultiHwSwitchHandler::getHwSwitchRunStates() {
  std::map<int32_t, SwitchRunState> runStates;
  if (!isRunning()) {
    throw FbossError("multi hw switch handler not started");
  }
  for (const auto& [switchId, syncer] : hwSwitchSyncers_) {
    runStates[static_cast<int32_t>(switchId)] = syncer->getHwSwitchRunState();
  }
  return runStates;
}

void MultiHwSwitchHandler::fillHwAgentConnectionStatus(AgentStats& agentStats) {
  for (const auto& [switchId, _] : hwSwitchSyncers_) {
    auto switchIndex =
        sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
    agentStats.hwagentConnectionStatus()[switchIndex] =
        connectionStatusTable_.getConnectionStatus(switchId);
  }
}

state::SwitchState MultiHwSwitchHandler::reconstructSwitchState(SwitchID id) {
  throw FbossError(
      "reconstructSwitchState Not implemented in MultiHwSwitchHandler");
}

} // namespace facebook::fboss
