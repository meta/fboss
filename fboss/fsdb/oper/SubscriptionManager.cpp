// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/oper/SubscriptionManager.h"
#include "fboss/fsdb/oper/SubscriptionMetadataServer.h"

#include <algorithm>

namespace facebook::fboss::fsdb {

void SubscriptionManagerBase::registerExtendedSubscription(
    std::shared_ptr<ExtendedSubscription> subscription) {
  if (subscription->type() == PubSubType::PATCH && !useIdPaths_) {
    throw std::runtime_error(
        "Cannot support patch type subscriptions without id paths");
  }
  auto index = bucketFor(subscription->serveIntervalMs());
  pendingExtendedSubscriptions_[index].withWLock([&](auto& pending) {
    pending.push_back(std::move(subscription));
    pendingBucketSubscriberCounts_[index].fetch_add(
        1, std::memory_order_relaxed);
  });
}

void SubscriptionManagerBase::registerSubscription(
    std::unique_ptr<Subscription> subscription) {
  if (subscription->type() == PubSubType::PATCH && !useIdPaths_) {
    throw std::runtime_error(
        "Cannot support patch type subscriptions without id paths");
  }
  auto index = bucketFor(subscription->serveIntervalMs());
  pendingSubscriptions_[index].withWLock([&](auto& pending) {
    pending.push_back(std::move(subscription));
    pendingBucketSubscriberCounts_[index].fetch_add(
        1, std::memory_order_relaxed);
  });
}

void SubscriptionManagerBase::pruneCancelledSubscriptions() {
  for (auto& store : stores_) {
    store.wlock()->pruneCancelledSubscriptions();
  }
}

std::optional<FsdbErrorCode>
SubscriptionManagerBase::addPatchSubscriptionPathsImpl(
    const SubscriptionIdentifier& id,
    const ExtSubPathMap& newPaths,
    const std::optional<std::string>& publisherRoot,
    std::optional<StreamRevision> streamRevision) {
  if (!useIdPaths_) {
    throw std::runtime_error(
        "Cannot support patch type subscriptions without id paths");
  }
  // A subscription lives in exactly one bucket, either registered in its store
  // or pending registration. Locking the store before pending matches
  // registerPendingSubscriptions, so the subscription cannot move between the
  // two mid-search.
  for (size_t b = 0; b < stores_.size(); ++b) {
    auto store = stores_[b].wlock();
    auto result = store->addPatchSubscriptionPaths(
        id, newPaths, publisherRoot, streamRevision);
    if (result != FsdbErrorCode::ID_NOT_FOUND) {
      return result;
    }
    auto pending = pendingExtendedSubscriptions_[b].wlock();
    // Last match mirrors registration, where a later subscription with the
    // same identifier owns the lookup slot.
    auto it = std::find_if(
        pending->rbegin(), pending->rend(), [&id](const auto& subscription) {
          return subscription->subscriptionId() == id;
        });
    if (it != pending->rend()) {
      // No added-path work is queued: the initial sync after registration
      // resolves every path of the subscription.
      auto addedKeys = SubscriptionStore::validateAndAddPatchSubscriptionPaths(
          **it, newPaths, publisherRoot, streamRevision);
      if (addedKeys.hasError()) {
        return addedKeys.error();
      }
      return std::nullopt;
    }
  }
  return FsdbErrorCode::ID_NOT_FOUND;
}

std::optional<FsdbErrorCode> SubscriptionManagerBase::addPatchSubscriptionPaths(
    const SubscriptionIdentifier& id,
    std::map<SubscriptionKey, RawOperPath> newPaths,
    const std::optional<std::string>& publisherRoot,
    std::optional<StreamRevision> streamRevision) {
  // Convert raw paths to extended (raw-token) paths, mirroring
  // ExtendedPatchSubscription::create.
  ExtSubPathMap extPaths;
  for (auto& [key, path] : newPaths) {
    std::vector<OperPathElem> extendedPath;
    extendedPath.reserve(path.path()->size());
    for (auto& tok : *path.path()) {
      extendedPath.emplace_back().set_raw(std::move(tok));
    }
    extPaths[key].path() = std::move(extendedPath);
  }
  return addPatchSubscriptionPathsImpl(
      id, extPaths, publisherRoot, streamRevision);
}

std::optional<FsdbErrorCode> SubscriptionManagerBase::addPatchSubscriptionPaths(
    const SubscriptionIdentifier& id,
    const ExtSubPathMap& newPaths,
    const std::optional<std::string>& publisherRoot,
    std::optional<StreamRevision> streamRevision) {
  return addPatchSubscriptionPathsImpl(
      id, newPaths, publisherRoot, streamRevision);
}

void SubscriptionManagerBase::closeNoPublisherActiveSubscriptions(
    const SubscriptionMetadataServer& metadataServer,
    FsdbErrorCode disconnectReason) {
  for (auto& store : stores_) {
    store.wlock()->closeNoPublisherActiveSubscriptions(
        metadataServer, disconnectReason);
  }
}

std::vector<OperSubscriberInfo> SubscriptionManagerBase::getSubscriptions()
    const {
  std::vector<OperSubscriberInfo> toRet;
  for (const auto& storeSync : stores_) {
    auto store = storeSync.rlock();
    toRet.reserve(
        toRet.size() + store->subscriptions().size() +
        store->extendedSubscriptions().size());
    for (auto& [id, subscription] : store->subscriptions()) {
      OperSubscriberInfo info;
      info.subscriberId() = subscription->subscriberId();
      info.type() = subscription->type();
      if (auto interval = subscription->serveIntervalMs()) {
        // Rounded up so a sub-second grant never reports as 0.
        info.serveIntervalSec() =
            static_cast<int32_t>((*interval + 999) / 1000);
      }
      OperPath p;
      p.raw() = subscription->path();
      info.path() = std::move(p);
      info.subscriptionUid() = subscription->subscriptionUid();
      info.subscriptionQueueWatermark() = subscription->getQueueWatermark();
      info.subscriptionChunksCoalesced() = subscription->getChunksCoalesced();
      info.enqueuedDataSize() = subscription->getEnqueuedDataSize();
      info.servedDataSize() = subscription->getServedDataSize();
      info.initialSyncCompletedAt() = subscription->getInitialSyncCompletedAt();
      info.lastUpdateEnqueuedAt() = subscription->getLastUpdateEnqueuedAt();
      info.lastHeartbeatSentAt() = subscription->getLastHeartbeatSentAt();
      info.lastEnqueuedUpdatePublishedAt() =
          subscription->getLastEnqueuedUpdatePublishedAt();
      auto streamInfo = subscription->getSharedStreamInfo();
      if (streamInfo) {
        info.lastUpdateWrittenAt() =
            streamInfo->lastUpdateWrittenAt.load(std::memory_order_relaxed);
        info.numUpdatesServed() =
            streamInfo->numUpdatesServed.load(std::memory_order_relaxed);
      }
      toRet.push_back(std::move(info));
    }
    for (auto& [id, subscription] : store->extendedSubscriptions()) {
      OperSubscriberInfo info;
      info.subscriberId() = subscription->subscriberId();
      info.type() = subscription->type();
      if (auto interval = subscription->serveIntervalMs()) {
        // Rounded up so a sub-second grant never reports as 0.
        info.serveIntervalSec() =
            static_cast<int32_t>((*interval + 999) / 1000);
      }

      auto subscribedPaths = subscription->paths();
      std::vector<ExtendedOperPath> paths(subscribedPaths.size());
      std::transform(
          subscribedPaths.begin(),
          subscribedPaths.end(),
          paths.begin(),
          [](const auto& pair) { return pair.second; });
      info.extendedPaths() = std::move(paths);

      info.subscriptionUid() = subscription->subscriptionUid();
      info.subscriptionQueueWatermark() = subscription->getQueueWatermark();
      info.subscriptionChunksCoalesced() = subscription->getChunksCoalesced();
      info.enqueuedDataSize() = subscription->getEnqueuedDataSize();
      info.servedDataSize() = subscription->getServedDataSize();
      info.initialSyncCompletedAt() = subscription->getInitialSyncCompletedAt();
      info.lastUpdateEnqueuedAt() = subscription->getLastUpdateEnqueuedAt();
      info.lastHeartbeatSentAt() = subscription->getLastHeartbeatSentAt();
      info.lastEnqueuedUpdatePublishedAt() =
          subscription->getLastEnqueuedUpdatePublishedAt();
      auto extStreamInfo = subscription->getSharedStreamInfo();
      if (extStreamInfo) {
        info.lastUpdateWrittenAt() =
            extStreamInfo->lastUpdateWrittenAt.load(std::memory_order_relaxed);
        info.numUpdatesServed() =
            extStreamInfo->numUpdatesServed.load(std::memory_order_relaxed);
      }
      toRet.push_back(std::move(info));
    }
  }
  return toRet;
}

std::map<FsdbClient, SubscriberStats>
SubscriptionManagerBase::getSubscriberStats() const {
  std::map<FsdbClient, SubscriberStats> stats;
  for (const auto& storeSync : stores_) {
    auto store = storeSync.rlock();
    for (auto& [client, current] : store->getSubscriberStats()) {
      auto& merged = stats[client];
      merged.numSubscriptions += current.numSubscriptions;
      merged.numExtendedSubscriptions += current.numExtendedSubscriptions;
      merged.subscriptionServeQueueWatermark = std::max(
          merged.subscriptionServeQueueWatermark,
          current.subscriptionServeQueueWatermark);
      merged.subscriptionChunksCoalesced += current.subscriptionChunksCoalesced;
      merged.enqueuedDataSize += current.enqueuedDataSize;
      merged.servedDataSize += current.servedDataSize;
      merged.numSlowSubscriptionDisconnects +=
          current.numSlowSubscriptionDisconnects;
    }
  }
  return stats;
}

void SubscriptionManagerBase::registerPendingSubscriptions(
    SubscriptionStore& store,
    size_t bucket) {
  auto index = bucket;
  auto pendingSubscriptions = pendingSubscriptions_[index].wlock();
  auto pendingExtendedSubscriptions =
      pendingExtendedSubscriptions_[index].wlock();
  store.registerPendingSubscriptions(
      std::move(*pendingSubscriptions),
      std::move(*pendingExtendedSubscriptions));
  *pendingSubscriptions = PendingSubscriptions();
  *pendingExtendedSubscriptions = PendingExtendedSubscriptions();
  pendingBucketSubscriberCounts_[index].store(0, std::memory_order_relaxed);
}

} // namespace facebook::fboss::fsdb
