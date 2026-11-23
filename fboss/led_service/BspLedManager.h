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

#include <folly/Synchronized.h>
#include <folly/io/async/EventBase.h>
#include <folly/logging/xlog.h>
#include <stdexcept>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/platforms/common/PlatformMappingUtils.h"
#include "fboss/led_service/LedManager.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"

namespace facebook::fboss {

/*
 * BspLedManager class definiton:
 *
 * The BspLedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class BspLedManager : public LedManager {
 public:
  BspLedManager();
  virtual ~BspLedManager() override {}

  // Initialize the system container and mapping
  template <typename ContainerType, typename MappingType>
  void init() {
    bspSystemContainer_ =
        BspGenericSystemContainer<ContainerType>::getInstance().get();
    bspSystemContainer_->createBspLedContainers();
    platformMapping_ = std::make_unique<MappingType>();

    // Subscribe to FSDB after initializing the platform mapping.
    subscribeToFsdb();
  }

  template <typename ContainerType>
  void init(PlatformType platformType) {
    bspSystemContainer_ =
        BspGenericSystemContainer<ContainerType>::getInstance().get();
    bspSystemContainer_->createBspLedContainers();
    platformMapping_ = utility::initPlatformMapping(platformType);
    if (!platformMapping_) {
      throw FbossError(
          "Failed to load platform mapping for ",
          static_cast<int>(platformType));
    }

    // Subscribe to FSDB after initializing the platform mapping.
    subscribeToFsdb();
  }

  bool blinkingSupported() const override {
    return true;
  }

  BspSystemContainer* getBspSystemContainer() const {
    return bspSystemContainer_;
  }

  std::set<int> getLedIdFromSwPort(
      uint32_t portId,
      cfg::PortProfileID portProfile) const;

  // Forbidden copy constructor and assignment operator
  BspLedManager(BspLedManager const&) = delete;
  BspLedManager& operator=(BspLedManager const&) = delete;

 protected:
  // System container to get LED controller
  BspSystemContainer* bspSystemContainer_{nullptr};

  virtual led::LedState calculateLedState(
      uint32_t portId,
      cfg::PortProfileID portProfile) const override;

  virtual void setLedState(
      uint32_t portId,
      cfg::PortProfileID portProfile,
      led::LedState ledState) override;

  std::set<led::LedState> getLedStateFromHW(uint32_t portId) const override;

  std::vector<uint32_t> getCommonLedSwPorts(
      uint32_t portId,
      cfg::PortProfileID portProfile) const;

 private:
  void setLedBasedOnLOS(
      const uint32_t portId,
      const std::set<int>& ledIds,
      const std::map<uint32_t, std::pair<LedIO*, std::set<int>>>& controllers,
      led::LedState ledState);
};

} // namespace facebook::fboss
