// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "fboss/fsdb/common/Utils.h"
#include "fboss/fsdb/if/FsdbModel.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_constants.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"
#include "fboss/fsdb/oper/DeltaValue.h"
#include "fboss/fsdb/oper/SubscriptionCommon.h"
#include "fboss/fsdb/oper/SubscriptionMetadataServer.h"
#include "fboss/fsdb/oper/SubscriptionServeQueue.h"
#include "fboss/thrift_cow/gen-cpp2/patch_types.h"

#include <boost/core/noncopyable.hpp>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/coro/AsyncPipe.h>
#include <folly/coro/AsyncScope.h>
#include <folly/io/async/EventBase.h>
#include <folly/json/dynamic.h>
#include <set>

// Only held behind std::shared_ptr here, so an incomplete type suffices. Kept
// out of this header because it is pulled in transitively by NSDB and other
// downstream consumers that never touch regex matching.
namespace re2 {
class RE2;
} // namespace re2

DECLARE_bool(forceCloseSlowSubscriber);

namespace facebook::fboss::fsdb {

// Counter name for tracking subscription serve coalescing events
inline constexpr auto kNumSubscriptionServesCoalesced =
    "num_subscription_serves_coalesced";

inline int64_t getCurrentTimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

class BaseSubscription {
 public:
  virtual PubSubType type() const = 0;

  SubscriptionIdentifier subscriptionId() const {
    return subId_;
  }

  const SubscriberId subscriberId() const {
    return subId_.subscriberId();
  }

  uint64_t subscriptionUid() const {
    return subId_.uid();
  }

  virtual ~BaseSubscription();

  virtual bool shouldConvertToDynamic() const {
    return false;
  }

  OperProtocol operProtocol() const {
    return protocol_;
  }

  virtual bool isActive() const = 0;

  void requestPruneWithReason(FsdbErrorCode reason) {
    if (reason == FsdbErrorCode::SUBSCRIPTION_SERVE_UPDATES_PENDING) {
      // don't prune, we will retry later
      return;
    }
    pruneReason_ = reason;
  }

  std::optional<FsdbErrorCode> pruneReason() const {
    return pruneReason_;
  }

  virtual bool shouldPrune() const {
    if (pruneReason_.has_value()) {
      // prune if subscription is marked with a reason to prune
      return true;
    }
    return !isActive();
  }

  virtual void allPublishersGone(
      FsdbErrorCode disconnectReason = FsdbErrorCode::ALL_PUBLISHERS_GONE,
      const std::string& msg = "All publishers dropped") = 0;

  std::string publisherTreeRoot() const {
    // TODO - make clients deal with null publisher tree roots
    return publisherTreeRoot_ ? *publisherTreeRoot_ : "";
  }

  void updateMetadata(const SubscriptionMetadataServer& metadataServer) {
    auto operTreeMetadata = metadataServer.getMetadata(publisherTreeRoot());
    if (operTreeMetadata.has_value()) {
      lastServedMetadata_ = operTreeMetadata->operMetadata;
    }
  }

  std::optional<OperMetadata> getLastMetadata() const {
    return lastServedMetadata_;
  }

  OperMetadata getMetadata(
      const SubscriptionMetadataServer& metadataServer) const {
    OperMetadata metadata;
    auto operTreeMetadata = metadataServer.getMetadata(publisherTreeRoot());
    if (operTreeMetadata) {
      metadata = operTreeMetadata->operMetadata;
    }
    metadata.lastServedAt() =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    return metadata;
  }

  virtual std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) = 0;

  virtual std::optional<FsdbErrorCode> serveHeartbeat() = 0;

  folly::EventBase* heartbeatEvb() const {
    return heartbeatEvb_;
  }

  std::chrono::milliseconds heartbeatInterval() const {
    return heartbeatInterval_;
  }

  // Granted interval (already rounded/clamped by the storage); nullopt means
  // this subscription is served at the storage's default interval.
  std::optional<uint32_t> serveIntervalMs() const {
    return serveIntervalMs_;
  }

  void setServeIntervalMs(std::optional<uint32_t> serveIntervalMs) {
    serveIntervalMs_ = serveIntervalMs;
  }

  uint32_t getQueueWatermark() const {
    return *queueWatermark_.rlock();
  }

  uint32_t getChunksCoalesced() const {
    return *chunksCoalesced_.rlock();
  }

  uint64_t getEnqueuedDataSize() const {
    return streamInfo_->enqueuedDataSize.load();
  }

  uint64_t getServedDataSize() const {
    return streamInfo_->servedDataSize.load();
  }

  int64_t getInitialSyncCompletedAt() const {
    return initialSyncCompletedAt_.load(std::memory_order_relaxed);
  }

  int64_t getLastUpdateEnqueuedAt() const {
    return lastUpdateEnqueuedAt_.load(std::memory_order_relaxed);
  }

  int64_t getLastHeartbeatSentAt() const {
    return lastHeartbeatSentAt_.load(std::memory_order_relaxed);
  }

  int64_t getLastEnqueuedUpdatePublishedAt() const {
    return lastEnqueuedUpdatePublishedAt_.load(std::memory_order_relaxed);
  }

  std::shared_ptr<SubscriptionStreamInfo> getSharedStreamInfo() {
    return streamInfo_;
  }

  void stop();

  void recordInitialSyncCompleted() {
    initialSyncCompletedAt_.store(
        getCurrentTimeMs(), std::memory_order_relaxed);
  }

 protected:
  BaseSubscription(
      SubscriptionIdentifier&& subId,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      std::optional<uint32_t> serveIntervalMs = std::nullopt);

  template <typename T, typename V>
  std::optional<FsdbErrorCode> tryWrite(
      folly::coro::BoundedAsyncPipe<T>& pipe,
      V&& val,
      size_t updateSize,
      const std::string& /* dbgStr */) {
    std::optional<FsdbErrorCode> ret{std::nullopt};
    queueWatermark_.withWLock([&](auto& queueWatermark) {
      auto queuedChunks = pipe.getOccupiedSpace();
      if (queuedChunks > queueWatermark) {
        queueWatermark = queuedChunks;
      }
    });
    if (!pipe.try_write(std::forward<V>(val))) {
      if (pipe.isClosed()) {
        XLOG(DBG0) << "Subscription " << subscriberId()
                   << " pipe already closed, skip write";
      } else {
        // queue full, avoid unbounded queue build up.
        const auto coalesced =
            chunksCoalesced_.withRLock([](auto& v) { return v; });
        if (FLAGS_forceCloseSlowSubscriber) {
          ret = FsdbErrorCode::SUBSCRIPTION_SERVE_QUEUE_FULL;
          XLOG(INFO) << "Slow subscription: " << subscriberId()
                     << " chunksCoalesced=" << coalesced
                     << " publisherTreeRoot="
                     << publisherTreeRoot_.value_or("--")
                     << " pipe full. Force closing subscription.";
          std::move(pipe).close(
              Utils::createFsdbException(
                  ret.value(),
                  "Force closing slow subscription on pipe-full: ",
                  subscriberId()));
        } else {
          XLOG(ERR) << "Slow subscription: " << subscriberId()
                    << " chunksCoalesced=" << coalesced << " publisherTreeRoot="
                    << publisherTreeRoot_.value_or("--")
                    << " pipe full, update dropped!";
        }
      }
    } else {
      streamInfo_->enqueuedDataSize.fetch_add(updateSize);
      lastUpdateEnqueuedAt_.store(
          getCurrentTimeMs(), std::memory_order_relaxed);
      if (lastServedMetadata_.has_value() &&
          lastServedMetadata_->lastPublishedAt().has_value()) {
        lastEnqueuedUpdatePublishedAt_.store(
            *lastServedMetadata_->lastPublishedAt(), std::memory_order_relaxed);
      }
    }
    return ret;
  }

  template <typename T, typename V>
  std::optional<FsdbErrorCode> tryCoalescingWrite(
      folly::coro::BoundedAsyncPipe<T>& pipe,
      std::optional<V>& val,
      const std::string& /* dbgStr */) {
    auto queuedChunks = pipe.getOccupiedSpace();
    bool isCoalescing = queuedChunks > 1;

    // Increment counter when we transition from not coalescing to coalescing
    if (isCoalescing && !isCurrentlyCoalescing_) {
      isCurrentlyCoalescing_ = true;
      numSubscriptionServesCoalescedCounter_.addValue(1);
    } else if (!isCoalescing && isCurrentlyCoalescing_) {
      isCurrentlyCoalescing_ = false;
    }

    if (isCoalescing) {
      XLOG(DBG2) << "Subscription " << subscriberId()
                 << " pending chunks, coalesce update";
      chunksCoalesced_.withWLock(
          [&](auto& chunksCoalesced) { chunksCoalesced++; });
      return FsdbErrorCode::SUBSCRIPTION_SERVE_UPDATES_PENDING;
    }

    // try_write moves object only if there is space in the pipe
    // NOLINTNEXTLINE(bugprone-use-after-move)
    if (pipe.try_write(std::move(val.value()))) {
      val.reset();
      lastUpdateEnqueuedAt_.store(
          getCurrentTimeMs(), std::memory_order_relaxed);
      if (lastServedMetadata_.has_value() &&
          lastServedMetadata_->lastPublishedAt().has_value()) {
        lastEnqueuedUpdatePublishedAt_.store(
            *lastServedMetadata_->lastPublishedAt(), std::memory_order_relaxed);
      }
    } else {
      XLOG(DBG0) << "Subscription " << subscriberId()
                 << " pipe closed, skip write";
    }

    return std::nullopt;
  }

 private:
  folly::coro::Task<void> heartbeatLoop();

  const SubscriptionIdentifier subId_;
  const OperProtocol protocol_;
  const std::optional<std::string> publisherTreeRoot_;
  folly::EventBase* heartbeatEvb_;
  folly::coro::CancellableAsyncScope backgroundScope_;
  std::chrono::milliseconds heartbeatInterval_;
  std::optional<uint32_t> serveIntervalMs_;
  folly::Synchronized<uint32_t> queueWatermark_{0};
  folly::Synchronized<uint32_t> chunksCoalesced_{0};
  std::optional<FsdbErrorCode> pruneReason_{std::nullopt};
  std::optional<OperMetadata> lastServedMetadata_;
  std::shared_ptr<SubscriptionStreamInfo> streamInfo_;
  std::atomic<int64_t> initialSyncCompletedAt_{0};
  std::atomic<int64_t> lastUpdateEnqueuedAt_{0};
  std::atomic<int64_t> lastHeartbeatSentAt_{0};
  std::atomic<int64_t> lastEnqueuedUpdatePublishedAt_{0};
  bool isCurrentlyCoalescing_{false};
  fb303::ThreadCachedServiceData::TLTimeseries
      numSubscriptionServesCoalescedCounter_;
};

class Subscription : public BaseSubscription {
 public:
  const std::vector<std::string>& path() const {
    return path_;
  }

  bool needsFirstChunk() const {
    return needsFirstChunk_;
  }

  virtual std::optional<FsdbErrorCode> sendEmptyInitialChunk() {
    return serveHeartbeat();
  }

  void firstChunkSent() {
    needsFirstChunk_ = false;
    recordInitialSyncCompleted();
  }

 protected:
  Subscription(
      SubscriptionIdentifier&& subscriber,
      std::vector<std::string> path,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval)
      : BaseSubscription(
            std::move(subscriber),
            std::move(protocol),
            std::move(publisherRoot),
            heartbeatEvb,
            std::move(heartbeatInterval)),
        path_(std::move(path)) {}

 private:
  const std::vector<std::string> path_;
  bool needsFirstChunk_{true};
};

using ExtSubPathMap = std::map<SubscriptionKey, ExtendedOperPath>;

class ExtendedSubscription;
bool shouldDynamicallyResolve(const ExtendedSubscription& subscription);

class ExtendedSubscription : public BaseSubscription {
 public:
  const ExtSubPathMap& paths() const {
    return paths_;
  }

  std::size_t size() const {
    return paths_.size();
  }

  bool shouldPrune() const override {
    return shouldPrune_;
  }

  bool markShouldPruneIfInactive() {
    if (!isActive()) {
      shouldPrune_ = true;
    }
    return shouldPrune_;
  }

  const ExtendedOperPath& pathAt(SubscriptionKey key) const {
    return paths_.at(key);
  }

  // Appends newPaths (skipping keys already present) and returns the inserted
  // keys. Caller must hold the SubscriptionStore wlock: paths_ is otherwise
  // read lock-free via paths()/pathAt() while serving.
  virtual std::vector<SubscriptionKey> addPaths(ExtSubPathMap&& newPaths) {
    std::vector<SubscriptionKey> added;
    added.reserve(newPaths.size());
    for (auto& [key, path] : newPaths) {
      if (paths_.emplace(key, std::move(path)).second) {
        added.push_back(key);
      }
    }
    return added;
  }

  // Stream-revision token from the most recent add-paths request. One-shot:
  // stamped onto OperMetadata.streamRevision of the first chunk served after
  // the add (the "took effect" boundary), then cleared via
  // takeStreamRevision().
  void setStreamRevision(StreamRevision rev) {
    streamRevision_ = rev;
  }
  const std::optional<StreamRevision>& streamRevision() const {
    return streamRevision_;
  }
  // Return the pending stream revision (if any) and clear it, so it is applied
  // to exactly one served chunk.
  std::optional<StreamRevision> takeStreamRevision() {
    return std::exchange(streamRevision_, std::nullopt);
  }

  virtual std::unique_ptr<Subscription> resolve(
      const SubscriptionKey& key,
      const std::vector<std::string>& path) = 0;

 protected:
  ExtendedSubscription(
      SubscriptionIdentifier&& subscriber,
      ExtSubPathMap paths,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval)
      : BaseSubscription(
            std::move(subscriber),
            std::move(protocol),
            std::move(publisherRoot),
            std::move(heartbeatEvb),
            std::move(heartbeatInterval)),
        paths_(std::move(paths)) {}

 private:
  ExtSubPathMap paths_;
  bool shouldPrune_{false};
  std::optional<StreamRevision> streamRevision_;
};

class BasePathSubscription : public Subscription {
 public:
  using Subscription::Subscription;

  virtual std::optional<FsdbErrorCode> offer(DeltaValue<OperState> newVal) = 0;

  PubSubType type() const override {
    return PubSubType::PATH;
  }

  size_t getUpdateSize(const auto& /* val */) {
    // we don't track update sizes for path subscriptions
    return 0;
  }
};

class PathSubscription : public BasePathSubscription,
                         private boost::noncopyable {
 public:
  using value_type = DeltaValue<OperState>;

  virtual ~PathSubscription() override {
    stop();
  }
  using PathIter = std::vector<std::string>::const_iterator;

  std::optional<FsdbErrorCode> offer(DeltaValue<OperState> newVal) override {
    nextOffered_ = std::move(newVal);
    return tryCoalescingWrite(pipe_, nextOffered_, "path.offer");
  }

  bool isActive() const override {
    return !pipe_.isClosed();
  }

  static std::pair<
      folly::coro::AsyncGenerator<value_type&&>,
      std::unique_ptr<PathSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity) {
    auto [generator, pipe] =
        folly::coro::BoundedAsyncPipe<value_type>::create(pipeCapacity);
    std::vector<std::string> path(begin, end);
    auto subscription = std::make_unique<PathSubscription>(
        std::move(subscriber),
        std::move(path),
        std::move(pipe),
        std::move(protocol),
        std::move(publisherRoot),
        std::move(heartbeatEvb),
        std::move(heartbeatInterval));
    return std::make_pair(std::move(generator), std::move(subscription));
  }

  bool shouldConvertToDynamic() const override {
    return false;
  }

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override {
    tryWrite(
        pipe_,
        Utils::createFsdbException(disconnectReason, msg),
        0 /* updateSize */,
        "path.pubsGone");
  }

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override {
    updateMetadata(metadataServer);
    if (nextOffered_.has_value()) {
      return tryCoalescingWrite(pipe_, nextOffered_, "path.flush");
    }
    return std::nullopt;
  }

  std::optional<FsdbErrorCode> serveHeartbeat() override {
    value_type t;
    t.newVal = OperState();
    // need to explicitly set a flag for OperState else it looks like a deletion
    t.newVal->isHeartbeat() = true;
    auto md = getLastMetadata();
    if (md.has_value()) {
      t.newVal->metadata() = md.value();
    }
    return tryWrite(pipe_, std::move(t), 0 /* updateSize */, "path.hb");
  }

  PathSubscription(
      SubscriptionIdentifier&& subscriber,
      std::vector<std::string> path,
      folly::coro::BoundedAsyncPipe<value_type> pipe,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval)
      : BasePathSubscription(
            std::move(subscriber),
            std::move(path),
            std::move(protocol),
            std::move(publisherTreeRoot),
            std::move(heartbeatEvb),
            std::move(heartbeatInterval)),
        pipe_(std::move(pipe)) {}

 private:
  folly::coro::BoundedAsyncPipe<value_type> pipe_;
  std::optional<DeltaValue<OperState>> nextOffered_;
};

class BaseDeltaSubscription : public Subscription {
 public:
  PubSubType type() const override {
    return PubSubType::DELTA;
  }

  bool shouldConvertToDynamic() const override {
    return false;
  }

  void appendRootDeltaUnit(const OperDeltaUnit& unit);

  BaseDeltaSubscription(
      SubscriptionIdentifier&& subscriber,
      std::vector<std::string> path,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval)
      : Subscription(
            std::move(subscriber),
            std::move(path),
            std::move(protocol),
            std::move(publisherTreeRoot),
            heartbeatEvb,
            std::move(heartbeatInterval)) {}

  std::optional<OperDelta> moveFromCurrDelta(
      const SubscriptionMetadataServer& metadataServer);

 private:
  std::optional<OperDelta> currDelta_;
};

class DeltaSubscription : public BaseDeltaSubscription,
                          private boost::noncopyable {
 public:
  virtual ~DeltaSubscription() override {
    stop();
  }

  using PathIter = std::vector<std::string>::const_iterator;

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  bool isActive() const override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

  static std::pair<
      folly::coro::AsyncGenerator<SubscriptionServeQueueElement<OperDelta>&&>,
      std::unique_ptr<DeltaSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0);

  DeltaSubscription(
      SubscriptionIdentifier&& subscriber,
      std::vector<std::string> path,
      folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<OperDelta>>
          pipe,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0)
      : BaseDeltaSubscription(
            std::move(subscriber),
            std::move(path),
            std::move(protocol),
            std::move(publisherTreeRoot),
            heartbeatEvb,
            std::move(heartbeatInterval)),
        pipe_(std::move(pipe)),
        subscriptionQueueMemoryLimit_(subscriptionQueueMemoryLimit),
        subscriptionQueueFullMinSize_(subscriptionQueueFullMinSize) {}

  size_t getUpdateSize(const OperDelta& val) {
    return getOperDeltaSize(val);
  }

  std::optional<FsdbErrorCode> serveHeartbeat() override;

 private:
  folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<OperDelta>> pipe_;
  size_t subscriptionQueueMemoryLimit_;
  int32_t subscriptionQueueFullMinSize_;
};

class ExtendedPathSubscription;

class FullyResolvedExtendedPathSubscription : public BasePathSubscription,
                                              private boost::noncopyable {
 public:
  virtual ~FullyResolvedExtendedPathSubscription() override {
    stop();
  }

  FullyResolvedExtendedPathSubscription(
      const std::vector<std::string>& path,
      ExtendedPathSubscription& subscription);

  PubSubType type() const override;

  bool isActive() const override;
  bool shouldPrune() const override;

  std::optional<FsdbErrorCode> offer(DeltaValue<OperState> newVal) override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  std::optional<FsdbErrorCode> serveHeartbeat() override;

 private:
  ExtendedPathSubscription& subscription_;
};

class ExtendedPathSubscription : public ExtendedSubscription,
                                 private boost::noncopyable {
 public:
  virtual ~ExtendedPathSubscription() override {
    stop();
  }
  // we only support encoded extended subscriptions
  using value_type = TaggedOperState;
  using gen_type = std::vector<DeltaValue<value_type>>;

  void buffer(DeltaValue<value_type>&& newVal);

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  std::optional<FsdbErrorCode> serveHeartbeat() override {
    return tryWrite(pipe_, gen_type(), 0 /* updateSize */, "ExtPath.hb");
  }

  PubSubType type() const override {
    return PubSubType::PATH;
  }

  virtual std::unique_ptr<Subscription> resolve(
      const SubscriptionKey& key,
      const std::vector<std::string>& path) override;

  static std::pair<
      folly::coro::AsyncGenerator<gen_type&&>,
      std::shared_ptr<ExtendedPathSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      std::optional<std::string> publisherRoot,
      OperProtocol protocol,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity);

  bool shouldConvertToDynamic() const override {
    return false;
  }

  bool isActive() const override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

  ExtendedPathSubscription(
      SubscriptionIdentifier&& subscriber,
      ExtSubPathMap paths,
      folly::coro::BoundedAsyncPipe<gen_type> pipe,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval)
      : ExtendedSubscription(
            std::move(subscriber),
            std::move(paths),
            std::move(protocol),
            std::move(publisherTreeRoot),
            std::move(heartbeatEvb),
            std::move(heartbeatInterval)),
        pipe_(std::move(pipe)) {}

  size_t getUpdateSize(const auto& /* val */) {
    // we don't track update sizes for path subscriptions
    return 0;
  }

 private:
  folly::coro::BoundedAsyncPipe<gen_type> pipe_;
  std::optional<gen_type> buffered_;
  std::optional<gen_type> nextOffered_;
};

class ExtendedDeltaSubscription;

class FullyResolvedExtendedDeltaSubscription : public BaseDeltaSubscription,
                                               private boost::noncopyable {
 public:
  virtual ~FullyResolvedExtendedDeltaSubscription() override {
    stop();
  }

  FullyResolvedExtendedDeltaSubscription(
      const std::vector<std::string>& path,
      ExtendedDeltaSubscription& subscription);

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  std::optional<FsdbErrorCode> serveHeartbeat() override;

  PubSubType type() const override;

  bool isActive() const override;
  bool shouldPrune() const override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

 private:
  ExtendedDeltaSubscription& subscription_;
};

class ExtendedDeltaSubscription : public ExtendedSubscription,
                                  private boost::noncopyable {
 public:
  virtual ~ExtendedDeltaSubscription() override {
    stop();
  }
  // we only support encoded extended subscriptions
  using value_type = std::vector<TaggedOperDelta>;

  // we flatten the delta lists in to one list, so just use the same type
  using gen_type = value_type;

  PubSubType type() const override {
    return PubSubType::DELTA;
  }

  virtual std::unique_ptr<Subscription> resolve(
      const SubscriptionKey& key,
      const std::vector<std::string>& path) override;

  static std::pair<
      folly::coro::AsyncGenerator<SubscriptionServeQueueElement<gen_type>&&>,
      std::shared_ptr<ExtendedDeltaSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      std::optional<std::string> publisherRoot,
      OperProtocol protocol,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0);

  void buffer(TaggedOperDelta&& newVal);

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  std::optional<FsdbErrorCode> serveHeartbeat() override;

  bool shouldConvertToDynamic() const override {
    return false;
  }

  bool isActive() const override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

  ExtendedDeltaSubscription(
      SubscriptionIdentifier&& subscriber,
      ExtSubPathMap paths,
      folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<value_type>>
          pipe,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0)
      : ExtendedSubscription(
            std::move(subscriber),
            std::move(paths),
            std::move(protocol),
            std::move(publisherTreeRoot),
            std::move(heartbeatEvb),
            std::move(heartbeatInterval)),
        pipe_(std::move(pipe)),
        subscriptionQueueMemoryLimit_(subscriptionQueueMemoryLimit),
        subscriptionQueueFullMinSize_(subscriptionQueueFullMinSize) {}

  size_t getUpdateSize(const gen_type& val) {
    return getExtendedDeltaSize(val);
  }

 private:
  folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<gen_type>> pipe_;
  std::optional<gen_type> buffered_;
  size_t subscriptionQueueMemoryLimit_;
  int32_t subscriptionQueueFullMinSize_;
};

class ExtendedPatchSubscription;
class PatchSubscription : public Subscription, private boost::noncopyable {
 public:
  using PathIter = std::vector<std::string>::const_iterator;

  PatchSubscription(
      const SubscriptionKey& key,
      std::vector<std::string> path,
      ExtendedPatchSubscription& subscription);

  virtual ~PatchSubscription() override {
    stop();
  }

  PubSubType type() const override {
    return PubSubType::PATCH;
  }

  std::optional<FsdbErrorCode> offer(thrift_cow::PatchNode node);

  std::optional<FsdbErrorCode> serveHeartbeat() override;

  std::optional<FsdbErrorCode> sendEmptyInitialChunk() override;

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  bool isActive() const override;

  virtual void allPublishersGone(
      FsdbErrorCode disconnectReason = FsdbErrorCode::ALL_PUBLISHERS_GONE,
      const std::string& msg = "All publishers dropped") override;

 private:
  SubscriptionKey key_;
  std::optional<Patch> currPatch_;
  ExtendedPatchSubscription& subscription_;
};

class ExtendedPatchSubscription : public ExtendedSubscription,
                                  private boost::noncopyable {
 public:
  using gen_type = SubscriberMessage;
  using CompiledRegex = std::shared_ptr<const re2::RE2>;
  using CompiledRegexPath = std::vector<CompiledRegex>;
  using CompiledRegexPathMap = std::map<SubscriptionKey, CompiledRegexPath>;

  virtual ~ExtendedPatchSubscription() override {
    stop();
  }

  // Single path
  static std::pair<
      folly::coro::AsyncGenerator<SubscriptionServeQueueElement<gen_type>&&>,
      std::unique_ptr<ExtendedPatchSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      std::vector<std::string> path,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0);

  // Multipath
  static std::pair<
      folly::coro::AsyncGenerator<SubscriptionServeQueueElement<gen_type>&&>,
      std::unique_ptr<ExtendedPatchSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      std::map<SubscriptionKey, RawOperPath> paths,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0);

  // Extended paths
  static std::pair<
      folly::coro::AsyncGenerator<SubscriptionServeQueueElement<gen_type>&&>,
      std::unique_ptr<ExtendedPatchSubscription>>
  create(
      SubscriptionIdentifier&& subscriber,
      ExtSubPathMap paths,
      OperProtocol protocol,
      std::optional<std::string> publisherRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      int32_t pipeCapacity,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0);

  ExtendedPatchSubscription(
      SubscriptionIdentifier&& subscriber,
      ExtSubPathMap paths,
      CompiledRegexPathMap compiledRegexes,
      bool hasWildcardPath,
      folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<gen_type>>
          pipe,
      OperProtocol protocol,
      std::optional<std::string> publisherTreeRoot,
      folly::EventBase* heartbeatEvb,
      std::chrono::milliseconds heartbeatInterval,
      size_t subscriptionQueueMemoryLimit = 0,
      int32_t subscriptionQueueFullMinSize = 0)
      : ExtendedSubscription(
            std::move(subscriber),
            std::move(paths),
            std::move(protocol),
            std::move(publisherTreeRoot),
            std::move(heartbeatEvb),
            std::move(heartbeatInterval)),
        compiledRegexes_(std::move(compiledRegexes)),
        hasWildcardPath_(hasWildcardPath),
        pipe_(std::move(pipe)),
        subscriptionQueueMemoryLimit_(subscriptionQueueMemoryLimit),
        subscriptionQueueFullMinSize_(subscriptionQueueFullMinSize) {}

  PubSubType type() const override {
    return PubSubType::PATCH;
  }

  std::vector<SubscriptionKey> addPaths(ExtSubPathMap&& newPaths) override;

  const CompiledRegexPath& compiledRegexesAt(SubscriptionKey key) const {
    return compiledRegexes_.at(key);
  }

  bool hasWildcardPath() const {
    return hasWildcardPath_;
  }

  bool isInitialSyncPending(SubscriptionKey key) const {
    return initialSyncPendingKeys_.contains(key);
  }

  void clearInitialSyncPending(const std::vector<SubscriptionKey>& keys) {
    for (const auto key : keys) {
      initialSyncPendingKeys_.erase(key);
    }
  }

  virtual std::unique_ptr<Subscription> resolve(
      const SubscriptionKey& key,
      const std::vector<std::string>& path) override;

  void buffer(const SubscriptionKey& key, Patch&& newVal);

  std::optional<FsdbErrorCode> flush(
      const SubscriptionMetadataServer& metadataServer) override;

  std::optional<FsdbErrorCode> serveHeartbeat() override;

  bool isActive() const override;

  void allPublishersGone(FsdbErrorCode disconnectReason, const std::string& msg)
      override;

  size_t getUpdateSize(const SubscriberChunk& val) {
    size_t totalSize = 0;
    for (const auto& [key, patchList] : *val.patchGroups()) {
      for (const auto& patch : patchList) {
        totalSize += getPatchNodeSize(*patch.patch());
      }
    }
    return totalSize;
  }

 private:
  std::optional<SubscriberChunk> moveCurChunk(
      const SubscriptionMetadataServer& metadataServer);

  static CompiledRegexPathMap compileRegexes(const ExtSubPathMap& paths);

  CompiledRegexPathMap compiledRegexes_;
  std::set<SubscriptionKey> initialSyncPendingKeys_;
  bool hasWildcardPath_{false};
  std::map<SubscriptionKey, std::vector<Patch>> buffered_;
  folly::coro::BoundedAsyncPipe<SubscriptionServeQueueElement<gen_type>> pipe_;
  size_t subscriptionQueueMemoryLimit_;
  int32_t subscriptionQueueFullMinSize_;
};

// Assemble a Patch for `basePath` and hand it to the extended subscription's
// aggregation buffer. Used by PatchSubscription::offer, the dynamic wildcard
// serve path, and the dynamic wildcard initial sync, so all three produce
// identically shaped patches.
void bufferPatch(
    ExtendedPatchSubscription& subscription,
    const SubscriptionKey& key,
    std::vector<std::string> basePath,
    thrift_cow::PatchNode patchNode);

} // namespace facebook::fboss::fsdb
