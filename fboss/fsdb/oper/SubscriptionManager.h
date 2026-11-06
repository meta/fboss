// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#pragma once

#include "fboss/fsdb/if/gen-cpp2/fsdb_common_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"
#include "fboss/fsdb/if/gen-cpp2/fsdb_types.h"
#include "fboss/fsdb/oper/Subscription.h"
#include "fboss/fsdb/oper/SubscriptionStore.h"

#include <folly/Demangle.h>
#include <folly/logging/xlog.h>
#include <atomic>
#include <string>
#include <typeinfo>
#include <vector>

namespace facebook::fboss::fsdb {

class SubscriptionMetadataServer;

class SubscriptionManagerBase {
 public:
  // numBuckets is fixed for the lifetime of the manager: sizing the bucket
  // vectors once here means the serve loop never races bucket creation or
  // destruction against a subscribe on a Thrift thread.
  explicit SubscriptionManagerBase(
      OperProtocol patchOperProtocol = OperProtocol::COMPACT,
      bool requireResponseOnInitialSync = false,
      size_t numBuckets = 1,
      uint32_t tickMs = 0)
      : stores_(numBuckets),
        patchOperProtocol_(patchOperProtocol),
        requireResponseOnInitialSync_(requireResponseOnInitialSync),
        tickMs_(tickMs),
        pendingBucketSubscriberCounts_(numBuckets),
        pendingSubscriptions_(numBuckets),
        pendingExtendedSubscriptions_(numBuckets) {
    CHECK_GT(numBuckets, 0u);
  }

  size_t numBuckets() const {
    return stores_.size();
  }

  // Slowest bucket; subscriptions without an explicit interval land here.
  size_t defaultBucket() const {
    return stores_.size() - 1;
  }

  void pruneCancelledSubscriptions();

  void closeNoPublisherActiveSubscriptions(
      const SubscriptionMetadataServer& metadataServer,
      FsdbErrorCode disconnectReason);

  void registerExtendedSubscription(
      std::shared_ptr<ExtendedSubscription> subscription);

  void registerSubscription(std::unique_ptr<Subscription> subscription);

  // Append newPaths to an existing patch subscription (by id); publisherRoot
  // must match the subscription's root. Returns an error code on failure.
  std::optional<FsdbErrorCode> addPatchSubscriptionPaths(
      const SubscriptionIdentifier& id,
      std::map<SubscriptionKey, RawOperPath> newPaths,
      const std::optional<std::string>& publisherRoot,
      std::optional<StreamRevision> streamRevision = std::nullopt);

  std::optional<FsdbErrorCode> addPatchSubscriptionPaths(
      const SubscriptionIdentifier& id,
      const ExtSubPathMap& newPaths,
      const std::optional<std::string>& publisherRoot,
      std::optional<StreamRevision> streamRevision = std::nullopt);

  size_t numSubscriptions() const {
    size_t total{0};
    for (const auto& store : stores_) {
      total += store.rlock()->subscriptions().size();
    }
    return total;
  }

  // Do not use, except for UTs that cross check numPathStores()
  size_t numPathStoresRecursive_Expensive() const {
    size_t total{0};
    for (const auto& store : stores_) {
      total += store.rlock()->numPathStoresRecursive_Expensive();
    }
    return total;
  }

  size_t numPathStores() const {
    size_t total{0};
    for (const auto& store : stores_) {
      total += store.rlock()->numPathStores();
    }
    return total;
  }

  // Excludes the last bucket, which is the default interval.
  size_t numIntervalSubscriptions() const {
    size_t total{0};
    for (size_t b = 0; b + 1 < stores_.size(); ++b) {
      total += stores_[b].rlock()->subscriptions().size();
    }
    return total;
  }

  bool hasAny(size_t bucket) const {
    return storeForBucket(bucket).rlock()->hasAny();
  }

  bool hasPending(size_t bucket) const {
    return pendingBucketSubscriberCounts_[bucket].load(
               std::memory_order_relaxed) > 0;
  }

  uint64_t numPathStoreAllocs() const {
    uint64_t total{0};
    for (const auto& store : stores_) {
      total += store.rlock()->numPathStoreAllocs();
    }
    return total;
  }

  uint64_t numPathStoreFrees() const {
    uint64_t total{0};
    for (const auto& store : stores_) {
      total += store.rlock()->numPathStoreFrees();
    }
    return total;
  }

  std::vector<OperSubscriberInfo> getSubscriptions() const;

  std::map<FsdbClient, SubscriberStats> getSubscriberStats() const;

  void useIdPaths(bool idPaths) {
    useIdPaths_ = idPaths;
  }

  // Serve response (even if empty) on initial sync, even when
  // no data is published for subscribed path
  void setRequireResponseOnInitialSync(bool requireResponse) {
    requireResponseOnInitialSync_ = requireResponse;
  }

  OperProtocol patchOperProtocol() {
    return patchOperProtocol_;
  }

 private:
  // Shared impl behind the raw/extended addPatchSubscriptionPaths overloads:
  // guards on useIdPaths_ and applies to the subscription whether registered or
  // pending registration.
  std::optional<FsdbErrorCode> addPatchSubscriptionPathsImpl(
      const SubscriptionIdentifier& id,
      const ExtSubPathMap& newPaths,
      const std::optional<std::string>& publisherRoot,
      std::optional<StreamRevision> streamRevision);

  void registerSubscription(
      std::string name,
      std::unique_ptr<Subscription> subscription);

  void registerExtendedSubscription(
      std::string name,
      std::shared_ptr<ExtendedSubscription> subscription);

 protected:
  void registerPendingSubscriptions(SubscriptionStore& store, size_t bucket);

  folly::Synchronized<SubscriptionStore>& storeForBucket(size_t bucket) {
    return stores_[bucket];
  }

  const folly::Synchronized<SubscriptionStore>& storeForBucket(
      size_t bucket) const {
    return stores_[bucket];
  }

  // Bucket a subscription's granted interval falls into; subscriptions without
  // an interval are served at the default (slowest) cadence.
  size_t bucketFor(std::optional<uint32_t> serveIntervalMs) const {
    if (!serveIntervalMs.has_value() || tickMs_ == 0) {
      return defaultBucket();
    }
    return std::min(
        serveBucketIndex(*serveIntervalMs, tickMs_), defaultBucket());
  }

  std::vector<folly::Synchronized<SubscriptionStore>> stores_;

  bool useIdPaths_{false};

  const OperProtocol patchOperProtocol_{OperProtocol::COMPACT};
  bool requireResponseOnInitialSync_{false};
  uint32_t tickMs_{0};

  std::deque<std::atomic<int64_t>> pendingBucketSubscriberCounts_;

 private:
  using PendingSubscriptions = std::vector<std::unique_ptr<Subscription>>;
  using PendingExtendedSubscriptions =
      std::vector<std::shared_ptr<ExtendedSubscription>>;
  std::vector<folly::Synchronized<PendingSubscriptions>> pendingSubscriptions_;
  std::vector<folly::Synchronized<PendingExtendedSubscriptions>>
      pendingExtendedSubscriptions_;
};

template <typename _Root, typename Impl>
class SubscriptionManager : public SubscriptionManagerBase {
 public:
  using Root = _Root;

  explicit SubscriptionManager(
      OperProtocol patchOperProtocol = OperProtocol::COMPACT,
      bool requireResponseOnInitialSync = false,
      size_t numBuckets = 1,
      uint32_t tickMs = 0)
      : SubscriptionManagerBase(
            patchOperProtocol,
            requireResponseOnInitialSync,
            numBuckets,
            tickMs),
        consecutiveServeFailures_(numBuckets),
        lastLoggedExceptionType_(numBuckets) {}

  // The freeze walk consumes the tree's "unpublished" marking, so it can only
  // run once per publish and must register added paths with every bucket. Lock
  // buckets in index order so concurrent callers cannot invert.
  void publishAndAddPaths(std::shared_ptr<Root>& root) {
    std::vector<folly::Synchronized<SubscriptionStore>::LockedPtr> locked;
    std::vector<SubscriptionStore*> raw;
    locked.reserve(stores_.size());
    raw.reserve(stores_.size());
    for (auto& store : stores_) {
      locked.emplace_back(store.wlock());
      raw.emplace_back(&*locked.back());
    }
    static_cast<Impl*>(this)->publishAndAddPaths(raw, root);
  }

  // Bucket is explicit: a defaulted "all buckets" mode would fan one bucket's
  // baseline out to every other bucket's subscribers.
  void serveSubscriptions(
      const std::shared_ptr<Root>& oldRoot,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer,
      size_t bucket) {
    auto impl = static_cast<Impl*>(this);
    auto store = this->storeForBucket(bucket).wlock();

    registerPendingSubscriptions(*store, bucket);

    store->pruneCancelledSubscriptions();

    if (oldRoot != newRoot) {
      try {
        impl->serveSubscriptions(*store, oldRoot, newRoot, metadataServer);
        consecutiveServeFailures_[bucket].store(0, std::memory_order_relaxed);
        lastLoggedExceptionType_[bucket].store(
            nullptr, std::memory_order_relaxed);
      } catch (const std::exception& ex) {
        logServeCycleFailure(ex, store->subscriptions().size(), bucket);
      }
      impl->pruneDeletedPaths(*store, oldRoot, newRoot);
    }
    // Serve new subscriptions after serving existing subscriptions.
    // New subscriptions will get a full object dump on first sync.
    // If we serve them before the loop above, we have to be careful
    // to not serve them again in the loop above. So just move to serve
    // after. Post the initial sync, these new subscriptions will be
    // pruned from initialSyncNeeded list and will get served on
    // changes only
    impl->doInitialSync(*store, newRoot, metadataServer);
    // Flush all subscription queues from serve and initial sync steps
    store->flush(metadataServer);
  }

 private:
  // don't let the subclass direct access to stores to simplify locking
  // policy. Instead we'll handle all the locking of the store here
  using SubscriptionManagerBase::stores_;

  // Log unconditionally on streak start or exception-type change;
  // otherwise rate-limit. Per-call-site XLOG_EVERY_MS is message-agnostic,
  // so without the type-change guard a different exception arriving
  // inside the throttle window would be dropped.
  void logServeCycleFailure(
      const std::exception& ex,
      size_t subscriberCount,
      size_t bucket) {
    const auto consecutive = consecutiveServeFailures_[bucket].fetch_add(
                                 1, std::memory_order_relaxed) +
        1;
    const auto* currentType = &typeid(ex);
    const auto* lastType =
        lastLoggedExceptionType_[bucket].load(std::memory_order_relaxed);
    const bool typeChanged = (currentType != lastType);
    if (consecutive == 1 || typeChanged) {
      lastLoggedExceptionType_[bucket].store(
          currentType, std::memory_order_relaxed);
      XLOG(ERR) << "FSDB serve cycle failed: subs=" << subscriberCount
                << " bucket=" << bucket
                << " ex=" << folly::demangle(currentType->name())
                << " what=" << ex.what()
                << " consecutiveFailures=" << consecutive;
    } else {
      XLOG_EVERY_MS(ERR, 1000)
          << "FSDB serve cycle failed: subs=" << subscriberCount
          << " bucket=" << bucket
          << " ex=" << folly::demangle(currentType->name())
          << " what=" << ex.what() << " consecutiveFailures=" << consecutive;
    }
  }

  // Streak length of consecutive serve-cycle failures, per bucket. Reset on
  // success.
  std::deque<std::atomic<uint64_t>> consecutiveServeFailures_;
  // Type of the most recently logged exception in the current streak.
  // Used to bypass the rate limit on type change. Reset on success.
  std::deque<std::atomic<const std::type_info*>> lastLoggedExceptionType_;
};

} // namespace facebook::fboss::fsdb
