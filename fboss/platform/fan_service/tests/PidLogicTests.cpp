// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include "fboss/platform/fan_service/PidLogic.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"

using namespace ::testing;
using namespace facebook::fboss::platform::fan_service;

TEST(PIDLogicTest, Basic) {
  PidSetting pidSetting;
  pidSetting.kp() = -3;
  pidSetting.ki() = -0.02;
  pidSetting.kd() = 0;
  pidSetting.setPoint() = 60;
  pidSetting.negHysteresis() = 2;
  pidSetting.posHysteresis() = 0;

  // CASE 1: The read value is lower than setPoint, and the previous target PWM
  //         is too high. PWM should go down. (60 -> 35.2)
  auto pidLogic1 = PidLogic(pidSetting, 5);
  pidLogic1.updateLastPwm(60);
  EXPECT_FLOAT_EQ(pidLogic1.calculatePwm(50), 35.2);
  EXPECT_FLOAT_EQ(pidLogic1.getLastError(), 8);

  // CASE 2: The read value is within desired range.
  //         PWM should not change. (30 -> 30)
  auto pidLogic2 = PidLogic(pidSetting, 5);
  pidLogic2.updateLastPwm(30);
  EXPECT_FLOAT_EQ(pidLogic2.calculatePwm(59), 30);
  EXPECT_FLOAT_EQ(pidLogic2.getLastError(), 0);

  // CASE 3: The read value is higher than setPoint, and the current target PWM
  //         is too low. PWM should go up a bit. (30 -> 54.8)
  auto pidLogic3 = PidLogic(pidSetting, 5);
  pidLogic3.updateLastPwm(30);
  EXPECT_FLOAT_EQ(pidLogic3.calculatePwm(68), 54.8);
  EXPECT_FLOAT_EQ(pidLogic3.getLastError(), -8);

  // CASE 4: Read value is higher than setPoint, PWM should go up (99 -> 100)
  auto pidLogic4 = PidLogic(pidSetting, 5);
  pidLogic4.updateLastPwm(99);
  EXPECT_FLOAT_EQ(pidLogic4.calculatePwm(68), 100);
  EXPECT_FLOAT_EQ(pidLogic4.getLastError(), -8);
}

TEST(PIDLogicTest, Convergence) {
  PidSetting pidSetting;
  pidSetting.kp() = -0.05;
  pidSetting.ki() = -0.01;
  pidSetting.kd() = 0;
  pidSetting.setPoint() = 70;
  pidSetting.negHysteresis() = 2;
  pidSetting.posHysteresis() = 0;
  auto pidLogic = PidLogic(pidSetting, 30);
  pidLogic.updateLastPwm(40);
  double measurement = 80;
  int max_steps = 50;
  bool setpointReached = false;
  for (int i = 0; i < max_steps; ++i) {
    auto lastPwm = pidLogic.getLastPwm();
    auto newPwm = pidLogic.calculatePwm(measurement);
    measurement = measurement - (newPwm - lastPwm); // Simulate system response
    XLOG(DBG3) << fmt::format(
        "Iteration {}: Measurement = {}, newPwm = {}, Error = {}",
        i,
        measurement,
        newPwm,
        *pidSetting.setPoint() - measurement);
    if (*pidSetting.setPoint() - *pidSetting.negHysteresis() <= measurement &&
        measurement <= *pidSetting.setPoint() + *pidSetting.posHysteresis()) {
      setpointReached = true;
      break;
    }
  }
  EXPECT_TRUE(setpointReached);
}

TEST(PIDLogicTest, Basic2) {
  PidSetting pidSetting;
  pidSetting.kp() = -4;
  pidSetting.ki() = -0.06;
  pidSetting.kd() = 0;
  pidSetting.setPoint() = 40;
  pidSetting.negHysteresis() = 3;
  pidSetting.posHysteresis() = 0;

  auto pidLogic1 = PidLogic(pidSetting, 5);
  pidLogic1.updateLastPwm(0);
  for (int measurement = 0; measurement < 100; measurement += 1) {
    auto newPwm = pidLogic1.calculatePwm(measurement);
    if (measurement < 40) {
      ASSERT_FLOAT_EQ(newPwm, 0);
    }
    if (measurement > 40) {
      ASSERT_GT(newPwm, 0);
    }
    if (measurement > 50) {
      ASSERT_FLOAT_EQ(newPwm, 100);
    }
    XLOG(DBG3) << fmt::format(
        "Measurement = {}, newPwm = {}", measurement, newPwm);
  }
}

TEST(IncrementalPIDLogicTest, Basic) {
  PidSetting pidSetting;
  pidSetting.kp() = -0.3;
  pidSetting.ki() = 1.0;
  pidSetting.kd() = -0.3;
  pidSetting.setPoint() = 50;
  pidSetting.negHysteresis() = 2;
  pidSetting.posHysteresis() = 2;

  auto incrementalPidLogic1 = IncrementalPidLogic(pidSetting);
  incrementalPidLogic1.updateLastPwm(40);
  // Emulates the system start-up situation where fan service starts,
  // then controlled fans to eventually decrease the system temperature
  incrementalPidLogic1.calculatePwm(30);
  incrementalPidLogic1.calculatePwm(40);
  incrementalPidLogic1.calculatePwm(55);
  auto initialPwm = incrementalPidLogic1.calculatePwm(50);

  // 1. Pos hysteresis check
  // Measurement within posHysteresis from set point. Pwm should not change
  // Hysteresis for preventing small oscillations around the set point.
  auto newPwm1 = incrementalPidLogic1.calculatePwm(51);
  ASSERT_EQ(newPwm1, initialPwm);

  // 2. Neg hysteresis check
  // Measurement within negHysteresis from set point. Pwm should not change
  // Hysteresis for preventing small oscillations around the set point.
  newPwm1 = incrementalPidLogic1.calculatePwm(49);
  ASSERT_EQ(newPwm1, initialPwm);

  // 3. Pwm increase check-1
  // Temperature going up multiple times. PWM should increase
  incrementalPidLogic1.calculatePwm(53);
  incrementalPidLogic1.calculatePwm(54);
  auto newPwm2 = incrementalPidLogic1.calculatePwm(55);
  ASSERT_GT(newPwm2, newPwm1);

  // 4. Pwm increase check-2
  // Increased pwm did not lower the measurement.
  // Temperature remains same, so system should increase pwm
  // so that it will go down to the setPoint
  auto newPwm3 = incrementalPidLogic1.calculatePwm(55);
  ASSERT_GT(newPwm3, newPwm2);

  // 5. Pwm decrease check-1
  // Temperature decreases below setPoint. PWM should go down
  incrementalPidLogic1.calculatePwm(53);
  incrementalPidLogic1.calculatePwm(50);
  incrementalPidLogic1.calculatePwm(47);
  auto newPwm4 = incrementalPidLogic1.calculatePwm(44);
  ASSERT_LT(newPwm4, newPwm3);

  // 6. Pwm decrease check-2
  // Temperature remains the same (low) even with low PWM.
  // PWM should go even lower
  auto newPwm5 = incrementalPidLogic1.calculatePwm(44);
  ASSERT_LT(newPwm5, newPwm4);
}
