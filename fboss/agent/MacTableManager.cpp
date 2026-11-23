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

} // namespace

MacTableManager::MacTableManager(SwSwitch* sw) : sw_(sw) {}

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
    sw_->updateStateNoCoalescing(
        folly::to<std::string>("Programming : ", l2Entry.str()),
        std::move(updateMacTableFn));
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
