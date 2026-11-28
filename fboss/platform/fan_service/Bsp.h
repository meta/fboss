// Copyright 2021- Facebook. All rights reserved.

// BSP Base Class, a part of Fan Service
//
// Role : Take care of all the I/O and other board specific details
//        such as emergency_shutdown, thrift read, and sysfs access.
//        A base class where it can be extended by platform specific class.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <folly/io/async/EventBase.h>

#include "fboss/fsdb/client/FsdbPubSubManager.h"
#include "fboss/platform/fan_service/FsdbSensorSubscriber.h"
#include "fboss/platform/fan_service/SensorData.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"
#include "fboss/qsfp_service/if/gen-cpp2/transceiver_types.h"

namespace facebook::fboss::platform::fan_service {

// fb303 counter name for tracking when thrift fallback is used for sensor data
inline constexpr auto kFsdbSensorDataThriftFallback =
    "fsdb_sensor_data_thrift_fallback";

// The optic type a transceiver's thermal table is keyed on. A missing or
// UNKNOWN media code maps to unknownFallback; a media code with no optic type
// maps to nullopt and the transceiver is ignored.
std::optional<std::string> getOpticTypeFromMediaCode(
    std::optional<facebook::fboss::MediaInterfaceCode> mediaInterfaceCode,
    const std::string& unknownFallback);

class Bsp {
 public:
  explicit Bsp(const FanServiceConfig& config);
  virtual ~Bsp();
  virtual void getSensorData(std::shared_ptr<SensorData> pSensorData);
  virtual void getOpticsData(std::shared_ptr<SensorData> pSensorData);
  virtual void getAsicTempData(const std::shared_ptr<SensorData>& pSensorData);
  virtual int emergencyShutdown(bool enable);
  void kickWatchdog();
  void closeWatchdog();
  virtual bool setFanPwmSysfs(const std::string& path, int pwm);
  virtual bool turnOnLedSysfs(const std::string& path);
  virtual std::optional<int> getLedMaxBrightness(const std::string& path) const;
  virtual uint64_t getCurrentTime() const;
  virtual bool checkIfInitialSensorDataRead() const;
  bool getEmergencyState() const;
  virtual float readSysfs(const std::string& path) const;
  FsdbSensorSubscriber* fsdbSensorSubscriber() {
    return fsdbSensorSubscriber_.get();
  }
  void getSensorDataThrift(std::shared_ptr<SensorData> pSensorData);

 protected:
  const FanServiceConfig config_;

 private:
  void getOpticData(
      const Optic& opticsGroup,
      std::shared_ptr<SensorData> pSensorData);
  void setEmergencyState(bool state);
  bool writeToWatchdog(const std::string& value);
  void getAsicTempDataFromSensorService(
      const std::shared_ptr<SensorData>& pSensorData);
  void getAsicTempThroughFsdb(const std::shared_ptr<SensorData>& pSensorData);
  virtual bool writeSysfs(const std::string& path, int value);
  std::map<std::string, std::vector<OpticData>> processOpticEntries(
      const Optic& opticsGroup,
      uint64_t& currentQsfpSvcTimestamp,
      const std::map<int32_t, TransceiverInfo>& transceiverInfoMap);

  folly::EventBase evb_;
  folly::EventBase evbSensor_;
  std::shared_ptr<std::thread> thread_{nullptr};
  std::unique_ptr<FsdbSensorSubscriber> fsdbSensorSubscriber_;
  std::unique_ptr<fsdb::FsdbPubSubManager> fsdbPubSubMgr_;
  bool emergencyShutdownState_{false};
  bool initialSensorDataRead_{false};
  std::optional<int> watchdogFd_;
};
} // namespace facebook::fboss::platform::fan_service
