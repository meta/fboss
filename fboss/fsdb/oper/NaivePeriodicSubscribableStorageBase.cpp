// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/oper/NaivePeriodicSubscribableStorageBase.h"
#include "fboss/fsdb/common/Utils.h"

#include <fb303/ThreadCachedServiceData.h>
#include <folly/coro/BlockingWait.h>
#include <folly/system/ThreadName.h>

#ifndef IS_OSS
#include "common/base/Proc.h"
namespace {
inline int64_t getMemoryUsage() {
  return facebook::Proc::getMemoryUsage();
}
} // namespace
#else
namespace {
inline int64_t getMemoryUsage() {
  return 0;
}
} // namespace
#endif

DEFINE_int32(
    storage_thread_heartbeat_ms,
    10000,
    "subscribable storage thread's heartbeat interval (ms)");
DEFINE_bool(
    serveHeartbeats,
    false,
    "Whether or not to serve hearbeats in subscription streams");
// default queue size for subscription serve updates
// is chosen to be large enough so that in prod we
// should not see any dropped updates. This value will
// be tuned to lower value after monitoring and max-scale
// testing.
// Rationale for choice of this specific large value:
// * minimum enqueue interval is state subscription serve interval (50msec).
// * Thrift streams holds ~100 updates before pipe queue starts building up.
// So with 1024 default queue size, pipe can get full
// only in the exceptional scenario where subscriber does not
// read any update for > 1 min.
DEFINE_int32(
    subscriptionServeQueueSize,
    1024,
    "Max subscription serve updates to queue, default 1024");

namespace facebook::fboss::fsdb {

NaivePeriodicSubscribableStorageBase::NaivePeriodicSubscribableStorageBase(
    StorageParams params,
    std::optional<OperPathToPublisherRoot> pathToRootHelper)
    : params_(std::move(params)),
      // params_.metricPrefix_ is a reference and dangles post-ctor; snapshot
      // now.
      metricPrefixOwned_(params_.metricPrefix_),
      rss_(fmt::format("{}.{}", metricPrefixOwned_, kRss)),
      registeredSubs_(
          fmt::format("{}.{}", metricPrefixOwned_, kRegisteredSubs)),
      registeredIntervalSubs_(
          fmt::format("{}.{}", metricPrefixOwned_, kServeIntervalSubs)),
      nPathStores_(fmt::format("{}.{}", metricPrefixOwned_, kPathStoreNum)),
      nPathStoreAllocs_(
          fmt::format("{}.{}", metricPrefixOwned_, kPathStoreAllocs)),
      serveSubMs_(fmt::format("{}.{}", metricPrefixOwned_, kServeSubMs)),
      serveSubNum_(fmt::format("{}.{}", metricPrefixOwned_, kServeSubNum)),
      publishTimePrefix_(
          fmt::format("{}.{}", metricPrefixOwned_, kPublishTimePrefix)),
      subscribeTimePrefix_(
          fmt::format("{}.{}", metricPrefixOwned_, kSubscribeTimePrefix)),
      subscriberPrefix_(
          fmt::format("{}.{}", metricPrefixOwned_, kSubscriberPrefix)) {
  if (params_.trackMetadata_) {
    metadataTracker_ = std::make_unique<FsdbOperTreeMetadataTracker>();
  }
  if (pathToRootHelper.has_value()) {
    pathToRootHelper_ = pathToRootHelper;
  } else {
    pathToRootHelper_ = OperPathToPublisherRoot();
  }

  // init metrics

  fb303::ThreadCachedServiceData::get()->addStatExportType(rss_, fb303::AVG);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      registeredSubs_, fb303::AVG);
  fb303::ThreadCachedServiceData::get()->addStatExportType(
      registeredIntervalSubs_, fb303::AVG);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      nPathStores_, fb303::AVG);

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      nPathStoreAllocs_, fb303::AVG);

  // Ranged to 2x the tick so an overrunning serve lands in a real bin.
  {
    const int64_t maxMs =
        std::max<int64_t>(2 * params_.serveTickInterval_.count(), 1000);
    fb303::ThreadCachedServiceData::get()->addHistogram(
        serveSubMs_, 20, 0, maxMs);
    fb303::ThreadCachedServiceData::get()->exportHistogram(
        serveSubMs_, 50, 95, 99);
  }

  fb303::ThreadCachedServiceData::get()->addStatExportType(
      serveSubNum_, fb303::SUM);

  {
    const auto numBuckets = serveBucketCount(
        static_cast<uint32_t>(params_.serveTickInterval_.count()),
        static_cast<uint32_t>(params_.subscriptionServeInterval_.count()));
    if (numBuckets > 1 && params_.exportPerBucketServeMetrics_) {
      const auto tickMs = params_.serveTickInterval_.count();
      for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
        // Keyed by cadence, not bucket index: the index means a different
        // interval under a different tick, so a tick change would silently
        // repoint an existing ODS series instead of starting a new one.
        const auto cadenceMs = tickMs * static_cast<int64_t>(bucket + 1);
        serveSubMsByBucket_.push_back(
            fmt::format("{}.interval_{}ms", serveSubMs_, cadenceMs));
        serveSubNumByBucket_.push_back(
            fmt::format("{}.interval_{}ms", serveSubNum_, cadenceMs));
        // addHistogram takes a bin width, not a count: a fixed width would give
        // the slowest bucket 50x the bins of the fastest.
        const int64_t maxMs = std::max<int64_t>(2 * cadenceMs, 1000);
        fb303::ThreadCachedServiceData::get()->addHistogram(
            serveSubMsByBucket_.back(),
            std::max<int64_t>(maxMs / kServeHistogramBins, 1),
            0,
            maxMs);
        fb303::ThreadCachedServiceData::get()->exportHistogram(
            serveSubMsByBucket_.back(), 50, 95, 99);
        fb303::ThreadCachedServiceData::get()->addStatExportType(
            serveSubNumByBucket_.back(), fb303::SUM);
      }
    }
  }

  if (FLAGS_serveHeartbeats) {
    heartbeatThread_ = std::make_unique<folly::ScopedEventBaseThread>(
        "SubscriptionHeartbeats");
  }
}

std::optional<uint32_t>
NaivePeriodicSubscribableStorageBase::resolveServeIntervalMs(
    const std::optional<SubscriptionStorageParams>& subscriptionParams,
    const SubscriptionIdentifier& subscriber) const {
  if (!subscriptionParams ||
      !subscriptionParams->serveIntervalMs_.has_value()) {
    return std::nullopt;
  }
  const auto requestedMs = *subscriptionParams->serveIntervalMs_;
  const auto tickMs = static_cast<uint32_t>(params_.serveTickInterval_.count());
  const auto defaultMs =
      static_cast<uint32_t>(params_.subscriptionServeInterval_.count());
  const auto grantedMs =
      normalizeServeIntervalMs(requestedMs, tickMs, defaultMs);
  if (grantedMs != requestedMs) {
    // Requests are never rejected, so without this a client asking for a
    // cadence the server cannot honour just quietly runs at another one.
    XLOG_EVERY_MS(WARN, 5000)
        << "FSDB[" << metricPrefixOwned_ << "] subscriber "
        << subscriber.subscriberId() << " requested a " << requestedMs
        << "ms serve interval, granted " << grantedMs << "ms ("
        << describeServeIntervalClamp(requestedMs, tickMs, defaultMs)
        << "; tick=" << tickMs << "ms default=" << defaultMs << "ms)";
  }
  return grantedMs;
}

FsdbOperTreeMetadataTracker NaivePeriodicSubscribableStorageBase::getMetadata()
    const {
  return metadataTracker_.withRLock([&](auto& tracker) {
    CHECK(tracker);
    return *tracker;
  });
}

void NaivePeriodicSubscribableStorageBase::start_impl() {
  auto runningLocked = running_.wlock();

  if (*runningLocked) {
    return;
  }

  subscriptionServingThread_ = std::make_unique<std::thread>([=, this] {
    folly::setThreadName("ServeSubscriptions");
    evb_.loopForever();
  });

  XLOG(DBG1) << "Starting subscribable storage thread heartbeat";
  auto heartbeatStatsFunc = [](int /* delay */, int /* backLog */) {};
  threadHeartbeat_ = std::make_shared<ThreadHeartbeat>(
      &evb_,
      "ServeSubscriptions",
      FLAGS_storage_thread_heartbeat_ms,
      heartbeatStatsFunc);

  // One loop drives every bucket; it runs on the main serve evb.
  backgroundScope_.add(co_withExecutor(&evb_, serveSubscriptionsTick()));

  *runningLocked = true;
}

void NaivePeriodicSubscribableStorageBase::stop_impl() {
  XLOG(DBG1) << "Stopping subscribable storage";
  bool wasRunning{false};
  {
    auto runningLocked = running_.wlock();
    wasRunning = *runningLocked;
    // Set running_ to false and give up lock. Else serveSubscriptions
    // thread could block waiting on acquiring running_.rlock() forever.
    // That causes subscriptionServingThread_->join to later deadlock
    *runningLocked = false;
  }

  if (wasRunning) {
    XLOG(DBG1) << "Cancelling background scope";
    folly::coro::blockingWait(backgroundScope_.cancelAndJoinAsync());
    XLOG(DBG1) << "Stopping eventbase";
    evb_.runImmediatelyOrRunInEventBaseThreadAndWait(
        [this] { evb_.terminateLoopSoon(); });
    subscriptionServingThread_->join();
  }

  if (threadHeartbeat_) {
    XLOG(DBG1) << "Stopping subscribable storage thread heartbeat";
    threadHeartbeat_.reset();
  }
  XLOG(INFO) << "Stopped subscribable storage";
}

void NaivePeriodicSubscribableStorageBase::registerPublisher(
    PathIter begin,
    PathIter end,
    bool skipThriftStreamLivenessCheck) {
  if (!params_.trackMetadata_) {
    return;
  }
  auto publisherRoot = getPublisherRoot(begin, end);
  CHECK(publisherRoot.has_value());
  registeredPublisherRoots_.withWLock([&](auto& roots) {
    if (roots.find(publisherRoot.value()) == roots.end()) {
      roots[publisherRoot.value()] = *begin;
      // add histograms for per-publisherRoot stats
      // publish time: histogram range [0, 1s], 10ms width (100 bins)
      auto publishTimeMetric = fmt::format("{}.{}", publishTimePrefix_, *begin);
      fb303::ThreadCachedServiceData::get()->addHistogram(
          publishTimeMetric, 10, 0, 1000);
      fb303::ThreadCachedServiceData::get()->exportHistogram(
          publishTimeMetric, 50, 95, 99);
      // subscribe time: histogram range [0, 10s], 100ms width (100 bins)
      auto subscribeTimeMetric =
          fmt::format("{}.{}", subscribeTimePrefix_, *begin);
      fb303::ThreadCachedServiceData::get()->addHistogram(
          subscribeTimeMetric, 100, 0, 1000);
      fb303::ThreadCachedServiceData::get()->exportHistogram(
          subscribeTimeMetric, 50, 95, 99);
    }
  });
  metadataTracker_.withWLock([&](auto& tracker) {
    CHECK(tracker);
    tracker->registerPublisherRoot(
        publisherRoot.value(), skipThriftStreamLivenessCheck);
  });
}

void NaivePeriodicSubscribableStorageBase::unregisterPublisher(
    PathIter begin,
    PathIter end,
    FsdbErrorCode disconnectReason) {
  if (!params_.trackMetadata_) {
    return;
  }
  metadataTracker_.withWLock([&](auto& tracker) {
    CHECK(tracker);
    auto publisherRoot = getPublisherRoot(begin, end);
    CHECK(publisherRoot);
    tracker->unregisterPublisherRoot(*publisherRoot);
    if (!tracker->getPublisherRootMetadata(*publisherRoot)) {
      subMgr().closeNoPublisherActiveSubscriptions(
          SubscriptionMetadataServer(tracker->getAllMetadata()),
          disconnectReason);
    }
  });
}

SubscriptionMetadataServer
NaivePeriodicSubscribableStorageBase::getCurrentMetadataServer() {
  std::optional<FsdbOperTreeMetadataTracker::PublisherRoot2Metadata> metadata;
  metadataTracker_.withRLock([&metadata](auto& tracker) {
    if (tracker) {
      metadata = tracker->getAllMetadata();
    }
  });
  return SubscriptionMetadataServer(std::move(metadata));
}

void NaivePeriodicSubscribableStorageBase::initExportedSubscriberStats(
    FsdbClient clientId) {
  auto it = registeredSubscriberClientIds_.find(clientId);
  if (it == registeredSubscriberClientIds_.end()) {
    registeredSubscriberClientIds_.insert(clientId);

    std::string clientName = fsdbClient2string(clientId);
    fb303::ThreadCachedServiceData::get()->addStatExportType(
        fmt::format(
            "{}.{}.{}",
            subscriberPrefix_,
            clientName,
            kSubscriptionQueueWatermark),
        fb303::AVG);
    fb303::ThreadCachedServiceData::get()->addStatExportType(
        fmt::format(
            "{}.{}.{}", subscriberPrefix_, clientName, kSubscriberConnected),
        fb303::COUNT);
    fb303::ThreadCachedServiceData::get()->addStatExportType(
        fmt::format(
            "{}.{}.{}",
            subscriberPrefix_,
            clientName,
            kSlowSubscriptionDisconnects),
        fb303::COUNT);
  }
}

void exportSubscriberStats(
    const std::string& prefix,
    const FsdbClient& clientId,
    const SubscriberStats& stat) {
  std::string clientName = fsdbClient2string(clientId);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      fmt::format("{}.{}.{}", prefix, clientName, kSubscriptionQueueWatermark),
      stat.subscriptionServeQueueWatermark,
      fb303::AVG);
  // TODO: export counter for number of connected Thrift streams per client
  fb303::ThreadCachedServiceData::get()->addStatValue(
      fmt::format("{}.{}.{}", prefix, clientName, kSubscriberConnected),
      (stat.numSubscriptions > 0),
      fb303::AVG);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      fmt::format("{}.{}.{}", prefix, clientName, kSlowSubscriptionDisconnects),
      stat.numSlowSubscriptionDisconnects,
      fb303::AVG);
}

void NaivePeriodicSubscribableStorageBase::exportTickMetrics() {
  int64_t memUsage = getMemoryUsage(); // RSS
  fb303::ThreadCachedServiceData::get()->addStatValue(
      rss_, memUsage, fb303::AVG);

  fb303::ThreadCachedServiceData::get()->addStatValue(
      registeredSubs_, numSubscriptions(), fb303::AVG);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      registeredIntervalSubs_, numIntervalSubscriptions(), fb303::AVG);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      nPathStores_, numPathStores(), fb303::AVG);
  fb303::ThreadCachedServiceData::get()->addStatValue(
      nPathStoreAllocs_, numPathStoreAllocs(), fb303::AVG);

  if (params_.exportPerSubscriberMetrics_) {
    std::map<FsdbClient, SubscriberStats> stats = subMgr().getSubscriberStats();
    for (const auto& [key, stat] : stats) {
      initExportedSubscriberStats(key);
      exportSubscriberStats(subscriberPrefix_, key, stat);
    }
  }
}

void NaivePeriodicSubscribableStorageBase::exportServeMetrics(
    std::chrono::steady_clock::time_point serveStartTime,
    SubscriptionMetadataServer& metadata,
    std::map<std::string, uint64_t>& lastServedPublisherRootUpdates,
    std::chrono::milliseconds bucketInterval,
    size_t bucket) {
  auto currentTimestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - serveStartTime);
  if (elapsed.count() > 0) {
    // ignore idle cycles, only count busy ones
    fb303::ThreadCachedServiceData::get()->addHistogramValue(
        serveSubMs_, elapsed.count());
    if (bucket < serveSubMsByBucket_.size()) {
      fb303::ThreadCachedServiceData::get()->addHistogramValue(
          serveSubMsByBucket_[bucket], elapsed.count());
    }
  }
  // Surface slow serve cycles (>2x interval) for OSS vendors without fb303/ODS.
  auto intervalMs = bucketInterval.count();
  if (intervalMs > 0 && elapsed.count() > 2 * intervalMs) {
    XLOG_EVERY_MS(WARN, 5000)
        << "FSDB[" << metricPrefixOwned_ << "] slow serve: " << elapsed.count()
        << "ms (bucket=" << bucket << " interval=" << intervalMs << "ms)";
  }
  fb303::ThreadCachedServiceData::get()->addStatValue(
      serveSubNum_, 1, fb303::SUM);
  if (bucket < serveSubNumByBucket_.size()) {
    fb303::ThreadCachedServiceData::get()->addStatValue(
        serveSubNumByBucket_[bucket], 1, fb303::SUM);
  }

  // for each publisher root that had updates served in this iteration,
  // export publish/serve times for last processed update
  auto served = metadata.getAllPublishersMetadata();
  if (served.has_value()) {
    for (auto& [root, md] : served.value()) {
      auto lockedRegisteredPublisherRoots = registeredPublisherRoots_.rlock();
      auto it = lockedRegisteredPublisherRoots->find(root);
      if (it == lockedRegisteredPublisherRoots->end()) {
        continue;
      }

      // skip root if there was no update since last served
      if (md.operMetadata.lastPublishedAt().value_or(0) == 0) {
        continue;
      }
      auto mitr = lastServedPublisherRootUpdates.find(root);
      if (mitr != lastServedPublisherRootUpdates.end()) {
        if (mitr->second == md.operMetadata.lastPublishedAt().value()) {
          continue;
        }
      }
      lastServedPublisherRootUpdates[root] =
          md.operMetadata.lastPublishedAt().value();

      // export metrics for the served root
      auto publishTime = md.lastPublishedUpdateProcessedAt -
          md.operMetadata.lastPublishedAt().value();
      auto serveTime =
          currentTimestamp - md.operMetadata.lastPublishedAt().value();
      fb303::ThreadCachedServiceData::get()->addHistogramValue(
          fmt::format("{}.{}", publishTimePrefix_, it->second), publishTime);
      fb303::ThreadCachedServiceData::get()->addHistogramValue(
          fmt::format("{}.{}", subscribeTimePrefix_, it->second), serveTime);
    }
  }
}

std::optional<std::string>
NaivePeriodicSubscribableStorageBase::getPublisherRoot(
    PathIter begin,
    PathIter end) const {
  auto path = convertPath(ConcretePath(begin, end));
  return params_.trackMetadata_
      ? std::make_optional(
            pathToRootHelper_->publisherRoot(path.begin(), path.end()))
      : std::nullopt;
}

std::optional<std::string>
NaivePeriodicSubscribableStorageBase::getPublisherRoot(
    const std::vector<ExtendedOperPath>& paths) const {
  auto convertedPaths = convertExtPaths(paths);
  return params_.trackMetadata_
      ? std::make_optional(pathToRootHelper_->publisherRoot(convertedPaths))
      : std::nullopt;
}

std::optional<std::string>
NaivePeriodicSubscribableStorageBase::getPublisherRoot(
    ExtPathIter begin,
    ExtPathIter end) const {
  auto path = convertPath(ExtPath(begin, end));
  return params_.trackMetadata_
      ? std::make_optional(
            pathToRootHelper_->publisherRoot(path.begin(), path.end()))
      : std::nullopt;
}

std::optional<std::string>
NaivePeriodicSubscribableStorageBase::getPublisherRoot(
    const std::map<SubscriptionKey, RawOperPath>& paths) const {
  return params_.trackMetadata_
      ? std::make_optional(pathToRootHelper_->publisherRoot(paths))
      : std::nullopt;
}

std::optional<std::string>
NaivePeriodicSubscribableStorageBase::getPublisherRoot(
    const std::map<SubscriptionKey, ExtendedOperPath>& paths) const {
  return params_.trackMetadata_
      ? std::make_optional(pathToRootHelper_->publisherRoot(paths))
      : std::nullopt;
}

void NaivePeriodicSubscribableStorageBase::updateMetadata(
    PathIter begin,
    PathIter end,
    const OperMetadata& metadata) {
  metadataTracker_.withWLock(
      [&](auto& tracker) {
        if (tracker) {
          auto publisherRoot = getPublisherRoot(begin, end);
          CHECK(publisherRoot)
              << "Publisher root must be available before metadata can "
              << "be tracked ";
          tracker->updateMetadata(*publisherRoot, metadata);
        }
      });
}

std::vector<ExtendedOperPath>
NaivePeriodicSubscribableStorageBase::convertExtPaths(
    const std::vector<ExtendedOperPath>& paths) const {
  if (!params_.convertSubsToIDPaths_) {
    return paths;
  }
  std::vector<ExtendedOperPath> convertedPaths;
  convertedPaths.reserve(paths.size());
  for (const auto& path : paths) {
    ExtendedOperPath p = path;
    p.path() = convertPath(*path.path());
    convertedPaths.emplace_back(std::move(p));
  }
  return convertedPaths;
}

folly::coro::AsyncGenerator<DeltaValue<OperState>&&>
NaivePeriodicSubscribableStorageBase::subscribe_encoded_impl(
    SubscriptionIdentifier&& subscriber,
    PathIter begin,
    PathIter end,
    OperProtocol protocol,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  auto path = convertPath(ConcretePath(begin, end));
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = PathSubscription::create(
      std::move(subscriber),
      path.begin(),
      path.end(),
      protocol,
      getPublisherRoot(path.begin(), path.end()),
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.pathSubscriptionServeQueueSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  subMgr().registerSubscription(std::move(subscription));
  return std::move(gen);
}

SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>
NaivePeriodicSubscribableStorageBase::subscribe_delta_impl(
    SubscriptionIdentifier&& subscriber,
    PathIter begin,
    PathIter end,
    OperProtocol protocol,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  auto path = convertPath(ConcretePath(begin, end));
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = DeltaSubscription::create(
      std::move(subscriber),
      path.begin(),
      path.end(),
      protocol,
      getPublisherRoot(path.begin(), path.end()),
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.defaultSubscriptionServeQueueSize_,
      params_.deltaSubscriptionQueueMemoryLimit_,
      params_.deltaSubscriptionQueueFullMinSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  auto sharedStreamInfo = subscription->getSharedStreamInfo();
  subMgr().registerSubscription(std::move(subscription));
  return SubscriptionStreamReader<SubscriptionServeQueueElement<OperDelta>>{
      std::move(gen), std::move(sharedStreamInfo)};
}

folly::coro::AsyncGenerator<std::vector<DeltaValue<TaggedOperState>>&&>
NaivePeriodicSubscribableStorageBase::subscribe_encoded_extended_impl(
    SubscriptionIdentifier&& subscriber,
    std::vector<ExtendedOperPath> paths,
    OperProtocol protocol,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  paths = convertExtPaths(paths);
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto publisherRoot = getPublisherRoot(paths);
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = ExtendedPathSubscription::create(
      std::move(subscriber),
      std::move(paths),
      std::move(publisherRoot),
      protocol,
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.pathSubscriptionServeQueueSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  subMgr().registerExtendedSubscription(std::move(subscription));
  return std::move(gen);
}

SubscriptionStreamReader<
    SubscriptionServeQueueElement<std::vector<TaggedOperDelta>>>
NaivePeriodicSubscribableStorageBase::subscribe_delta_extended_impl(
    SubscriptionIdentifier&& subscriber,
    std::vector<ExtendedOperPath> paths,
    OperProtocol protocol,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  paths = convertExtPaths(paths);
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto publisherRoot = getPublisherRoot(paths);
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = ExtendedDeltaSubscription::create(
      std::move(subscriber),
      std::move(paths),
      std::move(publisherRoot),
      protocol,
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.defaultSubscriptionServeQueueSize_,
      params_.deltaSubscriptionQueueMemoryLimit_,
      params_.deltaSubscriptionQueueFullMinSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  auto sharedStreamInfo = subscription->getSharedStreamInfo();
  subMgr().registerExtendedSubscription(std::move(subscription));
  return SubscriptionStreamReader<
      SubscriptionServeQueueElement<std::vector<TaggedOperDelta>>>{
      std::move(gen), std::move(sharedStreamInfo)};
}

SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
NaivePeriodicSubscribableStorageBase::subscribe_patch_impl(
    SubscriptionIdentifier&& subscriber,
    std::map<SubscriptionKey, RawOperPath> rawPaths,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  for (auto& [key, path] : rawPaths) {
    auto convertedPath = convertPath(std::move(*path.path()));
    path.path() = std::move(convertedPath);
  }
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto root = getPublisherRoot(rawPaths);
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = ExtendedPatchSubscription::create(
      std::move(subscriber),
      std::move(rawPaths),
      patchOperProtocol_,
      std::move(root),
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.defaultSubscriptionServeQueueSize_,
      params_.deltaSubscriptionQueueMemoryLimit_,
      params_.deltaSubscriptionQueueFullMinSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  auto sharedStreamInfo = subscription->getSharedStreamInfo();
  subMgr().registerExtendedSubscription(std::move(subscription));
  return SubscriptionStreamReader<
      SubscriptionServeQueueElement<SubscriberMessage>>{
      std::move(gen), std::move(sharedStreamInfo)};
}

SubscriptionStreamReader<SubscriptionServeQueueElement<SubscriberMessage>>
NaivePeriodicSubscribableStorageBase::subscribe_patch_extended_impl(
    SubscriptionIdentifier&& subscriber,
    std::map<SubscriptionKey, ExtendedOperPath> paths,
    std::optional<SubscriptionStorageParams> subscriptionParams) {
  for (auto& [key, path] : paths) {
    auto convertedPath = convertPath(std::move(*path.path()));
    path.path() = std::move(convertedPath);
  }
  auto heartbeatInterval = params_.subscriptionHeartbeatInterval_;
  if (subscriptionParams &&
      subscriptionParams->heartbeatInterval_.has_value()) {
    heartbeatInterval = subscriptionParams->heartbeatInterval_.value();
  }
  auto root = getPublisherRoot(paths);
  auto serveIntervalMs = resolveServeIntervalMs(subscriptionParams, subscriber);
  auto [gen, subscription] = ExtendedPatchSubscription::create(
      std::move(subscriber),
      std::move(paths),
      patchOperProtocol_,
      std::move(root),
      heartbeatThread_ ? heartbeatThread_->getEventBase() : nullptr,
      heartbeatInterval,
      params_.defaultSubscriptionServeQueueSize_,
      params_.deltaSubscriptionQueueMemoryLimit_,
      params_.deltaSubscriptionQueueFullMinSize_);
  subscription->setServeIntervalMs(serveIntervalMs);
  auto sharedStreamInfo = subscription->getSharedStreamInfo();
  subMgr().registerExtendedSubscription(std::move(subscription));
  return SubscriptionStreamReader<
      SubscriptionServeQueueElement<SubscriberMessage>>{
      std::move(gen), std::move(sharedStreamInfo)};
}

std::optional<FsdbErrorCode>
NaivePeriodicSubscribableStorageBase::add_patch_subscription_path_impl(
    SubscriptionIdentifier&& id,
    std::map<SubscriptionKey, RawOperPath> newPaths,
    std::optional<StreamRevision> streamRevision) {
  if (newPaths.empty()) {
    // Nothing to add; reject rather than silently recording a no-op.
    return FsdbErrorCode::INVALID_REQUEST;
  }
  for (auto& [key, path] : newPaths) {
    auto convertedPath = convertPath(std::move(*path.path()));
    path.path() = std::move(convertedPath);
  }
  auto root = getPublisherRoot(newPaths);
  return subMgr().addPatchSubscriptionPaths(
      id, std::move(newPaths), root, streamRevision);
}

std::optional<FsdbErrorCode>
NaivePeriodicSubscribableStorageBase::add_extended_patch_subscription_path_impl(
    SubscriptionIdentifier&& id,
    std::map<SubscriptionKey, ExtendedOperPath> newPaths,
    std::optional<StreamRevision> streamRevision) {
  if (newPaths.empty()) {
    return FsdbErrorCode::INVALID_REQUEST;
  }
  for (auto& [key, path] : newPaths) {
    auto convertedPath = convertPath(std::move(*path.path()));
    path.path() = std::move(convertedPath);
  }
  auto root = getPublisherRoot(newPaths);
  return subMgr().addPatchSubscriptionPaths(
      id, std::move(newPaths), root, streamRevision);
}

} // namespace facebook::fboss::fsdb
