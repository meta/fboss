/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gtest/gtest.h>

#include <folly/init/Init.h>
#include <thrift/lib/cpp2/async/RocketClientChannel.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/agent/EnumUtils.h"
#include "fboss/agent/FbossError.h"
#include "fboss/led_service/BspLedManager.h"
#include "fboss/led_service/LedManager.h"
#include "fboss/led_service/LedUtils.h"
#include "fboss/led_service/hw_test/LedServiceTest.h"
#include "fboss/lib/bsp/BspSystemContainer.h"
#include "fboss/lib/led/LedIO.h"

#ifndef IS_OSS
#include "common/base/Proc.h"
#endif

namespace facebook::fboss {

DEFINE_int32(visual_delay_sec, 0, "Add a delay to enable seeing LED change");

void LedServiceTest::SetUp() {
  // First use LedConfig to init default command line arguments.
  facebook::fboss::utility::initFlagDefaults(0, nullptr);

  // Create ensemble and initialize it
  ensemble_ = std::make_unique<LedEnsemble>();
  ensemble_->init();

  // Check if the LED manager is created correctly, the config file is loaded
  // and the LED is managed by service now
  ledManager_ = getLedEnsemble()->getLedManager();
  CHECK_NE(ledManager_, nullptr);
#ifndef IS_OSS
  CHECK(ledManager_->isLedControlledThroughService());
#endif
  platformMap_ = ledManager_->getPlatformMapping();
  CHECK_NE(platformMap_, nullptr);
}

void LedServiceTest::TearDown() {
  if (ensemble_ != nullptr) {
    EXPECT_NO_THROW(checkLedMemoryInBounds());
  }
  // Remove ensemble and objects contained there
  ensemble_.reset();
  ledManager_ = nullptr;
  platformMap_ = nullptr;
}

void LedServiceTest::checkLedMemoryInBounds() const {
#ifndef IS_OSS
  const auto* ledConfig = ledManager_->getLedConfig();
  if (ledConfig == nullptr) {
    throw FbossError("LED config is not loaded, cannot check memory bounds");
  }
  auto ledTestConfig = ledConfig->thriftConfig_.ledTestConfig();
  if (!ledTestConfig.has_value() ||
      !ledTestConfig->ledServiceMemLimitBytes().has_value()) {
    XLOG(INFO) << "ledServiceMemLimitBytes not set in led config, "
               << "skipping memory check";
    return;
  }
  int64_t memUsage = facebook::Proc::getMemoryUsage();
  auto memUsageThreshold = *ledTestConfig->ledServiceMemLimitBytes();
  if (memUsage > memUsageThreshold) {
    throw FbossError(
        "LED service RSS memory ",
        memUsage,
        " above expected ",
        memUsageThreshold);
  }
#endif
}

std::vector<TransceiverID> LedServiceTest::getAllTransceivers(
    const PlatformMapping* platformMapping) const {
  // Use a set to sort the transceiver IDs.
  std::set<TransceiverID> transceivers;
  const auto& chips = platformMapping->getChips();
  for (auto chip : chips) {
    if (*chip.second.type() == phy::DataPlanePhyChipType::TRANSCEIVER) {
      auto tcvrID = TransceiverID(*chip.second.physicalID());
      transceivers.insert(tcvrID);
    }
  }
  std::vector<TransceiverID> retVal;
  // For some platforms (e.g. Janga Test), the platform mapping has transceivers
  // missing logical port IDs which will fail the tests (not all transceivers
  // have logical port IDs). So, we will skip the missing ones here.
  for (auto tcvrID : transceivers) {
    try {
      auto swPorts = platformMap_->getSwPortListFromTransceiverId(tcvrID);
      if (!swPorts.empty()) {
        retVal.push_back(tcvrID);
      }
    } catch (FbossError&) {
    }
  }

  return retVal;
}

/*
 * Set the LED state by updating the LED manager and check if the LED color
 * changes accordingly
 */
TEST_F(LedServiceTest, checkForceLed) {
  // Test for all the modules
  // Use the ports max speed and profile
  auto transceivers = getAllTransceivers(platformMap_);

  for (auto tcvr : transceivers) {
    auto swPorts = platformMap_->getSwPortListFromTransceiverId(tcvr);
    CHECK_GT(swPorts.size(), 0);
    auto swPort = swPorts[0];
    auto swPortName = platformMap_->getPortNameByPortId(swPort);
    CHECK(swPortName.has_value());

    // The setExternalLedState will throw because first update from FSDB has not
    // happened to the LedManager
    EXPECT_THROW(
        ledManager_->setExternalLedState(
            swPort, PortLedExternalState::EXTERNAL_FORCE_OFF),
        FbossError);

    // Do the first update from FSDB to LedService
    auto maxSpeed = platformMap_->getPortMaxSpeed(swPort);
    auto profile = platformMap_->getProfileIDBySpeed(swPort, maxSpeed);
    LedManager::LedSwitchStateUpdate ledUpdate = {
        static_cast<short>(swPort),
        "",
        enumToName<cfg::PortProfileID>(profile),
        false,
        PortLedExternalState::NONE,
        false /*drain*/,
        false /*mismatchedNeighbor*/};

    std::map<short, LedManager::LedSwitchStateUpdate> switchUpdate_0;
    switchUpdate_0[swPort] = ledUpdate;
    ledManager_->updateLedStatus(switchUpdate_0);

    // Now the setting of external LED state should be successful
    EXPECT_NO_THROW(ledManager_->setExternalLedState(
        swPort, PortLedExternalState::EXTERNAL_FORCE_OFF));

    // Verify link Down, the expected LED color is OFF
    auto offLedColor = ledManager_->getCurrentLedColor(swPort);
    auto ledState = ledManager_->getPortLedState(swPortName.value());
    EXPECT_EQ(offLedColor, led::LedColor::OFF);
    EXPECT_EQ(
        ledState.currentLedState()->ledColor().value(), led::LedColor::OFF);
    EXPECT_TRUE(ledState.forcedOffState().value());

    // Verify forcing LED to on state that LED is set accordingly.
    ledManager_->setExternalLedState(
        swPort, PortLedExternalState::EXTERNAL_FORCE_ON);
    auto onLedColorCurrent = ledManager_->getCurrentLedColor(swPort);
    auto onLedColorExpected = ledManager_->forcedOnColor();
    ledState = ledManager_->getPortLedState(swPortName.value());
    EXPECT_EQ(onLedColorCurrent, onLedColorExpected);
    EXPECT_EQ(
        ledState.currentLedState()->ledColor().value(), onLedColorExpected);
    EXPECT_TRUE(ledState.forcedOnState().value());

    // If the flags is specified, add a delay before forcing LED off
    // to enable seeing the LED change
    sleep(FLAGS_visual_delay_sec);

    // Put it back to Off state and check again
    ledManager_->setExternalLedState(
        swPort, PortLedExternalState::EXTERNAL_FORCE_OFF);
    offLedColor = ledManager_->getCurrentLedColor(swPort);
    ledState = ledManager_->getPortLedState(swPortName.value());
    EXPECT_EQ(offLedColor, led::LedColor::OFF);
    EXPECT_EQ(
        ledState.currentLedState()->ledColor().value(), led::LedColor::OFF);
    EXPECT_TRUE(ledState.forcedOffState().value());
  }
}

void LedServiceTest::checkLedColor(
    PortID port,
    enum led::LedColor color,
    int testNum) {
  auto portName = platformMap_->getPortNameByPortId(port);
  CHECK(portName.has_value());
  auto currentColor = ledManager_->getCurrentLedColor(port);
  auto ledState = ledManager_->getPortLedState(portName.value());
  EXPECT_EQ(currentColor, color)
      << "LED color should be " << enumToName<led::LedColor>(color)
      << " for port " << portName.value() << " but current color is "
      << enumToName<led::LedColor>(currentColor) << " for test " << testNum;
  EXPECT_EQ(ledState.currentLedState()->ledColor().value(), color)
      << "LED state should be " << enumToName<led::LedColor>(color)
      << " for port " << portName.value() << " but current color is "
      << enumToName<led::LedColor>(
             ledState.currentLedState()->ledColor().value())
      << " for test " << testNum;
  std::set<led::LedState> ledStateSet = ledManager_->getLedStateFromHW(port);
  for (const auto& state : ledStateSet) {
    EXPECT_EQ(state.ledColor().value(), color)
        << "LED state in IO should be " << enumToName<led::LedColor>(color)
        << " for port " << portName.value() << " but current color is "
        << enumToName<led::LedColor>(state.ledColor().value()) << " for test "
        << testNum;
  }
  // // If the visual_delay_sec flag is specified, add a delay to enable seeing
  // the LED change
  /* sleep override */
  sleep(FLAGS_visual_delay_sec);
}

void LedServiceTest::checkLedBlink(
    PortID port,
    enum led::Blink blink,
    int testNum) {
  auto portName = platformMap_->getPortNameByPortId(port);
  CHECK(portName.has_value());
  auto ledState = ledManager_->getPortLedState(portName.value());
  auto currentBlink = ledState.currentLedState()->blink().value();
  EXPECT_EQ(currentBlink, blink)
      << "LED Blink should be " << enumToName<led::Blink>(blink) << " for port "
      << portName.value() << " but current blink is "
      << enumToName<led::Blink>(currentBlink) << " for test " << testNum;
  std::set<led::LedState> ledStateSet = ledManager_->getLedStateFromHW(port);
  for (const auto& state : ledStateSet) {
    EXPECT_EQ(state.blink().value(), blink)
        << "LED state in IO should be " << enumToName<led::Blink>(blink)
        << " for port " << portName.value() << " but current blink is "
        << enumToName<led::Blink>(state.blink().value()) << " for test "
        << testNum;
  }
  // If the visual_delay_sec flag is specified, add a delay to enable seeing the
  // LED blink.
  /* sleep override */
  sleep(FLAGS_visual_delay_sec);
}

TEST_F(LedServiceTest, checkLedColorChange) {
  auto transceivers = getAllTransceivers(platformMap_);

  for (auto tcvr : transceivers) {
    XLOG(INFO) << "Testing transceiver " << tcvr;
    auto swPorts = platformMap_->getSwPortListFromTransceiverId(tcvr);
    for (auto& swPort : swPorts) {
      // Do the first update from FSDB to LedService. LED set to inactive.
      auto maxSpeed = platformMap_->getPortMaxSpeed(swPort);
      auto profile = platformMap_->getProfileIDBySpeed(swPort, maxSpeed);
      XLOG(INFO) << "Testing SW Port " << swPort << " with speed "
                 << enumToName<cfg::PortSpeed>(maxSpeed) << " and profile "
                 << enumToName<cfg::PortProfileID>(profile);
      LedManager::LedSwitchStateUpdate ledUpdate = {
          static_cast<short>(swPort),
          "",
          enumToName<cfg::PortProfileID>(profile),
          false /*operationalState*/,
          PortLedExternalState::NONE,
          false /*drain*/,
          false /*mismatchedNeighbor*/,
      };

      std::map<short, LedManager::LedSwitchStateUpdate> updateMap;
      updateMap[swPort] = ledUpdate;
      ledManager_->updateLedStatus(updateMap);

      auto swPortName = platformMap_->getPortNameByPortId(swPort);
      CHECK(swPortName.has_value());

      int testNum = 0;
      // 1- Verify Default creation with operational state = false that the
      // LED color is off.
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);

      // 2- Set the operational state to true, verify that the LED color
      // is not off anymore. Platform determines its own on color.
      updateMap[swPort].operState = true;
      ledManager_->updateLedStatus(updateMap);
      auto colorBefore = ledManager_->getCurrentLedColor(swPort);
      EXPECT_NE(colorBefore, led::LedColor::OFF);
      checkLedColor(swPort, colorBefore, ++testNum);

      // If the flags is specified, add a delay before forcing LED off
      // to enable visual test for the LED change
      sleep(FLAGS_visual_delay_sec);

      // 3- Force LED to be off. The LED color should be off.
      ledManager_->setExternalLedState(
          swPort, PortLedExternalState::EXTERNAL_FORCE_OFF);
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);

      // 4- Unforce LED Off. LED should go back to the previous color.
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      checkLedColor(swPort, colorBefore, ++testNum);

      // 5- Force LED to be on. The LED color should not be off.
      // Each platform determines its own on color.
      ledManager_->setExternalLedState(
          swPort, PortLedExternalState::EXTERNAL_FORCE_ON);
      auto onLedColorExpected = ledManager_->forcedOnColor();
      checkLedColor(swPort, onLedColorExpected, ++testNum);

      // 6- Unforce LED On. LED should go back to the previous color.
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      checkLedColor(swPort, colorBefore, ++testNum);

      // 7- Set the cabling error, expect LED off since portsUp & !cablingErr =
      // 0.
      updateMap[swPort].ledExternalState = PortLedExternalState::CABLING_ERROR;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);

      // 8- Remove the cabling error, check the LED color goes back
      updateMap[swPort].ledExternalState = PortLedExternalState::NONE;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);

      // 9- Set the cabling error, force LED ON, check the LED color is on, and
      // not yellow.
      updateMap[swPort].ledExternalState = PortLedExternalState::CABLING_ERROR;
      ledManager_->updateLedStatus(updateMap);
      ledManager_->setExternalLedState(
          swPort, PortLedExternalState::EXTERNAL_FORCE_ON);
      checkLedColor(swPort, onLedColorExpected, ++testNum);

      // 10 - Remove force on, LED should go back to cabling error.
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);

      // 11- Force LED Off.
      ledManager_->setExternalLedState(
          swPort, PortLedExternalState::EXTERNAL_FORCE_OFF);
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);

      // 12 - Remove force off, LED should go back to cabling error.
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);

      // 13 - Remove Cabling Error, LED should go back to original color.
      updateMap[swPort].ledExternalState = PortLedExternalState::NONE;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);

      // 14 - Force LED Off.
      ledManager_->setExternalLedState(
          swPort, PortLedExternalState::EXTERNAL_FORCE_OFF);
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);

      // 15 - Remove Force Off. LED should be in original color.
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      checkLedColor(swPort, colorBefore, ++testNum);

      // 16 - set Operational state off. Expect LED to be off.
      updateMap[swPort].operState = false;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);

      // 17 - set cabling error loop detected. Expect LED to be yellow similar
      // to cabling error.
      updateMap[swPort].operState = true;
      updateMap[swPort].ledExternalState =
          PortLedExternalState::CABLING_ERROR_LOOP_DETECTED;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);

      // 18 - Remove external led state and set oper state to false. Expect
      // led to be OFF
      updateMap[swPort].operState = false;
      ledManager_->setExternalLedState(swPort, PortLedExternalState::NONE);
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, led::LedColor::OFF, ++testNum);
    }
  }
}

TEST_F(LedServiceTest, checkLedBlinking) {
  auto transceivers = getAllTransceivers(platformMap_);
  auto blinkSupported = ledManager_->blinkingSupported();

  for (auto tcvr : transceivers) {
    XLOG(INFO) << "Testing transceiver " << tcvr;
    auto swPorts = platformMap_->getSwPortListFromTransceiverId(tcvr);
    for (auto& swPort : swPorts) {
      // Do the first update from FSDB to LedService. LED set to inactive.
      auto maxSpeed = platformMap_->getPortMaxSpeed(swPort);
      auto profile = platformMap_->getProfileIDBySpeed(swPort, maxSpeed);
      XLOG(INFO) << "Testing SW Port " << swPort << " with speed "
                 << enumToName<cfg::PortSpeed>(maxSpeed) << " and profile "
                 << enumToName<cfg::PortProfileID>(profile);
      LedManager::LedSwitchStateUpdate ledUpdate = {
          static_cast<short>(swPort),
          "",
          enumToName<cfg::PortProfileID>(profile),
          true /*operationalState*/,
          PortLedExternalState::NONE,
          false /*drain*/,
          false /*mismatchedNeighbor*/,
      };

      std::map<short, LedManager::LedSwitchStateUpdate> updateMap;
      updateMap[swPort] = ledUpdate;
      ledManager_->updateLedStatus(updateMap);

      auto swPortName = platformMap_->getPortNameByPortId(swPort);
      CHECK(swPortName.has_value());

      int testNum = 0;
      // 1- Verify Default creation with operational state = true, drain = false
      // that the LED color is on, blink is off
      auto colorBefore = ledManager_->getCurrentLedColor(swPort);
      EXPECT_NE(colorBefore, led::LedColor::OFF);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(swPort, led::Blink::OFF, testNum);

      // 2- Set the drain state to true, verify that the LED color is same but
      // the LED blinks fast since port is up and there is no cabling error.
      updateMap[swPort].drained = true;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // 3- Introduce a cabling error. Thus, port is up and drained but there is
      // a cabling error. LED is expected to be Blue and blinking Slow since
      // number of portsUpAndCorrectReachability is 0 and there is no RX LOS
      updateMap[swPort].ledExternalState = PortLedExternalState::CABLING_ERROR;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // Reset attributes so that this port doesn't interfere in inferring led
      // color/blink of other ports sharing the same LED.
      updateMap[swPort].ledExternalState = PortLedExternalState::NONE;
      updateMap[swPort].operState = false;
      ledManager_->updateLedStatus(updateMap);
    }
  }
}

TEST_F(LedServiceTest, testTcvrLos) {
  auto transceivers = getAllTransceivers(platformMap_);
  auto blinkSupported = ledManager_->blinkingSupported();

  for (auto tcvr : transceivers) {
    XLOG(INFO) << "Testing transceiver " << tcvr;
    auto swPorts = platformMap_->getSwPortListFromTransceiverId(tcvr);
    for (auto& swPort : swPorts) {
      // Do the first update from FSDB to LedService. LED set to inactive.
      auto maxSpeed = platformMap_->getPortMaxSpeed(swPort);
      auto profile = platformMap_->getProfileIDBySpeed(swPort, maxSpeed);
      XLOG(INFO) << "Testing SW Port " << swPort << " with speed "
                 << enumToName<cfg::PortSpeed>(maxSpeed) << " and profile "
                 << enumToName<cfg::PortProfileID>(profile);
      LedManager::LedSwitchStateUpdate ledUpdate = {
          static_cast<short>(swPort),
          "",
          enumToName<cfg::PortProfileID>(profile),
          true /*operationalState*/,
          PortLedExternalState::NONE,
          false /*drain*/,
          false /*mismatchedNeighbor*/,
      };

      std::map<short, LedManager::LedSwitchStateUpdate> updateMap;
      updateMap[swPort] = ledUpdate;
      ledManager_->updateLedStatus(updateMap);

      auto swPortName = platformMap_->getPortNameByPortId(swPort);
      CHECK(swPortName.has_value());

      int testNum = 0;
      // 1- Verify Default creation with operational state = true, drain = false
      // that the LED color is on, blink is off
      auto colorBefore = ledManager_->getCurrentLedColor(swPort);
      EXPECT_NE(colorBefore, led::LedColor::OFF);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(swPort, led::Blink::OFF, testNum);

      // 2- Set the drain state to true, verify that the LED color is same but
      // the LED blinks fast since port is up and there is no cabling error.
      updateMap[swPort].drained = true;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // 3- Set the port state down error, and verify that the LED color is
      // yellow and fast blink if other neighboring ports have correct
      // reachability
      ++testNum;
      updateMap[swPort].operState = false;
      ledManager_->updateLedStatus(updateMap);
      if (ledManager_->portsUpAndCorrectReachability(swPort) > 0) {
        checkLedColor(swPort, led::LedColor::YELLOW, testNum);
        checkLedBlink(
            swPort,
            blinkSupported ? led::Blink::FAST : led::Blink::OFF,
            testNum);
      }

      // 4 Go back to operState up. LED state is same as before
      updateMap[swPort].operState = true;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      std::vector<PortLedExternalState> cablingErrors = {
          PortLedExternalState::CABLING_ERROR,
          PortLedExternalState::CABLING_ERROR_LOOP_DETECTED};

      // 5/6, 7/8- Set cabling Error. Check that its the same scenario as test 3
      for (auto& cablingError : cablingErrors) {
        updateMap[swPort].ledExternalState = cablingError;
        ledManager_->updateLedStatus(updateMap);
        if (ledManager_->portsUpAndCorrectReachability(swPort) > 0) {
          checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);
          checkLedBlink(
              swPort,
              blinkSupported ? led::Blink::FAST : led::Blink::OFF,
              testNum);
        }

        // Go back to no cabling error. LED state is same as before
        updateMap[swPort].ledExternalState = PortLedExternalState::NONE;
        ledManager_->updateLedStatus(updateMap);
        checkLedColor(swPort, colorBefore, ++testNum);
        checkLedBlink(
            swPort,
            blinkSupported ? led::Blink::FAST : led::Blink::OFF,
            testNum);
      }

      // 9- Test Mismatched Neighbor (resulting in cabling error). Same scenario
      // as test 3.
      updateMap[swPort].mismatchedNeighbor = true;
      ledManager_->updateLedStatus(updateMap);
      if (ledManager_->portsUpAndCorrectReachability(swPort) > 0) {
        checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);
        checkLedBlink(
            swPort,
            blinkSupported ? led::Blink::FAST : led::Blink::OFF,
            testNum);
      }

      // 10- Go back to no cabling error. LED state is same as before
      updateMap[swPort].mismatchedNeighbor = false;
      ledManager_->updateLedStatus(updateMap);
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // Now that we are drained, we want to play with the Rx LOS for the port.
      auto createSignals =
          [&](uint16_t size,
              uint16_t bitsLos) -> std::vector<MediaLaneSignals> {
        std::vector<MediaLaneSignals> ret(size);
        CHECK(bitsLos <= size);
        for (uint16_t i = 0; i < bitsLos; i++) {
          ret[i].rxLos() = true;
        }
        for (uint16_t i = bitsLos; i < size; i++) {
          ret[i].rxLos() = false;
        }
        return ret;
      };

      LedManager::LedTransceiverStateUpdate tcvrUpdate;
      tcvrUpdate.present = true;
      tcvrUpdate.mediaLaneSignals = createSignals(2, 0);
      tcvrUpdate.portNameToMediaLanes[swPortName.value()] = {1, 2};
      std::map<int, LedManager::LedTransceiverStateUpdate> xcvrLosMap;
      xcvrLosMap[tcvr] = tcvrUpdate;
      ledManager_->updateLedStatus(xcvrLosMap);

      // 11- Led color/blink remains same if all RX LOS are not in port, as long
      // as the port is not down
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // 12- set 1 lanes with LOS
      tcvrUpdate.mediaLaneSignals = createSignals(2, 1);
      xcvrLosMap[tcvr] = tcvrUpdate;
      ledManager_->updateLedStatus(xcvrLosMap);
      // Led color/blink remains same if all RX LOS are cleared in port, as long
      // as the port is not down
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // 13- set all lanes with LOS
      tcvrUpdate.mediaLaneSignals = createSignals(2, 2);
      xcvrLosMap[tcvr] = tcvrUpdate;
      ledManager_->updateLedStatus(xcvrLosMap);
      // Led color/blink remains same if all RX LOS are cleared in port, as long
      // as the port is not down
      checkLedColor(swPort, colorBefore, ++testNum);
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::FAST : led::Blink::OFF, testNum);

      // 14- As soon as port goes down, we will check how many ports have all
      // lanes with LOS. If some lanes have LOS, we will be YELLOW + blink slow
      // otherwise, we will be BLUE + blink slow (indicating some light is
      // detected)
      updateMap[swPort].ledExternalState = PortLedExternalState::NONE;
      updateMap[swPort].operState = false;
      ledManager_->updateLedStatus(updateMap);

      // Platforms that support LED blinking + RX Los choice: When drained
      // the led color will depend on how many ports have neighbors that
      // are also seeing LOS (i.e. BMC-Lite platforms excluding Darwin)
      // For platforms that dont support blinking, since the port operState is
      // down, the LED will be off (i.e. Pre BMC-Lite platforms)
      if (blinkSupported) {
        if (ledManager_->areAllNeighborsRxLos(swPort)) {
          checkLedColor(swPort, led::LedColor::YELLOW, ++testNum);
        } else {
          checkLedColor(swPort, led::LedColor::BLUE, ++testNum);
        }
      } else {
        checkLedColor(swPort, led::LedColor::OFF, ++testNum);
      }
      // Blinking should be slow if there is an LOS detected.
      checkLedBlink(
          swPort, blinkSupported ? led::Blink::SLOW : led::Blink::OFF, testNum);

      // 15- For multi-LED ports, test per-LED color differentiation.
      // When a port controls multiple LEDs and is in SLOW blink mode
      // (drained + down), each LED should individually show YELLOW
      // (all its lanes have LOS) or BLUE (some lanes have signal).
      auto* bspMgr = dynamic_cast<BspLedManager*>(ledManager_);
      if (bspMgr && blinkSupported) {
        auto ledCtrls =
            bspMgr->getBspSystemContainer()->getLedController(tcvr + 1);
        auto portLedIds = bspMgr->getLedIdFromSwPort(swPort, profile);

        // Filter LED controllers to those belonging to this port
        std::map<uint32_t, std::pair<LedIO*, std::set<int>>> portLedCtrls;
        for (const auto& [ledId, ctrlPair] : ledCtrls) {
          if (portLedIds.count(ledId)) {
            portLedCtrls[ledId] = ctrlPair;
          }
        }

        if (portLedCtrls.size() > 1) {
          ++testNum;
          XLOG(INFO) << "Testing multi-LED port " << swPort << " with "
                     << portLedCtrls.size() << " LEDs for test " << testNum;

          // Collect all lanes from this port's LED controllers
          std::vector<int> allPortLanes;
          int maxLane = 0;
          for (const auto& [ledId, ctrlPair] : portLedCtrls) {
            for (auto lane : ctrlPair.second) {
              allPortLanes.push_back(lane);
              maxLane = std::max(maxLane, lane);
            }
          }

          // Create signals with mixed LOS: first LED's lanes have LOS,
          // remaining LEDs' lanes have signal
          std::vector<MediaLaneSignals> signals(maxLane + 1);
          auto firstLedIt = portLedCtrls.begin();
          for (auto lane : firstLedIt->second.second) {
            signals[lane].rxLos() = true;
          }
          for (auto it = std::next(firstLedIt); it != portLedCtrls.end();
               ++it) {
            for (auto lane : it->second.second) {
              signals[lane].rxLos() = false;
            }
          }

          tcvrUpdate.mediaLaneSignals = signals;
          tcvrUpdate.portNameToMediaLanes[swPortName.value()] = allPortLanes;
          xcvrLosMap[tcvr] = tcvrUpdate;
          ledManager_->updateLedStatus(xcvrLosMap);

          // First LED: all its lanes have LOS -> YELLOW + SLOW
          auto firstLedState = firstLedIt->second.first->getLedState();
          EXPECT_EQ(firstLedState.ledColor().value(), led::LedColor::YELLOW)
              << "First LED should be YELLOW for port " << swPortName.value()
              << " test " << testNum;
          EXPECT_EQ(firstLedState.blink().value(), led::Blink::SLOW)
              << "First LED should blink SLOW for port " << swPortName.value()
              << " test " << testNum;

          // Remaining LEDs: some lanes have signal -> BLUE + SLOW
          for (auto it = std::next(firstLedIt); it != portLedCtrls.end();
               ++it) {
            auto ledState = it->second.first->getLedState();
            EXPECT_EQ(ledState.ledColor().value(), led::LedColor::BLUE)
                << "LED " << it->first << " should be BLUE for port "
                << swPortName.value() << " test " << testNum;
            EXPECT_EQ(ledState.blink().value(), led::Blink::SLOW)
                << "LED " << it->first << " should blink SLOW for port "
                << swPortName.value() << " test " << testNum;
          }
        }
      }
    }
  }
}

TEST_F(LedServiceTest, visualForceAllLeds) {
  auto* bspMgr = dynamic_cast<BspLedManager*>(ledManager_);
  if (!bspMgr) {
    GTEST_SKIP() << "Test only supported on BSP LED platforms";
  }

  auto transceivers = getAllTransceivers(platformMap_);

  auto forceAllLeds = [&](led::LedColor color, led::Blink blink) {
    auto ledState = utility::constructLedState(color, blink);
    for (auto tcvr : transceivers) {
      auto ledControllers =
          bspMgr->getBspSystemContainer()->getLedController(tcvr + 1);
      for (auto& [ledId, ctrlPair] : ledControllers) {
        ctrlPair.first->setLedState(ledState);
        auto actualState = ctrlPair.first->getLedState();
        EXPECT_EQ(actualState, ledState)
            << "LED " << ledId << " for transceiver " << tcvr
            << " expected color "
            << enumToName<led::LedColor>(ledState.ledColor().value())
            << " blink " << enumToName<led::Blink>(ledState.blink().value())
            << " but got color "
            << enumToName<led::LedColor>(actualState.ledColor().value())
            << " blink " << enumToName<led::Blink>(actualState.blink().value());
      }
    }
    /* sleep override */
    sleep(FLAGS_visual_delay_sec);
  };

  // 1- Force all LEDs to Blue (no blink)
  XLOG(INFO) << "Forcing all LEDs to Blue";
  forceAllLeds(led::LedColor::BLUE, led::Blink::OFF);

  // 2- Force all LEDs to Blue + slow blinking
  XLOG(INFO) << "Forcing all LEDs to Blue + slow blinking";
  forceAllLeds(led::LedColor::BLUE, led::Blink::SLOW);

  // 3- Force all LEDs to Blue + fast blinking
  XLOG(INFO) << "Forcing all LEDs to Blue + fast blinking";
  forceAllLeds(led::LedColor::BLUE, led::Blink::FAST);

  // 4- Force all LEDs to Yellow (no blink)
  XLOG(INFO) << "Forcing all LEDs to Yellow";
  forceAllLeds(led::LedColor::YELLOW, led::Blink::OFF);

  // 5- Force all LEDs to Yellow + slow blinking
  XLOG(INFO) << "Forcing all LEDs to Yellow + slow blinking";
  forceAllLeds(led::LedColor::YELLOW, led::Blink::SLOW);

  // 6- Force all LEDs to Yellow + fast blinking
  XLOG(INFO) << "Forcing all LEDs to Yellow + fast blinking";
  forceAllLeds(led::LedColor::YELLOW, led::Blink::FAST);

  // 7- Force all LEDs to Off + no blink
  XLOG(INFO) << "Forcing all LEDs to Off";
  forceAllLeds(led::LedColor::OFF, led::Blink::OFF);
}

} // namespace facebook::fboss

int main(int argc, char* argv[]) {
  // Parse command line flags
  testing::InitGoogleTest(&argc, argv);

  folly::Init init(&argc, &argv);

  return RUN_ALL_TESTS();
}
