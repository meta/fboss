/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/MacTableManager.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/FbossHwUpdateError.h"
#include "fboss/agent/L2Entry.h"
#include "fboss/agent/MacTableUtils.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {
bool sameL2Update(
    const L2Entry& lhs,
    L2EntryUpdateType lhsType,
    const L2Entry& rhs,
    L2EntryUpdateType rhsType) {
  return lhsType == rhsType && lhs.getPort() == rhs.getPort() &&
      lhs.getType() == rhs.getType() && lhs.getClassID() == rhs.getClassID();
}
} // namespace

MacTableManager::MacTableManager(SwSwitch* sw)
    : sw_(sw), queuedUpdates_(std::make_shared<QueuedUpdates>()) {}

bool MacTableManager::isHwUpdateProtected() {
  // this API return true if the platform supports hw protection
  // for this we are using transactionsSupported() API
  // and return true for SAI switches. MAC protection uses transactions
  // support in HW switch which is available only in SAI switches
  return sw_->getHwSwitchHandler()->transactionsSupported();
}

void MacTableManager::handleL2LearningUpdate(
    L2Entry l2Entry,
    L2EntryUpdateType l2EntryUpdateType) {
  auto updateMacTableFn =
      [l2Entry, l2EntryUpdateType](const std::shared_ptr<SwitchState>& state) {
        return MacTableUtils::updateMacTable(state, l2Entry, l2EntryUpdateType);
      };

  if (FLAGS_enable_hw_update_protection && isHwUpdateProtected()) {
    try {
      sw_->updateStateWithHwFailureProtection(
          folly::to<std::string>(
              "Programming with hw failure protection : ", l2Entry.str()),
          std::move(updateMacTableFn));
    } catch (const FbossHwUpdateError& e) {
      XLOG(ERR) << "Exception: " << e.what() << std::endl;
      sw_->stats()->macTableUpdateFailure();
    }
  } else {
    // The hw can report the same MAC many times before its entry is programmed,
    // and each report would otherwise queue its own non-coalescing update.
    // Skip a report identical to the last one still queued for this MAC:
    // applying it again would not change the state.
    QueuedUpdateKey key{l2Entry.getMac(), l2Entry.getVlanID()};
    // Updates run in queue order, so once the count drops to zero the last
    // queued update for this MAC has run.
    auto updateFn =
        [queuedUpdates = queuedUpdates_, key, fn = std::move(updateMacTableFn)](
            const std::shared_ptr<SwitchState>& state) {
          {
            auto queued = queuedUpdates->wlock();
            auto it = queued->find(key);
            if (it != queued->end() && --it->second.numQueued == 0) {
              queued->erase(it);
            }
          }
          return fn(state);
        };
    // Enqueue under the lock so the recorded update is always the last one in
    // the queue for this MAC, even with concurrent reporters.
    auto queued = queuedUpdates_->wlock();
    auto it = queued->find(key);
    if (it != queued->end() &&
        sameL2Update(
            it->second.l2Entry,
            it->second.updateType,
            l2Entry,
            l2EntryUpdateType)) {
      ++duplicateUpdatesSkipped_;
      sw_->stats()->macTableDuplicateUpdateSkipped();
      return;
    }
    if (it == queued->end()) {
      queued->emplace(key, QueuedUpdate{l2Entry, l2EntryUpdateType, 1});
    } else {
      it->second.l2Entry = l2Entry;
      it->second.updateType = l2EntryUpdateType;
      ++it->second.numQueued;
    }
    sw_->updateStateNoCoalescing(
        folly::to<std::string>("Programming : ", l2Entry.str()),
        std::move(updateFn));
  }
}

} // namespace facebook::fboss
