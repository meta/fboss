// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/mnpu/MultiSwitchHwSwitchHandler.h"

#include <algorithm>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/MultiSwitchPacketStreamMap.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/state/StateDelta.h"

DEFINE_int32(oper_delta_ack_timeout, 600, "Oper delta ack timeout in seconds");

namespace facebook::fboss {

MultiSwitchHwSwitchHandler::MultiSwitchHwSwitchHandler(
    const SwitchID& switchId,
    const cfg::SwitchInfo& info,
    SwSwitch* sw)
    : HwSwitchHandler(switchId, info), sw_(sw) {}

std::unique_ptr<TxPacket> MultiSwitchHwSwitchHandler::allocatePacket(
    uint32_t size) const {
  return TxPacket::allocateTxPacket(size);
}

bool MultiSwitchHwSwitchHandler::sendPacketOutOfPortAsync(
    std::unique_ptr<TxPacket> pkt,
    PortID portID,
    std::optional<uint8_t> queue) noexcept {
  return sendPacketOutViaThriftStream(std::move(pkt), portID, queue);
}

bool MultiSwitchHwSwitchHandler::sendPacketSwitchedSync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  return sendPacketOutViaThriftStream(std::move(pkt));
}

bool MultiSwitchHwSwitchHandler::sendPacketSwitchedAsync(
    std::unique_ptr<TxPacket> pkt) noexcept {
  return sendPacketOutViaThriftStream(std::move(pkt));
}

bool MultiSwitchHwSwitchHandler::sendPacketOutOfPortSyncForPktType(
    std::unique_ptr<TxPacket> pkt,
    const PortID& portID,
    PacketType packetType) noexcept {
  return sendPacketOutViaThriftStream(
      std::move(pkt), portID, std::nullopt /*queue*/, packetType);
}

bool MultiSwitchHwSwitchHandler::transactionsSupported(
    std::optional<cfg::SdkVersion> sdkVersion) const {
  auto asicType = getSwitchInfo().asicType().value();
  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    return true;
  }
  if (asicType == cfg::AsicType::ASIC_TYPE_EBRO ||
      asicType == cfg::AsicType::ASIC_TYPE_P200 ||
      asicType == cfg::AsicType::ASIC_TYPE_YUBA ||
      asicType == cfg::AsicType::ASIC_TYPE_G202X) {
    return true;
  }
  if (sdkVersion.has_value() && sdkVersion.value().saiSdk().has_value()) {
    return true;
  }
  return false;
}

std::map<PortID, FabricEndpoint>
MultiSwitchHwSwitchHandler::getFabricConnectivity() const {
  // TODO - retire this after clients move to HwAgent thrift api
  return std::map<PortID, FabricEndpoint>();
}

std::vector<PortID> MultiSwitchHwSwitchHandler::getSwitchReachability(
    SwitchID /*switchId*/) const {
  return std::vector<PortID>();
}

FabricReachabilityStats MultiSwitchHwSwitchHandler::getFabricReachabilityStats()
    const {
  // TODO: implement this
  return {};
}

bool MultiSwitchHwSwitchHandler::needL2EntryForNeighbor(
    const cfg::SwitchConfig* config) const {
  auto asicType = getSwitchInfo().asicType().value();
  if (asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
      asicType == cfg::AsicType::ASIC_TYPE_CHENAB2) {
    return false;
  }
  // if config is not present, fall back to true
  // if sdk version is not set (for test configs), fall back to true
  // if sai, return true else false
  // falling back to true as assumption is split mode is for SAI alone
  return !config || !config->sdkVersion().has_value() ||
      config->sdkVersion()->saiSdk().has_value();
}

bool MultiSwitchHwSwitchHandler::sendPacketOutViaThriftStream(
    std::unique_ptr<TxPacket> pkt,
    std::optional<PortID> portID,
    std::optional<uint8_t> queue,
    std::optional<PacketType> packetType) {
  SwitchID switchId;
  // Find the actual switch ID that owns the port
  if (portID.has_value()) {
    try {
      // Use ScopeResolver to find which switch owns this port
      auto matcher = sw_->getScopeResolver()->scope(*portID);
      switchId = matcher.switchId();
    } catch (const std::exception& ex) {
      // If we can't find the switch ID for the port, log and use default
      switchId = getSwitchId();
      XLOG_EVERY_MS(ERR, 5000)
          << "Send packet: Failure to get switch ID for port " << *portID
          << ": " << ex.what() << ", using default switch ID " << switchId;
    }
  } else {
    switchId = getSwitchId();
  }
  return sw_->sendPacketOutViaThriftStream(
      std::move(pkt), switchId, std::move(portID), queue, packetType);
}

bool MultiSwitchHwSwitchHandler::checkOperSyncStateLocked(
    HwSwitchOperDeltaSyncState state,
    const std::unique_lock<std::mutex>& /*lock*/) const {
  return operDeltaSyncState_ == state;
}

/*
 * - A successful operation in HwSwitch would return empty operDelta
 *   (desired == applied)
 * - A failed operation in HwSwitch would return the oper delta between the
 *   applied to desired state
 * - For cancelled case, return the full operDelta equivalent to vector of
 *   deltas passed in to maintain similar semantics for all responses
 */
std::pair<fsdb::OperDelta, HwSwitchStateUpdateStatus>
MultiSwitchHwSwitchHandler::stateChanged(
    const std::vector<fsdb::OperDelta>& deltas,
    bool transaction,
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  multiswitch::StateOperDelta stateDelta;
  CHECK_GE(deltas.size(), 1);
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    SCOPE_EXIT {
      // clear any ack before waiting. initial sync might set ack
      // when there is no update thread waiting for ack
      prevOperDeltaResult_ = nullptr;
    };
    prevUpdateSwitchState_ = newState;
    if (checkOperSyncStateLocked(
            HwSwitchOperDeltaSyncState::DISCONNECTED, lk) ||
        checkOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk)) {
      // return incoming delta to indicate that none of the changes were applied
      return {
          StateDelta(oldState, newState).getOperDelta(),
          HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_CANCELLED};
    }
    // block state update till hwswitch resync is complete
    if (checkOperSyncStateLocked(
            HwSwitchOperDeltaSyncState::INITIAL_SYNC_SENT, lk) ||
        pauseStateUpdates_) {
      // Wait for initial sync to complete
      if (!waitForOperSyncAck(lk, FLAGS_oper_delta_ack_timeout)) {
        setOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk);
        // initial sync was cancelled
        return {
            StateDelta(oldState, newState).getOperDelta(),
            HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_CANCELLED};
      }
    }
    fillMultiswitchOperDelta(
        stateDelta,
        prevUpdateSwitchState_,
        deltas,
        transaction,
        currOperDeltaSeqNum_,
        hwWriteBehavior,
        deltaApplicationBehavior);
    ++currOperDeltaSeqNum_;
    nextOperDelta_ = &stateDelta;
  }
  // state update ready. notify waiting thread
  stateUpdateCV_.notify_one();
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    SCOPE_EXIT {
      nextOperDelta_ = nullptr;
    };
    if (waitForOperSyncAck(lk, FLAGS_oper_delta_ack_timeout)) {
      SCOPE_EXIT {
        prevOperDeltaResult_ = nullptr;
      };
      // received ack. return result from HwSwitch
      CHECK_EQ(prevOperDeltaResult_->operDeltas()->size(), 1);
      auto prevOperDeltaResponse = prevOperDeltaResult_->operDeltas()->back();
      return {
          prevOperDeltaResponse,
          prevOperDeltaResponse.changes()->empty()
              ? HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_SUCCEEDED
              : HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_FAILED};
    } else {
      setOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk);
      // return incoming delta to indicate that none of the changes were applied
      return {
          StateDelta(oldState, newState).getOperDelta(),
          HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_CANCELLED};
    }
  }
}

multiswitch::StateOperDelta MultiSwitchHwSwitchHandler::getNextStateOperDelta(
    std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
    int64_t lastUpdateSeqNum) {
  bool serveOperSyncRequest{false};
  SCOPE_EXIT {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    // Reset the flag only if we are serving the request.
    if (serveOperSyncRequest) {
      operRequestInProgress_ = false;
      pauseStateUpdates_ = false;
    }
  };
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    // check if there is a pending oper delta request. This can happen if
    // client resends request due to thrift shutdown. Ignore the
    // new request and force full sync by setting state to cancelled.
    if (operRequestInProgress_) {
      XLOG(DBG2)
          << "Ignoring oper delta request as another request is in progress for hwswitch "
          << getSwitchId();
      // Reset the last acked seqnum to -1 so that next request results in full
      // sync
      lastAckedOperDeltaSeqNum_ = -1;
      // Indicate that new state updates are blocked till old request times out
      pauseStateUpdates_ = true;
      multiswitch::StateOperDelta cancelledResponse;
      cancelledResponse.seqNum() = 0;
      return cancelledResponse;
    }
    serveOperSyncRequest = true;
    operRequestInProgress_ = true;
    // Deltas are numbered from 1, so a client seqnum of 0 is never an ack: the
    // HwSwitch restarted and has nothing programmed. lastAckedOperDeltaSeqNum_
    // can itself be 0, set below by a HwSwitch that connected before SwSwitch
    // had any state to send.
    if (!checkOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk) &&
        lastUpdateSeqNum > 0 &&
        (lastUpdateSeqNum == lastAckedOperDeltaSeqNum_)) {
      // HwSwitch has resent an ack for the previous delta. This indicates
      // that hwswitch timedout waiting for a new oper delta to be available.
      // Wait for a new delta to be available.
    } else if (
        !lastUpdateSeqNum || (lastUpdateSeqNum != currOperDeltaSeqNum_) ||
        checkOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk)) {
      // Mark initial sync needed if seq number from client is 0 or
      // mismatches with the current expected sequence number
      XLOG(DBG2) << "Need resync for hwswitch:" << getSwitchId()
                 << " last seen seqnum=" << lastUpdateSeqNum
                 << " curr seqnum=" << currOperDeltaSeqNum_;
      if (lastUpdateSeqNum <= 0 && currOperDeltaSeqNum_ > 0) {
        XLOG(WARNING) << "hwswitch:" << getSwitchId()
                      << " reconnected with nothing programmed; deltas up to "
                      << currOperDeltaSeqNum_
                      << " never reached hardware, resyncing from scratch";
      }
      sw_->getHwSwitchHandler()->connected(getSwitchId());
      // If HwSwitchHandler has a valid state, send full sync delta
      if (prevUpdateSwitchState_) {
        setOperSyncStateLocked(
            HwSwitchOperDeltaSyncState::INITIAL_SYNC_SENT, lk);
        // A previous session's seqnum can match this sync's and drop its ack.
        lastAckedOperDeltaSeqNum_ = -1;
        multiswitch::StateOperDelta fullOperResponse;
        fullOperResponse.seqNum() = ++currOperDeltaSeqNum_;
        // TODO (ravi) This state needs to go through consolidater as well
        fullOperResponse.operDeltas() = {
            getFullSyncOperDelta(prevUpdateSwitchState_)};
        fullOperResponse.isFullState() = true;
        return fullOperResponse;
      } else {
        // Swswitch received an operdelta request before it had a chance to set
        // the first switch state. Mark hwswitchsyncer state as connected so
        // that next state update will include this HwSwitch. The first
        // operdelta will be a full sync delta. ie
        // StateDelta(std::make_shared<SwitchState>(), initialState). So it is
        // fine to send the first operdelta as a full sync delta.
        setOperSyncStateLocked(HwSwitchOperDeltaSyncState::SYNC_CONNECTED, lk);
      }
    } else {
      if (checkOperSyncStateLocked(
              HwSwitchOperDeltaSyncState::INITIAL_SYNC_SENT, lk)) {
        setOperSyncStateLocked(HwSwitchOperDeltaSyncState::SYNC_CONNECTED, lk);
      }
      prevOperDeltaResult_ = prevOperResult.get();
    }
    lastAckedOperDeltaSeqNum_ = lastUpdateSeqNum;
  }
  stateUpdateCV_.notify_one();
  // wait for new delta to be available or for cancellation.
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    SCOPE_EXIT {
      prevOperDeltaResult_ = nullptr;
    };
    // Wait for either delta to be available or for cancellation.
    // Clients does a timed wait for response from swswitch to avoid waiting
    // forever if the server crashes. Server also needs to do a timed wait
    // so that the worker threads corresponding to timed out clients do not
    // block on server forever
    if (nextOperDelta_ ||
        waitForOperDeltaReady(lk, FLAGS_oper_sync_req_timeout)) {
      SCOPE_EXIT {
        nextOperDelta_ = nullptr;
      };
      return *nextOperDelta_;
    } else {
      // return empty delta to HwSwitch incase of cancellation or timeout.
      // cancellation occurs when the client disconnects or when server
      // undergoes a graceful shutdown.
      multiswitch::StateOperDelta cancelledResponse;
      // currOperDeltaSeqNum_ may be ahead of what this HwSwitch applied;
      // echoing it would let it return claiming to be caught up.
      // lastAckedOperDeltaSeqNum_ can itself be negative (e.g. reset to -1 to
      // force a resync), so floor at 0 to avoid handing back a seqnum the
      // client would misread as a prior ack.
      cancelledResponse.seqNum() =
          std::max<int64_t>(lastAckedOperDeltaSeqNum_, 0);
      return cancelledResponse;
    }
  }
}

void MultiSwitchHwSwitchHandler::notifyHwSwitchDisconnected() {
  // cancel any pending operations.
  cancelOperDeltaSync();
}

void MultiSwitchHwSwitchHandler::cancelOperDeltaSync() {
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    setOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk);
    nextOperDelta_ = nullptr;
  }
  stateUpdateCV_.notify_all();
}

MultiSwitchHwSwitchHandler::~MultiSwitchHwSwitchHandler() {
  cancelOperDeltaSync();
}

HwSwitchOperDeltaSyncState
MultiSwitchHwSwitchHandler::getHwSwitchOperDeltaSyncState() {
  std::unique_lock<std::mutex> lk(stateUpdateMutex_);
  return operDeltaSyncState_;
}

SwitchRunState MultiSwitchHwSwitchHandler::getHwSwitchRunState() {
  HwSwitchOperDeltaSyncState syncState;
  {
    std::unique_lock<std::mutex> lk(stateUpdateMutex_);
    syncState = getOperSyncState();
  }

  switch (syncState) {
    case HwSwitchOperDeltaSyncState::DISCONNECTED:
      return SwitchRunState::UNINITIALIZED;
    case HwSwitchOperDeltaSyncState::INITIAL_SYNC_SENT:
      return SwitchRunState::INITIALIZED;
    case HwSwitchOperDeltaSyncState::SYNC_CONNECTED:
      return SwitchRunState::CONFIGURED;
    case HwSwitchOperDeltaSyncState::CANCELLED:
      return SwitchRunState::EXITING;
  }
  throw FbossError("Unknown hw switch run state");
}

bool MultiSwitchHwSwitchHandler::waitForOperSyncAck(
    std::unique_lock<std::mutex>& lk,
    uint64_t timeoutInSec) {
  if (!stateUpdateCV_.wait_for(
          lk, std::chrono::seconds(timeoutInSec), [this, &lk] {
            return (
                prevOperDeltaResult_ ||
                checkOperSyncStateLocked(
                    HwSwitchOperDeltaSyncState::CANCELLED, lk));
          })) {
    XLOG(DBG2) << "Timed out waiting oper delta ack from switch "
               << getSwitchId();
    operDeltaAckTimeout();
    sw_->getHwSwitchHandler()->disconnected(getSwitchId());
    return false;
  }
  return checkOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk)
      ? false
      : true;
}

bool MultiSwitchHwSwitchHandler::waitForOperDeltaReady(
    std::unique_lock<std::mutex>& lk,
    uint64_t timeoutInSec) {
  if (!stateUpdateCV_.wait_for(
          lk, std::chrono::seconds(timeoutInSec), [this, &lk] {
            return (
                nextOperDelta_ ||
                checkOperSyncStateLocked(
                    HwSwitchOperDeltaSyncState::CANCELLED, lk));
          })) {
    XLOG(DBG3) << "Timed out waiting for next state oper delta for switch "
               << getSwitchId();
    return false;
  }
  return checkOperSyncStateLocked(HwSwitchOperDeltaSyncState::CANCELLED, lk)
      ? false
      : true;
}

void MultiSwitchHwSwitchHandler::fillMultiswitchOperDelta(
    multiswitch::StateOperDelta& stateDelta,
    const std::shared_ptr<SwitchState>& state,
    const std::vector<fsdb::OperDelta>& deltas,
    bool transaction,
    int64_t lastSeqNum,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  // Send full delta if this is first switchstate update.
  // Sequence number 0 indicates first update
  if (lastSeqNum == 0) {
    stateDelta.isFullState() = true;
    // TODO (ravi) This state needs to go through consolidater as well
    stateDelta.operDeltas() = {getFullSyncOperDelta(state)};
    CHECK(!transaction);
  } else {
    stateDelta.isFullState() = false;
    stateDelta.operDeltas() = deltas;
  }
  stateDelta.transaction() = transaction;
  stateDelta.seqNum() = lastSeqNum + 1;
  stateDelta.hwWriteBehavior() = hwWriteBehavior;
  if (deltaApplicationBehavior.has_value()) {
    stateDelta.deltaApplicationBehavior() = deltaApplicationBehavior.value();
  }
}

void MultiSwitchHwSwitchHandler::operDeltaAckTimeout() {
  auto switchIndex =
      sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(getSwitchId());
  sw_->stats()->hwAgentUpdateTimeout(switchIndex);
}

state::SwitchState MultiSwitchHwSwitchHandler::reconstructSwitchState() {
  throw FbossError(
      "reconstructSwitchState Not implemented in MultiSwitchHwSwitchHandler");
}

bool MultiSwitchHwSwitchHandler::isValidStateUpdate(
    const StateDelta& /*delta*/) const {
  throw FbossError(
      "isValidStateUpdate Not implemented in MultiSwitchHwSwitchHandler, use state update validator instead");
}
} // namespace facebook::fboss
