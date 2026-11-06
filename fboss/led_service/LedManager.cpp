// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/LedManager.h"
#include "fboss/agent/EnumUtils.h"
#include "fboss/lib/CommonFileUtils.h"
#include "fboss/lib/platforms/PlatformProductInfo.h"

namespace facebook::fboss {

/*
 * LedManager ctor()
 *
 * LedManager constructor will create PubSub manager to subscribe to the
 * SwitchState update on FSDB. The switch state info is pushed by callback from
 * FsdbSwitchStateSubscriber and the updates used here
 */
LedManager::LedManager() {
  // Create Event base
  eventBase_ = std::make_unique<folly::EventBase>();
  auto* evb = eventBase_.get();
  ledManagerThread_ =
      std::make_unique<std::thread>([=] { evb->loopForever(); });

  // Read the LED config from file
  try {
    ledConfig_ = LedConfig::fromDefaultFile();
  } catch (FbossError& e) {
    XLOG(ERR) << "LED config file not present: " << e.what();
  }

  XLOG(INFO) << "Created base LED Manager";
}

void LedManager::subscribeToFsdb() {
  // Subscribe to Fsdb
  fsdbPubSubMgr_ = std::make_unique<fsdb::FsdbPubSubManager>("led_service");
  fsdbSwitchStateSubscriber_ =
      std::make_unique<FsdbSwitchStateSubscriber>(fsdbPubSubMgr_.get());
  fsdbSwitchStateSubscriber_->subscribeToStates(this);
}

/*
 * stop
 *
 * Remove the FSDB subscriptions, stop the event base thread and run any work
 * still queued on the event base. Queued work calls the platform's virtual
 * methods, so the owner must call this before destroying the object.
 */
void LedManager::stop() {
  if (fsdbSwitchStateSubscriber_) {
    fsdbSwitchStateSubscriber_->removeStateSubscriptions();
    fsdbSwitchStateSubscriber_.reset();
    XLOG(INFO) << "Removed LED manager subscription from FSDB";
  }

  if (eventBase_) {
    eventBase_->terminateLoopSoon();
  }
  if (ledManagerThread_) {
    ledManagerThread_->join();
    ledManagerThread_.reset();
  }
  eventBase_.reset();
}

/*
 * LedManager destructor()
 *
 * Stop event base thread for Led Manager
 */
LedManager::~LedManager() {
  stop();
  XLOG(INFO)
      << "Removed LED manager thread and deleting Led Manager object now";
}

/*
 * triggerLedUpdate
 * This function is called from updateLedStatus.
 * The function will trigger re-calculation of the ledState for all ports
 */
void LedManager::triggerLedUpdate(const std::vector<PortID>& portIds) {
  for (const auto& portId : portIds) {
    auto itr = portDisplayMap_.find(portId);
    if (itr == portDisplayMap_.end()) {
      XLOG(ERR) << "Unexpected portId " << portId
                << " not found in portDisplayMap_";
      continue;
    }
    auto& portDisplayInfo = itr->second;
    if (portDisplayInfo.forcedOn || portDisplayInfo.forcedOff) {
      continue;
    }
    auto& portName = portDisplayInfo.portName;
    auto portProfile = portDisplayInfo.portProfileId;
    try {
      auto newLedState = calculateLedState(portId, portProfile);
      if (newLedState != portDisplayInfo.currentLedState) {
        setLedState(portId, portProfile, newLedState);
        portDisplayInfo.currentLedState = newLedState;
        XLOG(DBG2) << fmt::format(
            "Port {:s} LED color changed to {:s}, Blink changed to {:s}",
            portName,
            enumToName<led::LedColor>(newLedState.ledColor().value()),
            enumToName<led::Blink>(newLedState.blink().value()));
      }
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Failed to update LED color for port " << portName << ": "
                << ex.what();
    }
  }
}

/*
 * updateLedStatus
 *
 * This function does these two things:
 * 1. Look into the FSDB pushed data (switch states) in newSwitchState and
 *    updates the local structure portDisplayMap_ as per the operational values
 *    in latest switch state
 * 2. Trigger an LED update (triggerLedUpdate. This will Compute LED colors for
 * new state and if the color for a port is different than existing one then set
 * the new color on LED
 */
void LedManager::updateLedStatus(
    const std::map<short, LedSwitchStateUpdate>& newSwitchState) {
  if (newSwitchState.empty()) {
    // No change in port info so return from here
    return;
  }
  std::vector<PortID> portsToTriggerLedUpdate;

  for (const auto& [portId, switchStateUpdate] : newSwitchState) {
    // Step 1. Update all operational info
    auto portName = switchStateUpdate.portName;
    auto portProfile = switchStateUpdate.portProfile;
    auto portProfileEnumVal = nameToEnum<cfg::PortProfileID>(portProfile);
    PortDisplayInfo portInfo;
    portInfo.portName = portName;
    portInfo.portProfileId = portProfileEnumVal;
    portInfo.operationStateUp = switchStateUpdate.operState;
    bool cablignError{false}, cablingLoop{false};
    if (switchStateUpdate.ledExternalState.has_value()) {
      cablignError = switchStateUpdate.ledExternalState.value() ==
          PortLedExternalState::CABLING_ERROR;
      cablingLoop = switchStateUpdate.ledExternalState.value() ==
          PortLedExternalState::CABLING_ERROR_LOOP_DETECTED;
    }
    portInfo.cablingError =
        (cablignError || cablingLoop || switchStateUpdate.mismatchedNeighbor);
    if (portDisplayMap_.find(portId) != portDisplayMap_.end()) {
      // If the port info exists then carry the current color and port forced
      // LED info
      portInfo.currentLedState = portDisplayMap_.at(portId).currentLedState;
      portInfo.forcedOn = portDisplayMap_.at(portId).forcedOn;
      portInfo.forcedOff = portDisplayMap_.at(portId).forcedOff;
    } else {
      portInfo.currentLedState = utility::constructLedState(
          led::LedColor::UNKNOWN, led::Blink::UNKNOWN);
    }
    portInfo.drained = switchStateUpdate.drained;

    portDisplayMap_[portId] = portInfo;
    portsToTriggerLedUpdate.emplace_back(portId);
  }

  triggerLedUpdate(portsToTriggerLedUpdate);
}

/*
 * updateLedStatus
 *
 * This function checks the presence of transceiver. If present,
 * get the LOS map (per port). Check that if LOS is set for all lanes
 * of a specific port then declare an LOS (from LED point of view) for
 * this port.
 * LOS is used in the drained state LED color calculation
 *
 */
void LedManager::updateLedStatus(
    const std::map<int, LedTransceiverStateUpdate>& newTcvrUpdate) {
  if (newTcvrUpdate.empty()) {
    // No change in port info so return from here
    return;
  }
  std::vector<facebook::fboss::PortID> portsToTriggerLedUpdate;
  std::map<uint32_t, PortLosInfo> portLosMap;
  for (const auto& [tcvrId, stateUpdate] : newTcvrUpdate) {
    if (!stateUpdate.present) {
      // We will use this and ignore LOS values, inside the calculation
      // of the LED state.
      continue;
    }
    auto swPorts = platformMapping_->getSwPortListFromTransceiverId(tcvrId);
    auto& mediaSignals = stateUpdate.mediaLaneSignals;
    if (mediaSignals.empty()) {
      // Applicable to DAC.
      XLOG(DBG3) << "No media signals for tcvrId " << tcvrId;
      continue;
    }
    for (auto& swPort : swPorts) {
      auto portName = platformMapping_->getPortNameByPortId(swPort).value_or(
          "UNKNOWN_PORT");
      if (portName == "UNKNOWN_PORT") {
        XLOG(ERR) << "Port " << swPort << " name not found in platform mapping";
        continue;
      }
      const auto& portNameToMediaLanes = stateUpdate.portNameToMediaLanes;
      // Map of lane to rxLos for a specific swPort
      std::map<int, bool> rxLos;
      auto itr = portNameToMediaLanes.find(portName);
      if (itr != portNameToMediaLanes.end()) {
        for (auto& lane : itr->second) {
          if (mediaSignals.size() > lane) {
            rxLos[lane] = mediaSignals[lane].rxLos().value_or(false);
          } else {
            XLOG(ERR) << "Unexpected lane " << lane << " for port " << portName;
            continue;
          }
        }
        portLosMap[swPort].rxLos = rxLos;

        std::string los;
        for (auto& [lane, laneLos] : rxLos) {
          los += fmt::format(" Lane={:d} rxLos={:d} ", lane, laneLos);
        }
        XLOG(DBG3) << "Port " << swPort << " name " << portName << los;
        portsToTriggerLedUpdate.push_back(swPort);
      }
    }
  }
  if (portLosMap_ != portLosMap) {
    portLosMap_ = portLosMap;
    XLOG(DBG2) << "Port LOS map changed";
    triggerLedUpdate(portsToTriggerLedUpdate);
  }
}

/*
 * setExternalLedState
 *
 * This function sets the LED forced mode (on/off) as told through thrift call.
 * This function will also set the led color accordingly. The regular FSDB
 * based update function will treat the user forced on/off as higher priority
 * and they will not override it based on current port status
 */
void LedManager::setExternalLedState(
    int32_t portNum,
    PortLedExternalState ledState) {
  // Set the Forced on/off values in portDisplayMap_
  if (portDisplayMap_.find(portNum) == portDisplayMap_.end()) {
    // If the PortInfo has not been updated from FSDB yet then the important
    // info like port profile is not available so we need to bail out from
    // this functon
    throw FbossError(
        fmt::format(
            "setExternalLedState: Port info not available for {:d} yet",
            portNum));
  } else {
    portDisplayMap_[portNum].forcedOn =
        ledState == PortLedExternalState::EXTERNAL_FORCE_ON;
    portDisplayMap_[portNum].forcedOff =
        ledState == PortLedExternalState::EXTERNAL_FORCE_OFF;
  }

  // Step 2. Update LED color if required
  auto portName = portDisplayMap_[portNum].portName;
  auto portProfile = portDisplayMap_[portNum].portProfileId;
  auto newLedState = calculateLedState(portNum, portProfile);
  if (newLedState != portDisplayMap_[portNum].currentLedState) {
    setLedState(portNum, portProfile, newLedState);
    portDisplayMap_[portNum].currentLedState = newLedState;
    XLOG(DBG2) << fmt::format(
        "Port {:s} LED color changed to {:s}, Blink changed to {:s}",
        portName,
        enumToName<led::LedColor>(newLedState.ledColor().value()),
        enumToName<led::Blink>(newLedState.blink().value()));
  }
}

/*
 * isLedControlledThroughService
 *
 * Returns if the LED is being controlled through LED Service
 * - If /etc/coop/led.conf does not exist then return false
 * - If the led.conf file exist then return the value of boolean
 *   ledControlledThroughService from that file
 */
bool LedManager::isLedControlledThroughService() const {
  if (ledConfig_.get()) {
    return ledConfig_->thriftConfig_.ledControlledThroughService().value();
  }
  return false;
}

/*
 * getLedColorForPort
 *
 * Returns the latest LED color for a given SW Port
 */
led::LedColor LedManager::getCurrentLedColor(int32_t portNum) const {
  led::LedColor ledColor = led::LedColor::UNKNOWN;

  if (portDisplayMap_.find(portNum) != portDisplayMap_.end()) {
    ledColor = portDisplayMap_.at(portNum).currentLedState.ledColor().value();
  } else {
    auto portName = platformMapping_->getPortNameByPortId(PortID(portNum));
    XLOG(ERR) << fmt::format(
        "Port {:s} not found in portDisplayMap_",
        (portName.has_value() ? portName.value() : ""));
  }

  return ledColor;
}

/*
 * getPortLedState
 *
 * Returns the LED state information for a given SW port. It reads from local
 * LED cache and throws if the cache is not updated yet from FSDB. The function
 * expects to be called under Led Manager event base.
 */
led::PortLedState LedManager::getPortLedState(
    const std::string& swPortName) const {
  led::PortLedState portLedState;

  auto portId = platformMapping_->getPortID(swPortName);
  if (portDisplayMap_.find(portId) == portDisplayMap_.end()) {
    // If the PortInfo has not been updated from FSDB yet
    throw FbossError(
        fmt::format(
            "getPortLedState: Port info not available for {:s} yet",
            swPortName));
  }
  led::LedState ledState = portDisplayMap_.at(portId).currentLedState;

  portLedState.swPortId() = portId;
  portLedState.swPortName() = swPortName;
  portLedState.currentLedState() = ledState;
  portLedState.forcedOnState() = portDisplayMap_.at(portId).forcedOn;
  portLedState.forcedOffState() = portDisplayMap_.at(portId).forcedOff;

  return portLedState;
}

/*
 * areAllNeighborsRxLos
 * Multiple ports can control the same LED. This function will return true if
 * all the neighbors of a port are in Rx LOS state. LOS State means that
 * all the lanes of the port are in LOS state.
 */
bool LedManager::areAllNeighborsRxLos(uint32_t portID) const {
  auto itr = portDisplayMap_.find(portID);
  if (itr == portDisplayMap_.end()) {
    throw(std::runtime_error(
        fmt::format("Port {:d} not found in portLosMap_", portID)));
  }
  auto neighborInfo = itr->second.portNeighborState;
  if (neighborInfo.portsWithAllLanesRxLos == neighborInfo.totalPorts) {
    return true;
  }
  return false;
}

/*
 * portsUpAndCorrectReachability
 * For the number of ports that control the same LED, return the number of
 * ports that are up and have the correct reachability.
 */
uint32_t LedManager::portsUpAndCorrectReachability(uint32_t portID) const {
  auto itr = portDisplayMap_.find(portID);
  if (itr == portDisplayMap_.end()) {
    throw(std::runtime_error(
        fmt::format("Port {:d} not found in portLosMap_", portID)));
  }
  auto neighborInfo = itr->second.portNeighborState;
  return neighborInfo.portsUpAndCorrectReachability;
}

} // namespace facebook::fboss
