// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include <fboss/fsdb/oper/CowSubscriptionManager.h>
#include <fboss/fsdb/oper/DeltaValue.h>
#include <fboss/fsdb/oper/NaivePeriodicSubscribableStorageBase.h>
#include <fboss/fsdb/oper/PathConverter.h>
#include <fboss/fsdb/oper/SubscribableStorage.h>
#include <fboss/thrift_cow/storage/CowStorage.h>
#include <fboss/thrift_cow/storage/Storage.h>

#include <folly/Expected.h>
#include <folly/coro/Sleep.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <chrono>
#include <deque>
#include <optional>
#include <utility>

namespace facebook::fboss::fsdb {

template <typename Storage, typename SubscribeManager>
class NaivePeriodicSubscribableStorage
    : public NaivePeriodicSubscribableStorageBase,
      public SubscribableStorage<
          typename Storage::RootT,
          NaivePeriodicSubscribableStorage<Storage, SubscribeManager>> {
 public:
  // TODO: more flexibility here for all forward iterator types
  using RootT = typename Storage::RootT;
  using ConcretePath = typename Storage::ConcretePath;
  using PathIter = typename Storage::PathIter;
  using ExtPath = typename Storage::ExtPath;
  using ExtPathIter = typename Storage::ExtPathIter;
  using RootNode = typename Storage::StorageImpl;

  template <typename T>
  using Result = typename Storage::template Result<T>;

  using Self = NaivePeriodicSubscribableStorage<Storage, SubscribeManager>;
  using Base = SubscribableStorage<RootT, Self>;

  explicit NaivePeriodicSubscribableStorage(
      const RootT& initialState,
      StorageParams params = StorageParams());

  ~NaivePeriodicSubscribableStorage();

  using NaivePeriodicSubscribableStorageBase::start_impl;
  using NaivePeriodicSubscribableStorageBase::stop_impl;

  using Base::get;
  using Base::get_encoded;
  using Base::get_encoded_extended;
  using Base::remove;
  using Base::set;
  using Base::set_encoded;
  using Base::start;
  using Base::stop;
  using Base::subscribe;
  using Base::subscribe_delta;
  using Base::subscribe_delta_extended;
  using Base::subscribe_encoded;
  using Base::subscribe_encoded_extended;
  using Base::subscribe_patch;

  const folly::Synchronized<Storage>& publishedStateForReads() const {
    return lastPublishedState_;
  }

  bool publishedStateCurrent() const;

  size_t numServeBuckets() const {
    return subscriptions_.numBuckets();
  }

  // GET reads are served from lastPublishedState_, so it has to keep tracking
  // current state even on ticks where no bucket is due and even with zero
  // subscribers; otherwise reads would freeze at process start.
  void refreshPublishedStateForReads();

  // A bucket nobody subscribes to has nothing to diff against, and a retained
  // baseline would pin that tree version for as long as the bucket stays empty.
  void releaseBucketBaseline(size_t bucket);

  template <typename T>
  Result<T> get_impl(PathIter begin, PathIter end) const {
    if (params_.serveGetRequestsWithLastPublishedState_) {
      auto state = Storage(*publishedStateForReads().rlock());
      return state.template get<T>(begin, end);
    } else {
      // hold rlock on current state to avoid racing with writers
      auto currentState = currentState_.rlock();
      return currentState->template get<T>(begin, end);
    }
  }

  Result<OperState>
  get_encoded_impl(PathIter begin, PathIter end, OperProtocol protocol) const;

  Result<std::vector<TaggedOperState>> get_encoded_extended_impl(
      ExtPathIter begin,
      ExtPathIter end,
      OperProtocol protocol) const;

  template <typename T>
  std::optional<StorageError>
  set_impl(PathIter begin, PathIter end, T&& value) {
    auto state = currentState_.wlock();
    updateMetadata(begin, end);
    return state->set(begin, end, std::forward<T>(value));
  }

  std::optional<StorageError>
  set_encoded_impl(PathIter begin, PathIter end, const OperState& value);

  template <typename T>
  std::optional<StorageError>
  add_impl(PathIter begin, PathIter end, T&& value) {
    auto state = currentState_.wlock();
    updateMetadata(begin, end);
    return state->add(begin, end, std::forward<T>(value));
  }

  void remove_impl(PathIter begin, PathIter end);

  std::optional<StorageError> patch_impl(Patch&& patch);
  using NaivePeriodicSubscribableStorageBase::add_patch_subscription_path_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_patch_extended_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_patch_impl;

  std::optional<StorageError> patch_impl(const fsdb::OperDelta& delta);

  std::optional<StorageError> patch_impl(
      const fsdb::TaggedOperState& operState);

  using NaivePeriodicSubscribableStorageBase::subscribe_delta_extended_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_delta_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_encoded_extended_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_encoded_impl;
  using NaivePeriodicSubscribableStorageBase::subscribe_impl;

  // Each bucket keeps its own oldRoot: a single shared baseline would make a
  // slow bucket diff against the last tick instead of against what it last
  // served, silently dropping every change in between.
  std::tuple<
      std::shared_ptr<RootNode>,
      std::shared_ptr<RootNode>,
      SubscriptionMetadataServer>
  publishCurrentState(size_t bucket);

  folly::coro::Task<void> serveSubscriptionsTick() override;

  // A bucket is due on ticks its interval divides, so same-interval subscribers
  // stay phase-aligned and share a single publish.
  folly::coro::Task<void> serveSubscriptionsLoop(
      std::chrono::milliseconds tick);

  using NaivePeriodicSubscribableStorageBase::getSubscriptions;
  using NaivePeriodicSubscribableStorageBase::numPathStores;
  // Do not use, except for UTs that cross check numPathStores()
  using NaivePeriodicSubscribableStorageBase::numPathStoresRecursive_Expensive;
  using NaivePeriodicSubscribableStorageBase::numSubscriptions;
  using NaivePeriodicSubscribableStorageBase::setConvertToIDPaths;

  /*
   * Expensive API to copy current root. To be used only
   * in tests
   */
  RootT currentStateExpensive() const;

  OperState publishedStateEncoded(OperProtocol protocol);

 protected:
  const SubscriptionManagerBase& subMgr() const override {
    return subscriptions_;
  }

  SubscriptionManagerBase& subMgr() override {
    return subscriptions_;
  }

  ConcretePath convertPath(ConcretePath&& path) const override;

  ExtPath convertPath(const ExtPath& path) const override;

  folly::Synchronized<Storage> currentState_;
  // Backs GET reads, separately from any bucket's baseline.
  folly::Synchronized<Storage> lastPublishedState_;
  // Baseline per bucket, so each bucket diffs against what its own subscribers
  // last saw. Empty while the bucket has no subscribers.
  std::deque<folly::Synchronized<std::optional<Storage>>> bucketPublishedState_;

  SubscribeManager subscriptions_;
};

// Out of class with explicit return types so extern templates suppress them.
template <typename Storage, typename SubscribeManager>
NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    NaivePeriodicSubscribableStorage(
        const RootT& initialState,
        StorageParams params)
    : NaivePeriodicSubscribableStorageBase(params),
      currentState_(std::in_place, initialState),
      lastPublishedState_(*currentState_.rlock()),
      subscriptions_(
          patchOperProtocol_,
          params.requireResponseOnInitialSync_,
          serveBucketCount(
              static_cast<uint32_t>(params.serveTickInterval_.count()),
              static_cast<uint32_t>(params.subscriptionServeInterval_.count())),
          static_cast<uint32_t>(params.serveTickInterval_.count())) {
  for (size_t i = 0; i < subscriptions_.numBuckets(); ++i) {
    bucketPublishedState_.emplace_back();
  }
  subscriptions_.useIdPaths(params.convertSubsToIDPaths_);
  auto currentState = currentState_.wlock();
  currentState->publish();
}

template <typename Storage, typename SubscribeManager>
NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    ~NaivePeriodicSubscribableStorage() {
  stop();
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    publishedStateCurrent() const -> bool {
  auto& slot = lastPublishedState_;
  auto lastState = slot.rlock();
  auto currentState = currentState_.rlock();
  return lastState->root() == currentState->root();
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    refreshPublishedStateForReads() -> void {
  if (publishedStateCurrent()) {
    return;
  }
  auto lastState = lastPublishedState_.wlock();
  auto currentState = currentState_.wlock();
  auto newRoot = currentState->root();
  if (lastState->root() != newRoot) {
    subscriptions_.publishAndAddPaths(newRoot);
    *lastState = Storage(*currentState);
  }
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    releaseBucketBaseline(size_t bucket) -> void {
  auto& slot = bucketPublishedState_[bucket];
  if (!slot.rlock()->has_value()) {
    return;
  }
  slot.wlock()->reset();
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    get_encoded_impl(PathIter begin, PathIter end, OperProtocol protocol) const
    -> Result<OperState> {
  Result<OperState> result = folly::makeUnexpected(
      StorageError(StorageError::Code::INVALID_PATH, "Unknown"));
  if (params_.serveGetRequestsWithLastPublishedState_) {
    auto state = Storage(*publishedStateForReads().rlock());
    result = state.get_encoded(begin, end, protocol);
  } else {
    // hold rlock on current state to avoid racing with writers
    auto currentState = currentState_.rlock();
    result = currentState->get_encoded(begin, end, protocol);
  }
  if (result.hasValue() && params_.trackMetadata_) {
    auto publisherRoot = getPublisherRoot(begin, end);
    metadataTracker_.withRLock([&](auto& tracker) {
      CHECK(tracker);
      auto metadata = tracker->getPublisherRootMetadata(*publisherRoot);
      if (metadata && *metadata->operMetadata.lastConfirmedAt() > 0) {
        result.value().metadata() = metadata->operMetadata;
        result.value().metadata()->lastServedAt() =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
      } else {
        throw Utils::createFsdbException(
            FsdbErrorCode::PUBLISHER_NOT_READY,
            fmt::format("Publisher not ready for root: {}", *publisherRoot));
      }
    });
  }
  return result;
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    get_encoded_extended_impl(
        ExtPathIter begin,
        ExtPathIter end,
        OperProtocol protocol) const -> Result<std::vector<TaggedOperState>> {
  Result<std::vector<TaggedOperState>> result = folly::makeUnexpected(
      StorageError(StorageError::Code::INVALID_PATH, "Unknown"));
  if (params_.serveGetRequestsWithLastPublishedState_) {
    auto state = Storage(*publishedStateForReads().rlock());
    result = state.get_encoded_extended(begin, end, protocol);
  } else {
    auto currentState = currentState_.rlock();
    result = currentState->get_encoded_extended(begin, end, protocol);
  }
  if (result.hasValue() && params_.trackMetadata_) {
    auto publisherRoot = getPublisherRoot(begin, end);
    metadataTracker_.withRLock([&](auto& tracker) {
      CHECK(tracker);
      auto metadata = tracker->getPublisherRootMetadata(*publisherRoot);
      if (metadata && *metadata->operMetadata.lastConfirmedAt() > 0) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        for (auto& state : result.value()) {
          state.state()->metadata() = metadata->operMetadata;
          state.state()->metadata()->lastServedAt() = now;
        }
      } else {
        throw Utils::createFsdbException(
            FsdbErrorCode::PUBLISHER_NOT_READY,
            fmt::format("Publisher not ready for root: {}", *publisherRoot));
      }
    });
  }
  return result;
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    set_encoded_impl(PathIter begin, PathIter end, const OperState& value)
        -> std::optional<StorageError> {
  auto state = currentState_.wlock();
  auto metadata = value.metadata() ? *value.metadata() : OperMetadata();
  updateMetadata(begin, end, metadata);
  return state->set_encoded(begin, end, value);
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::remove_impl(
    PathIter begin,
    PathIter end) -> void {
  auto state = currentState_.wlock();
  updateMetadata(begin, end);
  state->remove(begin, end);
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::patch_impl(
    Patch&& patch) -> std::optional<StorageError> {
  if (patch.patch()->getType() == thrift_cow::PatchNode::Type::__EMPTY__) {
    XLOG(DBG3) << "Patch is empty, nothing to do";
    return StorageError(StorageError::Code::TYPE_ERROR, "Empty patch");
  }
  auto& path = *patch.basePath();
  auto state = currentState_.wlock();
  updateMetadata(path.begin(), path.end(), *patch.metadata());
  return state->patch(std::move(patch));
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::patch_impl(
    const fsdb::OperDelta& delta) -> std::optional<StorageError> {
  if (!delta.changes()->size()) {
    return std::nullopt;
  }
  // Pick the publisher root path from first unit.
  // TODO - have caller to patch send the path like
  // we do for oper state
  auto& path = *delta.changes()->begin()->path()->raw();
  auto state = currentState_.wlock();
  auto metadata = delta.metadata() ? *delta.metadata() : OperMetadata();
  updateMetadata(path.begin(), path.end(), metadata);
  return state->patch(delta);
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::patch_impl(
    const fsdb::TaggedOperState& operState) -> std::optional<StorageError> {
  auto& path = *operState.path()->path();
  auto state = currentState_.wlock();
  auto metadata = operState.state()->metadata() ? *operState.state()->metadata()
                                                : OperMetadata();
  updateMetadata(path.begin(), path.end(), metadata);
  return state->patch(operState);
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    publishCurrentState(size_t bucket) -> std::tuple<
        std::shared_ptr<RootNode>,
        std::shared_ptr<RootNode>,
        SubscriptionMetadataServer> {
  auto lastState = bucketPublishedState_[bucket].wlock();
  // wlock not rlock: the COW freeze walk mutates published_ on shared nodes
  // and must exclude both concurrent freezes and the rlock GET readers.
  auto currentState = currentState_.wlock();

  auto newRoot = currentState->root();
  // An empty slot means the bucket just gained its first subscriber, which
  // gets the whole tree from initial sync rather than a delta.
  auto oldRoot = lastState->has_value() ? (*lastState)->root() : newRoot;
  // Read metadata under the lock so it matches currentState.
  SubscriptionMetadataServer metadataServer = getCurrentMetadataServer();

  // A first serve carries no delta, but the tree still has to be frozen
  // before it is handed to initial sync.
  if (oldRoot != newRoot || !newRoot->isPublished()) {
    subscriptions_.publishAndAddPaths(newRoot);
  }

  lastState->emplace(*currentState);
  return std::make_tuple(oldRoot, newRoot, metadataServer);
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    serveSubscriptionsTick() -> folly::coro::Task<void> {
  return serveSubscriptionsLoop(serveTickInterval());
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    serveSubscriptionsLoop(std::chrono::milliseconds tick)
        -> folly::coro::Task<void> {
  // Per bucket: buckets serve different updates, so a shared map would let
  // whichever bucket ran first on a tick suppress the others' latency export.
  std::vector<std::map<std::string, uint64_t>> lastServedPublisherRootUpdates(
      subscriptions_.numBuckets());
  uint64_t tickCount = 0;

  while (true) {
    if (auto runningLocked = running_.rlock(); !*runningLocked) {
      break;
    }

    ++tickCount;
    const auto numBuckets = subscriptions_.numBuckets();
    std::vector<size_t> due;
    for (size_t bucket = 0; bucket < numBuckets; ++bucket) {
      if (!subscriptions_.hasAny(bucket) &&
          !subscriptions_.hasPending(bucket)) {
        releaseBucketBaseline(bucket);
        continue;
      }
      // Cadence alone decides. A pending subscription must NOT force the
      // bucket due: a client that re-subscribes in a loop would otherwise
      // drag every established subscriber in the bucket into an off-cadence
      // full serve. The newcomer waits for its bucket's tick, which is the
      // interval it asked for and matches the pre-bucketing behaviour.
      if ((tickCount % (bucket + 1)) == 0) {
        due.push_back(bucket);
      }
    }

    if (due.empty()) {
      // Keeps GET advancing with no subscribers, at the default cadence.
      if (tickCount % numBuckets == 0) {
        refreshPublishedStateForReads();
      }
      co_await folly::coro::sleep(tick);
      continue;
    }

    for (auto bucket : due) {
      // Timed per bucket: a shared tick-start would bill each bucket for
      // every bucket served before it on the same tick.
      auto start = std::chrono::steady_clock::now();
      auto [oldRoot, newRoot, metadataServer] = publishCurrentState(bucket);
      subscriptions_.serveSubscriptions(
          oldRoot, newRoot, metadataServer, bucket);
      exportServeMetrics(
          start,
          metadataServer,
          lastServedPublisherRootUpdates[bucket],
          tick * (bucket + 1),
          bucket);
    }
    // Default cadence only: these are process levels, and an idle storage must
    // not pay a /proc read per tick (NSDB runs one loop per shard).
    if (tickCount % numBuckets == 0) {
      exportTickMetrics();
    }
    refreshPublishedStateForReads();

    co_await folly::coro::sleep(tick);
  }
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    currentStateExpensive() const -> RootT {
  return currentState_.rlock()->root()->toThrift();
}

template <typename Storage, typename SubscribeManager>
auto NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::
    publishedStateEncoded(OperProtocol protocol) -> OperState {
  auto lastState = Storage(*publishedStateForReads().rlock());
  std::vector<std::string> rootPath;
  return *lastState.get_encoded(rootPath.begin(), rootPath.end(), protocol);
}

template <typename Storage, typename SubscribeManager>
typename Storage::ConcretePath
NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::convertPath(
    ConcretePath&& path) const {
  return params_.convertSubsToIDPaths_
      ? PathConverter<RootT>::pathToIdTokens(std::move(path))
      : path;
}

template <typename Storage, typename SubscribeManager>
typename Storage::ExtPath
NaivePeriodicSubscribableStorage<Storage, SubscribeManager>::convertPath(
    const ExtPath& path) const {
  return params_.convertSubsToIDPaths_
      ? PathConverter<RootT>::extPathToIdTokens(path)
      : path;
}

template <typename Root, bool EnableHybridStorage = false>
using NaivePeriodicSubscribableCowStorage = NaivePeriodicSubscribableStorage<
    CowStorage<
        Root,
        thrift_cow::ThriftStructNode<
            Root,
            thrift_cow::ThriftStructResolver<Root, EnableHybridStorage>,
            EnableHybridStorage>>,
    CowSubscriptionManager<thrift_cow::ThriftStructNode<
        Root,
        thrift_cow::ThriftStructResolver<Root, EnableHybridStorage>,
        EnableHybridStorage>>>;
} // namespace facebook::fboss::fsdb
