// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/oper/SubscriptionStore.h"
#include "fboss/fsdb/common/Utils.h"
#include "fboss/fsdb/oper/SubscriptionPathStore.h"
#include "fboss/thrift_cow/visitors/ExtendedPathMatcher.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/String.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss::fsdb {

void updateSubscriberStats(
    std::map<FsdbClient, SubscriberStats>& stats,
    const BaseSubscription& subscription,
    const std::function<void(SubscriberStats&, const BaseSubscription&)>&
        updater) {
  FsdbClient key = *subscriberId2ClientId(subscription.subscriberId()).client();
  auto it = stats.find(key);
  if (it == stats.end()) {
    stats.emplace(key, SubscriberStats());
    it = stats.find(key);
  }
  updater(it->second, subscription);
}

SubscriptionStore::~SubscriptionStore() {
  initialSyncNeeded_.clear(&pathStoreStats_);
  initialSyncNeededExtended_.clear();
  extendedSubsWithAddedPaths_.clear();

  lookup_.clear(&pathStoreStats_);
  // fully resolved extended subs have a ref to the extended sub
  // make sure to destroy those before destroy the extended sub
  subscriptions_.clear();
  extendedSubsByIdentifier_.clear();
  extendedSubscriptions_.clear();
}

std::string getPublisherDroppedMessage(
    FsdbErrorCode disconnectReason,
    std::string pubRoot) {
  return (disconnectReason == FsdbErrorCode::PUBLISHER_GR_DISCONNECT)
      ? fmt::format("publisher dropped for GR for root: {}", std::move(pubRoot))
      : fmt::format("All publishers dropped for root: {}", std::move(pubRoot));
}

void SubscriptionStore::pruneSimpleSubscriptions() {
  std::vector<std::string> toDelete;
  for (auto& [name, subscription] : subscriptions_) {
    if (subscription->shouldPrune()) {
      toDelete.push_back(name);
    }
  }

  for (const auto& name : toDelete) {
    XLOG(DBG1) << "Removing cancelled subscription '" << name << "'...";
    unregisterSubscription(name);
  }
}

std::vector<std::string>
SubscriptionStore::markExtendedSubscriptionsThatNeedPruning() {
  std::vector<std::string> toDelete;
  for (auto& [name, subscription] : extendedSubscriptions_) {
    if (subscription->markShouldPruneIfInactive()) {
      toDelete.push_back(name);
    }
  }
  return toDelete;
}

void SubscriptionStore::pruneExtendedSubscriptions(
    const std::vector<std::string>& toDelete) {
  for (const auto& name : toDelete) {
    XLOG(DBG1) << "Removing cancelled extended subscription '" << name
               << "'...";
    unregisterExtendedSubscription(name);
  }
}

void SubscriptionStore::pruneCancelledSubscriptions() {
  // subscriptions also contains FullyResolved*Subscritions which need to be
  // cleaned up BEFORE pruning ExtendedSubscriptions
  auto extendedSubsToPrune = markExtendedSubscriptionsThatNeedPruning();
  pruneSimpleSubscriptions();
  pruneExtendedSubscriptions(extendedSubsToPrune);
}

void SubscriptionStore::registerExtendedSubscription(
    std::shared_ptr<ExtendedSubscription> subscription) {
  auto uuid = boost::uuids::to_string(boost::uuids::random_generator()());
  registerExtendedSubscription(std::move(uuid), std::move(subscription));
}

void SubscriptionStore::registerSubscription(
    std::unique_ptr<Subscription> subscription) {
  // This flavor of registerSubscription automatically chooses a name.
  auto uuid = boost::uuids::to_string(boost::uuids::random_generator()());
  const auto& path = subscription->path();
  auto pathStr = folly::join("/", path.begin(), path.end());
  auto candidateName = fmt::format("{}-{}", pathStr, std::move(uuid));
  registerSubscription(std::move(candidateName), std::move(subscription));
}

void SubscriptionStore::registerPendingSubscriptions(
    std::vector<std::unique_ptr<Subscription>>&& subscriptions,
    std::vector<std::shared_ptr<ExtendedSubscription>>&&
        extendedSubscriptions) {
  for (auto& subscription : subscriptions) {
    registerSubscription(std::move(subscription));
  }
  for (auto& extendedSubscription : extendedSubscriptions) {
    registerExtendedSubscription(std::move(extendedSubscription));
  }
}

void updateSubscriberStatsDisconnectReason(
    SubscriberStats& stats,
    const BaseSubscription& subscription) {
  auto pruneReason = subscription.pruneReason();
  if (pruneReason) {
    if (pruneReason.value() == FsdbErrorCode::SUBSCRIPTION_SERVE_QUEUE_FULL) {
      stats.numSlowSubscriptionDisconnects++;
    }
  }
}

void SubscriptionStore::unregisterSubscription(const std::string& name) {
  XLOG(DBG1) << "Unregistering subscription " << name;
  if (auto it = subscriptions_.find(name); it != subscriptions_.end()) {
    Subscription* rawPtr = it->second.get();
    if (rawPtr->pruneReason()) {
      updateSubscriberStats(
          subscriberStats_, *rawPtr, updateSubscriberStatsDisconnectReason);
    }
    adjustSubscriberCount(-1);
    // TODO: trim empty path stores
    initialSyncNeeded_.remove(rawPtr);
    lookup_.remove(rawPtr);
    subscriptions_.erase(it);
  }
}

void SubscriptionStore::unregisterExtendedSubscription(
    const std::string& name) {
  XLOG(DBG1) << "Unregistering extended subscription " << name;
  if (auto it = extendedSubscriptions_.find(name);
      it != extendedSubscriptions_.end()) {
    if (it->second.get()->pruneReason()) {
      updateSubscriberStats(
          subscriberStats_,
          *it->second.get(),
          updateSubscriberStatsDisconnectReason);
    }
    // Only erase the index entry if it still points at this subscription:
    // subscriptions can share an identifier (default uid 0) and a later
    // registration may own the slot.
    if (auto idxIt =
            extendedSubsByIdentifier_.find(it->second->subscriptionId());
        idxIt != extendedSubsByIdentifier_.end() &&
        idxIt->second.lock() == it->second) {
      extendedSubsByIdentifier_.erase(idxIt);
    }
    adjustSubscriberCount(-1);
    initialSyncNeededExtended_.erase(it->second);
    // Drop pending added-path work for this subscription: its entry holds a
    // strong shared_ptr, so leaving it would pin the unregistered subscription
    // alive and let resolveAddedPatchPaths later resolve off a dead
    // subscription.
    std::erase_if(
        extendedSubsWithAddedPaths_,
        [&](const ExtendedSubscriptionAddedPaths& added) {
          return added.subscription == it->second;
        });
    extendedSubscriptions_.erase(it);
  }
}

std::shared_ptr<ExtendedSubscription>
SubscriptionStore::findExtendedSubscription(const SubscriptionIdentifier& id) {
  auto it = extendedSubsByIdentifier_.find(id);
  if (it == extendedSubsByIdentifier_.end()) {
    return nullptr;
  }
  auto subscription = it->second.lock();
  if (!subscription) {
    extendedSubsByIdentifier_.erase(it);
  }
  return subscription;
}

folly::Expected<std::vector<SubscriptionKey>, FsdbErrorCode>
SubscriptionStore::validateAndAddPatchSubscriptionPaths(
    ExtendedSubscription& subscription,
    ExtSubPathMap newPaths,
    const std::optional<std::string>& publisherRoot,
    std::optional<StreamRevision> streamRevision) {
  if (subscription.type() != PubSubType::PATCH) {
    XLOG(DBG1) << "addPatchSubscriptionPaths: subscription for subscriber "
               << subscription.subscriberId() << " is not a patch subscription";
    return folly::makeUnexpected(FsdbErrorCode::INVALID_REQUEST);
  }
  if (newPaths.empty()) {
    XLOG(DBG1) << "addPatchSubscriptionPaths: no paths provided for "
               << "subscriber " << subscription.subscriberId();
    return folly::makeUnexpected(FsdbErrorCode::INVALID_REQUEST);
  }
  // New paths must resolve to the subscription's single publisher root.
  if (subscription.publisherTreeRoot() != publisherRoot.value_or("")) {
    XLOG(DBG1) << "addPatchSubscriptionPaths: publisher root mismatch for "
               << "subscriber " << subscription.subscriberId();
    return folly::makeUnexpected(FsdbErrorCode::INVALID_REQUEST);
  }
  // Reject colliding SubscriptionKeys: buffered_/patchGroups() are keyed by
  // them.
  const auto& existingPaths = subscription.paths();
  for (const auto& [key, _] : newPaths) {
    if (existingPaths.find(key) != existingPaths.end()) {
      XLOG(DBG1) << "addPatchSubscriptionPaths: SubscriptionKey " << key
                 << " already exists for subscriber "
                 << subscription.subscriberId();
      return folly::makeUnexpected(FsdbErrorCode::ID_ALREADY_EXISTS);
    }
  }
  const auto& patchSubscription =
      static_cast<const ExtendedPatchSubscription&>(subscription);
  if (FLAGS_dynamicWildcardPatchResolution &&
      subscription.getInitialSyncCompletedAt() != 0 &&
      !patchSubscription.hasWildcardPath() &&
      std::any_of(newPaths.begin(), newPaths.end(), [](const auto& path) {
        return thrift_cow::hasWildcard(*path.second.path());
      })) {
    XLOG(DBG1) << "addPatchSubscriptionPaths: cannot add the first wildcard "
                  "path to an initialized eager subscription for subscriber "
               << subscription.subscriberId();
    return folly::makeUnexpected(FsdbErrorCode::INVALID_REQUEST);
  }
  auto addedKeys = subscription.addPaths(std::move(newPaths));
  // Stash the client-supplied stream revision so it is echoed back in
  // OperMetadata.streamRevision on patches served after this add.
  if (streamRevision.has_value()) {
    subscription.setStreamRevision(*streamRevision);
  }
  return addedKeys;
}

std::optional<FsdbErrorCode> SubscriptionStore::addPatchSubscriptionPaths(
    const SubscriptionIdentifier& id,
    ExtSubPathMap newPaths,
    const std::optional<std::string>& publisherRoot,
    std::optional<StreamRevision> streamRevision) {
  auto subscription = findExtendedSubscription(id);
  if (!subscription) {
    XLOG(DBG1) << "addPatchSubscriptionPaths: no subscription found for "
               << "subscriber " << id.subscriberId() << " uid " << id.uid();
    return FsdbErrorCode::ID_NOT_FOUND;
  }
  auto addedKeys = validateAndAddPatchSubscriptionPaths(
      *subscription, std::move(newPaths), publisherRoot, streamRevision);
  if (addedKeys.hasError()) {
    return addedKeys.error();
  }
  // If the subscription is still awaiting its own extended initial sync,
  // doInitialSyncExtended will resolve ALL its paths_ (including these) next
  // cycle. Only defer resolution here once that initial sync is done, else
  // resolveAddedPatchPaths would resolve the same keys twice (duplicate
  // chunks).
  if (initialSyncNeededExtended_.find(subscription) ==
      initialSyncNeededExtended_.end()) {
    extendedSubsWithAddedPaths_.push_back(
        ExtendedSubscriptionAddedPaths{
            std::move(subscription), std::move(addedKeys).value()});
  }
  return std::nullopt;
}

void SubscriptionStore::registerSubscription(
    std::string name,
    std::unique_ptr<Subscription> subscription) {
  XLOG(DBG1) << "Registering subscription " << name;
  auto rawPtr = subscription.get();
  auto ret = subscriptions_.emplace(name, std::move(subscription));
  if (!ret.second) {
    throw Utils::createFsdbException(
        FsdbErrorCode::ID_ALREADY_EXISTS, name + " already exixts");
  }
  adjustSubscriberCount(1);
  initialSyncNeeded_.add(rawPtr, &pathStoreStats_);
}

void SubscriptionStore::registerExtendedSubscription(
    std::string name,
    std::shared_ptr<ExtendedSubscription> subscription) {
  XLOG(DBG1) << "Registering extended subscription " << name;
  DCHECK(subscription);
  auto ret = extendedSubscriptions_.emplace(name, subscription);
  if (!ret.second) {
    throw Utils::createFsdbException(
        FsdbErrorCode::ID_ALREADY_EXISTS, name + " already exixts");
  }
  extendedSubsByIdentifier_[subscription->subscriptionId()] = subscription;
  adjustSubscriberCount(1);
  initialSyncNeededExtended_.insert(std::move(subscription));
}

void SubscriptionStore::closeNoPublisherActiveSubscriptions(
    const SubscriptionMetadataServer& metadataServer,
    FsdbErrorCode disconnectReason) {
  XLOG(DBG2) << " closeSubscriptions: "
             << apache::thrift::util::enumNameSafe(disconnectReason);
  for (auto& [name, subscription] : subscriptions_) {
    if (!metadataServer.getMetadata(subscription->publisherTreeRoot())) {
      subscription->allPublishersGone(
          disconnectReason,
          getPublisherDroppedMessage(
              disconnectReason, subscription->publisherTreeRoot()));
    }
  }
  for (auto& [name, subscription] : extendedSubscriptions_) {
    if (!metadataServer.getMetadata(subscription->publisherTreeRoot())) {
      subscription->allPublishersGone(
          disconnectReason,
          getPublisherDroppedMessage(
              disconnectReason, subscription->publisherTreeRoot()));
    }
  }
}

void SubscriptionStore::flush(
    const SubscriptionMetadataServer& metadataServer) {
  // flushes all subscriptions that need to be flushed.
  //
  // NOTE: it is important we flush regular subscriptions BEFORE
  // extended subscriptions, as some of the regular subscriptions
  // will "flush" in to an extended subscription and be aggregated
  // w/ other changes before flushing out to subscriber.
  //
  // TODO: hint which subscriptions need to be flushed to avoid full
  // loop
  for (auto& [_, subscription] : subscriptions_) {
    auto ret = subscription->flush(metadataServer);
    if (ret.has_value()) {
      subscription->requestPruneWithReason(ret.value());
    }
  }
  for (auto& [_, subscription] : extendedSubscriptions_) {
    auto ret = subscription->flush(metadataServer);
    if (ret.has_value()) {
      subscription->requestPruneWithReason(ret.value());
    }
  }
}

void SubscriptionStore::processAddedPath(
    std::vector<std::string>::const_iterator begin,
    std::vector<std::string>::const_iterator end) {
  lookup_.processAddedPath(*this, begin, begin, end);
}

std::map<FsdbClient, SubscriberStats> SubscriptionStore::getSubscriberStats()
    const {
  std::map<FsdbClient, SubscriberStats> toRet = folly::copy(subscriberStats_);
  auto updater = [](SubscriberStats& stats,
                    const BaseSubscription& subscription) {
    stats.numSubscriptions++;
    stats.subscriptionServeQueueWatermark = std::max(
        stats.subscriptionServeQueueWatermark,
        subscription.getQueueWatermark());
    stats.subscriptionChunksCoalesced = subscription.getChunksCoalesced();
    stats.enqueuedDataSize = subscription.getEnqueuedDataSize();
    stats.servedDataSize = subscription.getServedDataSize();
  };
  for (auto& [id, subscription] : subscriptions_) {
    updateSubscriberStats(toRet, *subscription, updater);
  }
  for (auto& [id, subscription] : extendedSubscriptions_) {
    updateSubscriberStats(toRet, *subscription, updater);
  }
  return toRet;
}

} // namespace facebook::fboss::fsdb
