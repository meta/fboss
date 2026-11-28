// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/platform/fan_service/OvertempCondition.h"

using namespace ::testing;
using namespace facebook::fboss::platform::fan_service;

TEST(OvertempConditionTest, Basic) {
  // No sliding window (that is, sliding window size)
  OvertempCondition overtempCondition;

  overtempCondition.setNumOvertempSensorForShutdown(1);
  overtempCondition.addSensorForTracking("TimmyHick9_Core_Temp", 102.84, 1);
  overtempCondition.addSensorForTracking("OSFP3200_Port1_Core_Temp", 70.1, 1);

  // Make sure the registered sensors are tracked.
  EXPECT_TRUE(overtempCondition.isTracked("TimmyHick9_Core_Temp"));
  EXPECT_TRUE(overtempCondition.isTracked("OSFP3200_Port1_Core_Temp"));

  overtempCondition.processSensorData("TimmyHick9_Core_Temp", 70.4);
  overtempCondition.processSensorData("OSFP3200_Port1_Core_Temp", 60.1);

  // Make sure there is no false positive
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  overtempCondition.processSensorData("OSFP3200_Port1_Core_Temp", 70.2);
  // Make sure there is no false negative
  EXPECT_TRUE(overtempCondition.checkIfOvertemp());
}

TEST(OvertempConditionTest, SlidingWindow) {
  OvertempCondition overtempCondition;
  overtempCondition.setNumOvertempSensorForShutdown(2);
  overtempCondition.addSensorForTracking("Spooktrim7_Core_Temp", 95.7, 3);
  overtempCondition.addSensorForTracking("Retimer_Port1_Temp", 80.1, 5);
  overtempCondition.addSensorForTracking("Zinclake_Hotswap_Temp", 85.0, 4);

  // Make sure there is no false positive
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  overtempCondition.processSensorData("Spooktrim7_Core_Temp", 93.1);
  overtempCondition.processSensorData("Retimer_Port1_Temp", 78.1);
  overtempCondition.processSensorData("Zinclake_Hotswap_Temp", 83.4);
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  overtempCondition.processSensorData("Spooktrim7_Core_Temp", 94.4);
  overtempCondition.processSensorData("Retimer_Port1_Temp", 79.2);
  overtempCondition.processSensorData("Zinclake_Hotswap_Temp", 84.9);
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  overtempCondition.processSensorData("Spooktrim7_Core_Temp", 94.1);
  overtempCondition.processSensorData("Retimer_Port1_Temp", 79.1);
  overtempCondition.processSensorData("Zinclake_Hotswap_Temp", 84.4);
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  // Only one sensor is showing overtemp. No shutdown should be triggered.
  overtempCondition.processSensorData("Spooktrim7_Core_Temp", 140.1);
  overtempCondition.processSensorData("Retimer_Port1_Temp", 79.3);
  overtempCondition.processSensorData("Zinclake_Hotswap_Temp", 84.3);
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  // Overtemp condition should be met after this
  overtempCondition.processSensorData("Spooktrim7_Core_Temp", 110.2);
  overtempCondition.processSensorData("Retimer_Port1_Temp", 79.1);
  overtempCondition.processSensorData("Zinclake_Hotswap_Temp", 120.1);
  EXPECT_TRUE(overtempCondition.checkIfOvertemp());
}

TEST(OvertempConditionTest, SensorWithoutData) {
  OvertempCondition overtempCondition;
  overtempCondition.setNumOvertempSensorForShutdown(1);
  overtempCondition.addSensorForTracking("Asic_Core_Temp", 112.0, 1);
  overtempCondition.addSensorForTracking("Board_Diode_Temp", 122.0, 1);
  EXPECT_FALSE(overtempCondition.checkIfOvertemp());

  // A sensor that never reported must not disable the check for the others.
  overtempCondition.processSensorData("Board_Diode_Temp", 125.3);
  EXPECT_TRUE(overtempCondition.checkIfOvertemp());
}
