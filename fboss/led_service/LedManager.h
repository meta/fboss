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
#include <set>
#include <stdexcept>
#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/switch_state_types.h"
#include "fboss/agent/platforms/common/PlatformMapping.h"
#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/led_service/FsdbSwitchStateSubscriber.h"
#include "fboss/led_service/LedConfig.h"
#include "fboss/led_service/LedUtils.h"
#include "fboss/lib/led/LedIO.h"
#include "fboss/lib/led/gen-cpp2/led_mapping_types.h"

namespace facebook::fboss {

/*
 * LedManager class definiton:
 *
 * The LedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class LedManager {
 protected:
  // struct describing the state of the port neightbors. This helps
  // us in calculating the LED state.
  // e.g. eth1/1/1 and eth1/1/2 may control the same LED, and will influence
  // led state based on how all these ports interact.
  using PortNeighborState = struct PortNeighborState {
    bool operator==(const PortNeighborState& other) const {
      return (
          totalPorts == other.totalPorts &&
          portsUpAndCorrectReachability ==
              other.portsUpAndCorrectReachability &&
          portsWithAllLanesRxLos == other.portsWithAllLanesRxLos &&
          anyForcedOn == other.anyForcedOn &&
          anyForcedOff == other.anyForcedOff &&
          anyUndrainedPort == other.anyUndrainedPort &&
          anyPortUp == other.anyPortUp);
    }
    uint32_t totalPorts{0};
    uint32_t portsUpAndCorrectReachability{0};
    uint32_t portsWithAllLanesRxLos{0};
    bool anyForcedOn{false};
    bool anyForcedOff{false};
    bool anyUndrainedPort{false};
    bool anyPortUp{false};
  };

 private:
  using PortDisplayInfo = struct PortDisplayInfo {
    std::string portName;
    cfg::PortProfileID portProfileId;
    bool operationStateUp{false};
    bool cablingError{false};
    bool forcedOn{false};
    bool forcedOff{false};
    led::LedState currentLedState{utility::constructLedState(
        led::LedColor::UNKNOWN,
        led::Blink::UNKNOWN)};
    bool drained{false};
    mutable PortNeighborState portNeighborState;
  };

  using PortLosInfo = struct PortLosInfo {
    std::optional<std::map<int, bool>> rxLos{std::nullopt};
    bool operator==(const PortLosInfo& other) const {
      return rxLos == other.rxLos;
    }
  };

 public:
  using LedSwitchStateUpdate = struct LedSwitchStateUpdate {
    short swPortId;
    std::string portName;
    std::string portProfile;
    bool operState;
    std::optional<PortLedExternalState> ledExternalState;
    bool drained;
    bool mismatchedNeighbor{false};
  };

  using LedTransceiverStateUpdate = struct LedTransceiverStateUpdate {
    bool present{false};
    std::map<std::string, std::vector<int>> portNameToMediaLanes;
    std::vector<fboss::MediaLaneSignals> mediaLaneSignals;
  };

  LedManager();
  virtual ~LedManager();

  // Must be called before destruction, while the derived object is alive
  void stop();

  void subscribeToFsdb();

  // Initialize the Led Manager, get system container
  virtual void initLedManager() {}

  // On getting the update from FSDB for Agent switch change, update
  // portDisplayMap_
  void updateLedStatus(
      const std::map<short, LedSwitchStateUpdate>& newSwitchState);

  // On getting the update from FSDB for transceiver state change, update
  // portLosMap_
  // The reason LedTransceiverStateUpdate is not added to portDisplayMap_ is
  // because the transceiver may report ports that are not active in agent,
  // which will impact logic of led color calculation.
  void updateLedStatus(
      const std::map<int, LedTransceiverStateUpdate>& newTcvrUpdate);

  folly::EventBase* getEventBase() {
    return eventBase_.get();
  }

  fsdb::FsdbPubSubManager* pubSubMgr() const {
    return fsdbPubSubMgr_.get();
  }

  void setExternalLedState(int32_t portNum, PortLedExternalState ledState);

  led::PortLedState getPortLedState(const std::string& swPortName) const;

  bool isLedControlledThroughService() const;

  led::LedColor getCurrentLedColor(int32_t portNum) const;

  // Derived platforms can set the forcedforcedOnColor
  virtual led::LedColor forcedOnColor() const {
    return led::LedColor::BLUE;
  }

  const PlatformMapping* getPlatformMapping() const {
    return platformMapping_.get();
  }

  const LedConfig* getLedConfig() const {
    return ledConfig_.get();
  }

  virtual bool blinkingSupported() const {
    return false;
  }

  bool areAllNeighborsRxLos(uint32_t portID) const;

  uint32_t portsUpAndCorrectReachability(uint32_t portID) const;

  // Forbidden copy constructor and assignment operator
  LedManager(LedManager const&) = delete;
  LedManager& operator=(LedManager const&) = delete;

  virtual std::set<led::LedState> getLedStateFromHW(
      uint32_t /* portId */) const {
    return std::set<led::LedState>();
  };

 protected:
  // Platform mapping to get SW Port -> Lane mapping
  std::unique_ptr<PlatformMapping> platformMapping_;

  // Port Name to PortDisplayInfo map, no lock needed
  std::map<uint32_t, PortDisplayInfo> portDisplayMap_;

  // Port Name to PortLosInfo map, no lock needed
  std::map<uint32_t, PortLosInfo> portLosMap_;

  std::unique_ptr<FsdbSwitchStateSubscriber> fsdbSwitchStateSubscriber_;
  std::unique_ptr<fsdb::FsdbPubSubManager> fsdbPubSubMgr_;

  std::unique_ptr<LedConfig> ledConfig_;

  virtual led::LedState calculateLedState(
      uint32_t /* portId */,
      cfg::PortProfileID /* portProfiled */) const = 0;

  virtual void setLedState(
      uint32_t /* portId */,
      cfg::PortProfileID /* portProfile */,
      led::LedState /* ledState */) = 0;

 private:
  std::unique_ptr<std::thread> ledManagerThread_{nullptr};
  std::unique_ptr<folly::EventBase> eventBase_;

  void triggerLedUpdate(const std::vector<PortID>& portIds);
};

} // namespace facebook::fboss
