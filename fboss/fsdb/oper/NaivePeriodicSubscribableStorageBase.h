// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/fsdb/oper/SubscribableStorage.h"
#include "fboss/fsdb/oper/SubscriptionCommon.h"
#include "fboss/fsdb/oper/SubscriptionManager.h"
#include "fboss/fsdb/oper/SubscriptionMetadataServer.h"
#include "fboss/fsdb/server/FsdbOperTreeMetadataTracker.h"
#include "fboss/fsdb/server/OperPathToPublisherRoot.h"
#include "fboss/lib/ThreadHeartbeat.h"
#include "fboss/thrift_cow/gen-cpp2/patch_types.h"

#include <folly/Synchronized.h>
#include <folly/coro/AsyncScope.h>
#include <folly/io/async/EventBase.h>
#include <folly/io/async/ScopedEventBaseThread.h>

DECLARE_int32(storage_thread_heartbeat_ms);
DECLARE_bool(serveHeartbeats);
DECLARE_int32(subscriptionServeQueueSize);

namespace facebook::fboss::fsdb {

inline constexpr std::string_view kServeSubMs{"storage.serve_sub_ms"};
inline constexpr std::string_view kServeSubNum{"storage.serve_sub_num"};
inline constexpr int64_t kServeHistogramBins{20};
inline constexpr std::string_view kRss{"rss"};
inline constexpr std::string_view kRegisteredSubs{"subscriptions.registered"};
inline constexpr std::string_view kServeIntervalSubs{
    "storage.serve.interval.subscriptions.registered"};
inline constexpr std::string_view kPathStoreNum{"object.count.pathStores"};
inline constexpr std::string_view kPathStoreAllocs{"object.allocs.pathStores"};
inline constexpr std::string_view kPublishTimePrefix{"publish_time_ms"};
inline constexpr std::string_view kSubscribeTimePrefix{"subscribe_time_ms"};
inline constexpr std::string_view kSubscriberPrefix{"subscriber"};
inline constexpr std::string_view kSubscriptionQueueWatermark{
    "queue_watermark"};
inline constexpr std::string_view kSlowSubscriptionDisconnects{
    "disconnects.slow_subscriber"};
inline constexpr std::string_view kSubscriberConnected{"connected"};

// non-templated parts of NaivePeriodicSubscribableStorage to help with
// compilation
class NaivePeriodicSubscribableStorageBase {
 public:
  using ConcretePath = typename OperPathToPublisherRoot::Path;
  using PathIter = typename OperPathToPublisherRoot::PathIter;
  using ExtPath = typename OperPathToPublisherRoot::ExtPath;
  using ExtPathIter = typename OperPathToPublisherRoot::ExtPathIter;

  struct StorageParams {
    explicit StorageParams(
        std::chrono::milliseconds subscriptionServeInterval =
            std::chrono::milliseconds(50),
        std::chrono::milliseconds subscriptionHeartbeatInterval =
            std::chrono::seconds(5),
        bool trackMetadata = false,
        const std::string& metricPrefix = "fsdb",
        bool convertToIDPaths = false,
        bool requireResponseOnInitialSync = false,
        bool exportPerSubscriberMetrics = false,
        bool serveGetRequestsWithLastPublishedState = true,
        int32_t pathSubscriptionServeQueueSize =
            FLAGS_subscriptionServeQueueSize,
        int32_t defaultSubscriptionServeQueueSize =
            FLAGS_subscriptionServeQueueSize)
        : subscriptionServeInterval_(subscriptionServeInterval),
          serveTickInterval_(subscriptionServeInterval),
          subscriptionHeartbeatInterval_(subscriptionHeartbeatInterval),
          trackMetadata_(trackMetadata),
          metricPrefix_(metricPrefix),
          convertSubsToIDPaths_(convertToIDPaths),
          requireResponseOnInitialSync_(requireResponseOnInitialSync),
          exportPerSubscriberMetrics_(exportPerSubscriberMetrics),
          serveGetRequestsWithLastPublishedState_(
              serveGetRequestsWithLastPublishedState),
          pathSubscriptionServeQueueSize_(pathSubscriptionServeQueueSize),
          defaultSubscriptionServeQueueSize_(
              defaultSubscriptionServeQueueSize) {
      validateServeIntervals();
    }

    // Tick granularity for the serve loop. Subscribers may request any
    // interval; it is rounded up to a whole tick and clamped to
    // subscriptionServeInterval_, which bounds the bucket count.
    StorageParams& setServeTickInterval(std::chrono::milliseconds tick) {
      serveTickInterval_ = tick;
      validateServeIntervals();
      return *this;
    }

    StorageParams& setServeGetRequestsWithLastPublishedState(bool val) {
      serveGetRequestsWithLastPublishedState_ = val;
      return *this;
    }

    // Off by default: up to kMaxServeBuckets * 4 ODS series per switch.
    StorageParams& setExportPerBucketServeMetrics(bool val) {
      exportPerBucketServeMetrics_ = val;
      return *this;
    }

    StorageParams& setDeltaSubscriptionQueueMemoryLimit(size_t val) {
      deltaSubscriptionQueueMemoryLimit_ = val;
      return *this;
    }

    /*
     * deltaSubscriptionQueueMemoryLimit_ will be checked only if there
     * are atleast deltaSubscriptionQueueFullMinSize_ number of pending
     * updates for the subscription in queue.
     * The reason for checking for number of updates in conjunction with
     * total memory for updates is to prune scenarios where there is just
     * 1-2 large update enqueued and subscriber is taking a while to process
     * it. In such scenarios, fsdb can detect a genuinely slow subscriber
     * by deferring the slow subscriber detection till next updates.
     */
    StorageParams& setDeltaSubscriptionQueueFullMinSize(size_t val) {
      deltaSubscriptionQueueFullMinSize_ = val;
      return *this;
    }

    std::chrono::milliseconds subscriptionServeInterval_;
    std::chrono::milliseconds serveTickInterval_;
    const std::chrono::milliseconds subscriptionHeartbeatInterval_;
    const bool trackMetadata_;
    const std::string& metricPrefix_;
    bool convertSubsToIDPaths_;
    const bool requireResponseOnInitialSync_;
    const bool exportPerSubscriberMetrics_;
    bool serveGetRequestsWithLastPublishedState_;
    bool exportPerBucketServeMetrics_{false};
    const int32_t pathSubscriptionServeQueueSize_;
    const int32_t defaultSubscriptionServeQueueSize_;
    size_t deltaSubscriptionQueueMemoryLimit_{0};
    size_t deltaSubscriptionQueueFullMinSize_{0};

   private:
    void validateServeIntervals() const {
      CHECK_GT(subscriptionServeInterval_.count(), 0)
          << "default serve interval must be positive";
      CHECK_GT(serveTickInterval_.count(), 0)
          << "serve tick interval must be positive";
      CHECK_LE(serveTickInterval_.count(), subscriptionServeInterval_.count())
          << "serve tick must not exceed the default serve interval";
      // Every occupied bucket retains its own tree baseline, so an unbounded
      // count turns a bad interval pair into unbounded memory.
      CHECK_LE(
          serveBucketCount(
              static_cast<uint32_t>(serveTickInterval_.count()),
              static_cast<uint32_t>(subscriptionServeInterval_.count())),
          kMaxServeBuckets)
          << "serve interval / tick yields too many buckets";
    }
  };

  explicit NaivePeriodicSubscribableStorageBase(
      StorageParams params,
      std::optional<OperPathToPublisherRoot> pathToRootHelper = std::nullopt);

  virtual ~NaivePeriodicSubscribableStorageBase() = default;

  const StorageParams& params() const {
    return params_;
  }

  FsdbOperTreeMetadataTracker getMetadata() const;

  std::shared_ptr<ThreadHeartbeat> getThreadHeartbeat() {
    return threadHeartbeat_;
  }

  void start_impl();
  void stop_impl();

  void registerPublisher(
      PathIter begin,
      PathIter end,
      bool skipThriftStreamLivenessCheck);

  void unregisterPublisher(
      PathIter begin,
      PathIter end,
      FsdbErrorCode disconnectReason = FsdbErrorCode::ALL_PUBLISHERS_GONE);

  template <typename T, typename TC>
  folly::coro::AsyncGenerator<DeltaValue<T>&&> subscribe_impl(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end) {
    auto sourceGen = subscribe_encoded_impl(
        std::move(subscriber), begin, end, OperProtocol::BINARY);
    return folly::coro::co_invoke(
        [&, gen = std::move(sourceGen)]() mutable
            -> folly::coro::AsyncGenerator<DeltaValue<T>&&> {
          while (auto item = co_await gen.next()) {
            auto&& encodedValue = *item;
            DeltaValue<T> typedValue;
            if (encodedValue.oldVal) {
              if (auto contents = encodedValue.oldVal->contents()) {
                typedValue.oldVal = thrift_cow::deserialize<TC, T>(
                    OperProtocol::BINARY, *contents);
              }
            }
            if (encodedValue.newVal) {
              if (auto contents = encodedValue.newVal->contents()) {
                typedValue.newVal = thrift_cow::deserialize<TC, T>(
                    OperProtocol::BINARY, *contents);
              }
            }
            co_yield std::move(typedValue);
          }
        });
  }

  folly::coro::AsyncGenerator<DeltaValue<OperState>&&> subscribe_encoded_impl(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
  subscribe_delta_impl(
      SubscriptionIdentifier&& subscriber,
      PathIter begin,
      PathIter end,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  folly::coro::AsyncGenerator<std::vector<DeltaValue<TaggedOperState>>&&>
  subscribe_encoded_extended_impl(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  SubscriptionStreamReader<
      SubscriptionServeQueueElement<std::vector<TaggedOperDelta>>>
  subscribe_delta_extended_impl(
      SubscriptionIdentifier&& subscriber,
      std::vector<ExtendedOperPath> paths,
      OperProtocol protocol,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch_impl(
      SubscriptionIdentifier&& subscriber,
      std::map<SubscriptionKey, RawOperPath> rawPaths,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
  subscribe_patch_extended_impl(
      SubscriptionIdentifier&& subscriber,
      std::map<SubscriptionKey, ExtendedOperPath> paths,
      std::optional<SubscriptionStorageParams> subscriptionParams =
          std::nullopt);

  std::optional<FsdbErrorCode> add_patch_subscription_path_impl(
      SubscriptionIdentifier&& id,
      std::map<SubscriptionKey, RawOperPath> newPaths,
      std::optional<StreamRevision> streamRevision = std::nullopt);

  std::optional<FsdbErrorCode> add_extended_patch_subscription_path_impl(
      SubscriptionIdentifier&& id,
      std::map<SubscriptionKey, ExtendedOperPath> newPaths,
      std::optional<StreamRevision> streamRevision = std::nullopt);

  void publisherHeartbeat(
      PathIter begin,
      PathIter end,
      const OperMetadata& metadata) {
    updateMetadata(begin, end, metadata);
  }

  size_t numSubscriptions() const {
    return subMgr().numSubscriptions();
  }
  size_t numIntervalSubscriptions() const {
    return subMgr().numIntervalSubscriptions();
  }
  std::vector<OperSubscriberInfo> getSubscriptions() const {
    return subMgr().getSubscriptions();
  }
  size_t numPathStores() const {
    return subMgr().numPathStores();
  }
  size_t numPathStoresRecursive_Expensive() const {
    return subMgr().numPathStoresRecursive_Expensive();
  }
  uint64_t numPathStoreAllocs() const {
    return subMgr().numPathStoreAllocs();
  }
  uint64_t numPathStoreFrees() const {
    return subMgr().numPathStoreFrees();
  }

  void setConvertToIDPaths(bool convertToIDPaths) {
    params_.convertSubsToIDPaths_ = convertToIDPaths;
    subMgr().useIdPaths(convertToIDPaths);
  }

  // Normalizes a requested interval onto the tick grid. Unsupported or absent
  // requests fall back to the default interval rather than failing the
  // subscribe.
  std::optional<uint32_t> resolveServeIntervalMs(
      const std::optional<SubscriptionStorageParams>& subscriptionParams,
      const SubscriptionIdentifier& subscriber) const;

 protected:
  std::chrono::milliseconds serveTickInterval() const {
    return params_.serveTickInterval_;
  }

  virtual folly::coro::Task<void> serveSubscriptionsTick() = 0;
  virtual ConcretePath convertPath(ConcretePath&& path) const = 0;
  virtual ExtPath convertPath(const ExtPath& path) const = 0;
  virtual const SubscriptionManagerBase& subMgr() const = 0;
  virtual SubscriptionManagerBase& subMgr() = 0;

  SubscriptionMetadataServer getCurrentMetadataServer();
  // bucketInterval is the bucket's own cadence, not the tick: a slow bucket
  // legitimately takes longer than a tick and must not be called slow for it.
  void exportServeMetrics(
      std::chrono::steady_clock::time_point serveStartTime,
      SubscriptionMetadataServer& metadata,
      std::map<std::string, uint64_t>& lastServedPublisherRootUpdates,
      std::chrono::milliseconds bucketInterval,
      size_t bucket);
  // RSS alone is a /proc read; don't repeat it once per due bucket.
  void exportTickMetrics();

  std::optional<std::string> getPublisherRoot(PathIter begin, PathIter end)
      const;
  std::optional<std::string> getPublisherRoot(
      const std::vector<ExtendedOperPath>& paths) const;

  std::optional<std::string> getPublisherRoot(
      ExtPathIter begin,
      ExtPathIter end) const;

  std::optional<std::string> getPublisherRoot(
      const std::map<SubscriptionKey, RawOperPath>& paths) const;

  std::optional<std::string> getPublisherRoot(
      const std::map<SubscriptionKey, ExtendedOperPath>& paths) const;

  void updateMetadata(
      PathIter begin,
      PathIter end,
      const OperMetadata& metadata = {});

  std::vector<ExtendedOperPath> convertExtPaths(
      const std::vector<ExtendedOperPath>& paths) const;

  folly::Synchronized<bool> running_{false};

  StorageParams params_;
  // Owned snapshot — params_.metricPrefix_ is a reference and may dangle
  // post-ctor; runtime readers must use this.
  const std::string metricPrefixOwned_;
  folly::Synchronized<std::unique_ptr<FsdbOperTreeMetadataTracker>>
      metadataTracker_;

  // as an optimization, for now we decide what protocol is used in patches
  // instead of letting the client choose
  const OperProtocol patchOperProtocol_{OperProtocol::COMPACT};

 private:
  std::optional<OperPathToPublisherRoot> pathToRootHelper_;

  void initExportedSubscriberStats(FsdbClient subscriberClientId);

  folly::Synchronized<std::map<std::string, std::string>>
      registeredPublisherRoots_;
  std::set<FsdbClient> registeredSubscriberClientIds_;
  folly::coro::CancellableAsyncScope backgroundScope_;
  std::unique_ptr<std::thread> subscriptionServingThread_;
  folly::EventBase evb_;
  std::unique_ptr<folly::ScopedEventBaseThread> heartbeatThread_;

  std::shared_ptr<ThreadHeartbeat> threadHeartbeat_;

  // metric names
  const std::string rss_;
  const std::string registeredSubs_;
  const std::string registeredIntervalSubs_;
  const std::string nPathStores_;
  const std::string nPathStoreAllocs_;
  const std::string serveSubMs_;
  const std::string serveSubNum_;
  // Empty when there is only one bucket; the aggregates already say everything.
  std::vector<std::string> serveSubMsByBucket_;
  std::vector<std::string> serveSubNumByBucket_;
  // per-PublisherRoot metrics
  const std::string publishTimePrefix_;
  const std::string subscribeTimePrefix_;
  // per-subscriber metrics
  const std::string subscriberPrefix_;

  // delete copy constructors
  NaivePeriodicSubscribableStorageBase(
      NaivePeriodicSubscribableStorageBase const&) = delete;
  NaivePeriodicSubscribableStorageBase& operator=(
      NaivePeriodicSubscribableStorageBase const&) = delete;
  NaivePeriodicSubscribableStorageBase(NaivePeriodicSubscribableStorageBase&&) =
      delete;
  NaivePeriodicSubscribableStorageBase& operator=(
      NaivePeriodicSubscribableStorageBase&&) = delete;
};

} // namespace facebook::fboss::fsdb
