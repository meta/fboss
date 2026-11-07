// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <condition_variable>
#include <mutex>

#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/if/gen-cpp2/MultiSwitchCtrl.h"

namespace facebook::fboss {

class MultiSwitchThriftHandler
    : public apache::thrift::ServiceHandler<multiswitch::MultiSwitchCtrl> {
 public:
  explicit MultiSwitchThriftHandler(SwSwitch* sw) : sw_(sw) {}

#if FOLLY_HAS_COROUTINES
  void cancelEventSyncers();
  void waitForEventSyncers();
#endif

#if FOLLY_HAS_COROUTINES
  folly::coro::Task<
      apache::thrift::SinkConsumer<multiswitch::LinkChangeEvent, bool>>
  co_notifyLinkChangeEvent(int64_t switchId) override;
  folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::FdbEvent, bool>>
  co_notifyFdbEvent(int64_t switchId) override;

  folly::coro::Task<apache::thrift::SinkConsumer<multiswitch::RxPacket, bool>>
  co_notifyRxPacket(int64_t switchId) override;

  folly::coro::Task<apache::thrift::ServerStream<multiswitch::TxPacket>>
  co_getTxPackets(int64_t switchId) override;

  folly::coro::Task<
      apache::thrift::SinkConsumer<multiswitch::HwSwitchStats, bool>>
  co_syncHwStats(int16_t switchIndex) override;

  folly::coro::Task<apache::thrift::SinkConsumer<
      multiswitch::SwitchReachabilityChangeEvent,
      bool>>
  co_notifySwitchReachabilityChangeEvent(int64_t switchIndex) override;
#endif
  void getNextStateOperDelta(
      multiswitch::StateOperDelta& operDelta,
      int64_t switchId,
      std::unique_ptr<multiswitch::StateOperDelta> prevOperResult,
      int64_t lastUpdateSeqNum) override;

  void gracefulExit(int64_t switchId) override;

  static L2Entry getL2Entry(L2EntryThrift thriftEntry);

 private:
#if FOLLY_HAS_COROUTINES
  bool startEventSyncer();
  void finishEventSyncer();
#endif
  void processLinkState(
      SwitchID switchId,
      const multiswitch::LinkChangeEvent& linkChangeEvent);
  void processLinkActiveState(
      SwitchID switchId,
      const multiswitch::LinkChangeEvent& linkChangeEvent);
  void processLinkConnectivity(
      SwitchID switchId,
      const multiswitch::LinkChangeEvent& linkChangeEvent);
  void processLinkDisabledByFirmware(
      SwitchID switchId,
      const multiswitch::LinkChangeEvent& linkChangeEvent);
  void processSwitchReachabilityChangeEvent(
      SwitchID switchId,
      const multiswitch::SwitchReachabilityChangeEvent&
          switchReachabilityChangeEvent);
  void ensureConfigured(folly::StringPiece function) const;
  SwSwitch* sw_;
  folly::CancellationSource rxPktCancellationSource_;
  folly::CancellationSource linkCancellationSource_;
  folly::CancellationSource fdbCancellationSource_;
  folly::CancellationSource statsCancellationSource_;
  folly::CancellationSource switchReachabilityCancellationSource_;
#if FOLLY_HAS_COROUTINES
  // Guards the event syncer (sink) refcount and shutdown flag
  std::mutex eventSyncerMutex_;
  std::condition_variable eventSyncerCv_;
  int activeEventSyncers_{0};
  bool eventSyncersStopping_{false};
#endif
};
} // namespace facebook::fboss
