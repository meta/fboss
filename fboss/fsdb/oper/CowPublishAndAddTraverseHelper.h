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

#include "fboss/fsdb/oper/SubscriptionPathStore.h"
#include "fboss/fsdb/oper/SubscriptionStore.h"
#include "fboss/thrift_cow/visitors/TraverseHelper.h"

namespace facebook::fboss::fsdb {

struct CowPublishAndAddTraverseHelper
    : thrift_cow::TraverseHelper<CowPublishAndAddTraverseHelper> {
  using Base = thrift_cow::TraverseHelper<CowPublishAndAddTraverseHelper>;

  using Base::path;
  using Base::shouldShortCircuit;

  // node->publish() is a global one-shot, so a second UNPUBLISHED walk would
  // short-circuit at the root and register nothing. One walk therefore has to
  // feed every bucket's store, each with its own path-store stack.
  struct SubscriptionStoreTarget {
    SubscriptionStore* store{nullptr};
    std::vector<SubscriptionPathStore*> pathStores;
  };

  explicit CowPublishAndAddTraverseHelper(
      const std::vector<SubscriptionStore*>& stores);

  bool shouldShortCircuitImpl(thrift_cow::VisitorType visitorType) const;

  void onPushImpl(thrift_cow::ThriftTCType /* tc */);

  void onPopImpl(std::string&& /* popped */, thrift_cow::ThriftTCType /* tc */);

 private:
  std::vector<SubscriptionStoreTarget> targets_;
};

} // namespace facebook::fboss::fsdb
