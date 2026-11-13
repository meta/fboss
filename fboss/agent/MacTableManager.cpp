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
#include "fboss/agent/SwitchStats.h"
#include "fboss/agent/state/Port.h"
#include "fboss/agent/state/SwitchState.h"

namespace facebook::fboss {

namespace {

constexpr auto kMacLimitError = PortError::ERROR_DISABLE_MAC_LIMIT_EXCEEDED;

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
  const bool isAdd =
      l2EntryUpdateType == L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD;
  if (isAdd &&
      MacTableUtils::isMacLimitErrorDisabled(
          sw_->getState(), l2Entry.getPort())) {
    sw_->stats()->l2LearnPortLimitDrop();
    return;
  }

  auto updateMacTableFn = [sw = sw_, l2Entry, l2EntryUpdateType, isAdd](
                              const std::shared_ptr<SwitchState>& state) {
    if (isAdd) {
      return learnMac(sw, state, l2Entry);
    }
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

// Runs on the update thread against the latest state, so the limit is exact
// regardless of how learn updates are queued or coalesced.
std::shared_ptr<SwitchState> MacTableManager::learnMac(
    SwSwitch* sw,
    const std::shared_ptr<SwitchState>& state,
    const L2Entry& l2Entry) {
  const auto learn = [&]() {
    return MacTableUtils::updateMacTable(
        state, l2Entry, L2EntryUpdateType::L2_ENTRY_UPDATE_TYPE_ADD);
  };
  const auto portId = l2Entry.getPort().phyPortIDIf();
  if (!portId) {
    return learn();
  }
  const auto port = state->getPorts()->getNodeIf(*portId);
  if (!port) {
    return learn();
  }
  if (port->hasError(kMacLimitError)) {
    sw->stats()->l2LearnPortLimitDrop();
    return nullptr;
  }
  const auto limit = MacTableUtils::getMaxL2EntriesPerPort(
      state, port, *sw->getScopeResolver());
  if (!limit ||
      !MacTableUtils::exceedsMacLimit(
          state,
          l2Entry.getPort(),
          l2Entry.getVlanID(),
          l2Entry.getMac(),
          *limit)) {
    return learn();
  }
  XLOG(ERR) << "Port " << port->getName() << " (" << *portId
            << ") reached its limit of " << *limit
            << " MACs, error-disabling it";
  sw->stats()->l2LearnPortLimitDrop();
  sw->stats()->l2LearnPortLimitErrorDisable();
  return MacTableUtils::errorDisablePortForMacLimit(state, *portId);
}

} // namespace facebook::fboss
