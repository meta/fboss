// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/fsdb/client/FsdbSubscriber.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <folly/Synchronized.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>

namespace facebook::fboss::fsdb {

struct FsdbLiveAddRetryOptions {
  std::chrono::milliseconds rpcTimeout{5000};
  std::chrono::milliseconds initialBackoff{100};
  std::chrono::milliseconds maxBackoff{2000};
  // Time spent retrying one batch, including its key-by-key resend, before
  // falling back to reconnect().
  std::chrono::milliseconds budget{30000};
};

template <typename MessageType, typename SubUnit, typename PathElement>
class FsdbPatchSubscriberImpl : public FsdbSubscriber<SubUnit, PathElement> {
  using BaseT = FsdbSubscriber<SubUnit, PathElement>;
  using SubUnitT = typename BaseT::SubUnitT;

 public:
  using FsdbOperPatchUpdateCb = typename BaseT::FsdbSubUnitUpdateCb;
  using BaseT::BaseT;
  using FsdbStreamClient::clientId;
  ~FsdbPatchSubscriberImpl() override {
    BaseT::cancel();
  }
  FsdbPatchSubscriberImpl(const FsdbPatchSubscriberImpl&) = delete;
  FsdbPatchSubscriberImpl& operator=(const FsdbPatchSubscriberImpl&) = delete;
  FsdbPatchSubscriberImpl(FsdbPatchSubscriberImpl&&) = delete;
  FsdbPatchSubscriberImpl& operator=(FsdbPatchSubscriberImpl&&) = delete;

  // Append paths to this live patch subscription. Paths are recorded so every
  // (re)connect subscribes the full (original + added) set. While the current
  // stream supports live add-path, a serialized background drain also appends
  // them to the live server-side subscription for an immediate initial sync.
  // Returns ID_ALREADY_EXISTS, adding nothing, if a key is already subscribed
  // with a different path; re-adding a key with its path is a no-op. A path
  // the server rejects as INVALID_PATH or SUBSCRIPTION_NOT_PERMITTED is dropped
  // from later subscribe requests; other server-side rejections are logged.
  std::optional<FsdbErrorCode> addPaths(const PathElement& newPaths);

  // Whether the server on the current connection advertised live add-path
  // support by populating OperSubInitResponse.subscriptionUid. When true,
  // addPaths() extends the live server-side subscription in place (immediate
  // initial sync); when false -- an older server, or before this connection's
  // subscribe response arrives -- appended paths are delivered by the next
  // (re)connect or, once the response arrives, by a live add. Thread-safe.
  bool serverSupportsLiveAddPath() const {
    return liveAdd_.rlock()->uid.has_value();
  }

  // Test-only: runs on the stream EventBase right after each subscribe request
  // is built, before the subscribe RPC is sent.
  void setOnSubscribeRequestCreatedForTest(std::function<void()> hook) {
    *onSubscribeRequestCreatedForTest_.wlock() = std::move(hook);
  }

  using LiveAddRetryOptions = FsdbLiveAddRetryOptions;
  using LiveAddRpc =
      std::function<folly::coro::Task<void>(AddPatchSubscriptionPathsRequest)>;
  // Test-only: wraps each live add-paths RPC attempt; `next` issues the real
  // RPC.
  using LiveAddRpcInterceptor = std::function<folly::coro::Task<void>(
      AddPatchSubscriptionPathsRequest request,
      LiveAddRpc next)>;

  void setLiveAddRpcInterceptorForTest(LiveAddRpcInterceptor interceptor) {
    *liveAddRpcInterceptorForTest_.wlock() = std::move(interceptor);
  }
  void setLiveAddRetryOptionsForTest(LiveAddRetryOptions options) {
    *liveAddRetryOptions_.wlock() = options;
  }

 private:
  enum class LiveAddOutcome { APPLIED, REJECTED, ABANDONED, EXHAUSTED };

  // Live add-path state, guarded together so a subscribe request snapshot and
  // the keys it misses stay consistent.
  struct LiveAddState {
    // Paths appended post-subscribe, merged into every subscribe request.
    PathElement addedPaths;
    // Server-assigned uid of the current stream, from OperSubInitResponse.
    std::optional<uint64_t> uid;
    // Keys added after the current subscribe request was built and not yet
    // applied to the live subscription.
    std::set<SubscriptionKey> pendingKeys;
    // Incremented per subscribe attempt; identifies the stream a live add
    // targets, since uids can repeat across FSDB restarts.
    uint64_t generation{0};
    bool drainScheduled{false};
  };

#if FOLLY_HAS_COROUTINES
  using StreamT = typename BaseT::StreamT;
  using SubStreamT = typename BaseT::template SubStreamT<MessageType>;
  folly::coro::Task<StreamT> setupStream() override;
  folly::coro::Task<void> serveStream(StreamT&& stream) override;
  folly::coro::Task<void> drainLiveAdds();
  // outcomeUnknown: in/out, set once an attempt fails without a server verdict.
  folly::coro::Task<LiveAddOutcome> sendLiveAddBatch(
      uint64_t uid,
      uint64_t generation,
      const PathElement& batch,
      std::chrono::steady_clock::time_point deadline,
      bool& outcomeUnknown);
  folly::coro::Task<void> invokeLiveAddRpc(
      const AddPatchSubscriptionPathsRequest& request,
      std::chrono::milliseconds timeout);
  folly::coro::Task<void> liveAddRpc(
      AddPatchSubscriptionPathsRequest request,
      std::chrono::milliseconds timeout);
#endif

  // Marks a drain scheduled if one is needed and none is running.
  static bool claimLiveAddDrain(LiveAddState& state);
  void scheduleLiveAddDrain();
  SubRequest createRequest(const PathElement& addedPaths) const;
  AddPatchSubscriptionPathsRequest makeLiveAddRequest(
      uint64_t uid,
      const PathElement& paths) const;
  void eraseLiveAddKeys(uint64_t generation, const PathElement& batch);
  void fallBackToReconnect(uint64_t generation);

  folly::Synchronized<LiveAddState> liveAdd_;
  folly::Synchronized<std::function<void()>> onSubscribeRequestCreatedForTest_;
  folly::Synchronized<LiveAddRpcInterceptor> liveAddRpcInterceptorForTest_;
  folly::Synchronized<LiveAddRetryOptions> liveAddRetryOptions_;
  fb303::TimeseriesWrapper liveAddRetries_{
      this->getCounterPrefix() + ".liveAddPaths.retries",
      fb303::SUM,
      fb303::RATE};
  fb303::TimeseriesWrapper liveAddRejects_{
      this->getCounterPrefix() + ".liveAddPaths.rejected",
      fb303::SUM,
      fb303::RATE};
  fb303::TimeseriesWrapper liveAddReconnectFallbacks_{
      this->getCounterPrefix() + ".liveAddPaths.reconnectFallbacks",
      fb303::SUM,
      fb303::RATE};
};

using FsdbPatchSubscriber = FsdbPatchSubscriberImpl<
    SubscriberMessage,
    SubscriberChunk,
    std::map<SubscriptionKey, RawOperPath>>;
using FsdbExtPatchSubscriber = FsdbPatchSubscriberImpl<
    SubscriberMessage,
    SubscriberChunk,
    std::map<SubscriptionKey, ExtendedOperPath>>;
} // namespace facebook::fboss::fsdb
