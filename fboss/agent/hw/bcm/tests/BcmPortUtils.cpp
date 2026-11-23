/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/test/HwPortUtils.h"

#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPortTable.h"
#include "fboss/agent/hw/bcm/BcmPortUtils.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/platforms/tests/utils/BcmTestPlatform.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

extern "C" {
#include <bcm/port.h>
}

namespace {
bcm_port_t getBcmPort(facebook::fboss::PortID port) {
  return static_cast<bcm_port_t>(port);
}

int getUnit(const facebook::fboss::HwSwitch* hw) {
  CHECK(hw);
  return static_cast<const facebook::fboss::BcmSwitch*>(hw)->getUnit();
}
} // namespace

namespace facebook::fboss::utility {
bool portEnabled(const HwSwitch* hw, PortID port) {
  int enable = -1;
  auto rv = bcm_port_enable_get(getUnit(hw), getBcmPort(port), &enable);
  bcmCheckError(rv, "failed to get port enable status");
  CHECK(enable == 0 || enable == 1);
  return (enable == 1);
}

cfg::PortSpeed curPortSpeed(const HwSwitch* hw, PortID port) {
  int curSpeed;
  auto ret = bcm_port_speed_get(getUnit(hw), getBcmPort(port), &curSpeed);
  bcmCheckError(ret, "Failed to get current speed for port");
  return cfg::PortSpeed(curSpeed);
}

void assertPort(
    const HwSwitch* hw,
    PortID port,
    bool enabled,
    cfg::PortSpeed speed) {
  CHECK_EQ(enabled, portEnabled(hw, port));
  if (enabled) {
    // Only verify speed on enabled ports
    CHECK_EQ(
        static_cast<int>(speed),
        static_cast<int>(utility::curPortSpeed(hw, port)));
  }
}

void assertPortStatus(const HwSwitch* hw, PortID port) {
  CHECK(portEnabled(hw, port));
}

void assertPortsLoopbackMode(
    const HwSwitch* hw,
    const std::map<PortID, int>& port2LoopbackMode) {
  for (auto portAndLoopBackMode : port2LoopbackMode) {
    assertPortLoopbackMode(
        hw, portAndLoopBackMode.first, portAndLoopBackMode.second);
  }
}

void assertPortSampleDestination(
    const HwSwitch* hw,
    PortID port,
    int expectedSampleDestination) {
  int sampleDestination;
  auto rv = bcm_port_control_get(
      getUnit(hw),
      getBcmPort(port),
      bcmPortControlSampleIngressDest,
      &sampleDestination);
  bcmCheckError(rv, "Failed to get sample destination for port:", port);
  CHECK_EQ(expectedSampleDestination, sampleDestination);
}

void assertPortsSampleDestination(
    const HwSwitch* hw,
    const std::map<PortID, int>& port2SampleDestination) {
  for (auto portAndSampleDestination : port2SampleDestination) {
    assertPortSampleDestination(
        hw, portAndSampleDestination.first, portAndSampleDestination.second);
  }
}

void assertPortLoopbackMode(
    const HwSwitch* hw,
    PortID port,
    int expectedLoopbackMode) {
  int loopbackMode;
  auto rv = bcm_port_loopback_get(getUnit(hw), getBcmPort(port), &loopbackMode);
  bcmCheckError(rv, "Failed to get loopback mode for port:", port);
  CHECK_EQ(expectedLoopbackMode, loopbackMode);
}

void cleanPortConfig(
    cfg::SwitchConfig* /* config */,
    std::vector<PortID> /* allPortsinGroup */) {
  // no need to remove portCfg not in allPortsinGroup for bcm tests
  // do nothing
}

void assertQUADMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    std::vector<PortID> allPortsinGroup) {
  auto portItr = allPortsinGroup.begin();
  for (; portItr != allPortsinGroup.end(); portItr++) {
    utility::assertPort(hw, *portItr, true, enabledLaneSpeed);
  }
}

void assertDUALMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed disabledLaneSpeed,
    std::vector<PortID> allPortsinGroup) {
  bool odd_lane;
  auto portItr = allPortsinGroup.begin();
  for (; portItr != allPortsinGroup.end(); portItr++) {
    odd_lane = (*portItr - allPortsinGroup.front()) % 2 == 0 ? true : false;
    bool enabled = odd_lane ? true : false;
    auto speed = odd_lane ? enabledLaneSpeed : disabledLaneSpeed;
    if (enabled || !hw->getPlatform()->supportsAddRemovePort()) {
      utility::assertPort(hw, *portItr, enabled, speed);
    }
  }
}

void assertSINGLEMode(
    HwSwitch* hw,
    cfg::PortSpeed enabledLaneSpeed,
    cfg::PortSpeed disabledLaneSpeed,
    std::vector<PortID> allPortsinGroup) {
  auto portItr = allPortsinGroup.begin();
  for (; portItr != allPortsinGroup.end(); portItr++) {
    bool enabled = *portItr == allPortsinGroup.front() ? true : false;
    auto speed = enabled ? enabledLaneSpeed : disabledLaneSpeed;
    if (enabled || !hw->getPlatform()->supportsAddRemovePort()) {
      utility::assertPort(hw, *portItr, enabled, speed);
    }
  }
}

void verifyInterfaceMode(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* bcmSwitch = static_cast<BcmSwitch*>(platform->getHwSwitch());
  auto* bcmPort = bcmSwitch->getPortTable()->getBcmPort(portID);
  auto platformPort = bcmPort->getPlatformPort();

  if (!platformPort->shouldUsePortResourceAPIs()) {
    auto speed = bcmPort->getSpeed();
    bcm_port_if_t curMode = bcm_port_if_t(0);
    auto ret = bcm_port_interface_get(bcmSwitch->getUnit(), portID, &curMode);
    bcmCheckError(
        ret, "Failed to get current interface setting for port ", portID);
    // TODO - Feed other transmitter technology, speed and build a
    // exhaustive set of speed, transmitter tech to test for
    // All profiles should have medium info at this point
    if (!expectedProfileConfig.medium()) {
      throw FbossError(
          "Missing medium info in profile ",
          apache::thrift::util::enumNameSafe(profileID));
    }
    auto expectedMode = getSpeedToTransmitterTechAndMode().at(speed).at(
        *expectedProfileConfig.medium());
    EXPECT_EQ(expectedMode, curMode);
  } else {
    // On platforms that use port resource APIs, phy lane config determines
    // the interface mode.
    bcm_port_resource_t portResource;
    auto ret =
        bcm_port_resource_get(bcmSwitch->getUnit(), portID, &portResource);
    bcmCheckError(
        ret, "Failed to get current port resource settings for: ", portID);

    auto expectedPhyLaneConfig =
        utility::getDesiredPhyLaneConfig(expectedProfileConfig);
    EXPECT_EQ(expectedPhyLaneConfig, portResource.phy_lane_config);
  }
}

void verifyTxSettting(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const std::vector<phy::PinConfig>& expectedPinConfigs) {
  auto* bcmSwitch = static_cast<BcmSwitch*>(platform->getHwSwitch());
  int minLane = -1;
  for (const auto& pinCfg : expectedPinConfigs) {
    auto lane = *pinCfg.id()->lane();
    if (minLane < 0 || lane < minLane) {
      minLane = lane;
    }
  }
  for (const auto& pinCfg : expectedPinConfigs) {
    auto expectedTx = pinCfg.tx();
    if (!expectedTx.has_value()) {
      continue;
    }

    auto lane = *pinCfg.id()->lane() - minLane;
    auto* bcmPort = bcmSwitch->getPortTable()->getBcmPort(portID);
    auto programmedTx = bcmPort->getTxSettingsForLane(lane);
    EXPECT_EQ(programmedTx.pre(), expectedTx->pre());
    EXPECT_EQ(programmedTx.main(), expectedTx->main());
    EXPECT_EQ(programmedTx.post(), expectedTx->post());
    EXPECT_EQ(programmedTx.pre2(), expectedTx->pre2());
    EXPECT_EQ(programmedTx.post2(), expectedTx->post2());
    EXPECT_EQ(programmedTx.post3(), expectedTx->post3());
    EXPECT_EQ(programmedTx.driveCurrent(), expectedTx->driveCurrent());
  }
}

bool verifyLedStatus(Platform* platform, PortID port, bool up) {
  auto* bcmPlatform = static_cast<BcmTestPlatform*>(platform);
  return bcmPlatform->verifyLEDStatus(port, up);
}

void verifyRxSettting(
    PortID /*portID*/,
    cfg::PortProfileID /*profileID*/,
    Platform* /*platform*/,
    const std::vector<phy::PinConfig>& /* expectedPinConfigs */) {
  // no rx settings
  return;
}

void verifyFec(
    PortID portID,
    cfg::PortProfileID profileID,
    Platform* platform,
    const phy::ProfileSideConfig& expectedProfileConfig) {
  auto* bcmSwitch = static_cast<BcmSwitch*>(platform->getHwSwitch());
  auto bcmPort = bcmSwitch->getPortTable()->getBcmPort(portID);
  EXPECT_EQ(*expectedProfileConfig.fec(), bcmPort->getFECMode());
}

void enableSixtapProgramming() {
  // Flag only needed for Sai
  return;
};
} // namespace facebook::fboss::utility
