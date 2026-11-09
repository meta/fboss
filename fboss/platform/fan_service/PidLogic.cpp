// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <folly/logging/xlog.h>

#include "fboss/platform/fan_service/PidLogic.h"

namespace facebook::fboss::platform::fan_service {

float PidLogic::calculatePwm(float measurement) {
  float newPwm, pwmDelta{0};
  float minVal = *pidSetting_.setPoint() - *pidSetting_.negHysteresis();
  float maxVal = *pidSetting_.setPoint() + *pidSetting_.posHysteresis();
  std::optional<float> error{};

  if (measurement < minVal) {
    error = minVal - measurement;
  } else if (measurement > maxVal) {
    error = maxVal - measurement;
  }

  if (error.has_value()) {
    integral_ = integral_ + *error * dT_;
    auto derivative = (*error - lastError_) / dT_;
    pwmDelta = (*pidSetting_.kp() * error.value()) +
        (*pidSetting_.ki() * integral_) + (*pidSetting_.kd() * derivative);
    newPwm = lastPwm_ + pwmDelta;
  } else {
    newPwm = lastPwm_;
  }

  if (newPwm < 0) {
    newPwm = 0;
  } else if (newPwm > 100) {
    newPwm = 100;
  }

  // When measurement is lower than setPoint for a prolonged period of time,
  // integral_ will grow to a very large value. This will cause delay in
  // increasing pwm when measurement is higher than setPoint. To avoid this,
  // we reset integral_ when measurement is lower than setPoint via
  // back-calculation anti-windup (integral_ = pwmDelta / ki).
  //
  // Guard against ki == 0 (e.g. platforms running P/PD-only control such as
  // morgan800cc): dividing by zero would set integral_ to +/-inf, and on the
  // next iteration ki * integral_ evaluates to 0 * inf = NaN, which then
  // propagates into pwmDelta and newPwm, freezing the fan response to
  // measurement changes. When ki == 0 the integral term is unused, so the
  // anti-windup reset is unnecessary.
  if (*pidSetting_.ki() != 0 && measurement <= maxVal) {
    integral_ = pwmDelta / *pidSetting_.ki();
  }

  XLOG(DBG2) << fmt::format(
      "Measurement: {}, Error: {}, Last PWM: {}, integral_: {}, "
      "PWM Delta: {}, New PWM: {}",
      measurement,
      error.value_or(0),
      lastPwm_,
      integral_,
      pwmDelta,
      newPwm);

  lastPwm_ = newPwm;
  lastError_ = error.value_or(0);

  return newPwm;
}

float IncrementalPidLogic::calculatePwm(float measurement) {
  float pwmDelta{0};
  float minVal = *pidSetting_.setPoint() - *pidSetting_.negHysteresis();
  float maxVal = *pidSetting_.setPoint() + *pidSetting_.posHysteresis();

  if ((measurement > maxVal) || (measurement < minVal)) {
    pwmDelta = (*pidSetting_.kp() * (measurement - previousRead1_)) +
        (*pidSetting_.ki() * (measurement - *pidSetting_.setPoint())) +
        (*pidSetting_.kd() *
         (measurement - 2 * previousRead1_ + previousRead2_));
  }
  float newPwm = lastPwm_ + pwmDelta;

  XLOG(DBG2) << fmt::format(
      "Measurement: {}, Previous Read 1: {}, Previous Read 2: {}, "
      "Last PWM: {}, New PWM: {}",
      measurement,
      previousRead1_,
      previousRead2_,
      lastPwm_,
      newPwm);

  previousRead2_ = previousRead1_;
  previousRead1_ = measurement;
  lastPwm_ = newPwm;

  return newPwm;
}

} // namespace facebook::fboss::platform::fan_service
