/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/qsfp_service/test/MockStateMachine.h"
#include "fboss/agent/FbossError.h"

namespace facebook::fboss {

using MockStateMachineController =
    StateMachineController<MockID, MockEvent, MockState, MockStateMachine>;

const std::string mockEventToString(MockEvent event) {
  std::unordered_map<MockEvent, std::string> mockEventMap = {
      {MockEvent::EVENT_1, "EVENT_1"},
      {MockEvent::EVENT_2, "EVENT_2"},
      {MockEvent::EVENT_3, "EVENT_3"},
  };

  if (mockEventMap.find(event) == mockEventMap.end()) {
    throw FbossError("MockEvent ", static_cast<int>(event), " doesn't exist.");
  }
  return mockEventMap[event];
}

MockState getMockStateByOrder(int currentStateOrder) {
  switch (currentStateOrder) {
    case 0:
      return MockState::STATE_1;
    case 1:
      return MockState::STATE_2;
    case 2:
      return MockState::STATE_3;
    default:
      throw FbossError("Unsupported MockState order: ", currentStateOrder);
  }
}

template <>
MockState MockStateMachineController::getStateByOrder(
    int currentStateOrder) const {
  switch (currentStateOrder) {
    case 0:
      return MockState::STATE_1;
    case 1:
      return MockState::STATE_2;
    case 2:
      return MockState::STATE_3;
    default:
      throw FbossError("Unsupported MockState order: ", currentStateOrder);
  }
}

template <>
std::string MockStateMachineController::getUpdateString(MockEvent event) const {
  return fmt::format(
      "[MockObj: {}, Event:{}]",
      fmt::underlying(id_),
      mockEventToString(event));
};

template <>
void MockStateMachineController::applyUpdate(MockEvent event) {
  auto lockedStateMachine = stateMachine_.wlock();

  switch (event) {
    case MockEvent::EVENT_1:
      lockedStateMachine->process_event(Event1);
      break;
    case MockEvent::EVENT_2:
      lockedStateMachine->process_event(Event2);
      break;
    case MockEvent::EVENT_3:
      lockedStateMachine->process_event(Event3);
      break;
    default:
      XLOG(ERR) << "Unsupported MockStateMachine for "
                << getUpdateString(event);
  }
};

template <>
void MockStateMachineController::logCurrentState(
    int curStateOrder,
    MockState state) const {
  XLOG(DBG4) << "Current mockObj:" << static_cast<int32_t>(id_)
             << ", state order:" << curStateOrder
             << ", state:" << static_cast<int>(state);
};

template <>
void MockStateMachineController::setStateMachineAttributes() {
  stateMachine_.withWLock(
      [&](auto& stateMachine) { stateMachine.get_attribute(mockID) = id_; });
}
} // namespace facebook::fboss
