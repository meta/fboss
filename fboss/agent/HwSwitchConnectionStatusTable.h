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
#include <memory>
#include <string>

#include "fboss/agent/types.h"

namespace facebook::fboss {

class SwSwitch;

class HwSwitchConnectionStatusTable {
 public:
  explicit HwSwitchConnectionStatusTable(SwSwitch* sw) : sw_(sw) {}
  void connected(SwitchID switchId);
  // Connection lost without an exit notification, i.e. the HwSwitch crashed.
  bool disconnected(SwitchID switchId);
  // The HwSwitch notified a graceful exit and saved its warm boot state.
  bool gracefullyExited(SwitchID switchId);
  // Blocks until at least numSwitches are connected. One is enough for a
  // caller that just needs somewhere to send to. A caller that fans an update
  // out to every switch has to ask for all of them, because a switch that has
  // not registered yet takes no part in the update and reports it cancelled
  // rather than applied or failed.
  bool waitUntilHwSwitchConnected(size_t numSwitches = 1);
  void cancelWait();
  int getConnectionStatus(SwitchID switchId);

 private:
  std::set<SwitchID> connectedSwitches_;
  std::condition_variable hwSwitchConnectedCV_;
  std::mutex hwSwitchConnectedMutex_;
  bool connectionWaitCancelled_{false};
  SwSwitch* sw_;
};
} // namespace facebook::fboss
