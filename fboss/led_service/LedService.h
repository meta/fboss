// Copyright 2021- Facebook. All rights reserved.
#pragma once

#include <gflags/gflags.h>
#include <string>
#include "fboss/led_service/LedManager.h"

namespace folly {
class EventBase;
}

namespace facebook::fboss {

/*
 * LedService
 *
 * The main class for handling the Led Service. The service subscribes to the
 * FSDB for Switch State updates and then manages LED as per updates.
 * Related classes:
 *    LedManager - To manage the LED based on FSDB updates
 */

class LedService {
 public:
  // Constructor / destructor
  LedService() {}
  ~LedService() {
    if (pLedManager_) {
      pLedManager_->stop();
    }
  }

  // Instantiates all data used by LED Service
  void kickStart();

  void updateLedStatus(
      std::map<short, LedManager::LedSwitchStateUpdate>& newSwitchState) {
    pLedManager_->updateLedStatus(newSwitchState);
  }

  void setExternalLedState(int32_t portNum, PortLedExternalState ledState) {
    auto evb = pLedManager_->getEventBase();
    if (!evb) {
      throw FbossError("Event base not available for Led Manager");
    }
    folly::via(evb).thenValue([=, this](auto&&) {
      pLedManager_->setExternalLedState(portNum, ledState);
    });
  }

  led::PortLedState getPortLedState(const std::string& swPortName) {
    auto evb = pLedManager_->getEventBase();
    if (!evb) {
      throw FbossError("Event base not available for Led Manager");
    }
    return folly::via(evb)
        .thenValue([=, this](auto&&) {
          return pLedManager_->getPortLedState(swPortName);
        })
        .get();
  }

  int64_t serviceRunningForMsec() {
    auto serviceRunTime = std::chrono::steady_clock::now() - serviceStartTime_;
    auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(serviceRunTime);
    return milliseconds.count();
  }

 private:
  // LED manager object
  std::unique_ptr<LedManager> pLedManager_;
  // Service bookeeping info
  std::chrono::steady_clock::time_point serviceStartTime_{};
};
} // namespace facebook::fboss
