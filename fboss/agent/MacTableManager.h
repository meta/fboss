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

#include "fboss/agent/L2Entry.h"
#include "fboss/agent/types.h"

#include <folly/MacAddress.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/hash/Hash.h>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace facebook::fboss {

class SwSwitch;

class MacTableManager {
 public:
  explicit MacTableManager(SwSwitch* sw);

  MacTableManager(MacTableManager const&) = delete;
  MacTableManager& operator=(MacTableManager const&) = delete;

  void handleL2LearningUpdate(
      L2Entry l2Entry,
      L2EntryUpdateType l2EntryUpdateType);
  bool isHwUpdateProtected();

  uint64_t getDuplicateUpdatesSkipped() const {
    return duplicateUpdatesSkipped_.load();
  }

 private:
  struct QueuedUpdate {
    L2Entry l2Entry;
    L2EntryUpdateType updateType{};
    uint32_t numQueued{0};
  };
  using QueuedUpdateKey = std::pair<folly::MacAddress, VlanID>;
  struct QueuedUpdateKeyHash {
    size_t operator()(const QueuedUpdateKey& key) const {
      return folly::hash::hash_128_to_64(
          std::hash<folly::MacAddress>()(key.first),
          std::hash<VlanID>()(key.second));
    }
  };
  // Shared with the queued update functions, which can run after this manager
  // is destroyed: SwSwitch::stop() resets it before draining pendingUpdates_.
  using QueuedUpdates = folly::Synchronized<
      folly::F14FastMap<QueuedUpdateKey, QueuedUpdate, QueuedUpdateKeyHash>>;

  SwSwitch* sw_{nullptr};
  std::shared_ptr<QueuedUpdates> queuedUpdates_;
  std::atomic<uint64_t> duplicateUpdatesSkipped_{0};
};

} // namespace facebook::fboss
