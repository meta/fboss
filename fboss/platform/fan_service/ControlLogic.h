// Copyright 2021- Facebook. All rights reserved.

#pragma once

#include "fboss/platform/fan_service/Bsp.h"
#include "fboss/platform/fan_service/OvertempCondition.h"
#include "fboss/platform/fan_service/PidLogic.h"
#include "fboss/platform/fan_service/SensorData.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_types.h"
#include "fboss/platform/helpers/StructuredLogger.h"

namespace facebook::fboss::platform::fan_service {

struct SensorReadCache {
  // This is the last read value from the sensor which has been processed.
  float processedReadValue{0};
  // This is the last read value from the sensor which is yet to be processed.
  float lastReadValue{0};
  float targetPwmCache{0};
  uint64_t lastUpdatedTime{0};
  bool sensorFailed{false};
};

// ControlLogic class is a part of Fan Service
// Role : This class contains the logics to detect sensor/fan failures and,
//        the logics to calculate the PWM values from sensor reading.
//        Currently supports PID, Incremental PID and Four Tables method
class ControlLogic {
 public:
  ControlLogic(FanServiceConfig config, std::shared_ptr<Bsp> bsp);
  ~ControlLogic() = default;

  void updateControl(std::shared_ptr<SensorData> pS);
  void setTransitionValue();
  const std::map<std::string, FanStatus> getFanStatuses() {
    return fanStatuses_.copy();
  }
  const std::map<std::string, SensorReadCache>& getSensorCaches() {
    return sensorReadCaches_;
  }

  void controlFan();
  bool getAllTemperatureData();
  void getSensorDataThrift(std::shared_ptr<SensorData> pSensorData) const {
    return pBsp_->getSensorDataThrift(pSensorData);
  }
  const SensorData& sensorData() const {
    return *(pSensorData_.get());
  }
  uint64_t lastSensorFetchTimeSec() const {
    return lastSensorFetchTimeSec_;
  }
  unsigned int getControlFrequency() const;
  unsigned int getSensorFetchFrequency() const;

 private:
  const FanServiceConfig config_;
  helpers::StructuredLogger structuredLogger_;
  OvertempCondition overtempCondition_;
  std::vector<std::string> overtempWatchList_;
  std::shared_ptr<Bsp> pBsp_;
  // The timestamp of the last PWM control logic execution
  uint64_t lastControlExecutionTimeSec_{0};
  // The timestamp of the last sensor data fetch
  uint64_t lastSensorFetchTimeSec_{0};

  void setupPidLogics();
  int updateSensorPwms(const SensorData& sensorData, int numFanFailed);
  std::tuple<
      bool /*fanAccessFailed*/,
      int /*rpm*/,
      uint64_t /*timestamp*/,
      bool /*fanAbsent*/>
  readFanRpm(const Fan& fan);
  int updateOpticsPwms(SensorData& sensorData);
  bool /* pwm update fail */
  programFan(const Zone& zone, const Fan& fan, float fanPwm);
  float calculateZonePwm(const Zone& zone, bool boostMode);
  void updateTargetPwm(const Sensor& sensorItem, int numFanFailed);
  void programLed(const Fan& fan, bool fanFailed);
  bool isFanPresentInDevice(const Fan& fan);
  float calculateFanPwm(uint16_t slope, float currentFanPwm, float zonePwm);
  void updatePwmState(const Zone& zone, float fanPwm);

  folly::Synchronized<std::map<std::string /* fanName */, FanStatus>>
      fanStatuses_;
  std::map<std::string /* sensorName */, SensorReadCache> sensorReadCaches_;
  std::map<std::string /* sensorName */, float /* pwm */> opticReadCaches_;
  std::map<std::string /* sensorName */, std::unique_ptr<PidLogicBase>>
      pidLogics_;
  std::shared_ptr<SensorData> pSensorData_{nullptr};
  bool boostMode_{false};
};
} // namespace facebook::fboss::platform::fan_service
