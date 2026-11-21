// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/agent/HwSwitchHandler.h"
#include "fboss/agent/Utils.h"
#include "fboss/agent/state/StateDelta.h"
#include "folly/futures/Promise.h"

namespace facebook::fboss {

HwSwitchStateUpdate::HwSwitchStateUpdate(
    const std::vector<StateDelta>& deltas,
    bool transaction,
    bool soleConsumer)
    : oldState(deltas.front().oldState()),
      newState(deltas.back().newState()),
      isTransaction(transaction) {
  // A sole consumer needs no cached copy: build one, move it to the HwSwitch.
  for (const auto& delta : deltas) {
    operDeltas.push_back(
        soleConsumer ? delta.computeOperDeltaUncached() : delta.getOperDelta());
  }
}

HwSwitchHandler::HwSwitchHandler(
    const SwitchID& switchId,
    const cfg::SwitchInfo& info)
    : switchId_(switchId), info_(info), operDeltaFilter_(switchId) {}

void HwSwitchHandler::start() {
  hwSwitchManagerThread_.reset(new std::thread([this]() { run(); }));
}

void HwSwitchHandler::run() {
  initThread(folly::to<std::string>("HwSwitchHandler-", switchId_));
  hwSwitchManagerEvb_.loopForever();
}

void HwSwitchHandler::stop() {
  if (!hwSwitchManagerThread_) {
    return;
  }
  cancelOperDeltaSync();
  hwSwitchManagerEvb_.runInFbossEventBaseThreadAndWait(
      [this]() { hwSwitchManagerEvb_.terminateLoopSoon(); });
  hwSwitchManagerThread_->join();
  hwSwitchManagerThread_.reset();
}

HwSwitchHandler::~HwSwitchHandler() {
  stop();
}

folly::Future<HwSwitchStateUpdateResult> HwSwitchHandler::stateChanged(
    HwSwitchStateUpdate update,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  auto [promise, semiFuture] =
      folly::makePromiseContract<HwSwitchStateUpdateResult>();

  hwSwitchManagerEvb_.runInFbossEventBaseThread(
      [capturedPromise = std::move(promise),
       update = std::move(update),
       hwWriteBehavior = hwWriteBehavior,
       deltaApplicationBehavior = deltaApplicationBehavior,
       this]() mutable {
        capturedPromise.setWith(
            [&update, &hwWriteBehavior, &deltaApplicationBehavior, this]() {
              return stateChangedImpl(
                  update, hwWriteBehavior, deltaApplicationBehavior);
            });
      });

  auto future = std::move(semiFuture).via(&hwSwitchManagerEvb_);
  return future;
}

HwSwitchStateUpdateResult HwSwitchHandler::stateChangedImpl(
    HwSwitchStateUpdate& update,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  std::vector<fsdb::OperDelta> inDeltas;
  for (auto& operDelta : update.operDeltas) {
    auto inDelta =
        operDeltaFilter_.filterWithSwitchStateRootPath(std::move(operDelta));
    if (inDelta.has_value()) {
      inDeltas.push_back(std::move(*inDelta));
    }
  }
  if (inDeltas.empty()) {
    // no-op
    return {
        update.newState,
        HwSwitchStateUpdateStatus::HWSWITCH_STATE_UPDATE_SUCCEEDED};
  }
  auto stateUpdateResult = stateChangedImpl(
      std::move(inDeltas),
      update.isTransaction,
      update.oldState,
      update.newState,
      hwWriteBehavior,
      deltaApplicationBehavior);
  auto outDelta = stateUpdateResult.first;
  // this is the success case where an empty delta is returned
  // applicable to both SaiSwitch and BcmSwitch
  if (outDelta.changes()->empty()) {
    return {update.newState, stateUpdateResult.second};
  }
  // multi switch mode returns full delta if cancelled
  // HwSwitch rollback returns full delta if transaction fails and rollback
  // succeeds
  // applicable only to SaiSwitch and multi (absent in BcmSwitch)
  auto inDelta = operDeltaFilter_.filterWithSwitchStateRootPath(
      StateDelta(update.oldState, update.newState).getOperDelta());
  if (inDelta && *inDelta == outDelta) {
    return {update.oldState, stateUpdateResult.second};
  }
  // obtain the state that actually got programmed
  // applicable only to BcmSwitch
  return {
      StateDelta(update.newState, outDelta).newState(),
      stateUpdateResult.second};
}

HwSwitchStateOperUpdateResult HwSwitchHandler::stateChangedImpl(
    std::vector<fsdb::OperDelta> deltas,
    bool transaction,
    const std::shared_ptr<SwitchState>& oldState,
    const std::shared_ptr<SwitchState>& newState,
    const HwWriteBehavior& hwWriteBehavior,
    const std::optional<StateDeltaApplication>& deltaApplicationBehavior) {
  return stateChanged(
      std::move(deltas),
      transaction,
      oldState,
      newState,
      hwWriteBehavior,
      deltaApplicationBehavior);
}

fsdb::OperDelta HwSwitchHandler::getFullSyncOperDelta(
    const std::shared_ptr<SwitchState>& state) const {
  auto delta = StateDelta(std::make_shared<SwitchState>(), state);
  auto filteredOper =
      operDeltaFilter_.filterWithSwitchStateRootPath(delta.getOperDelta());
  CHECK(filteredOper.has_value());
  return filteredOper.value();
}

} // namespace facebook::fboss
