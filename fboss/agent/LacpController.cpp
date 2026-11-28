/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/LacpController.h"
#include "fboss/agent/gen-cpp2/switch_config_types.h"

#include <cstring>

namespace facebook::fboss {

using folly::io::Cursor;
using folly::io::RWPrivateCursor;
using std::string;

LacpController::LacpController(
    PortID portID,
    FbossEventBase* evb,
    LacpServicerIf* servicer)
    : portID_(portID),
      tx_(*this, evb, servicer),
      rx_(*this,
          evb,
          servicer,
          cfg::switch_config_constants::DEFAULT_LACP_HOLD_TIMER_MULTIPLIER()),
      periodicTx_(*this, evb),
      mux_(*this, evb, servicer),
      selector_(*this),
      evb_(evb),
      servicer_(servicer) {
  actorState_ &= ~LacpState::AGGREGATABLE;
}

LacpController::LacpController(
    PortID portID,
    FbossEventBase* evb,
    uint16_t portPriority,
    cfg::LacpPortRate rate,
    cfg::LacpPortActivity activity,
    uint16_t holdTimerMultiplier,
    AggregatePortID aggPortID,
    uint16_t systemPriority,
    folly::MacAddress systemID,
    uint8_t minLinkCount,
    std::optional<uint8_t> minimumLinkCountToUp,
    LacpServicerIf* servicer)
    : aggPortID_(aggPortID),
      portID_(portID),
      portPriority_(portPriority),
      systemPriority_(systemPriority),
      tx_(*this, evb, servicer),
      rx_(*this, evb, servicer, holdTimerMultiplier),
      periodicTx_(*this, evb),
      mux_(*this, evb, servicer),
      selector_(*this, minLinkCount, minimumLinkCountToUp),
      evb_(evb),
      servicer_(servicer) {
  std::memcpy(systemID_.begin(), systemID.bytes(), systemID_.size());

  actorState_ |= LacpState::AGGREGATABLE;
  if (rate == cfg::LacpPortRate::FAST) {
    actorState_ |= LacpState::SHORT_TIMEOUT;
  }
  if (activity == cfg::LacpPortActivity::ACTIVE) {
    actorState_ |= LacpState::LACP_ACTIVE;
  }
}

void LacpController::startMachines() {
  evb()->runInFbossEventBaseThread([self = shared_from_this()]() {
    self->mux_.start();
    self->tx_.start();
    self->periodicTx_.start();
    self->rx_.start();
    self->selector_.start();
    XLOG(DBG2) << "Started LACP State Machine " << self->portID();
  });
}

void LacpController::restoreMachines(
    const AggregatePort::PartnerState& partnerState) {
  evb()->runInFbossEventBaseThread([self = shared_from_this(), partnerState]() {
    self->mux_.start();
    self->selector_.start();
    self->rx_.restoreState(partnerState);
    self->mux_.restoreState();
    self->selector_.restoreState();
    self->mux_.enableCollectingDistributing();
    self->tx_.start();
    self->ntt();
    self->periodicTx_.start();
    XLOG(DBG3) << "Restored LACP State Machine " << self->portID();
  });
}

void LacpController::stopMachines() {
  evb()->runInFbossEventBaseThreadAndWait([self = shared_from_this()]() {
    self->mux_.stop();
    self->tx_.stop();
    self->periodicTx_.stop();
    self->rx_.stop();
    self->selector_.stop();
  });
}

LacpController::~LacpController() {}

FbossEventBase* LacpController::evb() const {
  return evb_;
}

void LacpController::portUp() {
  evb()->runInFbossEventBaseThread([self = shared_from_this()]() {
    self->rx_.portUp();
    self->periodicTx_.portUp();
  });
}

void LacpController::portDown() {
  evb()->runInFbossEventBaseThread([self = shared_from_this()]() {
    self->rx_.portDown();
    self->periodicTx_.portDown();
    self->selector_.portDown();
  });
}

void LacpController::received(const LACPDU& lacpdu) {
  evb()->runInFbossEventBaseThread(
      [self = shared_from_this(), lacpdu]() { self->rx_.rx(lacpdu); });
}

ParticipantInfo LacpController::actorInfo() const {
  ParticipantInfo info;

  evb()->runImmediatelyOrRunInFbossEventBaseThreadAndWait(
      [self = shared_from_this(), &info]() {
        info.systemID = self->systemID_;
        info.systemPriority = self->systemPriority_;
        info.port = static_cast<ParticipantInfo::Port>(self->portID_);
        info.portPriority = self->portPriority_;
        info.key = static_cast<ParticipantInfo::Key>(self->aggPortID_);
        info.state = self->actorState_;
      });

  return info;
}

void LacpController::setActorState(LacpState state) {
  actorState_ = state;
}

LacpState LacpController::actorState() const {
  return actorState_;
}

ParticipantInfo LacpController::partnerInfo() const {
  ParticipantInfo info;

  evb()->runImmediatelyOrRunInFbossEventBaseThreadAndWait(
      [self = shared_from_this(), &info]() { info = self->rx_.partnerInfo(); });

  return info;
}

void LacpController::ntt() {
  auto actor = actorInfo();
  auto partner = partnerInfo();
  // 802.1AX Transmit machine: no LACPDU while the Periodic machine is in
  // NO_PERIODIC. Otherwise two passive ends keep re-syncing each other off
  // the PDUs every Mux transition emits.
  if ((actor.state & LacpState::LACP_ACTIVE) == 0 &&
      (partner.state & LacpState::LACP_ACTIVE) == 0) {
    XLOG(DBG4) << "LacpController[" << portID_
               << "]: skipping ntt, actor and partner are both passive";
    return;
  }
  tx_.ntt(LACPDU(actor, partner));
}

PortID LacpController::portID() const {
  return portID_;
}

AggregatePortID LacpController::aggregatePortID() const {
  return aggPortID_;
}

void LacpController::selected() {
  selector_.selected();
  auto sel = selector_.getSelection();
  mux_.selected(static_cast<AggregatePortID>(sel.lagID.actorKey));
}

void LacpController::unselected() {
  selector_.unselected();
  mux_.unselected();
}

void LacpController::standby() {
  mux_.standby();
  selector_.standby();
}

void LacpController::matched(bool partnerChanged) {
  mux_.matched(partnerChanged);
}

void LacpController::notMatched() {
  mux_.notMatched();
}

void LacpController::startPeriodicTransmissionMachine() {
  periodicTx_.start();
}

void LacpController::select() {
  selector_.select();
}

void LacpController::selected(
    folly::Range<std::vector<PortID>::const_iterator> ports) {
  auto controllersToSignal = servicer_->getControllersFor(ports);

  for (const auto& controller : controllersToSignal) {
    controller->selected();
  }
}

void LacpController::standby(
    folly::Range<std::vector<PortID>::const_iterator> ports) {
  auto controllersToSignal = servicer_->getControllersFor(ports);

  for (const auto& controller : controllersToSignal) {
    controller->standby();
  }
}

bool LacpController::getLacpLastTransmissionResult() {
  return tx_.getLacpLastTransmissionResult();
}

std::chrono::seconds LacpController::getCurrentTransmissionPeriod() const {
  return periodicTx_.getCurrentTransmissionPeriod();
}
} // namespace facebook::fboss
