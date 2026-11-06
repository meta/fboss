// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/fsdb/oper/CowPublishAndAddTraverseHelper.h"

namespace facebook::fboss::fsdb {

CowPublishAndAddTraverseHelper::CowPublishAndAddTraverseHelper(
    const std::vector<SubscriptionStore*>& stores) {
  targets_.reserve(stores.size());
  for (auto* store : stores) {
    SubscriptionStoreTarget target;
    target.store = store;
    target.pathStores.emplace_back(&store->lookup());
    targets_.emplace_back(std::move(target));
  }
}

bool CowPublishAndAddTraverseHelper::shouldShortCircuitImpl(
    thrift_cow::VisitorType /* visitorType */) const {
  return false;
}

void CowPublishAndAddTraverseHelper::onPushImpl(
    thrift_cow::ThriftTCType /* tc */) {
  const auto& currPath = path();
  const auto& newTok = currPath.back();

  for (auto& target : targets_) {
    auto* lastPathStore = target.pathStores.back();

    SubscriptionPathStore* child{nullptr};
    if (lastPathStore) {
      if (FLAGS_lazyPathStoreCreation) {
        child = lastPathStore->child(newTok);
      } else {
        child = lastPathStore->getOrCreateChild(
            newTok, target.store->getPathStoreStats());
      }
      // this assumes currPath has size > 0, which we know because we
      // would have added at least one elem in TraverseHelper::push().
      lastPathStore->processAddedPath(
          *target.store, currPath.begin(), currPath.end() - 1, currPath.end());
    }
    // on push, always add the child to the pathStores_ even if lastPathStore
    // is null, so that pathStores_.size() is always equal to pathlen()
    target.pathStores.emplace_back(child);
  }
}

void CowPublishAndAddTraverseHelper::onPopImpl(
    std::string&& /* popped */,
    thrift_cow::ThriftTCType /* tc */) {
  for (auto& target : targets_) {
    target.pathStores.pop_back();
  }
}

} // namespace facebook::fboss::fsdb
