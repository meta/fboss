// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <array>
#include <atomic>

#include "fboss/fsdb/oper/Subscription.h"
#include "fboss/fsdb/oper/SubscriptionPathStore.h"

#include <folly/Expected.h>

namespace facebook::fboss::fsdb {

// SubscriberStats -- per-client summary counters covering
// active as well as disconnected subscriptions, owned by
// SubscriptionStore. Keeping only summary counters here so
// that callers copy these stats while holding lock on store.
struct SubscriberStats {
  uint32_t numSubscriptions{0};
  uint32_t numExtendedSubscriptions{0};
  uint32_t subscriptionServeQueueWatermark{0};
  uint32_t subscriptionChunksCoalesced{0};
  uint64_t enqueuedDataSize{0};
  uint64_t servedDataSize{0};
  uint32_t numSlowSubscriptionDisconnects{0};
};

// Paths newly appended to a live extended subscription, awaiting resolution +
// initial sync on the next serve cycle. Holds only keys; values live in the
// sub.
struct ExtendedSubscriptionAddedPaths {
  std::shared_ptr<ExtendedSubscription> subscription;
  std::vector<SubscriptionKey> newKeys;
};

class SubscriptionStore {
 public:
  SubscriptionStore()
      : initialSyncNeeded_(&pathStoreStats_), lookup_(&pathStoreStats_) {}

  virtual ~SubscriptionStore();

  void pruneCancelledSubscriptions();

  virtual void registerSubscription(std::unique_ptr<Subscription> subscription);

  void registerExtendedSubscription(
      std::shared_ptr<ExtendedSubscription> subscription);

  void registerPendingSubscriptions(
      std::vector<std::unique_ptr<Subscription>>&& subscriptions,
      std::vector<std::shared_ptr<ExtendedSubscription>>&&
          extendedSubscriptions);

  void unregisterSubscription(const std::string& name);

  void unregisterExtendedSubscription(const std::string& name);

  // Locate a live extended subscription by its SubscriptionIdentifier.
  std::shared_ptr<ExtendedSubscription> findExtendedSubscription(
      const SubscriptionIdentifier& id);

  // Append newPaths to an existing patch subscription (by id); resolved +
  // initial-synced next serve cycle. Returns an error code on failure (unknown
  // id, non-patch sub, publisher-root mismatch, or colliding SubscriptionKey).
  std::optional<FsdbErrorCode> addPatchSubscriptionPaths(
      const SubscriptionIdentifier& id,
      ExtSubPathMap newPaths,
      const std::optional<std::string>& publisherRoot,
      std::optional<StreamRevision> streamRevision = std::nullopt);

  // Validates newPaths for appending to a patch subscription and, if valid,
  // appends them and stashes streamRevision. Caller must hold the lock guarding
  // `subscription`. Returns the inserted keys or the rejection code.
  static folly::Expected<std::vector<SubscriptionKey>, FsdbErrorCode>
  validateAndAddPatchSubscriptionPaths(
      ExtendedSubscription& subscription,
      ExtSubPathMap newPaths,
      const std::optional<std::string>& publisherRoot,
      std::optional<StreamRevision> streamRevision);

  void processAddedPath(
      std::vector<std::string>::const_iterator begin,
      std::vector<std::string>::const_iterator end);

  void closeNoPublisherActiveSubscriptions(
      const SubscriptionMetadataServer& metadataServer,
      FsdbErrorCode disconnectReason);

  void flush(const SubscriptionMetadataServer& metadataServer);

  const auto& subscriptions() const {
    return subscriptions_;
  }
  const auto& extendedSubscriptions() const {
    return extendedSubscriptions_;
  }
  const auto& initialSyncNeeded() const {
    return initialSyncNeeded_;
  }
  const auto& initialSyncNeededExtended() const {
    return initialSyncNeededExtended_;
  }
  const auto& extendedSubsWithAddedPaths() const {
    return extendedSubsWithAddedPaths_;
  }
  const auto& lookup() const {
    return lookup_;
  }
  auto& subscriptions() {
    return subscriptions_;
  }
  auto& extendedSubscriptions() {
    return extendedSubscriptions_;
  }
  auto& initialSyncNeeded() {
    return initialSyncNeeded_;
  }
  auto& initialSyncNeededExtended() {
    return initialSyncNeededExtended_;
  }
  auto& extendedSubsWithAddedPaths() {
    return extendedSubsWithAddedPaths_;
  }
  auto& lookup() {
    return lookup_;
  }

  SubscriptionPathStoreTreeStats* getPathStoreStats() {
    return &pathStoreStats_;
  }

  size_t numPathStoresRecursive_Expensive() const {
    return initialSyncNeeded_.numPathStoresRecursive_Expensive() +
        lookup_.numPathStoresRecursive_Expensive();
  }

  auto numPathStores() const {
    return pathStoreStats_.numPathStores;
  }
  auto numPathStoreAllocs() const {
    return pathStoreStats_.numPathStoreAllocs;
  }
  auto numPathStoreFrees() const {
    return pathStoreStats_.numPathStoreFrees;
  }

  std::map<FsdbClient, SubscriberStats> getSubscriberStats() const;

  // Best-effort, lock-free count used by the serve loop's idle short-circuit;
  // relaxed is fine because the loop tolerates a one-tick-stale read.
  bool hasAny() const {
    return subscriberCount_.load(std::memory_order_relaxed) > 0;
  }

 private:
  std::map<FsdbClient, SubscriberStats> subscriberStats_;

  std::vector<std::string> markExtendedSubscriptionsThatNeedPruning();

  void pruneSimpleSubscriptions();

  void pruneExtendedSubscriptions(const std::vector<std::string>& toDelete);

  void registerSubscription(
      std::string name,
      std::unique_ptr<Subscription> subscription);

  void registerExtendedSubscription(
      std::string name,
      std::shared_ptr<ExtendedSubscription> subscription);

  void adjustSubscriberCount(int64_t delta) {
    auto prev = subscriberCount_.fetch_add(delta, std::memory_order_relaxed);
    DCHECK_GE(prev + delta, 0) << "subscriber count underflow";
  }

  // stats for tree<SubscriptionPathStore>
  SubscriptionPathStoreTreeStats pathStoreStats_;

  // owned subscriptions, keyed on name they were registered with
  std::unordered_map<std::string, std::unique_ptr<Subscription>> subscriptions_;
  std::unordered_map<std::string, std::shared_ptr<ExtendedSubscription>>
      extendedSubscriptions_;
  // Identifier -> live extended subscription index, for O(1) lookup by id.
  std::unordered_map<
      SubscriptionIdentifier,
      std::weak_ptr<ExtendedSubscription>,
      SubscriptionIdentifier::Hash>
      extendedSubsByIdentifier_;
  SubscriptionPathStore initialSyncNeeded_;
  std::unordered_set<std::shared_ptr<ExtendedSubscription>>
      initialSyncNeededExtended_;
  // Patch subs with paths appended post-creation, awaiting next-cycle
  // resolution.
  std::vector<ExtendedSubscriptionAddedPaths> extendedSubsWithAddedPaths_;

  // lookup for the subscriptions, keyed on path
  SubscriptionPathStore lookup_;

  std::atomic<int64_t> subscriberCount_{0};
};

} // namespace facebook::fboss::fsdb
