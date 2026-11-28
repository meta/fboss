/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/LacpMachines.h"
#include "fboss/agent/LacpController.h"
#include "fboss/agent/LinkAggregationManager.h"
#include "fboss/lib/AlertLogger.h"

#include <folly/Conv.h>
#include <folly/ExceptionString.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <algorithm>
#include <exception>

namespace facebook::fboss {

using folly::ByteRange;

void toAppend(ReceiveMachine::ReceiveState state, std::string* result) {
  std::string stateAsString;

  switch (state) {
    case ReceiveMachine::ReceiveState::CURRENT:
      stateAsString = "current";
      break;
    case ReceiveMachine::ReceiveState::EXPIRED:
      stateAsString = "expired";
      break;
    case ReceiveMachine::ReceiveState::INITIALIZED:
      stateAsString = "initialized";
      break;
    case ReceiveMachine::ReceiveState::DEFAULTED:
      stateAsString = "defaulted";
      break;
    case ReceiveMachine::ReceiveState::DISABLED:
      stateAsString = "disabled";
      break;
  }

  folly::toAppend(stateAsString, result);
}

void toAppend(
    PeriodicTransmissionMachine::PeriodicState state,
    std::string* result) {
  std::string stateAsString;

  switch (state) {
    case PeriodicTransmissionMachine::PeriodicState::NONE:
      stateAsString = "none";
      break;
    case PeriodicTransmissionMachine::PeriodicState::SLOW:
      stateAsString = "slow";
      break;
    case PeriodicTransmissionMachine::PeriodicState::FAST:
      stateAsString = "fast";
      break;
    case PeriodicTransmissionMachine::PeriodicState::TX:
      stateAsString = "tx";
      break;
  }

  folly::toAppend(stateAsString, result);
}

void toAppend(MuxMachine::MuxState state, std::string* result) {
  std::string stateAsString;

  switch (state) {
    case MuxMachine::MuxState::DETACHED:
      stateAsString = "detached";
      break;
    case MuxMachine::MuxState::WAITING:
      stateAsString = "waiting";
      break;
    case MuxMachine::MuxState::ATTACHED:
      stateAsString = "attached";
      break;
    case MuxMachine::MuxState::COLLECTING_DISTRIBUTING:
      stateAsString = "collecting&distributing";
      break;
  }

  folly::toAppend(stateAsString, result);
}

// Needed for CHECK_* macros to work with ReceiveMachine::ReceiveState
std::ostream& operator<<(std::ostream& out, ReceiveMachine::ReceiveState s) {
  std::string stateAsString;

  switch (s) {
    case ReceiveMachine::ReceiveState::CURRENT:
      stateAsString = "current";
      break;
    case ReceiveMachine::ReceiveState::EXPIRED:
      stateAsString = "expired";
      break;
    case ReceiveMachine::ReceiveState::INITIALIZED:
      stateAsString = "initialized";
      break;
    case ReceiveMachine::ReceiveState::DEFAULTED:
      stateAsString = "defaulted";
      break;
    case ReceiveMachine::ReceiveState::DISABLED:
      stateAsString = "disabled";
      break;
  }

  return (out << stateAsString);
}

// Needed for CHECK_* macros to work with MuxMachine::MuxState
std::ostream& operator<<(std::ostream& out, MuxMachine::MuxState s) {
  std::string stateAsString;

  switch (s) {
    case MuxMachine::MuxState::DETACHED:
      stateAsString = "detached";
      break;
    case MuxMachine::MuxState::WAITING:
      stateAsString = "waiting";
      break;
    case MuxMachine::MuxState::ATTACHED:
      stateAsString = "attached";
      break;
    case MuxMachine::MuxState::COLLECTING_DISTRIBUTING:
      stateAsString = "collecting&distributing";
      break;
  }

  return (out << stateAsString);
}

ReceiveMachine::ReceiveMachine(
    LacpController& controller,
    folly::EventBase* evb,
    LacpServicerIf* servicer,
    uint16_t holdTimerMultiplier)
    : folly::AsyncTimeout(evb),
      controller_(controller),
      servicer_(servicer),
      slowEpochSeconds_(std::chrono::seconds(30 * holdTimerMultiplier)),
      fastEpochSeconds_(std::chrono::seconds(1 * holdTimerMultiplier)) {}

ReceiveMachine::~ReceiveMachine() {}

void ReceiveMachine::start() {
  initialize();
}

void ReceiveMachine::stop() {
  cancelTimeout();
}

void ReceiveMachine::rx(LACPDU lacpdu) {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  XLOG(DBG4) << "ReceiveMachine[" << controller_.portID() << "]: " << "RX("
             << lacpdu.describe() << ")";

  if (state_ == ReceiveState::DISABLED) {
    XLOG(DBG4) << "ReceiveMachine[" << controller_.portID()
               << "]: " << "Ignoring frame reception in DISABLED state";
    return;
  }

  current(lacpdu);
}

void ReceiveMachine::portUp() {
  CHECK(controller_.evb()->inRunningEventBaseThread());
  if (state_ != ReceiveState::DISABLED) {
    XLOG(DBG2) << "ReceiveMachine[" << controller_.portID()
               << "]: Ignoring UP in state " << state_;
    return;
  }

  XLOG(DBG2) << "ReceiveMachine[" << controller_.portID() << "]: UP";

  expired();
}

void ReceiveMachine::portDown() {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  XLOG(DBG4) << "ReceiveMachine[" << controller_.portID() << "]: DOWN";

  disabled();
}

void ReceiveMachine::initialize() {
  // This state is entered on either initialization or port movement. Since
  // port movement is not yet implemented, it can currently only be entered
  // during initialization, when state_ == ReceiveState::INITIALIZED.
  // TODO(samank): remove CHECK once port movement functionality has been added
  CHECK_EQ(state_, ReceiveState::INITIALIZED);

  controller_.unselected();

  recordDefault();

  controller_.setActorState(controller_.actorState() & ~LacpState::EXPIRED);

  disabled(); // UCT
}

void ReceiveMachine::defaulted() {
  updateState(ReceiveState::DEFAULTED);

  updateDefaultSelected();

  recordDefault();

  controller_.setActorState(controller_.actorState() & ~LacpState::EXPIRED);
}

void ReceiveMachine::expired() {
  updateState(ReceiveState::EXPIRED);

  partnerInfo_.state |= LacpState::SHORT_TIMEOUT;

  partnerInfo_.state &= ~LacpState::IN_SYNC;
  controller_.notMatched();

  controller_.setActorState(controller_.actorState() | LacpState::EXPIRED);

  startNextEpoch(fastEpochSeconds_);
}

void ReceiveMachine::disabled() {
  updateState(ReceiveState::DISABLED);

  endThisEpoch();

  partnerInfo_.state &= ~LacpState::IN_SYNC;
  controller_.notMatched();
}

void ReceiveMachine::updateSelected(LACPDU& lacpdu) {
  if (lacpdu.actorInfo.systemID == partnerInfo_.systemID &&
      lacpdu.actorInfo.systemPriority == partnerInfo_.systemPriority &&
      lacpdu.actorInfo.key == partnerInfo_.key &&
      lacpdu.actorInfo.portPriority == partnerInfo_.portPriority &&
      lacpdu.actorInfo.port == partnerInfo_.port &&
      (lacpdu.actorInfo.state & LacpState::AGGREGATABLE) ==
          (partnerInfo_.state & LacpState::AGGREGATABLE)) {
    return;
  }
  XLOG(DBG4) << "ReceiveMachine[" << controller_.portID()
             << "]: " << "Partner claimed " << lacpdu.actorInfo.describe()
             << " but I have " << partnerInfo_.describe();
  controller_.unselected();
}

void ReceiveMachine::updateDefaultSelected() {
  auto adminDefault = ParticipantInfo();

  if (adminDefault != controller_.partnerInfo()) {
    controller_.unselected();
  }
}

bool ReceiveMachine::updateNTT(LACPDU& lacpdu) {
  // TODO(samank): skip over DEFAULTED, EXPIRED, COLLECTING, DISTRIBUTING?
  return lacpdu.partnerInfo != controller_.actorInfo();
}

void ReceiveMachine::current(LACPDU lacpdu) {
  updateState(ReceiveState::CURRENT);

  updateSelected(lacpdu);

  bool ntt = updateNTT(lacpdu);

  auto partnerStateChanged = [&](LacpState flag) {
    return (lacpdu.actorInfo.state & flag) != (partnerInfo_.state & flag);
  };
  bool transmissionRateChanged = partnerStateChanged(LacpState::LACP_ACTIVE) ||
      partnerStateChanged(LacpState::SHORT_TIMEOUT);

  recordPDU(lacpdu);

  controller_.setActorState(controller_.actorState() & ~LacpState::EXPIRED);

  controller_.select();

  if (ntt) {
    controller_.ntt();
  }

  if (transmissionRateChanged) {
    controller_.startPeriodicTransmissionMachine();
  }

  startNextEpoch(epochDuration());
}

std::chrono::seconds ReceiveMachine::epochDuration() {
  return controller_.actorInfo().state & LacpState::SHORT_TIMEOUT
      ? fastEpochSeconds_
      : slowEpochSeconds_;
}

void ReceiveMachine::recordPDU(LACPDU& lacpdu) {
  bool partnerChanged = partnerInfo_ != lacpdu.actorInfo;
  // TODO(samank): needs to be moved after
  controller_.setActorState(controller_.actorState() & ~LacpState::DEFAULTED);

  bool matched = lacpdu.partnerInfo.systemPriority ==
          controller_.actorInfo().systemPriority &&
      lacpdu.partnerInfo.systemID == controller_.actorInfo().systemID &&
      lacpdu.partnerInfo.key == controller_.actorInfo().key &&
      lacpdu.partnerInfo.portPriority == controller_.actorInfo().portPriority &&
      lacpdu.partnerInfo.port == controller_.actorInfo().port;

  if ((matched || ((lacpdu.actorInfo.state & LacpState::AGGREGATABLE) == 0)) &&
      (lacpdu.actorInfo.state & LacpState::IN_SYNC)) {
    partnerInfo_ = lacpdu.actorInfo;
    partnerInfo_.state |= LacpState::IN_SYNC;
    controller_.matched(partnerChanged);
  } else {
    // if partner transitions out of sync, LACP will disable fwding on the port
    if (partnerInfo_.state & LacpState::IN_SYNC) {
      XLOG(WARNING) << PortAlert() << "ReceiveMachine[" << controller_.portID()
                    << "]: " << "Partner not in sync. Got LACP PDU ("
                    << lacpdu.describe() << ") Previous State ("
                    << partnerInfo_.describe() << ")";
      servicer_->recordLacpMismatchPduTeardown();
    }
    partnerInfo_ = lacpdu.actorInfo;
    partnerInfo_.state &= ~LacpState::IN_SYNC;
    controller_.notMatched();
  }
}

void ReceiveMachine::startNextEpoch(std::chrono::seconds duration) {
  scheduleTimeout(duration);
}

void ReceiveMachine::endThisEpoch() {
  cancelTimeout();
}

void ReceiveMachine::timeoutExpired() noexcept {
  try {
    switch (state_) {
      case ReceiveState::CURRENT:
        XLOG(WARNING) << PortAlert() << "ReceiveMachine["
                      << controller_.portID()
                      << "]: RX timer expired in CURRENT state";
        expired();
        servicer_->recordLacpTimeout();
        break;
      case ReceiveState::EXPIRED:
        defaulted();
        break;
      case ReceiveState::DISABLED:
      case ReceiveState::INITIALIZED:
      case ReceiveState::DEFAULTED:
        throw LACPError("invalid transition to ", state_);
    }
  } catch (...) {
    std::exception_ptr e = std::current_exception();
    CHECK(e);
    XLOG(FATAL) << "ReceiveMachine::timeoutExpired(): "
                << folly::exceptionStr(e);
  }
}

void ReceiveMachine::recordDefault() {
  partnerInfo_ = ParticipantInfo();
  controller_.setActorState(controller_.actorState() | LacpState::DEFAULTED);
}

// TODO(samank): compiler not seeing overlaod of ostream& operator<< in
// in LOG macros but does see it for CHECK macros...
void ReceiveMachine::updateState(ReceiveState nextState) {
  prevState_ = state_;
  state_ = nextState;
  XLOG(DBG4) << "ReceiveMachine[" << controller_.portID() << "]: " << prevState_
             << "-->" << state_;
}

ParticipantInfo ReceiveMachine::partnerInfo() const {
  return partnerInfo_;
}

void ReceiveMachine::restoreState(AggregatePort::PartnerState partnerState) {
  updateState(ReceiveState::CURRENT);
  partnerInfo_ = partnerState;
  controller_.setActorState(
      controller_.actorState() | LacpState::DISTRIBUTING |
      LacpState::COLLECTING | LacpState::IN_SYNC);
  XLOG(DBG3) << "ReceiveMachine[" << controller_.portID()
             << "]: restored actor state " << controller_.actorState();
  startNextEpoch(epochDuration());
}

const std::chrono::seconds PeriodicTransmissionMachine::SHORT_PERIOD(1);
const std::chrono::seconds PeriodicTransmissionMachine::LONG_PERIOD(30);

PeriodicTransmissionMachine::PeriodicTransmissionMachine(
    LacpController& controller,
    folly::EventBase* evb)
    : folly::AsyncTimeout(evb), controller_(controller) {}

PeriodicTransmissionMachine::~PeriodicTransmissionMachine() {}

void PeriodicTransmissionMachine::start() {
  state_ = determineTransmissionRate();
  beginNextPeriod();
}

void PeriodicTransmissionMachine::stop() {
  cancelTimeout();
}

void PeriodicTransmissionMachine::portUp() {
  // TODO(samank): start vs portUp
  CHECK(controller_.evb()->inRunningEventBaseThread());

  state_ = determineTransmissionRate();
  beginNextPeriod();
}

void PeriodicTransmissionMachine::portDown() {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  cancelTimeout();
}

void PeriodicTransmissionMachine::beginNextPeriod() {
  switch (state_) {
    case PeriodicState::SLOW:
      XLOG(DBG4) << "PeriodicTransmissionMachine[" << controller_.portID()
                 << "]: scheduling timeout for long period";
      scheduleTimeout(LONG_PERIOD);
      break;
    case PeriodicState::FAST:
      XLOG(DBG4) << "PeriodicTransmissionMachine[" << controller_.portID()
                 << "]: scheduling timeout for short period";
      scheduleTimeout(SHORT_PERIOD);
      break;
    case PeriodicState::NONE:
      XLOG(DBG4) << "PeriodicTransmissionMachine[" << controller_.portID()
                 << "]: not scheduling a timeout";
      break;
    case PeriodicState::TX:
      throw LACPError("invalid transition to ", state_);
  }
}

void PeriodicTransmissionMachine::timeoutExpired() noexcept {
  try {
    XLOG(DBG4) << "PeriodicTransmissionMachine[" << controller_.portID()
               << "]: end of period";

    state_ = PeriodicState::TX;

    controller_.ntt();

    state_ = determineTransmissionRate();

    beginNextPeriod();
  } catch (...) {
    std::exception_ptr e = std::current_exception();
    CHECK(e);
    XLOG(FATAL) << "PeriodicTranmissionMachine::timeoutExpired(): "
                << folly::exceptionStr(e);
  }
}

PeriodicTransmissionMachine::PeriodicState
PeriodicTransmissionMachine::determineTransmissionRate() {
  auto actorInfo = controller_.actorInfo();
  auto partnerInfo = controller_.partnerInfo();

  if ((actorInfo.state & LacpState::LACP_ACTIVE) == 0 &&
      (partnerInfo.state & LacpState::LACP_ACTIVE) == 0) {
    return PeriodicState::NONE;
  }
  // If last LACPDU transmission was unsuccessful, we should not
  // wait for SLOW interval to transmit again, instead we should
  // transmit immediately. This will help in faster convergence
  // durin warmboot
  if (!controller_.getLacpLastTransmissionResult()) {
    return PeriodicState::FAST;
  }

  return controller_.partnerInfo().state & LacpState::SHORT_TIMEOUT
      ? PeriodicState::FAST
      : PeriodicState::SLOW;
}

std::chrono::seconds PeriodicTransmissionMachine::getCurrentTransmissionPeriod()
    const {
  return state_ == PeriodicState::FAST ? SHORT_PERIOD : LONG_PERIOD;
}

const std::chrono::seconds TransmitMachine::TX_REPLENISH_RATE(1);
const int TransmitMachine::MAX_TRANSMISSIONS_IN_SHORT_PERIOD = 3;

TransmitMachine::TransmitMachine(
    LacpController& controller,
    folly::EventBase* evb,
    LacpServicerIf* servicer)
    : folly::AsyncTimeout(evb), controller_(controller), servicer_(servicer) {}

TransmitMachine::~TransmitMachine() {}

void TransmitMachine::start() {
  scheduleTimeout(TransmitMachine::TX_REPLENISH_RATE);
}

void TransmitMachine::stop() {
  cancelTimeout();
}

void TransmitMachine::timeoutExpired() noexcept {
  replenishTranmissionsLeft();
}

void TransmitMachine::replenishTranmissionsLeft() noexcept {
  transmissionsLeft_ = std::min(
      transmissionsLeft_ + 1,
      TransmitMachine::MAX_TRANSMISSIONS_IN_SHORT_PERIOD);
  scheduleTimeout(TransmitMachine::TX_REPLENISH_RATE);
}

void TransmitMachine::ntt(LACPDU lacpdu) {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  if (transmissionsLeft_ == 0) {
    // TODO(samank): figure out stale ntt details
    XLOG(DBG4) << "TransmitMachine[" << controller_.portID()
               << "]: " << "skipping ntt request";
    return;
  }

  auto outPort = controller_.portID();
  if (!servicer_->transmit(lacpdu, outPort)) {
    isLastTransmissionSuccessful_ = false;
    return;
  }
  isLastTransmissionSuccessful_ = true;

  XLOG(DBG4) << "TransmitMachine[" << controller_.portID() << "]: " << "TX("
             << lacpdu.describe() << ")";

  --transmissionsLeft_;
  XLOG(DBG4) << transmissionsLeft_ << " transmissions left";
}

bool TransmitMachine::getLacpLastTransmissionResult() const {
  return isLastTransmissionSuccessful_;
}

const std::chrono::seconds MuxMachine::AGGREGATE_WAIT_DURATION(2);
MuxMachine::MuxMachine(
    LacpController& controller,
    folly::EventBase* evb,
    LacpServicerIf* servicer)
    : AsyncTimeout(evb), controller_(controller), servicer_(servicer) {}

MuxMachine::~MuxMachine() {}

void MuxMachine::selected(AggregatePortID selection) {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  selection_ = selection;

  switch (state_) {
    case MuxState::DETACHED:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: SELECTED in "
                 << state_;
      waiting(true);
      break;
    case MuxState::WAITING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: SELECTED in "
                 << state_;
      attached();
      break;
    case MuxState::ATTACHED:
    case MuxState::COLLECTING_DISTRIBUTING:
      XLOG(WARNING) << "MuxMachine[" << controller_.portID()
                    << "]: Ignoring SELECTED in " << state_;
      break;
  }
}

void MuxMachine::unselected() {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  switch (state_) {
    case MuxState::DETACHED:
      XLOG(WARNING) << "MuxMachine[" << controller_.portID()
                    << "]: Ignoring UNSELECTED in " << state_;
      break;
    case MuxState::WAITING:
    case MuxState::ATTACHED:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: UNSELECTED in "
                 << state_;
      detached();
      break;
    case MuxState::COLLECTING_DISTRIBUTING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: UNSELECTED in "
                 << state_;
      attached();
      break;
  }
}

void MuxMachine::standby() {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  switch (state_) {
    case MuxState::DETACHED:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: STANDBY in "
                 << state_;
      waiting(false);
      break;
    case MuxState::WAITING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID()
                 << "]: Ignoring STANDBY in " << state_;
      break;
    case MuxState::ATTACHED:
    case MuxState::COLLECTING_DISTRIBUTING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: STANDBY in "
                 << state_;
      detached();
      break;
  }
}

void MuxMachine::matched(bool partnerChanged) {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  matched_ = true;

  switch (state_) {
    case MuxState::DETACHED:
    case MuxState::WAITING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID()
                 << "]: Ignoring MATCHED in " << state_;
      break;
    case MuxState::COLLECTING_DISTRIBUTING:
      if (partnerChanged) {
        XLOG(DBG4) << "MuxMachine[" << controller_.portID()
                   << "]: MATCHED partner changed in " << state_;
        enableCollectingDistributing();
      } else {
        XLOG(DBG4) << "MuxMachine[" << controller_.portID()
                   << "]: Ignoring MATCHED in " << state_;
      }
      break;
    case MuxState::ATTACHED:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: MATCHED in "
                 << state_;
      collectingDistributing();
      break;
  }
}

void MuxMachine::notMatched() {
  CHECK(controller_.evb()->inRunningEventBaseThread());

  matched_ = false;

  switch (state_) {
    case MuxState::DETACHED:
    case MuxState::WAITING:
    case MuxState::ATTACHED:
      XLOG(WARNING) << "MuxMachine[" << controller_.portID()
                    << "]: Ignoring NOT MATCHED in " << state_;
      break;
    case MuxState::COLLECTING_DISTRIBUTING:
      XLOG(DBG4) << "MuxMachine[" << controller_.portID()
                 << "]: NOT MATCHED in " << state_;
      attached();
      break;
  }
}

void MuxMachine::start() {}

void MuxMachine::stop() {}

void MuxMachine::enableCollectingDistributing() const {
  auto portID = controller_.portID();
  auto aggPortID = selection_;

  /* In testing, we've found that it can take up to 100 ms _after_ a link-up
   * interrupt has been issued for the link to actually be usable (ie. able to
   * transmit and receive frames). Thus, immediately expanding the LAG group
   * to include the newly-up port can result in packet loss.
   * Packet loss of this nature is avoided in the case of dynamic link
   * aggregation by an application of fate-sharing: instead of immediately
   * expanding the LAG group, the LACP machinery is first notified of the event,
   * resulting in a LAC PDU exchange between the local interface and the remote
   * interface. Once the LACP handshake has completed, the LAG group is expanded
   * to include the newly-up port. Since the link must have been able to
   * transmit and receive to complete the handshake, expanding the LAG group
   * afterwards must avoid packet loss. Conversely, if the LACP handshake does
   * not complete, whether due to the link not yet being usable or due to normal
   * LACP operation, then we would not want to add the newly-up port to the LAG
   * group, as dictated by LACP.
   */
  servicer_->enableForwardingAndSetPartnerState(
      portID, aggPortID, controller_.partnerInfo());
}

void MuxMachine::disableCollectingDistributing() const {
  auto portID = controller_.portID();
  auto aggPortID = selection_;

  servicer_->disableForwardingAndSetPartnerState(
      portID, aggPortID, controller_.partnerInfo());
}

void MuxMachine::detached() {
  updateState(MuxState::DETACHED);

  // Detach_Mux_From_Aggregator

  controller_.setActorState(
      controller_.actorState() & ~(LacpState::IN_SYNC | LacpState::COLLECTING));

  disableCollectingDistributing();

  controller_.setActorState(
      controller_.actorState() & ~LacpState::DISTRIBUTING);

  controller_.ntt();
}

void MuxMachine::waiting(bool shouldScheduleTimeout) {
  updateState(MuxState::WAITING);

  if (shouldScheduleTimeout) {
    scheduleTimeout(AGGREGATE_WAIT_DURATION);
  }
}

void MuxMachine::timeoutExpired() noexcept {
  try {
    attached();
    if (matched_) {
      collectingDistributing();
    }
  } catch (...) {
    std::exception_ptr e = std::current_exception();
    CHECK(e);
    XLOG(FATAL) << "MuxMachine::timeoutExpired(): " << folly::exceptionStr(e);
  }
}

void MuxMachine::attached() {
  updateState(MuxState::ATTACHED);

  // Attach_Mux_To_Aggregator

  // TODO(samank): this needs to change on port down for min-links
  controller_.setActorState(controller_.actorState() | LacpState::IN_SYNC);

  controller_.setActorState(controller_.actorState() & ~LacpState::COLLECTING);

  disableCollectingDistributing();

  controller_.setActorState(
      controller_.actorState() & ~LacpState::DISTRIBUTING);

  controller_.ntt();
}

void MuxMachine::collectingDistributing() {
  updateState(MuxState::COLLECTING_DISTRIBUTING);

  controller_.setActorState(controller_.actorState() | LacpState::DISTRIBUTING);

  enableCollectingDistributing();

  controller_.setActorState(controller_.actorState() | LacpState::COLLECTING);

  controller_.ntt();
}

void MuxMachine::updateState(MuxState nextState) {
  prevState_ = state_;
  state_ = nextState;

  XLOG(DBG4) << "MuxMachine[" << controller_.portID() << "]: " << prevState_
             << "-->" << state_;
}

void MuxMachine::restoreState(void) {
  updateState(MuxState::COLLECTING_DISTRIBUTING);
}

Selector::Selector(
    LacpController& controller,
    uint8_t minLinkCount,
    std::optional<uint8_t> minLinkCountToUp)
    : controller_(controller),
      minLinkCount_(minLinkCount),
      minLinkCountToUp_(minLinkCountToUp) {}

void Selector::start() {}

void Selector::stop() {}

// TODO(samank): Factor out into MininumLinksSelector
void Selector::select() {
  auto targetLagID = LinkAggregationGroupID::from(
      controller_.actorInfo(), controller_.partnerInfo());

  // If the Link Aggregation Group this port has SELECTED or STANDBYed is the
  // same as that identified by targetLagID, then there's nothing to do.
  auto maybeSelection = getSelectionIf();
  if (maybeSelection && maybeSelection->lagID == targetLagID) {
    XLOG(DBG4) << "Selection[" << controller_.portID()
               << "]: skipping selection logic";
    return;
  }

  auto chooseTargetLag = [this, targetLagID](SelectionState state) {
    Selector::portToSelection().insert(
        std::make_pair(controller_.portID(), Selection(targetLagID, state)));

    if (state == SelectionState::SELECTED) {
      XLOG(DBG4) << "Selection[" << controller_.portID() << "]: selected "
                 << targetLagID.describe();
      controller_.selected();
    } else {
      XLOG(DBG4) << "Selection[" << controller_.portID() << "]: standby "
                 << targetLagID.describe();
      controller_.standby();
    }
  };

  // If either the actor or the partner has specified it must operate as
  // INDIVIDUAL, we must select the Aggregator that uniquely belongs to it
  if ((controller_.partnerInfo().state & LacpState::AGGREGATABLE) == 0 ||
      (controller_.actorInfo().state & LacpState::AGGREGATABLE) == 0) {
    chooseTargetLag(SelectionState::SELECTED);
    return;
  }

  bool targetLagSelectedByOtherPort = false;
  for (const auto& portAndSelection : portToSelection()) {
    if (portAndSelection.second.lagID == targetLagID) {
      targetLagSelectedByOtherPort = true;
      break;
    }
  }

  bool chosen = false;
  if (targetLagSelectedByOtherPort) {
    chooseTargetLag(SelectionState::STANDBY);
    chosen = true;
  } else {
    // The target Link Aggregation Group can only be chosen if no other port has
    // SELECTED or STANDBYed a LAG with the same AggregatePortID (ie. the other
    // port's LinkAggregationGroupID.actorKey == controller_.aggregatePortID())
    if (isAvailable(controller_.aggregatePortID())) {
      chooseTargetLag(SelectionState::STANDBY);
      chosen = true;
    }
  }

  if (chosen) {
    auto targetLagMemberCount = std::count_if(
        portToSelection().begin(),
        portToSelection().end(),
        [targetLagID](PortIDToSelection::value_type el) {
          Selection s = el.second;
          return s.lagID == targetLagID;
        });

    auto minLinkCountToDown = minLinkCount_;
    auto minLinkCountToUp = minLinkCountToUp_.value_or(minLinkCount_);
    auto portsToSignal =
        getPortsWithSelection(Selection(targetLagID, SelectionState::STANDBY));
    // If count of selected or standby ports is more than minLinkCountToUp,
    // Put all STANDBY ports into SELECTED state.
    if (targetLagMemberCount >= minLinkCountToUp) {
      controller_.selected(
          folly::range(portsToSignal.begin(), portsToSignal.end()));
    } else {
      // If count of selected ports is >= minLinkCountToDown, then the LAG is in
      // UP state. Also Put all STANDBY ports into SELECTED state.
      auto selectedPortsCount =
          getPortsWithSelection(
              Selection(targetLagID, SelectionState::SELECTED))
              .size();
      if (selectedPortsCount >= minLinkCountToDown) {
        controller_.selected(
            folly::range(portsToSignal.begin(), portsToSignal.end()));
      }
    }
  }

  // TODO(samank): aggregate with the nil aggregator
}

Selector::Selection Selector::getSelection() {
  auto it = portToSelection().find(controller_.portID());
  if (it == portToSelection().end()) {
    throw LACPError();
  }
  return it->second;
}

std::optional<Selector::Selection> Selector::getSelectionIf() {
  auto it = portToSelection().find(controller_.portID());
  if (it == portToSelection().end()) {
    return std::nullopt;
  }
  return it->second;
}

bool Selector::isAvailable(AggregatePortID aggPortID) const {
  for (const auto& portAndSelection : portToSelection()) {
    Selection s = portAndSelection.second;
    if (static_cast<AggregatePortID>(s.lagID.actorKey) == aggPortID) {
      return false;
    }
  }
  return true;
}

Selector::PortIDToSelection& Selector::portToSelection() {
  static Selector::PortIDToSelection portToSelection_;
  return portToSelection_;
}

std::vector<PortID> Selector::getPortsWithSelection(Selection s) const {
  std::vector<PortID> ports;

  for (const auto& portIDAndSelection : portToSelection()) {
    auto portID = portIDAndSelection.first;
    auto selection = portIDAndSelection.second;
    if (selection == s) {
      ports.push_back(portID);
    }
  }

  return ports;
}

void Selector::portDown() {
  auto maybeSelection = getSelectionIf();
  if (!maybeSelection) {
    XLOG(DBG4) << "Selection[" << controller_.portID() << "]: no LAG chosen";
    return;
  }

  // ReceiveMachine::portDown() was invoked prior to this, which will eventually
  // signal NOT partner.SYNC. If the MuxMachine was in state
  // MuxState::COLLECTING_DISTRIBUTING, then it would have transitioned to
  // MuxMachine::ATTACHED on this signal. Signalling UNSELECTED at this point
  // will transition it to MuxMachine::DETACHED.
  controller_.unselected();
}

void Selector::unselected() {
  auto it = portToSelection().find(controller_.portID());
  if (it == portToSelection().end()) {
    return;
  }

  LinkAggregationGroupID myLagID = it->second.lagID;

  portToSelection().erase(it);

  auto ports =
      getPortsWithSelection(Selection(myLagID, SelectionState::SELECTED));

  // If two thresholds [minLinkCount_, minLinkCountToUp_] are configured for
  // min-links check, we still only need to put all selected ports into STANDBY
  // state if the low threshold is not met.
  if (ports.size() < minLinkCount_) {
    controller_.standby(folly::range(ports.begin(), ports.end()));
  } else {
    controller_.standby();
  }
}

void Selector::selected() {
  auto it = portToSelection().find(controller_.portID());
  CHECK(it != portToSelection().end());
  it->second.state = SelectionState::SELECTED;
}

void Selector::standby() {
  auto it = portToSelection().find(controller_.portID());
  if (it == portToSelection().end()) {
    return;
  }

  it->second.state = SelectionState::STANDBY;
}

void Selector::restoreState() {
  auto targetLagID = LinkAggregationGroupID::from(
      controller_.actorInfo(), controller_.partnerInfo());

  Selector::portToSelection().insert(
      std::make_pair(
          controller_.portID(),
          Selection(targetLagID, SelectionState::SELECTED)));

  XLOG(DBG4) << "Selection[" << controller_.portID() << "]: selected "
             << targetLagID.describe();
  controller_.selected();
}

} // namespace facebook::fboss
