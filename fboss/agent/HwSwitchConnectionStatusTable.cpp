/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/HwSwitchConnectionStatusTable.h"
#include "fboss/agent/AgentDirectoryUtil.h"
#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/SwSwitch.h"
#include "fboss/agent/SwitchStats.h"
#include "fboss/lib/CommonFileUtils.h"

#include <folly/logging/xlog.h>

namespace facebook::fboss {
void HwSwitchConnectionStatusTable::connected(SwitchID switchId) {
  {
    std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
    connectedSwitches_.insert(switchId);
  }
  // Waiters block on different thresholds and more than one can be blocked at
  // a time, so every connection is potentially the one that releases a waiter
  // and every waiter has to re-check.
  hwSwitchConnectedCV_.notify_all();
  auto switchIndex =
      sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
  sw_->stats()->hwAgentConnectionStatus(switchIndex, true /*connected*/);
}

bool HwSwitchConnectionStatusTable::disconnected(SwitchID switchId) {
  std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
  // Switch was not connected. This can happen if switch sends a
  // graceful restart notification and later thrift stream detects
  // a connection loss and notifies the disconnect.
  if (!connectedSwitches_.erase(switchId)) {
    return false;
  }
  // If there are no more active connections to HwSwitch, we cannot
  // apply any new state updates. Hence exit the SwSwitch.
  // In normal shutdown sequence, we exit SwSwitch first before shutting
  // down HwSwitch. Hence this condition can happen only if all HwSwitches crash
  if (connectedSwitches_.empty()) {
    XLOG(ERR)
        << "No active HwSwitch connections. Initiating shutdown and preventing warm boot on restart."
        << " Please investigate the cause of the hw agent restart";

    // Create cold boot marker files to prevent warm boot on next restart
    auto dirUtil = sw_->getDirUtil();

    // Create SW cold boot marker using touchFile
    touchFile(dirUtil->getSwColdBootOnceFile());
    XLOG(INFO) << "Created SW cold boot marker: "
               << dirUtil->getSwColdBootOnceFile();

    // Create HW cold boot markers for all switches
    auto switchIdToSwitchInfo =
        sw_->getSwitchInfoTable().getSwitchIdToSwitchInfo();
    for (const auto& [switchId_, switchInfo] : switchIdToSwitchInfo) {
      auto switchIndex = *switchInfo.switchIndex();
      touchFile(dirUtil->getHwColdBootOnceFile(switchIndex));
      XLOG(INFO) << "Created HW cold boot marker for switch " << switchIndex
                 << ": " << dirUtil->getHwColdBootOnceFile(switchIndex);
    }

    std::exit(EXIT_SUCCESS);
  }
  if (FLAGS_exit_for_any_hw_disconnect) {
    XLOG(FATAL)
        << "exit_for_any_hw_disconnect is enabled. Received disconnect from "
        << switchId << ".";
  }
  auto switchIndex =
      sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
  sw_->stats()->hwAgentConnectionStatus(switchIndex, false /*connected*/);
  return true;
}

bool HwSwitchConnectionStatusTable::gracefullyExited(SwitchID switchId) {
  std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
  if (!connectedSwitches_.erase(switchId)) {
    return false;
  }
  auto lastConnection = connectedSwitches_.empty();
  // As in disconnected(), the flag only guards against SwSwitch running on
  // with some HwSwitches gone; losing the last one exits SwSwitch anyway.
  if (!lastConnection && FLAGS_exit_for_any_hw_disconnect) {
    XLOG(FATAL)
        << "exit_for_any_hw_disconnect is enabled. Received graceful exit from "
        << switchId << ".";
  }
  lk.unlock();
  if (lastConnection) {
    // No cold boot markers: the HwSwitch will warm boot, so SwSwitch saves its
    // own warm boot state and exits the same way.
    XLOG(WARNING) << "HwSwitch " << switchId
                  << " exited gracefully and no HwSwitch is connected. "
                  << "Exiting SwSwitch with warm boot state";
    sw_->requestGracefulShutdown();
  }
  auto switchIndex =
      sw_->getSwitchInfoTable().getSwitchIndexFromSwitchId(switchId);
  sw_->stats()->hwAgentConnectionStatus(switchIndex, false /*connected*/);
  return true;
}

bool HwSwitchConnectionStatusTable::waitUntilHwSwitchConnected(
    size_t numSwitches) {
  std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
  auto enough = [this, numSwitches] {
    return connectedSwitches_.size() >= numSwitches;
  };
  if (enough()) {
    return true;
  }
  auto done = [&enough, this] { return enough() || connectionWaitCancelled_; };
  if (FLAGS_hw_agent_connection_timeout_ms != 0) {
    hwSwitchConnectedCV_.wait_for(
        lk,
        std::chrono::milliseconds(FLAGS_hw_agent_connection_timeout_ms),
        done);
    return enough() && !connectionWaitCancelled_;
  } else {
    // Wait forever for HwSwitch to connect
    hwSwitchConnectedCV_.wait(lk, done);
    return !connectionWaitCancelled_;
  }
}

void HwSwitchConnectionStatusTable::cancelWait() {
  {
    std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
    connectionWaitCancelled_ = true;
  }
  // Cancellation releases every waiter, and waiters blocked on different
  // connection counts can be waiting at once.
  hwSwitchConnectedCV_.notify_all();
}

int HwSwitchConnectionStatusTable::getConnectionStatus(SwitchID switchId) {
  std::unique_lock<std::mutex> lk(hwSwitchConnectedMutex_);
  return connectedSwitches_.find(switchId) != connectedSwitches_.end() ? 1 : 0;
}

} // namespace facebook::fboss
