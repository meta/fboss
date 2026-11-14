/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/mnpu/OperDeltaSyncer.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/HwSwitch.h"
#include "fboss/agent/TracingStackCheckpoint.h"
#include "fboss/agent/state/StateDelta.h"

#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/ReconnectingRequestChannel.h>
#include <thrift/lib/cpp2/async/RetryingRequestChannel.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include <memory>

DEFINE_int32(
    oper_sync_timeout,
    0x7fffffff, /* 24.86 days - max supported by setTimeout api */
    "idle timeout for oper sync client in milliseconds");

namespace facebook::fboss {

OperDeltaSyncer::OperDeltaSyncer(
    uint16_t serverPort,
    SwitchID switchId,
    HwSwitch* hw)
    : serverPort_(serverPort),
      switchId_(switchId),
      hw_(hw),
      operSyncEvbThread_(new folly::ScopedEventBaseThread("OperSyncThread")) {
  initOperDeltaSync();
}

void OperDeltaSyncer::initOperDeltaSync() {
  auto reconnectingChannel =
      apache::thrift::PooledRequestChannel::newSyncChannel(
          operSyncEvbThread_, [this](folly::EventBase& evb) {
            return apache::thrift::RetryingRequestChannel::newChannel(
                evb,
                2, /*retries before error*/
                apache::thrift::ReconnectingRequestChannel::newChannel(
                    *operSyncEvbThread_->getEventBase(),
                    [this](folly::EventBase& evb) {
                      auto socket = folly::AsyncSocket::UniquePtr(
                          new folly::AsyncSocket(&evb));
                      socket->connect(
                          nullptr, folly::SocketAddress("::1", serverPort_));
                      auto channel =
                          apache::thrift::RocketClientChannel::newChannel(
                              std::move(socket));
                      channel->setTimeout(FLAGS_oper_sync_timeout);
                      return channel;
                    }));
          });
  operSyncClient_.reset(
      new apache::thrift::Client<multiswitch::MultiSwitchCtrl>(
          std::move(reconnectingChannel)));
}

void OperDeltaSyncer::startOperSync() {
  operSyncRunning_.store(true);
  operSyncThread_ = std::make_unique<std::thread>([this]() { operSyncLoop(); });
}

void OperDeltaSyncer::operSyncLoop() {
  auto lastUpdateResult = fsdb::OperDelta();
  int64_t lastUpdateSeqNum{0};
  while (operSyncRunning_.load()) {
    multiswitch::StateOperDelta lastOperDeltaResult;
    lastOperDeltaResult.operDeltas() = {lastUpdateResult};
    multiswitch::StateOperDelta stateOperDelta;
    apache::thrift::RpcOptions options;
    /*
     * Set timeout in RPC call to avoid forever blocking wait when server
     * crashes. The timeout on client is set to be more than the timeout on
     * server side wait for next oper delta to be available so that in a
     * normal sequence of operation, client gets an empty delta periodically
     * and reconnects when new state update is not available. The client side
     * timeout should happen only when the server crashes.
     */
    options.setTimeout(std::chrono::seconds(2 * FLAGS_oper_sync_req_timeout));
    try {
      const TracingStackCheckpoint checkpoint;
      operSyncClient_->sync_getNextStateOperDelta(
          options,
          stateOperDelta,
          switchId_,
          lastOperDeltaResult,
          lastUpdateSeqNum);
    } catch (const apache::thrift::transport::TTransportException& ex) {
      if (ex.getType() ==
          apache::thrift::transport::TTransportException::TIMED_OUT) {
        XLOG(DBG2) << "Timed out waiting for next oper delta from swswitch";
      }
      continue;
    } catch (const std::exception& ex) {
      XLOG_EVERY_MS(ERR, 5000)
          << fmt::format("Failed to get next oper delta: {}", ex.what());
      lastUpdateSeqNum = 0;
      continue;
    }
    // SwSwitch can send empty operdelta when cancelling the service on
    // shutdown
    if (operSyncRunning_.load() && stateOperDelta.operDeltas()->size() &&
        stateOperDelta.operDeltas()->back().changes()->size()) {
      if (*stateOperDelta.isFullState()) {
        XLOG(DBG2) << "Received full state oper delta from swswitch";
        lastUpdateResult = processFullOperDelta(
            stateOperDelta, *stateOperDelta.hwWriteBehavior());
      } else {
        auto oldState = hw_->getProgrammedState();
        lastUpdateResult = stateOperDelta.transaction().value()
            ? hw_->stateChangedTransaction(
                  *stateOperDelta.operDeltas(),
                  HwWriteBehaviorRAII(*stateOperDelta.hwWriteBehavior()),
                  *stateOperDelta.deltaApplicationBehavior())
            : hw_->stateChanged(
                  *stateOperDelta.operDeltas(),
                  HwWriteBehaviorRAII(*stateOperDelta.hwWriteBehavior()));
        if (lastUpdateResult.changes()->empty()) {
          hw_->getPlatform()->stateChanged(
              StateDelta(oldState, hw_->getProgrammedState()));
        }
      }

      // If swswitch has transitioned to configured state,
      // then move hwswitch as well
      if ((hw_->getRunState() == SwitchRunState::INITIALIZED) &&
          (utility::getFirstNodeIf(
               hw_->getProgrammedState()->getSwitchSettings())
               ->getSwSwitchRunState() == SwitchRunState::CONFIGURED)) {
        hw_->switchRunStateChanged(SwitchRunState::CONFIGURED);
        hw_->initialStateApplied();
      }
    }
    lastUpdateSeqNum = *stateOperDelta.seqNum();
  }
}

fsdb::OperDelta OperDeltaSyncer::processFullOperDelta(
    multiswitch::StateOperDelta& stateOperDelta,
    const HwWriteBehavior& hwWriteBehavior) {
  // Enable deep comparison for full oper delta
  std::vector<StateDelta> deltas;
  DeltaComparison::PolicyRAII policyGuard{DeltaComparison::Policy::DEEP};
  CHECK_GE(stateOperDelta.operDeltas()->size(), 1);
  auto oldState = std::make_shared<SwitchState>();
  auto prevState = hw_->getProgrammedState();
  for (const auto& operDelta : *stateOperDelta.operDeltas()) {
    auto currentStateDelta = StateDelta(oldState, operDelta);
    deltas.emplace_back(prevState, currentStateDelta.newState());
    oldState = deltas.back().newState();
    prevState = deltas.back().newState();
  }
  auto appliedState =
      hw_->stateChanged(deltas, HwWriteBehaviorRAII(hwWriteBehavior));
  // return empty oper delta to indicate success. If update was not successful,
  // hwswitch would have crashed.
  CHECK(isStateDeltaEmpty(StateDelta(deltas.back().newState(), appliedState)));
  // TODO (ravi) verify if this requires full vector
  hw_->getPlatform()->stateChanged(deltas.back());
  return fsdb::OperDelta{};
}

void OperDeltaSyncer::stopOperSync() {
  // stop any new requests
  operSyncRunning_.store(false);
  // send exit notification to server with short timeout
  try {
    apache::thrift::RpcOptions options;
    options.setTimeout(std::chrono::milliseconds(1000));
    operSyncClient_->sync_gracefulExit(options, switchId_);
  } catch (const std::exception& ex) {
    XLOG(ERR) << fmt::format(
        "Failed to send graceful exit notification to swswitch: {}", ex.what());
  }
  if (operSyncThread_) {
    operSyncThread_->join();
  }
}
} // namespace facebook::fboss
