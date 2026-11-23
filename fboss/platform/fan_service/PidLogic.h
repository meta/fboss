// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"

namespace facebook::fboss::platform::fan_service {

// Explaination for PidLogic and IncrementalPidLogic
// https://www.yahboom.net/public/upload/upload-html/1653307952/1.%20PID%20algorithm%20theory.html
class PidLogicBase {
 public:
  explicit PidLogicBase(const PidSetting& pidSetting)
      : pidSetting_(pidSetting) {}
  virtual ~PidLogicBase() = default;
  virtual float calculatePwm(float measurement) = 0;
  float getSetPoint() const {
    return *pidSetting_.setPoint();
  }

  void updateLastPwm(float lastPwm) {
    lastPwm_ = lastPwm;
  }

  float getLastPwm() const {
    return lastPwm_;
  }

 protected:
  const PidSetting pidSetting_;
  float lastPwm_{0};
};

class PidLogic : public PidLogicBase {
 public:
  explicit PidLogic(const PidSetting& pidSetting, unsigned int dT)
      : PidLogicBase(pidSetting), dT_(dT) {
    if (dT_ == 0) {
      throw std::invalid_argument("dT cannot be less than or equal to 0");
    }
  }
  float calculatePwm(float measurement) override;
  float getLastError() const {
    return lastError_;
  }

 private:
  float integral_{0};
  float lastError_{0};
  const unsigned int dT_;
};

class IncrementalPidLogic : public PidLogicBase {
 public:
  explicit IncrementalPidLogic(const PidSetting& pidSetting)
      : PidLogicBase(pidSetting) {}
  float calculatePwm(float measurement) override;

 private:
  float previousRead1_{0};
  float previousRead2_{0};
};

} // namespace facebook::fboss::platform::fan_service
