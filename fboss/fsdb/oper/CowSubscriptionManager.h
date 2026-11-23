/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <fboss/fsdb/common/Utils.h>
#include <fboss/fsdb/oper/CowDeletePathTraverseHelper.h>
#include <fboss/fsdb/oper/CowInitialSyncTraverseHelper.h>
#include <fboss/fsdb/oper/CowPublishAndAddTraverseHelper.h>
#include <fboss/fsdb/oper/CowSubscriptionTraverseHelper.h>
#include <fboss/fsdb/oper/SubscriptionManager.h>
#include <fboss/thrift_cow/storage/CowStorage.h>
#include <fboss/thrift_cow/visitors/DeltaVisitor.h>
#include <fboss/thrift_cow/visitors/ExtendedPathVisitor.h>
#include <fboss/thrift_cow/visitors/PatchBuilder.h>
#include <fboss/thrift_cow/visitors/PathVisitor.h>
#include <fboss/thrift_cow/visitors/RecurseVisitor.h>
#include "fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h"

#include <folly/Function.h>
#include <folly/Traits.h>

namespace facebook::fboss::fsdb {

// Single type-erased callback type for extended-path traversal.
// ExtendedPathVisitor is templated on the callback, so every distinct callable
// type re-instantiates the whole traversal over the tree. Sharing one type
// across call sites keeps that to a single instantiation.
using ExtPathVisitFn = folly::FunctionRef<
    void(const std::vector<std::string>&, const thrift_cow::Serializable&)>;

namespace {

template <typename T>
struct is_shared_ptr : std::false_type {};
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
template <typename T>
constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

} // namespace

namespace csm_detail {
template <typename NodeT>
class OperUnitCache {
 public:
  OperUnitCache(
      const std::vector<std::string>& path,
      // TODO: use serializable
      const NodeT& oldNode,
      const NodeT& newNode)
      : path_(path), oldNode_(oldNode), newNode_(newNode) {}

  const OperDeltaUnit& getEncodedDelta(const fsdb::OperProtocol& protocol) {
    switch (protocol) {
      case fsdb::OperProtocol::BINARY:
        return getOrBuildDelta(binaryUnit_, fsdb::OperProtocol::BINARY);
      case fsdb::OperProtocol::COMPACT:
        return getOrBuildDelta(compactUnit_, fsdb::OperProtocol::COMPACT);
      case fsdb::OperProtocol::SIMPLE_JSON:
        return getOrBuildDelta(jsonUnit_, fsdb::OperProtocol::SIMPLE_JSON);
    }
    throw std::runtime_error("Unsupported protocol");
  }

  const std::optional<folly::fbstring>& getEncodedState(
      const fsdb::OperProtocol& protocol,
      bool newState = true) {
    switch (protocol) {
      case fsdb::OperProtocol::BINARY:
        return getOrBuildState(
            newState ? newStateBinary_ : oldStateBinary_,
            fsdb::OperProtocol::BINARY,
            newState);
      case fsdb::OperProtocol::COMPACT:
        return getOrBuildState(
            newState ? newStateCompact_ : oldStateCompact_,
            fsdb::OperProtocol::COMPACT,
            newState);
      case fsdb::OperProtocol::SIMPLE_JSON:
        return getOrBuildState(
            newState ? newStateJson_ : oldStateJson_,
            fsdb::OperProtocol::SIMPLE_JSON,
            newState);
    }
    throw std::runtime_error("Unsupported protocol");
  }

 private:
  const OperDeltaUnit& getOrBuildDelta(
      std::optional<OperDeltaUnit>& unit,
      const fsdb::OperProtocol& protocol) {
    if (!unit.has_value()) {
      unit.emplace();
      unit->path()->raw() = path_;
      unit->oldState().from_optional(
          getEncodedState(protocol, false /* newState */));
      unit->newState().from_optional(
          getEncodedState(protocol, true /* newState */));
    }
    return *unit;
  }
  const std::optional<folly::fbstring>& getOrBuildState(
      std::optional<folly::fbstring>& state,
      const fsdb::OperProtocol& protocol,
      bool newState = true) {
    const auto& node = newState ? newNode_ : oldNode_;
    if (!state.has_value() && node) {
      state = node->encode(protocol);
    }
    return state;
  }

  const std::vector<std::string>& path_;
  const NodeT& oldNode_;
  const NodeT& newNode_;
  std::optional<OperDeltaUnit> binaryUnit_, compactUnit_, jsonUnit_;
  std::optional<folly::fbstring> newStateBinary_, newStateCompact_,
      newStateJson_;
  std::optional<folly::fbstring> oldStateBinary_, oldStateCompact_,
      oldStateJson_;
};
} // namespace csm_detail

template <typename _Root>
class CowSubscriptionManager
    : public SubscriptionManager<_Root, CowSubscriptionManager<_Root>> {
 public:
  using Base = SubscriptionManager<_Root, CowSubscriptionManager<_Root>>;
  using Root = _Root;

  using Base::Base;
  using Base::patchOperProtocol;
  using Base::publishAndAddPaths;
  using Base::serveSubscriptions;

 private:
  template <typename OperCache>
  void servePathEncoded(
      BasePathSubscription* subscription,
      OperCache& cache,
      OperProtocol protocol,
      const SubscriptionMetadataServer& metadataServer) {
    std::optional<OperState> oldState, newState;
    if (cache.getEncodedState(protocol, false)) {
      oldState.emplace();
      oldState->contents().from_optional(
          cache.getEncodedState(protocol, false));
      oldState->protocol() = protocol;
      // No metadata for oldState, we don't maintain metadata
      // history
    }
    if (cache.getEncodedState(protocol, true)) {
      newState.emplace();
      newState->contents().from_optional(cache.getEncodedState(protocol, true));
      newState->protocol() = protocol;
      newState->metadata() = subscription->getMetadata(metadataServer);
    }
    auto value = DeltaValue<OperState>(oldState, newState);
    auto ret = subscription->offer(std::move(value));
    if (ret.has_value()) {
      subscription->requestPruneWithReason(ret.value());
    }
  }

  void doInitialSyncSimple(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer);

  // Resolves each of the subscription's keys into the lookup tree and seeds
  // matching data paths from the root. Shared by doInitialSyncExtended (all
  // keys) and resolveAddedPatchPaths (newly appended keys).
  // incrementallyResolve is not idempotent, so callers must resolve a given
  // (subscription, key) at most once per cycle.
  void resolveExtendedSubscriptionKeys(
      SubscriptionStore& store,
      const Root& root,
      const std::shared_ptr<ExtendedSubscription>& subscription,
      const std::vector<SubscriptionKey>& keys);

  // Dynamic initial sync for a wildcard PATCH extended subscription: instead of
  // resolving each matching concrete path into a PatchSubscription child, walk
  // the current root with the extended path and buffer a full-state patch for
  // each matched concrete path directly into the extended subscription. The
  // existing extended-subs flush pass emits the aggregated SubscriberChunk.
  void doInitialSyncExtendedDynamicPatch(
      const Root& root,
      const std::shared_ptr<ExtendedSubscription>& subscription,
      const std::vector<SubscriptionKey>& keys);

  void doInitialSyncExtended(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer);

  // Resolve + initial-sync only the newly appended paths (recorded in
  // extendedSubsWithAddedPaths_). Subscriptions still awaiting their first
  // extended sync have their entry dropped by doInitialSyncExtended, so entries
  // seen here belong to subscriptions that already completed initial sync.
  void resolveAddedPatchPaths(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer);

 public:
  void publishAndAddPaths(
      const std::vector<SubscriptionStore*>& stores,
      std::shared_ptr<Root>& root);

  void pruneDeletedPaths(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& oldRoot,
      const std::shared_ptr<Root>& newRoot);

  void serveSubscriptions(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& oldRoot,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer);

  void doInitialSync(
      SubscriptionStore& store,
      const std::shared_ptr<Root>& newRoot,
      const SubscriptionMetadataServer& metadataServer);

}; // namespace facebook::fboss::fsdb

// Out of class with explicit return types so extern templates suppress them.
template <typename _Root>
auto CowSubscriptionManager<_Root>::doInitialSyncSimple(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& newRoot,
    const SubscriptionMetadataServer& metadataServer) -> void {
  if (!store.initialSyncNeeded().numSubsRecursive()) {
    return;
  }

  auto processPath = [&](CowInitialSyncTraverseHelper& traverser,
                         const auto& node) {
    SubscriptionPathStore* lookup = traverser.currentStore();
    if (!lookup) {
      // no subscriptions here
      return;
    }
    // we should only get a visit if we have a subscription needing initial
    // sync, in which case we should have a lookup
    CHECK(lookup);
    CHECK(node);
    auto oldNode = std::remove_cvref_t<decltype(node)>();
    csm_detail::OperUnitCache operUnitCache(traverser.path(), oldNode, node);

    // TODO: maybe switch to reverse iter to erase is cheaper
    auto& subscriptions = lookup->subscriptions();
    auto it = subscriptions.begin();
    while (it != subscriptions.end()) {
      auto& subscription = *it;
      CHECK(subscription);
      if (!metadataServer.ready(subscription->publisherTreeRoot())) {
        // Publisher root not yet seen (initial sync in progress).
        XLOG_EVERY_MS(DBG1, 30000)
            << "subscriber " << subscription->subscriberId()
            << " waiting on publisher root "
            << subscription->publisherTreeRoot()
            << " (initial sync in progress)";
        ++it;
        continue;
      }
      subscription->updateMetadata(metadataServer);
      if (subscription->type() == PubSubType::PATH) {
        auto pathSubscription =
            static_cast<BasePathSubscription*>(subscription);
        std::optional<OperState> state;
        state.emplace();
        state->contents().from_optional(
            operUnitCache.getEncodedState(subscription->operProtocol()));
        state->protocol() = subscription->operProtocol();
        state->metadata() = subscription->getMetadata(metadataServer);
        auto value = DeltaValue<OperState>(std::nullopt, std::move(state));
        auto ret = pathSubscription->offer(std::move(value));
        if (ret.has_value()) {
          pathSubscription->requestPruneWithReason(ret.value());
        }
      } else if (subscription->type() == PubSubType::DELTA) {
        auto deltaSubscription =
            static_cast<BaseDeltaSubscription*>(subscription);
        deltaSubscription->appendRootDeltaUnit(
            operUnitCache.getEncodedDelta(subscription->operProtocol()));
      } else if (subscription->type() == PubSubType::PATCH) {
        auto patchSubscription = static_cast<PatchSubscription*>(subscription);
        thrift_cow::PatchNode patchNode;
        // TODO: try to change to a shared holding a IOBuf instead of
        // copying from an fbstring
        auto& state =
            operUnitCache.getEncodedState(subscription->operProtocol());
        auto buf = folly::IOBuf::copyBuffer(state->data(), state->length());
        patchNode.set_val(*buf);
        auto ret = patchSubscription->offer(std::move(patchNode));
        if (ret.has_value()) {
          patchSubscription->requestPruneWithReason(ret.value());
        }
      }
      store.lookup().add(subscription, store.getPathStoreStats());
      subscription->firstChunkSent();
      // TODO: trim empty path store nodes
      it = subscriptions.erase(it);
    }
  };

  auto traverser = CowInitialSyncTraverseHelper(&store.initialSyncNeeded());
  thrift_cow::RootRecurseVisitor::visit(
      traverser,
      newRoot,
      thrift_cow::RecurseVisitOptions(
          thrift_cow::RecurseVisitMode::FULL,
          thrift_cow::RecurseVisitOrder::CHILDREN_FIRST,
          this->useIdPaths_),
      std::move(processPath));
  if (this->requireResponseOnInitialSync_) {
    for (auto& [_, subscription] : store.subscriptions()) {
      if (metadataServer.ready(subscription->publisherTreeRoot()) &&
          subscription->needsFirstChunk()) {
        auto ret = subscription->sendEmptyInitialChunk();
        if (ret.has_value()) {
          subscription->requestPruneWithReason(ret.value());
        }
        subscription->firstChunkSent();
      }
    }
    // on initialSync, offer even an empty value
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::resolveExtendedSubscriptionKeys(
    SubscriptionStore& store,
    const Root& root,
    const std::shared_ptr<ExtendedSubscription>& subscription,
    const std::vector<SubscriptionKey>& keys) -> void {
  auto processImpl = [&](const std::vector<std::string>& path,
                         const thrift_cow::Serializable& /* node */) {
    store.processAddedPath(path.begin(), path.end());
  };
  ExtPathVisitFn process(processImpl);
  for (const auto& key : keys) {
    const auto& path = subscription->pathAt(key);
    // seed beginnings of the path in to lookup tree
    std::vector<std::string> emptyPathSoFar;
    store.lookup().incrementallyResolve(
        store, subscription, key, emptyPathSoFar);

    thrift_cow::ExtPathVisitorOptions options(this->useIdPaths_);
    thrift_cow::RootExtendedPathVisitor::visit(
        root, path.path()->begin(), path.path()->end(), options, process);
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::doInitialSyncExtendedDynamicPatch(
    const Root& root,
    const std::shared_ptr<ExtendedSubscription>& subscription,
    const std::vector<SubscriptionKey>& keys) -> void {
  auto* patchSub = static_cast<ExtendedPatchSubscription*>(subscription.get());
  thrift_cow::ExtPathVisitorOptions options(this->useIdPaths_);
  for (const auto key : keys) {
    const auto& extPath = subscription->pathAt(key);
    auto procImpl = [&, subKey = key](
                        const std::vector<std::string>& resolvedPath,
                        const thrift_cow::Serializable& node) {
      // Encode directly rather than via OperUnitCache: that cache shares one
      // encoded root across subscribers at the same path, and every matched
      // concrete path here is a different node.
      thrift_cow::PatchNode patchNode;
      patchNode.set_val(node.encodeBuf(patchSub->operProtocol()));
      bufferPatch(*patchSub, subKey, resolvedPath, std::move(patchNode));
    };
    ExtPathVisitFn proc(procImpl);
    thrift_cow::RootExtendedPathVisitor::visit(
        root, extPath.path()->begin(), extPath.path()->end(), options, proc);
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::doInitialSyncExtended(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& newRoot,
    const SubscriptionMetadataServer& metadataServer) -> void {
  const auto& root = *newRoot;

  auto it = store.initialSyncNeededExtended().begin();
  while (it != store.initialSyncNeededExtended().end()) {
    auto& subscription = *it;

    if (!metadataServer.ready(subscription->publisherTreeRoot())) {
      ++it;
      continue;
    }

    subscription->updateMetadata(metadataServer);

    std::vector<SubscriptionKey> keys;
    keys.reserve(subscription->paths().size());
    for (const auto& [key, _] : subscription->paths()) {
      keys.push_back(key);
    }
    if (shouldDynamicallyResolve(*subscription)) {
      // Dynamic branch: no resolved children, no lookup_ expansion.
      doInitialSyncExtendedDynamicPatch(root, subscription, keys);
    } else {
      resolveExtendedSubscriptionKeys(store, root, subscription, keys);
    }

    subscription->recordInitialSyncCompleted();
    // Initial sync already resolved the full path set (including keys
    // appended before it ran); drop pending added-path work so
    // resolveAddedPatchPaths does not resolve those keys again this cycle.
    std::erase_if(
        store.extendedSubsWithAddedPaths(),
        [&](const ExtendedSubscriptionAddedPaths& added) {
          return added.subscription == subscription;
        });
    it = store.initialSyncNeededExtended().erase(it);
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::resolveAddedPatchPaths(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& newRoot,
    const SubscriptionMetadataServer& metadataServer) -> void {
  const auto& root = *newRoot;

  auto& addedPaths = store.extendedSubsWithAddedPaths();
  auto it = addedPaths.begin();
  while (it != addedPaths.end()) {
    // Copy out the shared_ptr so its lifetime is independent of the vector
    // element, which the trailing erase() below invalidates.
    auto subscription = it->subscription;

    if (!metadataServer.ready(subscription->publisherTreeRoot())) {
      ++it;
      continue;
    }

    subscription->updateMetadata(metadataServer);
    if (shouldDynamicallyResolve(*subscription)) {
      doInitialSyncExtendedDynamicPatch(root, subscription, it->newKeys);
    } else {
      resolveExtendedSubscriptionKeys(store, root, subscription, it->newKeys);
    }
    static_cast<ExtendedPatchSubscription*>(subscription.get())
        ->clearInitialSyncPending(it->newKeys);
    it = addedPaths.erase(it);
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::publishAndAddPaths(
    const std::vector<SubscriptionStore*>& stores,
    std::shared_ptr<Root>& root) -> void {
  // this helper recurses through all unpublished paths and ensures
  // that we tell SubscriptionPathStore of any new paths.
  auto processPath = [&](CowPublishAndAddTraverseHelper& /*traverser*/,
                         auto&& node) mutable {
    if constexpr (is_shared_ptr_v<std::remove_cvref_t<decltype(node)>>) {
      // We only want to publish the node itself, not recurse to
      // children.  This invokes the base version of publish to
      // avoid recursing automatically.
      node->NodeBase::publish();
    }
  };

  CowPublishAndAddTraverseHelper traverser(stores);
  thrift_cow::RootRecurseVisitor::visit(
      traverser,
      root,
      thrift_cow::RecurseVisitOptions(
          thrift_cow::RecurseVisitMode::UNPUBLISHED,
          thrift_cow::RecurseVisitOrder::CHILDREN_FIRST,
          this->useIdPaths_),
      std::move(processPath));
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::pruneDeletedPaths(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& oldRoot,
    const std::shared_ptr<Root>& newRoot) -> void {
  // This helper uses DeltaVisitor to visit all deleted nodes in
  // CHILDREN_FIRST order, and remove SubscriptionPathStores created
  // for corresponding paths.
  if (!oldRoot) {
    return;
  }

  auto processDelta = [&](CowDeletePathTraverseHelper& traverser,
                          auto& oldNode,
                          auto& newNode,
                          thrift_cow::DeltaElemTag visitTag) {
    bool isOldNode = static_cast<bool>(oldNode);
    bool isNewNode = static_cast<bool>(newNode);

    if (isOldNode && !isNewNode) {
      auto currStore = traverser.currentStore();
      if (currStore && (currStore->numSubsRecursive() == 0)) {
        auto parentStore = traverser.parentStore();
        if (parentStore) {
          auto path = traverser.path();
          auto lastTok = path.back();
          parentStore->removeChild(lastTok, store.getPathStoreStats());
        }
      }
    }
  };

  CowDeletePathTraverseHelper traverser(&store.lookup());

  thrift_cow::RootDeltaVisitor::visit(
      traverser,
      oldRoot,
      newRoot,
      thrift_cow::DeltaVisitOptions(
          thrift_cow::DeltaVisitMode::FULL,
          thrift_cow::DeltaVisitOrder::CHILDREN_FIRST,
          this->useIdPaths_,
          false /* hybridNodeShallowTraversal */),
      std::move(processDelta));
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::serveSubscriptions(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& oldRoot,
    const std::shared_ptr<Root>& newRoot,
    const SubscriptionMetadataServer& metadataServer) -> void {
  auto processChange = [&](CowSubscriptionTraverseHelper& traverser,
                           auto& oldNode,
                           auto& newNode,
                           thrift_cow::DeltaElemTag visitTag) {
    // We need to serve delta+patch subscriptions if it's a MINIMAL change
    // OR
    // if this is a fully added/removed node and there are exact
    // subscriptions, we serve them. This handles the case
    // where a change won't be "MINIMAL" relative to the root,
    // but is relative to the subscription point. This is mainly
    // for children of a fully added/removed node higher in the tree.
    auto isMinimalOrAddedOrRemoved =
        visitTag == thrift_cow::DeltaElemTag::MINIMAL || !oldNode || !newNode;

    // build out patch before trying to send to subscribers.
    // Patches are only supported if we're using id paths
    if (isMinimalOrAddedOrRemoved && traverser.patchBuilder()) {
      XLOG(DBG6) << "Setting leaf patch";
      traverser.patchBuilder()->setLeafPatch(newNode);
    }

    const auto* lookup = traverser.currentStore();
    if (!FLAGS_lazyPathStoreCreation) {
      // lookup can't be none or we wouldn't be serving this subscription
      DCHECK(lookup);
    } else {
      // with lazy path store creation, we may not have created the PathStore
      // yet. But there can still be subscriptions at parent nodes to serve.
      // So proceed with processing the change.
    }

    auto& path = traverser.path();

    csm_detail::OperUnitCache operUnitCache(path, oldNode, newNode);

    if (lookup) {
      const auto& exactSubscriptions = lookup->subscriptions();
      for (auto& relevant : exactSubscriptions) {
        if (relevant->type() == PubSubType::PATH) {
          auto* pathSubscription = static_cast<BasePathSubscription*>(relevant);
          // TODO: cache encoded state
          servePathEncoded(
              pathSubscription,
              operUnitCache,
              pathSubscription->operProtocol(),
              metadataServer);
        } else if (relevant->type() == PubSubType::DELTA) {
          if (isMinimalOrAddedOrRemoved) {
            auto* deltaSubscription = static_cast<DeltaSubscription*>(relevant);
            deltaSubscription->appendRootDeltaUnit(
                operUnitCache.getEncodedDelta(relevant->operProtocol()));
          }
        } else if (
            relevant->type() == PubSubType::PATCH && traverser.patchBuilder()) {
          // patches only supported when using id paths
          auto* patchSubscription = static_cast<PatchSubscription*>(relevant);
          auto ret =
              patchSubscription->offer(traverser.patchBuilder()->curPatch());
          if (ret.has_value()) {
            patchSubscription->requestPruneWithReason(ret.value());
          }
        }
      }
    }

    // Serve any wildcard PATCH extended subscription whose pattern is fully
    // matched at this exact node. Equivalent to what PatchSubscription::offer
    // does for a resolved child, without the resolved object or its lookup_
    // node. A tracker is only attached when the flag is on and patches are
    // being built, so a patch builder is guaranteed here.
    if (auto* tracker = traverser.wildcardTracker()) {
      DCHECK(traverser.patchBuilder().has_value())
          << "wildcard tracker attached without a patch builder";
      for (const auto& candidate : tracker->matchedCandidates()) {
        bufferPatch(
            *candidate.sub,
            candidate.key,
            path,
            traverser.patchBuilder()->curPatch());
      }
    }

    if (visitTag != thrift_cow::DeltaElemTag::MINIMAL) {
      // Done with path subs which need full traversal. Now only care about
      // MINIMAL changes for delta subs
      return;
    }
    // serve MINIMAL changes to delta subscription at parent paths
    const auto& traverseElements = traverser.elementsAlongPath();
    for (auto it = traverseElements.begin(); it != traverseElements.end() - 1;
         ++it) {
      if (!it->lookup) {
        // no path store for this path, so subscriptions to serve
        continue;
      }
      const auto& parentSubscriptions = it->lookup->subscriptions();
      for (auto& relevant : parentSubscriptions) {
        if (relevant->type() != PubSubType::DELTA) {
          continue;
        }
        auto* deltaSubscription = static_cast<DeltaSubscription*>(relevant);
        deltaSubscription->appendRootDeltaUnit(
            operUnitCache.getEncodedDelta(relevant->operProtocol()));
      }
    }
  };

  // can only build patches with id paths
  std::optional<thrift_cow::PatchNodeBuilder> patchBuilder;
  if (this->useIdPaths_) {
    patchBuilder.emplace(patchOperProtocol(), true /* incrementallyCompress */);
  }
  CowSubscriptionTraverseHelper traverser(&store.lookup(), patchBuilder);

  // Dynamic wildcard PATCH resolution: when enabled and patches are being
  // built (id paths), seed a candidate tracker from the registered extended
  // subscriptions and attach it to the traverser. Matched candidates are
  // served inline in processChange above. The tracker must outlive the
  // traversal, hence it lives on this stack frame.
  WildcardPatchCandidateTracker wildcardTracker;
  if (FLAGS_dynamicWildcardPatchResolution && patchBuilder.has_value()) {
    if (wildcardTracker.seed(store.extendedSubscriptions(), metadataServer) >
        0) {
      traverser.setWildcardTracker(&wildcardTracker);
    }
  }

  if (oldRoot && newRoot) {
    thrift_cow::RootDeltaVisitor::visit(
        traverser,
        oldRoot,
        newRoot,
        thrift_cow::DeltaVisitOptions(
            thrift_cow::DeltaVisitMode::FULL,
            thrift_cow::DeltaVisitOrder::CHILDREN_FIRST,
            this->useIdPaths_),
        std::move(processChange));
  } else if (!oldRoot || !newRoot) {
    processChange(
        traverser, oldRoot, newRoot, thrift_cow::DeltaElemTag::MINIMAL);
  }
}

template <typename _Root>
auto CowSubscriptionManager<_Root>::doInitialSync(
    SubscriptionStore& store,
    const std::shared_ptr<Root>& newRoot,
    const SubscriptionMetadataServer& metadataServer) -> void {
  /*
   * It is important we do the extended subscriptions before the
   * regular ones. Resolving the extended subscriptions could extend
   * the initial sync needed set based on the initial matching paths.
   */

  doInitialSyncExtended(store, newRoot, metadataServer);
  resolveAddedPatchPaths(store, newRoot, metadataServer);
  doInitialSyncSimple(store, newRoot, metadataServer);
}

} // namespace facebook::fboss::fsdb
