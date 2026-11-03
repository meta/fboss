// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/logging/xlog.h>

#include "fboss/fsdb/client/FsdbDeltaPublisher.h"
#include "fboss/fsdb/client/FsdbDeltaSubscriber.h"
#include "fboss/fsdb/client/FsdbPatchPublisher.h"
#include "fboss/fsdb/client/FsdbPatchSubscriber.h"
#include "fboss/fsdb/client/FsdbStatePublisher.h"
#include "fboss/fsdb/client/FsdbStateSubscriber.h"
#include "fboss/fsdb/client/FsdbStreamClient.h"
#include "fboss/fsdb/if/FsdbModel.h"

#include "fboss/lib/CommonUtils.h"

namespace facebook::fboss::fsdb::test {

class TestFsdbStreamClient : public FsdbStreamClient {
 public:
  TestFsdbStreamClient(folly::EventBase* streamEvb, folly::EventBase* timerEvb);
  ~TestFsdbStreamClient() override;

  std::optional<FsdbStreamClient::State> lastStateUpdateSeen() const {
    return lastStateUpdateSeen_;
  }

 private:
  folly::coro::Task<StreamT> setupStream() override;
  folly::coro::Task<void> serveStream(StreamT&& /* stream */) override;

  std::optional<FsdbStreamClient::State> lastStateUpdateSeen_;
};

OperDelta makeDelta(const cfg::AgentConfig& cfg);
Patch makePatch(const cfg::AgentConfig& cfg);
/*
 * makeState serializes FsdbOperStateRoot, after updating
 * agent config inside the root object
 */
OperState makeState(const cfg::AgentConfig& cfg);

OperDelta makeDelta(
    const folly::F14FastMap<std::string, HwPortStats>& portStats);
Patch makePatch(const folly::F14FastMap<std::string, HwPortStats>& portStats);
/*
 * makeState serializes FsdbOperStatsRoot, after updating
 * agent config inside the root object
 */
OperState makeState(
    const folly::F14FastMap<std::string, HwPortStats>& portStats);

cfg::AgentConfig makeAgentConfig(
    const std::map<std::string, std::string>& cmdLinArgs);
cfg::AgentConfig
makeLargeAgentConfig(const std::string& argName, uint32_t bytes, char val);

folly::F14FastMap<std::string, HwPortStats> makePortStats(
    int64_t inBytes,
    const std::string& portName = "eth1/1/1");

folly::F14FastMap<std::string, HwPortStats> makeLargePortStats(
    int64_t counterValue,
    int64_t numPorts);

OperDelta makeSwitchStateOperDelta(const state::SwitchState& switchState);
OperState makeSwitchStateOperState(const state::SwitchState& switchState);

template <typename PubSubT>
class TestFsdbSubscriber : public PubSubT::SubscriberT {
 public:
  using SubUnitT = typename PubSubT::SubUnitT;
  using BaseT = typename PubSubT::SubscriberT;

  template <typename PathT>
  TestFsdbSubscriber(
      const std::string& clientId,
      const PathT& subscribePath,
      folly::EventBase* streamEvb,
      folly::EventBase* connRetryEvb,
      bool subscribeStats = false,
      std::optional<std::function<void()>> onInitialSync = std::nullopt,
      std::optional<std::function<void()>> onDisconnect = std::nullopt)
      : BaseT(
            clientId,
            subscribePath,
            streamEvb,
            connRetryEvb,
            [this, onInitialSync](SubUnitT&& unit) {
              publishedQueue_.wlock()->emplace_back(std::move(unit));
              if (!initialSyncDone_ && onInitialSync.has_value()) {
                onInitialSync.value()();
              }
              initialSyncDone_ = true;
            },
            subscribeStats,
            [this, onDisconnect](
                SubscriptionState /*oldState*/,
                SubscriptionState newState,
                std::optional<bool> /*initialSyncHasData*/) {
              if (newState != SubscriptionState::CONNECTED) {
                XLOG(DBG2) << " Subscriber: " << this->clientId()
                           << " not connected";
                initialSyncDone_ = false;
                if (onDisconnect.has_value()) {
                  onDisconnect.value()();
                }
              }
            }) {}
  ~TestFsdbSubscriber() override {
    this->cancel();
  }
  size_t queueSize() const {
    return publishedQueue_.rlock()->size();
  }
  std::vector<SubUnitT> queuedUnits() const {
    return *publishedQueue_.rlock();
  }
  bool initialSyncDone() const {
    return initialSyncDone_;
  }
  void assertQueue(int expectedSize, int retries = 10) const {
    WITH_RETRIES_N(retries, {
      ASSERT_EVENTUALLY_EQ(queueSize(), expectedSize);
      for (const auto& unit : queuedUnits()) {
        if constexpr (std::is_same_v<SubUnitT, SubscriberChunk>) {
          for (const auto& [_, patchGroup] : *unit.patchGroups()) {
            for (const auto& patch : patchGroup) {
              ASSERT_GT(*patch.metadata()->lastConfirmedAt(), 0);
            }
          }
        } else {
          ASSERT_GT(*unit.metadata()->lastConfirmedAt(), 0);
        }
      }
    });
  }

 private:
  folly::Synchronized<std::vector<SubUnitT>> publishedQueue_;
  bool initialSyncDone_{false};
};

struct DeltaSubT {
  using SubUnitT = OperDelta;
  using SubscriberT = FsdbDeltaSubscriber;
};
struct StateSubT {
  using SubUnitT = OperState;
  using SubscriberT = FsdbStateSubscriber;
};
struct StateExtSubT {
  using SubUnitT = OperState;
  using SubscriberT = FsdbExtStateSubscriber;
};
struct PatchSubT {
  using SubUnitT = SubscriberChunk;
  using SubscriberT = FsdbPatchSubscriber;
};
using TestFsdbDeltaSubscriber = TestFsdbSubscriber<DeltaSubT>;
using TestFsdbStateSubscriber = TestFsdbSubscriber<StateSubT>;
using TestFsdbPatchSubscriber = TestFsdbSubscriber<PatchSubT>;
using TestFsdbExtStateSubscriber = TestFsdbSubscriber<StateExtSubT>;

template <bool pubSubStats, bool enableHybridStateStorage = false>
struct DeltaPubSubT {
  using PubUnitT = OperDelta;
  using PublisherT = FsdbDeltaPublisher;
  using SubUnitT = OperDelta;
  using SubscriberT = TestFsdbDeltaSubscriber;
  static bool constexpr PubSubStats = pubSubStats;
  static bool constexpr EnableHybridStateStorage = enableHybridStateStorage;
};
using DeltaPubSubForStats = DeltaPubSubT<true>;
using DeltaPubSubForState = DeltaPubSubT<false>;
using DeltaPubSubForHybridState = DeltaPubSubT<false, true>;
template <bool pubSubStats, bool enableHybridStateStorage = false>
struct StatePubSubT {
  using PubUnitT = OperState;
  using PublisherT = FsdbStatePublisher;
  using SubUnitT = PubUnitT;
  using SubscriberT = TestFsdbStateSubscriber;
  static bool constexpr PubSubStats = pubSubStats;
  static bool constexpr EnableHybridStateStorage = enableHybridStateStorage;
};

using StatePubSubForStats = StatePubSubT<true>;
using StatePubSubForState = StatePubSubT<false>;
using StatePubSubForHybridState = StatePubSubT<false, true>;

template <bool pubSubStats, bool enableHybridStateStorage = false>
struct PatchPubSubT {
  using PubUnitT = Patch;
  using PublisherT = FsdbPatchPublisher;
  using SubUnitT = SubscriberChunk;
  using SubscriberT = TestFsdbPatchSubscriber;
  static bool constexpr PubSubStats = pubSubStats;
  static bool constexpr EnableHybridStateStorage = enableHybridStateStorage;
};

using PatchPubSubForStats = PatchPubSubT<true>;
using PatchPubSubForState = PatchPubSubT<false>;
using PatchPubSubForHybridState = PatchPubSubT<false, true>;
} // namespace facebook::fboss::fsdb::test
