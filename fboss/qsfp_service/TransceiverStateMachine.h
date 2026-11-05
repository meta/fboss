/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/agent/FbossError.h"
#include "fboss/agent/types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"
#include "fboss/qsfp_service/module/Transceiver.h"

#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/euml/euml.hpp>
#include <boost/msm/front/euml/state_grammar.hpp>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/qsfp_service/TransceiverLogging.h"

DECLARE_bool(use_new_state_machine);
DECLARE_bool(port_manager_mode);

// State machine log macros, similar to MODULE_LOG in TransceiverManager.h.
// Assumes `fsm` is in scope (all state machine actions/helpers have it).
// tcvrID is already a TransceiverID in the state machine context.
#define TCVR_SM_LOG(level, tcvrID) TCVR_LOG_BASE(level, "[SM]", tcvrID)

#define TCVR_SM_LOG_IF(level, cond, tcvrID) \
  TCVR_LOG_BASE_IF(level, cond, "[SM]", tcvrID)

namespace facebook::fboss {

using namespace boost::msm::front::euml;
using namespace boost::msm::back;

class TransceiverManager;

/*
 * Convert current_state() return int value to TransceiverStateMachineState
 * enum. Every state is an int value assigned in the order the states have been
 * accessed in BOOST_MSM_EUML_TRANSITION_TABLE.
 */
TransceiverStateMachineState getStateByOrder(int currentStateOrder);

/**************************** Transceiver State Machine ***********************/
// Module state machine attribute to tell whether the iphy port/ xphy port/
// transceiver programmed is already done
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, isIphyProgrammed)
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, isXphyProgrammed)
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, isTransceiverProgrammed)
// Use this bool value to gate whether we need to update the last down time
// so that we'll remediate the transceiver accordingly. Right now, we only
// need to update the lastDownTime_ of such Transceiver:
// 1) First time when we transfer from TRANSCEIVER_PROGRAMMED to INACTIVE
// 2) Every time when we transfer from ACTIVE to INACTIVE
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, needMarkLastDownTime)

// Module State Machine needs to trigger iphy/xphy/transceiver programming.
// Instead of adding a lot of complicated logic here, we can use
// TransceiverManager to provide a bunch public functions there.
// Besides, we might also need to call TransceiverManager::updateState() to
// update the state accordingly in the state machine.
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(class TransceiverManager*, transceiverMgrPtr)
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(TransceiverID, transceiverID)

// Use this bool value to gate whether we need to reset the data path for
// xphy/tcvr.
// Some of the high speed optics will stuck in a bad state if iphy reset during
// agent coldboot because iphy port might send some unstable signals during the
// coldboot. Therefore, we need to reset the data path for xphy/tcvr after
// agent coldboot and reset the iphy.
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, needResetDataPath)

BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, needToResetToDiscovered)

BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, newTransceiverInsertedAfterInit)

BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, forceRemoveTransceiver)

// Use this value in PortManager mode to know when a transceiver was just
// remediated, and consequently skip resetting port states.
BOOST_MSM_EUML_DECLARE_ATTRIBUTE(bool, isTransceiverJustRemediated)

// Helper function to encapsulate the resetProgrammingAttributes logic
template <class Fsm, class State>
static void executeResetProgrammingAttributes(Fsm& fsm, State& currState) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(currState))
      << ". Need to reset all programming attributes";
  fsm.get_attribute(isIphyProgrammed) = false;
  fsm.get_attribute(isXphyProgrammed) = false;
  fsm.get_attribute(isTransceiverProgrammed) = false;
  fsm.get_attribute(needMarkLastDownTime) = true;
  fsm.get_attribute(needToResetToDiscovered) = false;
  fsm.get_attribute(forceRemoveTransceiver) = false;

  if (!FLAGS_port_manager_mode) {
    // Port Manager will handle resetting port information when ports are
    // enabled / disabled.
    fsm.get_attribute(transceiverMgrPtr)
        ->resetProgrammedIphyPortToPortInfo(tcvrID);
  }
}

// clang-format off
BOOST_MSM_EUML_ACTION(resetToNotPresentProgrammingAttributes) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  executeResetProgrammingAttributes(fsm, currState);
  fsm.get_attribute(isTransceiverJustRemediated) = false;
  fsm.get_attribute(transceiverMgrPtr)
      ->markTransceiverReadyForProgramming(
          tcvrID, false /* transceiverReady */);
}
};

BOOST_MSM_EUML_ACTION(resetToDiscoveredProgrammingAttributes) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  executeResetProgrammingAttributes(fsm, currState);
  if (fsm.get_attribute(isTransceiverJustRemediated)) {
    // During Port Manager mode remediation, don't reset needMarkLastDownTime.
    // executeResetProgrammingAttributes unconditionally sets it to true, but
    // in the remediation path (TRANSCEIVER_PROGRAMMED -> DISCOVERED) we must
    // preserve the false value to match traditional mode behavior where
    // remediation goes through XPHY_PORTS_PROGRAMMED (which has no entry
    // action that resets the flag). This prevents lastDownTime from being
    // re-stamped on each failed remediation cycle.
    fsm.get_attribute(needMarkLastDownTime) = false;
  } else {
    // If the transceiver isn't just remediated, we can clear this attribute.
    fsm.get_attribute(transceiverMgrPtr)
        ->markTransceiverReadyForProgramming(
            fsm.get_attribute(transceiverID), false /* transceiverReady */);
  }
}
};


BOOST_MSM_EUML_ACTION(markLastDownTime) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  if (!fsm.get_attribute(needMarkLastDownTime)) {
    return;
  }
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(currState))
      << ". Need to mark lastDownTime to current";
  fsm.get_attribute(transceiverMgrPtr)->markLastDownTime(tcvrID);
  // Change needMarkLastDownTime false so that if there's not actual port up
  // during the two INACTIVE states, we don't have to update lastDownTime.
  // Because remediation needs to change the state back to
  // TRANSCEIVER_PROGRAMMED and it might be still not able to bring up the port
  // and the state will be in the INACTIVE again, we don't want to update the
  // lastDownTime to the new one since the port is still down during this
  // remediation
  fsm.get_attribute(needMarkLastDownTime) = false;
}
};

BOOST_MSM_EUML_ACTION(activeStateEntry) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(currState))
      << ". Update needMarkLastDownTime to true";
  fsm.get_attribute(needMarkLastDownTime) = true;
}
};

BOOST_MSM_EUML_ACTION(upgradingStateEntry) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(currState));
  try {
    fsm.get_attribute(transceiverMgrPtr)->doTransceiverFirmwareUpgrade(tcvrID);
  } catch (const std::exception& ex) {
    TCVR_SM_LOG(ERR, tcvrID)
        << "firmware upgrade failed with: " << ex.what();
  }
  fsm.get_attribute(needToResetToDiscovered) = true;
}
};

BOOST_MSM_EUML_ACTION(presentStateEntry) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& currState) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  // If Transceiver Manager has been fully Initialized (i.e. has had one full run of refreshStateMachines)
  // and then a transceiver goes into Present state, this means that it was just inserted.
  // If not, we could enter Present state for existing modules right after warm boot or cold boot
  auto newTcvrInsertedAfterInit = fsm.get_attribute(transceiverMgrPtr)->isFullyInitialized();
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(currState))
      << ". New transceiver inserted = " << newTcvrInsertedAfterInit;
  fsm.get_attribute(newTransceiverInsertedAfterInit) = newTcvrInsertedAfterInit;
}
};

BOOST_MSM_EUML_ACTION(clearTransceiverJustRemediated) {
template <class Event, class Fsm, class State>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    State& /* currState */) const {
  fsm.get_attribute(isTransceiverJustRemediated) = false;
  }
};
// clang-format on

// Transceiver State Machine States
BOOST_MSM_EUML_STATE((resetToNotPresentProgrammingAttributes), NOT_PRESENT)
BOOST_MSM_EUML_STATE((presentStateEntry), PRESENT)
BOOST_MSM_EUML_STATE((resetToDiscoveredProgrammingAttributes), DISCOVERED)
BOOST_MSM_EUML_STATE((), IPHY_PORTS_PROGRAMMED)
BOOST_MSM_EUML_STATE((), XPHY_PORTS_PROGRAMMED)
BOOST_MSM_EUML_STATE((clearTransceiverJustRemediated), TRANSCEIVER_PROGRAMMED)
BOOST_MSM_EUML_STATE((activeStateEntry), ACTIVE)
BOOST_MSM_EUML_STATE((markLastDownTime), INACTIVE)
BOOST_MSM_EUML_STATE((upgradingStateEntry), UPGRADING)
BOOST_MSM_EUML_STATE((), TRANSCEIVER_READY)

// Transceiver State Machine Events
BOOST_MSM_EUML_EVENT(DETECT_TRANSCEIVER)
BOOST_MSM_EUML_EVENT(READ_EEPROM)
BOOST_MSM_EUML_EVENT(PROGRAM_IPHY)
BOOST_MSM_EUML_EVENT(PROGRAM_XPHY)
BOOST_MSM_EUML_EVENT(PROGRAM_TRANSCEIVER)
BOOST_MSM_EUML_EVENT(PORT_UP)
BOOST_MSM_EUML_EVENT(ALL_PORTS_DOWN)
// Reset transceiver will reset back to NOT_PRESENT
BOOST_MSM_EUML_EVENT(RESET_TO_NOT_PRESENT)
// Reset to discover doesn't need another present detection or eeprom read
// Usually uses on a present transceiver
BOOST_MSM_EUML_EVENT(RESET_TO_DISCOVERED)
BOOST_MSM_EUML_EVENT(REMOVE_TRANSCEIVER)
// Remediate transceiver will reset back to XPHY_PORTS_PROGRAMMED so that we'll
// trigger a `PROGRAM_TRANSCEIVER` later
BOOST_MSM_EUML_EVENT(REMEDIATE_TRANSCEIVER)
BOOST_MSM_EUML_EVENT(PREPARE_TRANSCEIVER)
BOOST_MSM_EUML_EVENT(UPGRADE_FIRMWARE)

// Module State Machine Actions
template <class State>
TransceiverStateMachineState stateToStateEnum(State& /* state */) {
  if constexpr (std::is_same_v<State, decltype(NOT_PRESENT)>) {
    return TransceiverStateMachineState::NOT_PRESENT;
  } else if constexpr (std::is_same_v<State, decltype(PRESENT)>) {
    return TransceiverStateMachineState::PRESENT;
  } else if constexpr (std::is_same_v<State, decltype(DISCOVERED)>) {
    return TransceiverStateMachineState::DISCOVERED;
  } else if constexpr (std::is_same_v<State, decltype(IPHY_PORTS_PROGRAMMED)>) {
    return TransceiverStateMachineState::IPHY_PORTS_PROGRAMMED;
  } else if constexpr (std::is_same_v<State, decltype(XPHY_PORTS_PROGRAMMED)>) {
    return TransceiverStateMachineState::XPHY_PORTS_PROGRAMMED;
  } else if constexpr (std::is_same_v<State, decltype(TRANSCEIVER_READY)>) {
    return TransceiverStateMachineState::TRANSCEIVER_READY;
  } else if constexpr (std::
                           is_same_v<State, decltype(TRANSCEIVER_PROGRAMMED)>) {
    return TransceiverStateMachineState::TRANSCEIVER_PROGRAMMED;
  } else if constexpr (std::is_same_v<State, decltype(ACTIVE)>) {
    return TransceiverStateMachineState::ACTIVE;
  } else if constexpr (std::is_same_v<State, decltype(INACTIVE)>) {
    return TransceiverStateMachineState::INACTIVE;
  } else if constexpr (std::is_same_v<State, decltype(UPGRADING)>) {
    return TransceiverStateMachineState::UPGRADING;
  }

  throw FbossError("Unsupported TransceiverStateMachineState");
}

// clang-format couldn't handle the marco in a pretty way. So manually turn off
// the auto formating
// clang-format off
BOOST_MSM_EUML_ACTION(logStateChanged){
template <class Event, class Fsm, class Source, class Target>
void operator()(
    const Event& /* event */,
    Fsm& fsm,
    Source& source,
    Target& target) const {
  auto tcvrID = fsm.get_attribute(transceiverID);
  TCVR_SM_LOG(DBG2, tcvrID)
      << "State changed from "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(source))
      << " to "
      << apache::thrift::util::enumNameSafe(stateToStateEnum(target));
}
};

BOOST_MSM_EUML_ACTION(discoverTransceiver){
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    auto tcvrMgr = fsm.get_attribute(transceiverMgrPtr);
    // After discovery check if the EEPROM content is correct by verifying the
    // checksum in various pages.
    // For now, we ONLY LOG the eeprom checksums fail.
    tcvrMgr->verifyEepromChecksumsLocked(tcvrID);
    tcvrMgr->setDiagsCapabilityLocked(tcvrID);
    return true;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "discover transceiver failed:" << folly::exceptionStr(ex);
    return false;
  }
}
};

BOOST_MSM_EUML_ACTION(programIphyPorts) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    fsm.get_attribute(transceiverMgrPtr)->programInternalPhyPorts(tcvrID);
    fsm.get_attribute(isIphyProgrammed) = true;
    return true;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "programInternalPhyPorts failed:" << folly::exceptionStr(ex);
    return false;
  }
}
};

BOOST_MSM_EUML_ACTION(programXphyPorts) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    fsm.get_attribute(transceiverMgrPtr)->programExternalPhyPorts(
      tcvrID, fsm.get_attribute(needResetDataPath));
    fsm.get_attribute(isXphyProgrammed) = true;
    return true;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "programExternalPhyPorts failed:" << folly::exceptionStr(ex);
    return false;
  }
}
};

// Helper function to encapsulate the common readyTransceiver logic
template <class Fsm>
static bool executeReadyTransceiver(Fsm& fsm) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    bool ready = fsm.get_attribute(transceiverMgrPtr)->readyTransceiver(tcvrID);
    if (!ready) {
      TCVR_SM_LOG(WARN, tcvrID)
          << "readyTransceiver returned False";
    } else {
      TCVR_SM_LOG(INFO, tcvrID)
          << "readyTransceiver returned True";
      fsm.get_attribute(transceiverMgrPtr)->checkPresentThenValidateTransceiver(tcvrID);
    }
    return ready;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "readyTransceiver failed with abort:" << folly::exceptionStr(ex);
    return false;
  }
}

BOOST_MSM_EUML_ACTION(readyTransceiver) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  return executeReadyTransceiver(fsm);
}
};

BOOST_MSM_EUML_ACTION(programTransceiver) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    fsm.get_attribute(transceiverMgrPtr)->programTransceiver(
      tcvrID, fsm.get_attribute(needResetDataPath));
    fsm.get_attribute(isTransceiverProgrammed) = true;
    fsm.get_attribute(needResetDataPath) = false;
    // Clear this flag because the transceiver is not considered
    // as new after it has been programmed
    fsm.get_attribute(newTransceiverInsertedAfterInit) = false;
    return true;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "programTransceiver failed:" << folly::exceptionStr(ex);
    return false;
  }
}
};

BOOST_MSM_EUML_ACTION(isSafeToRemove) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  // Current areAllPortsDown() returns false if there's no programmed ports
  // But for case like a present transceiver w/ only disabled ports, we should
  // still allow to remove such transceiver. And since this function should
  // be only called after the programmed stage, it should be safe to remove
  // a transceiver if there's no enabled/programmed port on it
  auto xcvrMgr = fsm.get_attribute(transceiverMgrPtr);
  bool isEnabled = !xcvrMgr->getProgrammedIphyPortToPortInfo(tcvrID).empty();

  bool result = false;
  if (fsm.get_attribute(forceRemoveTransceiver)) {
    TCVR_SM_LOG(INFO, tcvrID) << "force removed.";
    result = true;
    // Reset forceRemoveTransceiver. It will be set again prior to the next time its required
    fsm.get_attribute(forceRemoveTransceiver) = false;
  } else if (!isEnabled) {
    TCVR_SM_LOG(DBG2, tcvrID) << "No enabled ports. Safe to remove";
    result = true;
  } else {
    // If Port Manager mode is enabled, transceiver can come down and we have other mechanisms to ensure no port flaps from transceiver programming / upgrading.
    result = FLAGS_port_manager_mode || xcvrMgr->areAllPortsDown(tcvrID).first;
    TCVR_SM_LOG_IF(WARN, !result, tcvrID)
        << "Not all ports down. Not Safe to remove";
  }

  // If the state machine can proceed (result=true) we reset transceiver associated
  // port states in FSDB.
  if (result) {
    xcvrMgr->resetPortState(tcvrID);
  }
  return result;
}
};

template <class Fsm>
static bool executeTryRemediateTransceiver(Fsm& fsm) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  try {
    bool remediationDone =
      fsm.get_attribute(transceiverMgrPtr)->tryRemediateTransceiver(tcvrID);
    if (remediationDone) {
      // If remediation is done, we need to reprogram the transceiver
      fsm.get_attribute(isTransceiverProgrammed) = false;
    }
    return remediationDone;
  } catch (const std::exception& ex) {
    // We have retry mechanism to handle failure. No crash here
    TCVR_SM_LOG(WARN, tcvrID)
        << "tryRemediateTransceiver failed:" << folly::exceptionStr(ex);
    return false;
  }
}

BOOST_MSM_EUML_ACTION(tryRemediateTransceiver) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  return executeTryRemediateTransceiver(fsm);
}
};

BOOST_MSM_EUML_ACTION(isPortManagerModeAndTryRemediateTransceiver) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  if (!FLAGS_port_manager_mode) {
    return false;
  }
  bool remediationDone = executeTryRemediateTransceiver(fsm);
  // We can reprogram with the previous settings after remediation is done.
  if (remediationDone) {
    fsm.get_attribute(transceiverMgrPtr)->markTransceiverReadyForProgramming(fsm.get_attribute(transceiverID), true);
    fsm.get_attribute(isTransceiverJustRemediated) = true;
  }
  return remediationDone;
}
};

BOOST_MSM_EUML_ACTION(firmwareUpgradeRequired) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  auto tcvrID = fsm.get_attribute(transceiverID);
  return fsm.get_attribute(transceiverMgrPtr)->firmwareUpgradeRequired(tcvrID);
}
};


BOOST_MSM_EUML_ACTION(isPortManagerModeAndReadyTcvr) {
template <class Event, class Fsm, class Source, class Target>
bool operator()(
    const Event& /* ev */,
    Fsm& fsm,
    Source& /* src */,
    Target& /* trg */) {
  return FLAGS_port_manager_mode && executeReadyTransceiver(fsm);
}
};

// clang-format on

// Transceiver State Machine State transition table
// clang-format off
BOOST_MSM_EUML_TRANSITION_TABLE((
//  Start                  + Event                  [Guard]                    / Action          == Next
// +-------------------------------------------------------------------------------------------------------------+
    NOT_PRESENT            + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    // Only allow PRESENT state to READ_EEPROM
    PRESENT                + READ_EEPROM            [discoverTransceiver]      / logStateChanged == DISCOVERED,
    // For non-present transceiver, we still want to call port program in case optic is actually
    // inserted but just can't read the present state
    NOT_PRESENT            + PROGRAM_IPHY           [programIphyPorts]         / logStateChanged == IPHY_PORTS_PROGRAMMED,
    DISCOVERED             + PROGRAM_IPHY           [programIphyPorts]         / logStateChanged == IPHY_PORTS_PROGRAMMED,
    IPHY_PORTS_PROGRAMMED  + PROGRAM_XPHY           [programXphyPorts]         / logStateChanged == XPHY_PORTS_PROGRAMMED,
    // For non-xphy platform, we will program tcvr after programming iphy ports
    IPHY_PORTS_PROGRAMMED + PREPARE_TRANSCEIVER     [readyTransceiver]         / logStateChanged == TRANSCEIVER_READY,
    XPHY_PORTS_PROGRAMMED + PREPARE_TRANSCEIVER     [readyTransceiver]         / logStateChanged == TRANSCEIVER_READY,
    TRANSCEIVER_READY     + PROGRAM_TRANSCEIVER     [programTransceiver]       / logStateChanged == TRANSCEIVER_PROGRAMMED,
    // Only trigger port status events after TRANSCEIVER_PROGRAMMED
    TRANSCEIVER_PROGRAMMED + PORT_UP                                           / logStateChanged == ACTIVE,
    TRANSCEIVER_PROGRAMMED + ALL_PORTS_DOWN                                    / logStateChanged == INACTIVE,
    ACTIVE                 + ALL_PORTS_DOWN                                    / logStateChanged == INACTIVE,
    INACTIVE               + PORT_UP                                           / logStateChanged == ACTIVE,
    // Flip all stable states back to DISCOVERED state for RESET_TO_DISCOVERED event. This is for present transceivers
    ACTIVE                 + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    INACTIVE               + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    TRANSCEIVER_READY      + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    TRANSCEIVER_PROGRAMMED + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    XPHY_PORTS_PROGRAMMED  + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    IPHY_PORTS_PROGRAMMED  + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    // Flip all stable states back to NOT_PRESENT state for RESET_TO_NOT_PRESENT event
    ACTIVE                 + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    INACTIVE               + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    TRANSCEIVER_READY      + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    TRANSCEIVER_PROGRAMMED + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    XPHY_PORTS_PROGRAMMED  + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    IPHY_PORTS_PROGRAMMED  + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    UPGRADING              + RESET_TO_NOT_PRESENT                              / logStateChanged == NOT_PRESENT,
    // Remove transceiver only if all ports are down
    ACTIVE                 + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    INACTIVE               + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    TRANSCEIVER_READY      + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    TRANSCEIVER_PROGRAMMED + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    XPHY_PORTS_PROGRAMMED  + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    IPHY_PORTS_PROGRAMMED  + REMOVE_TRANSCEIVER     [isSafeToRemove]           / logStateChanged == NOT_PRESENT,
    DISCOVERED             + REMOVE_TRANSCEIVER                                / logStateChanged == NOT_PRESENT,
    PRESENT                + REMOVE_TRANSCEIVER                                / logStateChanged == NOT_PRESENT,
    UPGRADING              + REMOVE_TRANSCEIVER                                / logStateChanged == NOT_PRESENT,
    // Remediate transciever if all ports are down
    INACTIVE               + REMEDIATE_TRANSCEIVER  [tryRemediateTransceiver]  / logStateChanged == XPHY_PORTS_PROGRAMMED,
    // As we allow programming events for not present transceiver, we might have the transceiver finish all programming
    // events and then insert the new transceiver later. For such case, we need to execute the new DETECT_TRANSCEIVER
    // event so that we can trigger reprogramming w/ the new transceiver info.
    // NOTE: the boost state machine library will use the order of the state in this table to assign the integer
    // value order, which has been used in getStateByOrder(). So we don't want to add the following transitions
    // at the beginning of this table to avoid changing the original order.
    IPHY_PORTS_PROGRAMMED  + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    XPHY_PORTS_PROGRAMMED  + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    TRANSCEIVER_READY      + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    TRANSCEIVER_PROGRAMMED + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    INACTIVE               + DETECT_TRANSCEIVER                                / logStateChanged == PRESENT,
    // May need to remediate transciever if some ports are down
    ACTIVE                 + REMEDIATE_TRANSCEIVER  [tryRemediateTransceiver]  / logStateChanged == XPHY_PORTS_PROGRAMMED,
    INACTIVE               + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    ACTIVE                 + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    DISCOVERED             + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    IPHY_PORTS_PROGRAMMED  + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    XPHY_PORTS_PROGRAMMED  + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    TRANSCEIVER_READY      + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    TRANSCEIVER_PROGRAMMED + UPGRADE_FIRMWARE [firmwareUpgradeRequired]        / logStateChanged == UPGRADING,
    UPGRADING              + RESET_TO_DISCOVERED                               / logStateChanged == DISCOVERED,
    // For non-present transceiver, we still want to call transceiver programming in case optic is actually present.
    // In Port Manager mode, transceiver will skip PHY programming and proced directly to tcvr programming.
    NOT_PRESENT           + PREPARE_TRANSCEIVER [isPortManagerModeAndReadyTcvr] / logStateChanged == TRANSCEIVER_READY,
    DISCOVERED            + PREPARE_TRANSCEIVER [isPortManagerModeAndReadyTcvr] / logStateChanged == TRANSCEIVER_READY,
    // Accounting for state machine transition
    TRANSCEIVER_PROGRAMMED + REMEDIATE_TRANSCEIVER [isPortManagerModeAndTryRemediateTransceiver]  / logStateChanged == DISCOVERED
//  +------------------------------------------------------------------------------------------------------------+
    ), TransceiverTransitionTable)
// clang-format on

// Define a Transceiver State Machine
BOOST_MSM_EUML_DECLARE_STATE_MACHINE(
    (TransceiverTransitionTable,
     init_ << NOT_PRESENT,
     no_action,
     no_action,
     attributes_ << isIphyProgrammed << isXphyProgrammed
                 << isTransceiverProgrammed << transceiverMgrPtr
                 << transceiverID << needMarkLastDownTime << needResetDataPath
                 << needToResetToDiscovered << newTransceiverInsertedAfterInit
                 << forceRemoveTransceiver << isTransceiverJustRemediated),
    TransceiverStateMachine)

} // namespace facebook::fboss
