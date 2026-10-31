// Copyright 2021- Facebook. All rights reserved.

// Implementation of Control Logic class. Refer to .h file
// for function description

#include "fboss/platform/fan_service/ControlLogic.h"

#include <folly/ScopeGuard.h>
#include <folly/logging/xlog.h>
#include <gpiod.h>

#include <set>

#include "fboss/agent/FbossError.h"
#include "fboss/lib/GpiodLine.h"
#include "fboss/platform/fan_service/FanServiceConfigTypes.h"
#include "fboss/platform/fan_service/SensorData.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_constants.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"

namespace {

auto constexpr kDefaultSensorReadFrequencyInSec = 30;
auto constexpr kDefaultFanControlFrequencyInSec = 30;

auto constexpr kFanWriteFailure = "{}.{}.pwm_write.failure";
auto constexpr kFanWriteValue = "{}.{}.pwm_write.value";
auto constexpr kFanAbsent = "{}.absent";
auto constexpr kFanReadRpmFailure = "{}.rpm_read.failure";
auto constexpr kFanReadRpmValue = "{}.rpm_read.value";
auto constexpr kSensorReadFailure = "{}.sensor_read.failure";
auto constexpr kSensorReadValue = "{}.sensor_read.value";
auto constexpr kOpticsReadMaxValue = "{}.optics_read.max.value";
auto constexpr kOpticsPwmValue = "{}.optics_pwm.value";
auto constexpr kOpticsNoProfile = "{}.optics_no_profile";
auto constexpr kOpticsAggPwmValue = "agg.optics_pwm.value";
auto constexpr kLedWriteFailure = "{}.led_write.failure";
auto constexpr kHasFanRpmReadFailure = "has.fan_rpm_read_failure";
auto constexpr kHasFanPwmWriteFailure = "has.fan_pwm_write_failure";
auto constexpr kHasFanAbsent = "has.fan_absent";
auto constexpr kFanFailThresholdInSec = 300;
auto constexpr kSensorFailThresholdInSec = 300;

using namespace facebook::fboss::platform::fan_service;
namespace constants =
    facebook::fboss::platform::fan_service::fan_service_config_constants;

template <typename T>
std::optional<T> getConfigOpticData(
    const std::string& opticType,
    const std::map<std::string, T>& dataMap) {
  auto it = dataMap.find(opticType);
  if (it != dataMap.end()) {
    return it->second;
  }
  return std::nullopt;
}

} // namespace

namespace facebook::fboss::platform::fan_service {

ControlLogic::ControlLogic(FanServiceConfig config, std::shared_ptr<Bsp> bsp)
    : config_(std::move(config)), structuredLogger_("fan_service"), pBsp_(bsp) {
  pSensorData_ = std::make_shared<SensorData>();

  setupPidLogics();
  overtempWatchList_ = overtempCondition_.setupShutdownConditions(config_);

  XLOG(INFO)
      << "Upon fan_service start up, program all fan pwm with transitional value of "
      << *config_.pwmTransitionValue();
  setTransitionValue();
}

unsigned int ControlLogic::getControlFrequency() const {
  if (config_.controlInterval()) {
    return *config_.controlInterval()->pwmUpdateInterval();
  }
  return kDefaultFanControlFrequencyInSec;
}

unsigned int ControlLogic::getSensorFetchFrequency() const {
  if (config_.controlInterval()) {
    return *config_.controlInterval()->sensorReadInterval();
  }
  return kDefaultSensorReadFrequencyInSec;
}

bool ControlLogic::getAllTemperatureData() {
  bool sensorReadOK = false;
  bool opticsReadOK = false;
  bool asicTempReadOK = false;

  // Get the updated sensor data
  try {
    pBsp_->getSensorData(pSensorData_);
    sensorReadOK = true;
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get sensor data with error : " << e.what();
  }

  try {
    // Get the updated optics data
    pBsp_->getOpticsData(pSensorData_);
    opticsReadOK = true;
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get optics data with error : " << e.what();
  }

  try {
    // Get the ASIC temperature data from sensor_service.
    // The response is converted to the Sensor Service data format by the OSS
    pBsp_->getAsicTempData(pSensorData_);
    asicTempReadOK = true;
  } catch (std::exception& e) {
    XLOG(ERR) << "Failed to get ASIC temperature data with error: " << e.what();
  }

  if (sensorReadOK && opticsReadOK && asicTempReadOK) {
    XLOG(INFO) << "Successfully fetched all temperature data.";
  }
  return sensorReadOK && opticsReadOK && asicTempReadOK;
}

void ControlLogic::controlFan() {
  uint64_t currentTimeSec = pBsp_->getCurrentTime();
  bool temperatureFetchSuccessful = false;
  // Update Sensor Value based according to fetch frequency
  if ((currentTimeSec - lastSensorFetchTimeSec_) >= getSensorFetchFrequency()) {
    temperatureFetchSuccessful = getAllTemperatureData();
    if (temperatureFetchSuccessful) {
      lastSensorFetchTimeSec_ = currentTimeSec;
    }
  }
  // Change Fan PWM as needed according to control execution frequency
  if ((currentTimeSec - lastControlExecutionTimeSec_) >=
      getControlFrequency()) {
    updateControl(pSensorData_);
    lastControlExecutionTimeSec_ = currentTimeSec;
  }

  pBsp_->kickWatchdog();
}

void ControlLogic::setupPidLogics() {
  for (const auto& sensor : *config_.sensors()) {
    if (*sensor.pwmCalcType() == constants::SENSOR_PWM_CALC_TYPE_PID()) {
      auto pidSetting = *sensor.pidSetting();
      pidLogics_.emplace(
          *sensor.sensorName(),
          std::make_unique<PidLogic>(pidSetting, getControlFrequency()));
    } else if (
        *sensor.pwmCalcType() ==
        constants::SENSOR_PWM_CALC_TYPE_INCREMENTAL_PID()) {
      auto pidSetting = *sensor.pidSetting();
      pidLogics_.emplace(
          *sensor.sensorName(),
          std::make_unique<IncrementalPidLogic>(pidSetting));
    }
  }
  for (const auto& optic : *config_.optics()) {
    if (*optic.aggregationType() == constants::OPTIC_AGGREGATION_TYPE_PID()) {
      for (const auto& [opticType, pidSetting] : *optic.pidSettings()) {
        auto cacheKey = *optic.opticName() + opticType;
        pidLogics_.emplace(
            cacheKey,
            std::make_unique<PidLogic>(pidSetting, getControlFrequency()));
      }
    } else if (
        *optic.aggregationType() ==
        constants::OPTIC_AGGREGATION_TYPE_INCREMENTAL_PID()) {
      for (const auto& [opticType, pidSetting] : *optic.pidSettings()) {
        auto cacheKey = *optic.opticName() + opticType;
        pidLogics_.emplace(
            cacheKey, std::make_unique<IncrementalPidLogic>(pidSetting));
      }
    }
  }
}

std::tuple<bool, int, uint64_t, bool> ControlLogic::readFanRpm(const Fan& fan) {
  std::string fanName = *fan.fanName();
  bool fanRpmReadSuccess{false};
  bool fanAbsent{false};
  int fanRpm{0};
  uint64_t rpmTimeStamp{0};
  fanAbsent = !isFanPresentInDevice(fan);
  if (!fanAbsent) {
    try {
      fanRpm = pBsp_->readSysfs(*fan.rpmSysfsPath());
      rpmTimeStamp = pBsp_->getCurrentTime();
      fanRpmReadSuccess = true;
    } catch (std::exception&) {
      XLOG(ERR) << fmt::format(
          "{}: Failed to read rpm from {}", fanName, *fan.rpmSysfsPath());
    }
  }
  fb303::fbData->setCounter(
      fmt::format(kFanReadRpmFailure, fanName), !fanRpmReadSuccess);
  if (fanRpmReadSuccess) {
    fb303::fbData->setCounter(fmt::format(kFanReadRpmValue, fanName), fanRpm);
    XLOG(INFO) << fmt::format("{}: RPM read is {}", fanName, fanRpm);
  }
  return std::make_tuple(!fanRpmReadSuccess, fanRpm, rpmTimeStamp, fanAbsent);
}

void ControlLogic::updateTargetPwm(const Sensor& sensor, int numFanFailed) {
  float targetPwm{0};
  TempToPwmMap tableToUse;
  auto& readCache = sensorReadCaches_[*sensor.sensorName()];
  const auto& pwmCalcType = *sensor.pwmCalcType();

  if (pwmCalcType == constants::SENSOR_PWM_CALC_TYPE_FOUR_LINEAR_TABLE()) {
    float previousSensorValue = readCache.processedReadValue;
    float sensorValue = readCache.lastReadValue;
    bool deadFanExists = (numFanFailed > 0);
    bool moreThanOneDeadFanExists = (numFanFailed > 1);
    bool accelerate =
        (previousSensorValue == 0) || (sensorValue > previousSensorValue);
    if (!deadFanExists) {
      tableToUse =
          accelerate ? *sensor.normalUpTable() : *sensor.normalDownTable();
    } else if (moreThanOneDeadFanExists) {
      tableToUse = accelerate
          ? (sensor.twoRotorsFailUpTable() ? *sensor.twoRotorsFailUpTable()
                                           : *sensor.failUpTable())
          : (sensor.twoRotorsFailDownTable() ? *sensor.twoRotorsFailDownTable()
                                             : *sensor.failDownTable());
    } else {
      tableToUse = accelerate ? *sensor.failUpTable() : *sensor.failDownTable();
    }

    // Start with the lowest value
    targetPwm = tableToUse.begin()->second;
    for (const auto& [temp, pwm] : tableToUse) {
      if (sensorValue > temp) {
        targetPwm = pwm;
      }
    }
    if (accelerate) {
      // If accleration is needed, new pwm should be bigger than old value
      if (targetPwm < readCache.targetPwmCache) {
        targetPwm = readCache.targetPwmCache;
      }
    } else {
      // If deceleration is needed, new pwm should be smaller than old one
      if (targetPwm > readCache.targetPwmCache) {
        targetPwm = readCache.targetPwmCache;
      }
    }
    readCache.processedReadValue = sensorValue;
  }

  if (pwmCalcType == constants::SENSOR_PWM_CALC_TYPE_PID() ||
      pwmCalcType == constants::SENSOR_PWM_CALC_TYPE_INCREMENTAL_PID()) {
    targetPwm = pidLogics_.at(*sensor.sensorName())
                    ->calculatePwm(readCache.lastReadValue);
  }

  XLOG(INFO) << fmt::format(
      "{}: Calculated PWM is {}", *sensor.sensorName(), targetPwm);
  readCache.targetPwmCache = targetPwm;
}

int ControlLogic::updateSensorPwms(
    const SensorData& sensorData,
    int numFanFailed) {
  int numSensorFailed = 0;
  for (const auto& sensor : *config_.sensors()) {
    bool sensorAccessFail = false;
    const auto sensorName = *sensor.sensorName();
    auto& readCache = sensorReadCaches_[sensorName];

    auto sensorEntry = sensorData.getSensorEntry(sensorName);
    if (sensorEntry) {
      readCache.lastReadValue = sensorEntry->value;
      readCache.lastUpdatedTime = sensorEntry->lastUpdated;
      readCache.sensorFailed = false;
      XLOG(INFO) << fmt::format(
          "{}: Sensor read value is {}", sensorName, sensorEntry->value);
    } else {
      XLOG(ERR) << fmt::format(
          "{}: Failure to get data (either wrong entry or read failure)",
          sensorName);
      sensorAccessFail = true;
    }

    fb303::fbData->setCounter(
        fmt::format(kSensorReadFailure, sensorName), sensorAccessFail);
    fb303::fbData->setCounter(
        fmt::format(kSensorReadValue, sensorName), readCache.lastReadValue);
    if (sensorAccessFail) {
      // If the sensor data cache is stale for a while, we consider it as the
      // failure of such sensor
      uint64_t timeDiffInSec =
          pBsp_->getCurrentTime() - readCache.lastUpdatedTime;
      if (timeDiffInSec >= kSensorFailThresholdInSec) {
        readCache.sensorFailed = true;
        numSensorFailed++;
      }
    }

    updateTargetPwm(sensor, numFanFailed);
  }
  return numSensorFailed;
}

int ControlLogic::updateOpticsPwms(SensorData& sensorData) {
  int numOpticsNoProfile = 0;
  bool readAnyOpticData = false;
  std::set<std::string> opticTypesSeen;
  for (const auto& optic : *config_.optics()) {
    std::string opticName = *optic.opticName();

    auto opticEntry = sensorData.getOpticEntry(opticName);
    if (!opticEntry || opticEntry->data.empty()) {
      continue;
    }
    readAnyOpticData = true;

    float aggOpticPwm = 0;
    const auto& aggregationType = *optic.aggregationType();

    if (aggregationType != constants::OPTIC_AGGREGATION_TYPE_MAX() &&
        aggregationType != constants::OPTIC_AGGREGATION_TYPE_PID() &&
        aggregationType !=
            constants::OPTIC_AGGREGATION_TYPE_INCREMENTAL_PID()) {
      throw facebook::fboss::FbossError(
          "Only max and pid aggregation is supported for optics!");
    }

    for (const auto& [opticType, opticDataList] : opticEntry->data) {
      float maxTemp = 0.0;
      int maxTempTxvrId = -1;
      for (const auto& opticData : opticDataList) {
        if (opticData.temp > maxTemp) {
          maxTemp = opticData.temp;
          maxTempTxvrId = opticData.txvrId;
        }
      }

      float pwmForThis = 0;
      bool profileFound = true;

      if (aggregationType == constants::OPTIC_AGGREGATION_TYPE_MAX()) {
        auto tablePointer =
            getConfigOpticData<TempToPwmMap>(opticType, *optic.tempToPwmMaps());
        if (tablePointer) {
          pwmForThis = tablePointer->begin()->second;
          for (const auto& [temp, pwm] : *tablePointer) {
            if (maxTemp >= temp) {
              pwmForThis = pwm;
            }
          }
        } else {
          profileFound = false;
        }
      } else {
        auto pidSetting =
            getConfigOpticData<PidSetting>(opticType, *optic.pidSettings());
        if (pidSetting) {
          // Safe: opticType is a key of optic.pidSettings(), and
          // setupPidLogics() creates a PidLogic for every such key whenever
          // aggregationType is PID or INCREMENTAL_PID.
          std::string cacheKey = opticName + opticType;
          pwmForThis = pidLogics_.at(cacheKey)->calculatePwm(maxTemp);
        } else {
          profileFound = false;
        }
      }

      // Publish the temperature even for an optic type we cannot control, so
      // that it stays visible in monitoring instead of disappearing entirely.
      opticTypesSeen.insert(opticType);
      fb303::fbData->setCounter(
          fmt::format(kOpticsReadMaxValue, opticType), maxTemp);
      fb303::fbData->setCounter(
          fmt::format(kOpticsNoProfile, opticType), profileFound ? 0 : 1);

      // Deliberately no pwm counter in the no-profile branch. A PWM was never
      // computed, and publishing 0 would be indistinguishable from a cool
      // optic asking for no additional cooling.
      if (!profileFound) {
        numOpticsNoProfile++;
        XLOG(ERR) << fmt::format(
            "{}: Max optic temp is {} from txvr {}. No thermal profile "
            "configured, so excluding from fan control",
            opticType,
            maxTemp,
            maxTempTxvrId);
      } else {
        XLOG(INFO) << fmt::format(
            "{}: Max optic temp is {} from txvr {}. PWM is {}",
            opticType,
            maxTemp,
            maxTempTxvrId,
            pwmForThis);

        fb303::fbData->setCounter(
            fmt::format(kOpticsPwmValue, opticType), pwmForThis);

        if (pwmForThis > aggOpticPwm) {
          aggOpticPwm = pwmForThis;
        }
      }
    }

    XLOG(INFO) << fmt::format(
        "Optics: {}. Aggregation Type: {}. Aggregate PWM is {}",
        opticName,
        aggregationType,
        aggOpticPwm);
    fb303::fbData->setCounter(kOpticsAggPwmValue, aggOpticPwm);
    opticReadCaches_[opticName] = aggOpticPwm;
    sensorData.updateOpticDataProcessingTimestamp(
        opticName, opticEntry->qsfpServiceTimeStamp);
  }

  // These counters are keyed by optic type, which is a property of the
  // installed hardware rather than of the config, and fb303 keeps a key until
  // the process exits. Reconcile against the full set of optic types every
  // pass so that an optic type which is no longer present stops reporting
  // instead of latching its last value. Done once here rather than per optic
  // group, since the keys are shared across groups.
  //
  // Only reconcile when we actually read something. Optic data goes empty
  // whenever qsfp_service is unreachable or its data has gone stale, and
  // clearing then would drop every optic temperature from monitoring at the
  // exact moment fan control starts running on a stale cache.
  if (readAnyOpticData) {
    for (const auto& opticType : allOpticTypes()) {
      if (!opticTypesSeen.contains(opticType)) {
        fb303::fbData->clearCounter(
            fmt::format(kOpticsReadMaxValue, opticType));
        fb303::fbData->clearCounter(fmt::format(kOpticsPwmValue, opticType));
        fb303::fbData->clearCounter(fmt::format(kOpticsNoProfile, opticType));
      }
    }
  }
  return numOpticsNoProfile;
}

bool ControlLogic::isFanPresentInDevice(const Fan& fan) {
  unsigned int readVal;
  bool readSuccessful = false;
  bool fanPresent = false;
  if (fan.presenceSysfsPath()) {
    try {
      readVal =
          static_cast<unsigned>(pBsp_->readSysfs(*fan.presenceSysfsPath()));
      readSuccessful = true;
    } catch (std::exception&) {
      XLOG(ERR) << "Failed to read sysfs " << *fan.presenceSysfsPath();
    }
    fanPresent = (readSuccessful && readVal == *fan.fanPresentVal());
    if (fanPresent) {
      XLOG(INFO) << fmt::format(
          "{}: is present in the host (through sysfs)", *fan.fanName());
    } else {
      XLOG(INFO) << fmt::format(
          "{}: is absent in the host (through sysfs)", *fan.fanName());
    }
  } else if (fan.presenceGpio()) {
    struct gpiod_chip* chip =
        gpiod_chip_open(fan.presenceGpio()->path()->c_str());
    SCOPE_EXIT {
      if (chip) {
        gpiod_chip_close(chip);
      }
    };
    int value = GpiodLine(chip, *fan.presenceGpio()->lineIndex(), "gpioline")
                    .getValue();
    if (value == *fan.presenceGpio()->desiredValue()) {
      fanPresent = true;
      XLOG(INFO) << fmt::format(
          "{}: is present in the host (through gpio)", *fan.fanName());
    } else {
      XLOG(INFO) << fmt::format(
          "{}: is absent in the host (through gpio)", *fan.fanName());
    }
  }
  fb303::fbData->setCounter(
      fmt::format(kFanAbsent, *fan.fanName()), !fanPresent);
  return fanPresent;
}

bool ControlLogic::programFan(const Zone& zone, const Fan& fan, float fanPwm) {
  bool writeSuccess{false};

  int pwmRawValue = static_cast<int>(
      ((*fan.pwmMax()) - (*fan.pwmMin())) * fanPwm / 100.0 + *fan.pwmMin());
  if (pwmRawValue < *fan.pwmMin()) {
    pwmRawValue = *fan.pwmMin();
  } else if (pwmRawValue > *fan.pwmMax()) {
    pwmRawValue = *fan.pwmMax();
  }
  writeSuccess = pBsp_->setFanPwmSysfs(*fan.pwmSysfsPath(), pwmRawValue);
  fb303::fbData->setCounter(
      fmt::format(kFanWriteFailure, *zone.zoneName(), *fan.fanName()),
      !writeSuccess);
  if (writeSuccess) {
    fb303::fbData->setCounter(
        fmt::format(kFanWriteValue, *zone.zoneName(), *fan.fanName()), fanPwm);
    XLOG(INFO) << fmt::format(
        "{}: Programmed with PWM {} (raw value {})",
        *fan.fanName(),
        fanPwm,
        pwmRawValue);
  } else {
    XLOG(INFO) << fmt::format(
        "{}: Failed to program with PWM {} (raw value {})",
        *fan.fanName(),
        fanPwm,
        pwmRawValue);
    structuredLogger_.logAlert(
        "fan_pwm_write_failure",
        fmt::format("Failed to program {} with PWM {}", *fan.fanName(), fanPwm),
        {{"zone_name", *zone.zoneName()},
         {"fan_name", *fan.fanName()},
         {"pwm_value", std::to_string(fanPwm)},
         {"pwm_raw_value", std::to_string(pwmRawValue)}});
  }

  return !writeSuccess;
}

void ControlLogic::programLed(const Fan& fan, bool fanFailed) {
  auto ledToWriteRef =
      fanFailed ? fan.failLedSysfsPath() : fan.goodLedSysfsPath();
  if (!ledToWriteRef.has_value()) {
    XLOG(INFO) << fmt::format(
        "{}: FAN LED sysfs path is empty. It's likely that FAN LED is controlled by hardware.",
        *fan.fanName());
    return;
  }
  std::string ledPath = *ledToWriteRef;
  XLOG(INFO) << fmt::format(
      "{}: Turning LED on ({})", *fan.fanName(), (fanFailed ? "Fail" : "Good"));
  bool ret = pBsp_->turnOnLedSysfs(ledPath);
  if (!ret) {
    XLOG(ERR) << fmt::format(
        "{}: Failed to set LED sysfs path {}", *fan.fanName(), ledPath);
  }
  fb303::fbData->setCounter(
      fmt::format(kLedWriteFailure, *fan.fanName()), !ret);
}

float ControlLogic::calculateZonePwm(const Zone& zone, bool boostMode) {
  auto zoneType = *zone.zoneType();
  float zonePwm{0};
  int totalPwmConsidered{0};
  for (const auto& sensorName : *zone.sensorNames()) {
    float pwmForThisSensor;
    if (sensorReadCaches_.find(sensorName) != sensorReadCaches_.end()) {
      pwmForThisSensor = sensorReadCaches_[sensorName].targetPwmCache;
    } else if (opticReadCaches_.find(sensorName) != opticReadCaches_.end()) {
      pwmForThisSensor = opticReadCaches_[sensorName];
    } else {
      continue;
    }
    if (zoneType == constants::ZONE_TYPE_MAX()) {
      zonePwm = std::max(zonePwm, pwmForThisSensor);
    } else if (zoneType == constants::ZONE_TYPE_MIN()) {
      zonePwm = std::min(zonePwm, pwmForThisSensor);
    } else if (zoneType == constants::ZONE_TYPE_AVG()) {
      zonePwm += pwmForThisSensor;
    } else {
      XLOG(ERR) << "Undefined Zone Type for zone : " << *zone.zoneName();
    }
    totalPwmConsidered++;
  }
  if (zoneType == constants::ZONE_TYPE_AVG() && totalPwmConsidered != 0) {
    zonePwm /= totalPwmConsidered;
  }
  if (boostMode) {
    zonePwm = std::max(zonePwm, static_cast<float>(*config_.pwmBoostValue()));
  }

  XLOG(INFO) << fmt::format(
      "{}: Components: {}. Aggregation Type: {}. Aggregate PWM is {}.",
      *zone.zoneName(),
      folly::join(",", *zone.sensorNames()),
      zoneType,
      zonePwm);

  return zonePwm;
}

void ControlLogic::setTransitionValue() {
  fanStatuses_.withWLock([&](auto& fanStatuses) {
    for (const auto& zone : *config_.zones()) {
      std::unordered_set<std::string> zoneFans(
          zone.fanNames()->begin(), zone.fanNames()->end());
      for (const auto& fan : *config_.fans()) {
        if (!zoneFans.contains(*fan.fanName())) {
          continue;
        }
        float fanPwm = *config_.pwmTransitionValue();
        bool fanFailed = programFan(zone, fan, fanPwm);

        for (auto& [key, pidLogic] : pidLogics_) {
          pidLogic->updateLastPwm(fanPwm);
        }
        fanStatuses[*fan.fanName()].pwmToProgram() = fanPwm;
        if (fanFailed) {
          programLed(fan, fanFailed);
        }
        fanStatuses[*fan.fanName()].fanFailed() = fanFailed;
      }
    }
  });
}

void ControlLogic::updateControl(std::shared_ptr<SensorData> pS) {
  int numFanFailed = 0;

  // If we have not yet successfully read so far,
  // it does not make sense to update fan PWM out of no data.
  // So check if we read sensor data at least once. If not,
  // do it now.
  if (!pBsp_->checkIfInitialSensorDataRead()) {
    XLOG(INFO) << "Reading sensors for the first time";
    pBsp_->getSensorData(pS);
  }

  // STEP 1: Check presence/rpm of fans
  XLOG(INFO) << "Processing Fans ...";
  bool hasAnyRpmFailure{false};
  bool hasAnyFanAbsent{false};
  fanStatuses_.withWLock([&](auto& fanStatuses) {
    // Update fan status with new rpm and timestamp.
    for (const auto& fan : *config_.fans()) {
      auto [fanAccessFailed, fanRpm, fanTimestamp, fanAbsent] = readFanRpm(fan);
      hasAnyRpmFailure = hasAnyRpmFailure || fanAccessFailed;
      hasAnyFanAbsent = hasAnyFanAbsent || fanAbsent;
      if (fanAccessFailed) {
        fanStatuses[*fan.fanName()].rpm().reset();
      } else {
        fanStatuses[*fan.fanName()].rpm() = fanRpm;
        fanStatuses[*fan.fanName()].lastSuccessfulAccessTime() = fanTimestamp;
      }
      // Ignore last access failure if it happened < kFanFailThresholdInSec
      auto timeSinceLastSuccessfulAccess = pBsp_->getCurrentTime() -
          *fanStatuses[*fan.fanName()].lastSuccessfulAccessTime();
      auto fanFailed = fanAccessFailed &&
          (timeSinceLastSuccessfulAccess >= kFanFailThresholdInSec);
      // If rpmMin or rpmMax is set, compare fanRpm with them to decide
      // whether fan is failed
      if (fan.rpmMin()) {
        if (fanRpm < *fan.rpmMin()) {
          fanFailed = true;
          XLOG(ERR) << fmt::format(
              "fan {} : rpm {} is below the minimum value {}",
              *fan.fanName(),
              fanRpm,
              *fan.rpmMin());
        }
      }
      if (fan.rpmMax()) {
        if (fanRpm > *fan.rpmMax()) {
          fanFailed = true;
          XLOG(ERR) << fmt::format(
              "fan {} : rpm {} is above the maximum value {}",
              *fan.fanName(),
              fanRpm,
              *fan.rpmMax());
        }
      }
      if (fanFailed) {
        numFanFailed++;
      }
      fanStatuses[*fan.fanName()].fanFailed() = fanFailed;
    }
  });

  // STEP 2: Read sensor values and calculate their PWM
  XLOG(INFO) << "Processing Sensors ...";
  int numSensorFailed = updateSensorPwms(*pS, numFanFailed);

  // STEP 3: Read optics values and calculate their PWM
  XLOG(INFO) << "Processing Optics ...";
  int numOpticsNoProfile = updateOpticsPwms(*pS);

  // STEP 3.5: Shutdown the system if overtemp is detected
  for (auto& sensorName : overtempWatchList_) {
    auto sensorEntry = pS->getSensorEntry(sensorName);
    if (sensorEntry) {
      overtempCondition_.processSensorData(sensorName, sensorEntry->value);
    }
  }
  if (overtempCondition_.checkIfOvertemp()) {
    XLOG(ERR) << fmt::format("Running shutdown command");
    structuredLogger_.logAlert(
        "emergency_shutdown",
        "System overtemp detected, running shutdown command",
        {{}});
    pBsp_->emergencyShutdown(true);
  }

  // STEP 4: Determine whether boost mode is necessary
  auto lastQsfpSvcTime = pS->getLastQsfpSvcTime();
  bool missingOpticsUpdate{false}, fanFailures{false}, sensorFailures{false},
      opticNoProfile{false};
  std::string boostModeReason;
  if (*config_.pwmBoostOnNoQsfpAfterInSec() != 0) {
    if (lastQsfpSvcTime == 0) {
      missingOpticsUpdate = true;
      boostModeReason = "Boost mode enabled for optics data never received";
      XLOG(INFO) << boostModeReason;
    } else {
      uint64_t secondsSinceLastOpticsUpdate =
          pBsp_->getCurrentTime() - lastQsfpSvcTime;
      if (secondsSinceLastOpticsUpdate >=
          *config_.pwmBoostOnNoQsfpAfterInSec()) {
        missingOpticsUpdate = true;
        boostModeReason = fmt::format(
            "Boost mode enabled for optics update missing for {}s",
            secondsSinceLastOpticsUpdate);
        XLOG(INFO) << boostModeReason;
      }
    }
  }
  if ((*config_.pwmBoostOnNumDeadFan() != 0) &&
      (numFanFailed >= *config_.pwmBoostOnNumDeadFan())) {
    fanFailures = true;
    boostModeReason =
        fmt::format("Boost mode enabled for {} fan failures", numFanFailed);
    XLOG(INFO) << boostModeReason;
  }
  if ((*config_.pwmBoostOnNumDeadSensor() != 0) &&
      (numSensorFailed >= *config_.pwmBoostOnNumDeadSensor())) {
    sensorFailures = true;
    boostModeReason = fmt::format(
        "Boost mode enabled for {} sensor failures", numSensorFailed);
    XLOG(INFO) << boostModeReason;
  }
  if (numOpticsNoProfile > 0) {
    opticNoProfile = true;
    boostModeReason = fmt::format(
        "Boost mode enabled for {} optic type(s) with no thermal profile",
        numOpticsNoProfile);
    XLOG(INFO) << boostModeReason;
  }
  bool previousBoostMode = boostMode_;
  boostMode_ =
      (missingOpticsUpdate || fanFailures || sensorFailures || opticNoProfile);
  if (boostMode_ && !previousBoostMode) {
    structuredLogger_.logEvent(
        "boost_mode_activated", {{"reason", boostModeReason}});
  }

  // STEP 5: Calculate and program fan PWMs
  bool hasAnyPwmFailure{false};
  fanStatuses_.withWLock([&](auto& fanStatuses) {
    for (const auto& zone : *config_.zones()) {
      float zonePwm = calculateZonePwm(zone, boostMode_);
      std::unordered_set<std::string> zoneFans(
          zone.fanNames()->begin(), zone.fanNames()->end());
      for (const auto& fan : *config_.fans()) {
        if (!zoneFans.contains(*fan.fanName())) {
          continue;
        }

        float fanPwm = calculateFanPwm(
            *zone.slope(),
            *fanStatuses[*fan.fanName()].pwmToProgram(),
            zonePwm);
        bool fanFailed = programFan(zone, fan, fanPwm);
        hasAnyPwmFailure = hasAnyPwmFailure || fanFailed;
        updatePwmState(zone, fanPwm);

        fanStatuses[*fan.fanName()].pwmToProgram() = fanPwm;
        if (fanFailed) {
          // Only override the fanFailed if fan programming failed.
          fanStatuses[*fan.fanName()].fanFailed() = fanFailed;
        }
      }
    }

    // STEP 6: Program fan LEDs
    for (const auto& fan : *config_.fans()) {
      programLed(fan, *fanStatuses[*fan.fanName()].fanFailed());
    }
    fb303::fbData->setCounter(kHasFanRpmReadFailure, hasAnyRpmFailure ? 1 : 0);
    fb303::fbData->setCounter(kHasFanPwmWriteFailure, hasAnyPwmFailure ? 1 : 0);
    fb303::fbData->setCounter(kHasFanAbsent, hasAnyFanAbsent ? 1 : 0);
  });
}

float ControlLogic::calculateFanPwm(
    uint16_t slope,
    float currentFanPwm,
    float zonePwm) {
  float newFanPwm = zonePwm;
  if ((slope == 0) || (currentFanPwm == 0)) {
    newFanPwm = zonePwm;
  } else if (std::abs(currentFanPwm - zonePwm) > slope) {
    newFanPwm = currentFanPwm + ((zonePwm > currentFanPwm) ? slope : -slope);
  }

  newFanPwm =
      std::min(newFanPwm, static_cast<float>(*config_.pwmUpperThreshold()));
  newFanPwm =
      std::max(newFanPwm, static_cast<float>(*config_.pwmLowerThreshold()));

  std::optional<int> fanHoldPwm = fanHoldPwm_.load();
  if (fanHoldPwm.has_value()) {
    newFanPwm = fanHoldPwm.value();
    XLOG(INFO) << "Using hold PWM " << newFanPwm;
  }
  return newFanPwm;
}

void ControlLogic::updatePwmState(const Zone& zone, float fanPwm) {
  for (const auto& sensorName : *zone.sensorNames()) {
    if (pidLogics_.find(sensorName) != pidLogics_.end()) {
      pidLogics_.at(sensorName)->updateLastPwm(fanPwm);
    }
    for (const auto& optic : *config_.optics()) {
      if (optic.opticName() != sensorName) {
        continue;
      }
      for (const auto& [opticType, _] : *optic.pidSettings()) {
        auto cacheKey = *optic.opticName() + opticType;
        if (pidLogics_.find(cacheKey) != pidLogics_.end()) {
          pidLogics_.at(cacheKey)->updateLastPwm(fanPwm);
        }
      }
    }
  }
}

void ControlLogic::setFanHold(std::optional<int> pwm) {
  fanHoldPwm_.store(pwm);
}

std::optional<int> ControlLogic::getFanHold() {
  return fanHoldPwm_.load();
}

} // namespace facebook::fboss::platform::fan_service
