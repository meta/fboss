/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/mnpu/SplitAgentThriftSyncerClient.h"
#include "fboss/lib/thrift_service_client/ConnectionOptions.h"

#include <thrift/lib/cpp2/async/PooledRequestChannel.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>

namespace {
DEFINE_int32(
    hwagent_reconnect_ms,
    1000,
    "reconnect to swswitch in milliseconds");

DEFINE_int32(
    hwagent_heartbeat_interval_ms,
    1000,
    "heartbeat interval for sink and stream in milliseconds");
} // namespace

namespace facebook::fboss {

SplitAgentThriftClient::SplitAgentThriftClient(
    const std::string& clientId,
    std::shared_ptr<folly::ScopedEventBaseThread> streamEvbThread,
    folly::EventBase* connRetryEvb,
    const std::string& counterPrefix,
    StreamStateChangeCb stateChangeCb,
    uint16_t serverPort,
    const SwitchID& switchId)
    : ReconnectingThriftClient(
          clientId,
          streamEvbThread->getEventBase(),
          connRetryEvb,
          counterPrefix,
          "multi_switch_streams",
          [this, stateChangeCb](State oldState, State newState) {
            // Track state transitions in health monitor
            if (ipcHealthMonitor_) {
              IpcConnectionState oldIpcState =
                  mapToIpcConnectionState(oldState);
              IpcConnectionState newIpcState =
                  mapToIpcConnectionState(newState);
              ipcHealthMonitor_->trackStateTransition(oldIpcState, newIpcState);
            }
            // Call the original callback if provided
            if (stateChangeCb) {
              stateChangeCb(oldState, newState);
            }
          },
          FLAGS_hwagent_reconnect_ms),
      streamEvbThread_(streamEvbThread),
      serverPort_(serverPort),
      switchId_(switchId),
      ipcHealthMonitor_(
          std::make_unique<IpcHealthMonitor>(clientId, counterPrefix)) {
  counterPrefix_ = counterPrefix;
  auto thriftClientHeartbeatFunc = [](int /* unused */, int /* unused */) {};

  thriftClientHeartbeat_ = std::make_shared<ThreadHeartbeat>(
      streamEvbThread_->getEventBase(),
      counterPrefix_,
      FLAGS_hwagent_heartbeat_interval_ms,
      thriftClientHeartbeatFunc);

  setConnectionOptions(utils::ConnectionOptions("::1", serverPort_));
  scheduleTimeout();

  // Start IPC health monitoring
  if (ipcHealthMonitor_) {
    ipcHealthMonitor_->start();
  }
}

SplitAgentThriftClient::~SplitAgentThriftClient() {
  // Stop IPC health monitoring
  if (ipcHealthMonitor_) {
    ipcHealthMonitor_->stop();
  }
}

std::shared_ptr<ThreadHeartbeat>
SplitAgentThriftClient::getThriftClientHeartbeat() {
  return thriftClientHeartbeat_;
}
void SplitAgentThriftClient::connectClient(
    const utils::ConnectionOptions& options) {
  auto channel = apache::thrift::PooledRequestChannel::newChannel(
      streamEvbThread_->getEventBase(),
      streamEvbThread_,
      [options = options](folly::EventBase& evb) mutable {
        return apache::thrift::RocketClientChannel::newChannel(
            folly::AsyncSocket::UniquePtr(
                new folly::AsyncSocket(&evb, options.getDstAddr())));
      });

  multiSwitchClient_.reset(
      new apache::thrift::Client<multiswitch::MultiSwitchCtrl>(
          std::move(channel)));
}

void SplitAgentThriftClient::connectToServer(
    const utils::ConnectionOptions& options) {
  try {
    connectClient(options);
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Connect to server failed with ex:" << ex.what();
    setState(State::DISCONNECTED);
    return;
  }
  try {
    startClientService();
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Error while starting stream or sink from " << clientId()
              << " : " << ex.what();
    setState(State::DISCONNECTED);
    return;
  }
  // set state to connecting. The event loop will start the sink generator
  // and move state to connected
  setState(State::CONNECTING);
}

#if FOLLY_HAS_COROUTINES
folly::coro::Task<void> SplitAgentThriftClient::serviceLoopWrapper() {
  setState(State::CONNECTED);
  try {
    co_await serveStream();
    // A normal stream end must also go to DISCONNECTED, so that the retry runs.
    if (!isCancelled()) {
      XLOG(INFO) << "Server ended stream for " << clientId();
      bool wasConnected = isConnectedToServer();
      setState(State::DISCONNECTED);
      if (wasConnected) {
        disconnected();
      }
    }
  } catch (const folly::OperationCancelled&) {
    XLOG(DBG2) << "Server cancelled stream for " << clientId();
    bool wasConnected = isConnectedToServer();
    setState(State::CANCELLED);
    if (wasConnected) {
      disconnected();
    }
  } catch (const std::exception& ex) {
    XLOG(ERR) << "Error while notifying events from " << clientId() << " : "
              << ex.what();
    bool wasConnected = isConnectedToServer();
    setState(State::DISCONNECTED);
    if (wasConnected) {
      disconnected();
    }
  }
  co_return;
}
#endif

apache::thrift::Client<multiswitch::MultiSwitchCtrl>*
SplitAgentThriftClient::getThriftClient() {
  return multiSwitchClient_.get();
}

void SplitAgentThriftClient::resetClient() {
  multiSwitchClient_.reset();
}

IpcConnectionState SplitAgentThriftClient::mapToIpcConnectionState(
    State state) {
  switch (state) {
    case State::CONNECTED:
      return IpcConnectionState::CONNECTED;
    case State::DISCONNECTED:
      return IpcConnectionState::DISCONNECTED;
    case State::CONNECTING:
      return IpcConnectionState::CONNECTING;
    case State::CANCELLED:
      return IpcConnectionState::CANCELLED;
    default:
      return IpcConnectionState::UNKNOWN;
  }
}

template <typename CallbackObjectT, typename EventQueueT>
ThriftSinkClient<CallbackObjectT, EventQueueT>::ThriftSinkClient(
    folly::StringPiece name,
    uint16_t serverPort,
    SwitchID switchId,
    ThriftSinkConnectFn connectFn,
    std::shared_ptr<folly::ScopedEventBaseThread> eventThread,
#if FOLLY_HAS_COROUTINES
    EventQueueT& eventsQueue,
#endif
    folly::EventBase* retryEvb,
    std::optional<std::string> multiSwitchStatsPrefix)
    : SplitAgentThriftClient(
          std::string(name),
          eventThread,
          retryEvb,
          std::string(name),
          [](State, State) {},
          serverPort,
          switchId),
#if FOLLY_HAS_COROUTINES
      eventsQueue_(eventsQueue),
#endif
      connectFn_(std::move(connectFn)),
      eventsDroppedCount_(
          folly::to<std::string>(
              multiSwitchStatsPrefix.has_value()
                  ? multiSwitchStatsPrefix.value()
                  : "",
              name,
              ".events_dropped"),
          fb303::SUM,
          fb303::RATE),
      eventSentCount_(
          folly::to<std::string>(
              multiSwitchStatsPrefix.has_value()
                  ? multiSwitchStatsPrefix.value()
                  : "",
              name,
              ".events_sent"),
          fb303::SUM,
          fb303::RATE) {
}

template <typename CallbackObjectT, typename EventQueueT>
void ThriftSinkClient<CallbackObjectT, EventQueueT>::startClientService() {
  sinkClient_.reset(new EventNotifierSinkClient(
      connectFn_(getSwitchId(), getThriftClient())));
}

template <typename CallbackObjectT, typename EventQueueT>
void ThriftSinkClient<CallbackObjectT, EventQueueT>::disconnected() {
#if FOLLY_HAS_COROUTINES
  while (!eventsQueue_.empty()) {
    eventsQueue_.try_dequeue();
    // TODO: remove this once ipcHealthMonitor is rolled out
    eventsDroppedCount_.add(1);
    // Track dropped event in health monitor
    if (getHealthMonitor()) {
      getHealthMonitor()->trackEventDropped();
    }
  }
  XLOG(DBG2) << "Discarded events from queue for " << clientId()
             << " on sw agent disconnect";
#endif
}

template <typename CallbackObjectT, typename EventQueueT>
void ThriftSinkClient<CallbackObjectT, EventQueueT>::resetClient() {
  sinkClient_.reset();
  SplitAgentThriftClient::resetClient();
}

#if FOLLY_HAS_COROUTINES
template <typename CallbackObjectT, typename EventQueueT>
folly::coro::Task<void>
ThriftSinkClient<CallbackObjectT, EventQueueT>::serveStream() {
  connected();
  co_await sinkClient_->sink(
      [&]() -> folly::coro::AsyncGenerator<CallbackObjectT&&> {
        while (true) {
          auto event = co_await eventsQueue_.dequeue();
          if (isCancelled()) {
            XLOG(DBG2) << "Cancelled stream for " << clientId();
            co_return;
          }
          co_yield std::move(event);
        }
      }());
  co_return;
}
#endif

template <typename CallbackObjectT, typename EventQueueT>
ThriftSinkClient<CallbackObjectT, EventQueueT>::~ThriftSinkClient() {
  CHECK(isCancelled());
}

/*
 * This function is called when the sink is cancelled on HwAgent shutdown
 * The sink async generator is constantly waiting for next event. In normal
 * sequence of events, the sink generator will get an exception when server
 * is unreachable or client disconnects. However in some cases, the exception
 * is not thrown and the sink generator is blocked forever. For such cases, we
 * need to enqueue a dummy event to unblock the sink generator and exit based on
 * the connection state. Another way to unblock the sink generator is to use
 * cancellation with dequeue. However in this case, the control does not return
 * to the caller resulting in no reconnection attempts when sw agent alone
 * restarts.
 */
template <typename CallbackObjectT, typename EventQueueT>
void ThriftSinkClient<CallbackObjectT, EventQueueT>::onCancellation() {
  auto dummyEvent = CallbackObjectT();
  if constexpr (std::is_same_v<
                    EventQueueT,
                    folly::coro::BoundedQueue<CallbackObjectT, true, true>>) {
    eventsQueue_.try_enqueue(std::move(dummyEvent));
  } else {
    eventsQueue_.enqueue(std::move(dummyEvent));
  }
}

template <typename StreamObjectT>
ThriftStreamClient<StreamObjectT>::ThriftStreamClient(
    folly::StringPiece name,
    uint16_t serverPort,
    SwitchID switchId,
#if FOLLY_HAS_COROUTINES
    ThriftStreamConnectFn connectFn,
#endif
    EventHandlerFn eventHandlerFn,
    HwSwitch* hw,
    std::shared_ptr<folly::ScopedEventBaseThread> eventThread,
    folly::EventBase* retryEvb,
    std::optional<std::string> multiSwitchStatsPrefix)
    : SplitAgentThriftClient(
          std::string(name),
          eventThread,
          retryEvb,
          std::string(name),
          [](State, State) {},
          serverPort,
          switchId),
#if FOLLY_HAS_COROUTINES
      connectFn_(std::move(connectFn)),
#endif
      eventHandlerFn_(std::move(eventHandlerFn)),
      hw_(hw),
      eventReceivedCount_(

          folly::to<std::string>(
              multiSwitchStatsPrefix.has_value()
                  ? multiSwitchStatsPrefix.value()
                  : "",
              name,
              ".events_received"),
          fb303::SUM,
          fb303::RATE) {
}

template <typename StreamObjectT>
void ThriftStreamClient<StreamObjectT>::startClientService() {
#if FOLLY_HAS_COROUTINES
  streamClient_.reset(new EventNotifierStreamClient(
      connectFn_(getSwitchId(), getThriftClient())));
#endif
}

#if FOLLY_HAS_COROUTINES
template <typename StreamObjectT>
folly::coro::Task<void> ThriftStreamClient<StreamObjectT>::serveStream() {
  while (const auto& event = co_await folly::coro::co_withCancellation(
             cancellationSource_.getToken(), streamClient_->next())) {
    if (isCancelled()) {
      co_return;
    }

    auto eventObj = *event;
    eventHandlerFn_(eventObj, hw_);

    eventReceivedCount_.add(1);

    // Track received event in health monitor
    if (getHealthMonitor()) {
      getHealthMonitor()->trackEventReceived();
    }
  }
  co_return;
}
#endif

template <typename StreamObjectT>
void ThriftStreamClient<StreamObjectT>::resetClient() {
#if FOLLY_HAS_COROUTINES
  streamClient_.reset();
#endif
  SplitAgentThriftClient::resetClient();
}

template <typename StreamObjectT>
void ThriftStreamClient<StreamObjectT>::onCancellation() {
  cancellationSource_.requestCancellation();
}

template <typename StreamObjectT>
ThriftStreamClient<StreamObjectT>::~ThriftStreamClient() {
  CHECK(isCancelled());
}

template class ThriftSinkClient<
    multiswitch::LinkChangeEvent,
    LinkChangeEventQueueType>;
template class ThriftSinkClient<multiswitch::FdbEvent, FdbEventQueueType>;
template class ThriftSinkClient<multiswitch::RxPacket, RxPktEventQueueType>;
template class ThriftStreamClient<multiswitch::TxPacket>;
template class ThriftSinkClient<
    multiswitch::HwSwitchStats,
    StatsEventQueueType>;
template class ThriftSinkClient<
    multiswitch::SwitchReachabilityChangeEvent,
    SwitchReachabilityChangeEventQueueType>;

} // namespace facebook::fboss
