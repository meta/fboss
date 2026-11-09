// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/fsdb/client/FsdbStreamClient.h"
#include "fboss/fsdb/common/PathHelpers.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <fmt/core.h>
#include <folly/String.h>
#include <folly/coro/AsyncGenerator.h>

#include <folly/logging/xlog.h>

#include <atomic>
#include <functional>

namespace facebook::fboss::fsdb {

constexpr std::string_view kSubscribeLatencyMetric = "subscribe_latency_ms";

enum class SubscriptionState : uint16_t {
  DISCONNECTED,
  DISCONNECTED_GR_HOLD,
  DISCONNECTED_GR_HOLD_EXPIRED,
  CANCELLED,
  CONNECTED,
};

enum class SubscriptionType {
  UNKNOWN = 0,
  PATH = 1,
  DELTA = 2,
  PATCH = 3,
};

static std::unordered_map<SubscriptionType, std::string> subscriptionTypeToStr =
    {
        {SubscriptionType::PATH, "Path"},
        {SubscriptionType::DELTA, "Delta"},
        {SubscriptionType::PATCH, "Patch"},
};

inline bool isConnected(const SubscriptionState& state) {
  return state == SubscriptionState::CONNECTED;
}

inline bool isDisconnected(const SubscriptionState& state) {
  return state == SubscriptionState::DISCONNECTED ||
      state == SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED ||
      state == SubscriptionState::CANCELLED;
}

inline bool isGRHold(const SubscriptionState& state) {
  return state == SubscriptionState::DISCONNECTED_GR_HOLD;
}

inline bool isGRHoldExpired(const SubscriptionState& state) {
  return state == SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED;
}

inline std::string subscriptionStateToString(SubscriptionState state) {
  switch (state) {
    case SubscriptionState::CONNECTED:
      return "CONNECTED";
    case SubscriptionState::DISCONNECTED:
      return "DISCONNECTED";
    case SubscriptionState::DISCONNECTED_GR_HOLD:
      return "DISCONNECTED_GR_HOLD";
    case SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED:
      return "DISCONNECTED_GR_HOLD_EXPIRED";
    case SubscriptionState::CANCELLED:
      return "CANCELLED";
  }
  throw std::runtime_error(
      "Unhandled fsdb::SubscriptionState::" +
      std::to_string(static_cast<int>(state)));
}

// Only for Patch subscriptions, on SubscriptionState::CONNECTED, the
// callback will include bool indicating whether the initial sync has
// data or not. This is useful for the caller to know whether to
// expect data callback on initialSync.
using SubscriptionStateChangeCb = std::function<
    void(SubscriptionState, SubscriptionState, std::optional<bool>)>;

using FsdbStreamHeartbeatCb = std::function<void(std::optional<OperMetadata>)>;

struct SubscriptionOptions {
  SubscriptionOptions() = default;
  explicit SubscriptionOptions(
      const std::string& clientId,
      bool subscribeStats = false,
      uint32_t grHoldTimeSec = 0,
      // only mark subscription as CONNECTED on initial sync
      bool requireInitialSyncToMarkConnect = false,
      bool forceSubscribe = false,
      std::optional<int64_t> heartbeatInterval = std::nullopt,
      bool exportPerSubscriptionMetrics = false,
      std::optional<uint32_t> serveIntervalSec = std::nullopt)
      : clientId_(clientId),
        subscribeStats_(subscribeStats),
        grHoldTimeSec_(grHoldTimeSec),
        requireInitialSyncToMarkConnect_(requireInitialSyncToMarkConnect),
        forceSubscribe_(forceSubscribe),
        exportPerSubscriptionMetrics_(exportPerSubscriptionMetrics) {
    if (heartbeatInterval.has_value()) {
      heartbeatInterval_ = heartbeatInterval.value();
    }
    if (serveIntervalSec.has_value()) {
      serveIntervalSec_ = serveIntervalSec.value();
    }
  }

  const std::string clientId_;
  bool subscribeStats_{false};
  uint32_t grHoldTimeSec_{0};
  bool requireInitialSyncToMarkConnect_{false};
  bool forceSubscribe_{false};
  bool exportPerSubscriptionMetrics_{false};
  std::optional<int32_t> heartbeatInterval_{std::nullopt};
  std::optional<uint32_t> serveIntervalSec_{std::nullopt};
};

struct SubscriptionInfo {
  std::string server;
  SubscriptionType subscriptionType;
  bool isStats;
  std::vector<std::string> paths;
  FsdbStreamClient::State state;
  FsdbErrorCode disconnectReason;
};

class FsdbSubscriberBase : public FsdbStreamClient {
 public:
  using FsdbStreamClient::FsdbStreamClient;

  virtual SubscriptionInfo getInfo() const = 0;
};

template <typename SubUnit, typename Paths>
class FsdbSubscriber : public FsdbSubscriberBase {
  std::string typeStr() const;
  std::string pathsStr(const Paths& path) const;

 public:
  using FsdbSubUnitUpdateCb = std::function<void(SubUnit&&)>;
  using SubUnitT = SubUnit;

  FsdbSubscriber(
      const std::string& clientId,
      const Paths& subscribePaths,
      folly::EventBase* streamEvb,
      folly::EventBase* connRetryEvb,
      FsdbSubUnitUpdateCb operSubUnitUpdate,
      bool subscribeStats,
      std::optional<SubscriptionStateChangeCb> streamStateChangeCb =
          std::nullopt,
      std::optional<FsdbStreamStateChangeCb> connectionStateChangeCb =
          std::nullopt,
      std::optional<FsdbStreamHeartbeatCb> heartbeatCb = std::nullopt)
      : FsdbSubscriber(
            std::move(SubscriptionOptions(clientId, subscribeStats)),
            subscribePaths,
            streamEvb,
            connRetryEvb,
            operSubUnitUpdate,
            streamStateChangeCb,
            connectionStateChangeCb,
            heartbeatCb) {}

  FsdbSubscriber(
      SubscriptionOptions&& options,
      const Paths& subscribePaths,
      folly::EventBase* streamEvb,
      folly::EventBase* connRetryEvb,
      FsdbSubUnitUpdateCb operSubUnitUpdate,
      std::optional<SubscriptionStateChangeCb> stateChangeCb = std::nullopt,
      std::optional<FsdbStreamStateChangeCb> connectionStateChangeCb =
          std::nullopt,
      std::optional<FsdbStreamHeartbeatCb> heartbeatCb = std::nullopt)
      : FsdbSubscriberBase(
            options.clientId_,
            streamEvb,
            connRetryEvb,
            fmt::format(
                "fsdb{}{}Subscriber_{}",
                typeStr(),
                (options.subscribeStats_ ? "Stat" : "State"),
                pathsStr(subscribePaths)),
            options.subscribeStats_,
            [this](State oldState, State newState) {
              handleConnectionState(oldState, newState);
            }),
        operSubUnitUpdate_(operSubUnitUpdate),
        subscribeLatencyMetric_(
            fmt::format("{}.{}", getCounterPrefix(), kSubscribeLatencyMetric)),
        clientPubsubLatencyMetric_(
            fmt::format(
                "FsdbClient.{}.{}",
                (options.subscribeStats_ ? "stats" : "state"),
                kSubscribeLatencyMetric)),
        subscribePaths_(subscribePaths),
        subscriptionOptions_(std::move(options)),
        subscriptionState_(
            subscriptionOptions_.grHoldTimeSec_ > 0
                ? SubscriptionState::DISCONNECTED_GR_HOLD
                : SubscriptionState::DISCONNECTED),
        connectionStateChangeCb_(connectionStateChangeCb),
        subscriptionStateChangeCb_(stateChangeCb),
        heartbeatCb_(heartbeatCb),
        staleStateTimer_(
            folly::AsyncTimeout::make(*streamEvb, [this]() noexcept {
              staleStateTimeoutExpired();
            })) {
    if (subscriptionOptions_.grHoldTimeSec_ > 0) {
      scheduleStaleStateTimeout();
    }
  }

  virtual ~FsdbSubscriber() override {
    cancelStaleStateTimeout();
  }

  static SubscriptionType subscriptionType();

  SubscriptionInfo getInfo() const override {
    return SubscriptionInfo{
        getServer(),
        subscriptionType(),
        this->isStats(),
        PathHelpers::toStringList(subscribePaths_),
        getState(),
        getDisconnectReason()};
  }

  // Force a transient disconnect so the FSDB reconnect machinery
  // re-establishes the stream. When noGR is true and the subscription
  // was created with grHoldTimeSec > 0, the post-disconnect transition skips
  // DISCONNECTED_GR_HOLD and lands directly on DISCONNECTED_GR_HOLD_EXPIRED
  // so consumers gated on isGRHoldExpired() can drop stale state immediately
  // instead of waiting out grHoldTimeSec_.
  void reconnect(bool noGR = false) override {
    if (noGR && subscriptionOptions_.grHoldTimeSec_ > 0) {
      // Resolve the noGR intent on streamEvb_ so the flag is only set when
      // there is an imminent CONNECTED -> DISCONNECTED transition queued
      // behind us (FsdbStreamClient::reconnect() schedules its lambda on the
      // same EventBase right after this one). Setting forceGRExpired_ in any
      // other state would leak: no upcoming transition would consume it, and
      // a later, unrelated CONNECTED -> DISCONNECTED would incorrectly skip
      // GR hold.
      this->getStreamEventBase()->runInEventBaseThread([this]() {
        switch (getSubscriptionState()) {
          case SubscriptionState::CONNECTED:
            forceGRExpired_.store(true);
            break;
          case SubscriptionState::DISCONNECTED_GR_HOLD:
            // Already past disconnect; jump straight to GR_HOLD_EXPIRED so
            // the contract holds even when no fresh stream-state edge
            // follows.
            cancelStaleStateTimeout();
            staleStateTimeoutExpired();
            break;
          case SubscriptionState::DISCONNECTED:
          case SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED:
          case SubscriptionState::CANCELLED:
            // No upcoming CONNECTED -> DISCONNECTED transition for the flag
            // to be consumed against -- intentionally do nothing.
            break;
        }
      });
    }
    FsdbStreamClient::reconnect(noGR);
  }

 protected:
  auto createRequest() const {
    if constexpr (std::is_same_v<Paths, std::vector<std::string>>) {
      OperPath operPath;
      operPath.raw() = subscribePaths_;
      OperSubRequest request;
      request.path() = operPath;
      request.subscriberId() = clientId();
      request.forceSubscribe() = subscriptionOptions_.forceSubscribe_;
      if (subscriptionOptions_.heartbeatInterval_.has_value()) {
        request.heartbeatInterval() =
            subscriptionOptions_.heartbeatInterval_.value();
      }
      if (subscriptionOptions_.serveIntervalSec_.has_value()) {
        request.serveIntervalSec() =
            subscriptionOptions_.serveIntervalSec_.value();
      }
      return request;
    } else if constexpr (std::is_same_v<Paths, std::vector<ExtendedOperPath>>) {
      OperSubRequestExtended request;
      request.paths() = subscribePaths_;
      request.subscriberId() = clientId();
      request.forceSubscribe() = subscriptionOptions_.forceSubscribe_;
      if (subscriptionOptions_.heartbeatInterval_.has_value()) {
        request.heartbeatInterval() =
            subscriptionOptions_.heartbeatInterval_.value();
      }
      if (subscriptionOptions_.serveIntervalSec_.has_value()) {
        request.serveIntervalSec() =
            subscriptionOptions_.serveIntervalSec_.value();
      }
      return request;
    }
  }
  SubscriptionState getSubscriptionState() const {
    return *subscriptionState_.rlock();
  }
  void updateSubscriptionState(
      SubscriptionState newState,
      std::optional<bool> initialSyncHasData = std::nullopt) {
    auto locked = subscriptionState_.wlock();
    auto oldState = *locked;
    if (oldState == newState) {
      return;
    }
    *locked = newState;
    if ((newState == SubscriptionState::CONNECTED) ||
        (newState == SubscriptionState::CANCELLED)) {
      cancelStaleStateTimeout();
    }
    if (subscriptionStateChangeCb_.has_value()) {
      subscriptionStateChangeCb_.value()(
          oldState, newState, initialSyncHasData);
    }
  }
  void handleConnectionState(State oldState, State newState) {
    switch (newState) {
      case State::DISCONNECTED:
        if (getSubscriptionState() == SubscriptionState::CONNECTED) {
          if (subscriptionOptions_.grHoldTimeSec_ == 0) {
            updateSubscriptionState(SubscriptionState::DISCONNECTED);
            return;
          } else {
            grDisconnectEvents_.add(1);
            if (forceGRExpired_.exchange(false)) {
              // noGR=true: skip GR hold entirely so consumers gated
              // on isGRHoldExpired() can drop stale state immediately.
              updateSubscriptionState(
                  SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
            } else {
              scheduleStaleStateTimeout();
              updateSubscriptionState(SubscriptionState::DISCONNECTED_GR_HOLD);
            }
          }
        }
        break;
      case State::CANCELLED:
        updateSubscriptionState(SubscriptionState::CANCELLED);
        break;
      default:
        XLOG(DBG2) << "No-op transition for ConnectionState: "
                   << connectionStateToString(oldState) << " -> "
                   << connectionStateToString(newState);
        break;
    }
    if (connectionStateChangeCb_.has_value()) {
      connectionStateChangeCb_.value()(oldState, newState);
    }
  }

  void scheduleStaleStateTimeout() {
    XLOG(DBG2) << "Scheduling stale state timeout for "
               << subscriptionOptions_.grHoldTimeSec_ << " seconds";
    getStreamEventBase()->runInEventBaseThread([this] {
      if (!staleStateTimer_->isScheduled()) {
        staleStateTimer_->scheduleTimeout(
            std::chrono::seconds(subscriptionOptions_.grHoldTimeSec_));
      }
    });
  }

  void cancelStaleStateTimeout() {
    getStreamEventBase()->runImmediatelyOrRunInEventBaseThreadAndWait([this] {
      if (staleStateTimer_ && staleStateTimer_->isScheduled()) {
        staleStateTimer_->cancelTimeout();
      }
    });
  }

  void staleStateTimeoutExpired() noexcept {
    updateSubscriptionState(SubscriptionState::DISCONNECTED_GR_HOLD_EXPIRED);
  }

  const Paths& subscribePaths() const {
    return subscribePaths_;
  }

  const SubscriptionOptions& subscriptionOptions() const {
    return subscriptionOptions_;
  }

  FsdbSubUnitUpdateCb operSubUnitUpdate_;

 protected:
  void onChunkReceived(bool isHeartbeat, std::optional<OperMetadata> md) {
    if (md.has_value()) {
      lastMetadata_ = md;
    }
    if (isHeartbeat) {
      if (heartbeatCb_.has_value()) {
        try {
          heartbeatCb_.value()(md);
        } catch (const std::exception& ex) {
          FsdbException e;
          e.message() = folly::exceptionStr(ex);
          e.errorCode() = FsdbErrorCode::SUBSCRIPTION_DATA_CALLBACK_ERROR;
          throw e;
        }
      }
    }
  }

  const std::string subscribeLatencyMetric_;
  const std::string clientPubsubLatencyMetric_;

 private:
  const Paths subscribePaths_;
  SubscriptionOptions subscriptionOptions_;
  folly::Synchronized<SubscriptionState> subscriptionState_;
  std::optional<FsdbStreamStateChangeCb> connectionStateChangeCb_;
  std::optional<SubscriptionStateChangeCb> subscriptionStateChangeCb_;
  std::optional<FsdbStreamHeartbeatCb> heartbeatCb_;
  std::unique_ptr<folly::AsyncTimeout> staleStateTimer_;
  std::optional<OperMetadata> lastMetadata_;
  fb303::TimeseriesWrapper grDisconnectEvents_{
      getCounterPrefix() + ".disconnectGRHold",
      fb303::SUM,
      fb303::RATE};
  // Set by reconnect(noGR=true) and consumed by handleConnectionState
  // on the next CONNECTED -> DISCONNECTED transition (atomic exchange) to
  // route through DISCONNECTED_GR_HOLD_EXPIRED instead of DISCONNECTED_GR_HOLD.
  std::atomic<bool> forceGRExpired_{false};
};
} // namespace facebook::fboss::fsdb
