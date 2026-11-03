// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "fboss/platform/fan_service/ConfigValidator.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_constants.h"
#include "fboss/platform/fan_service/if/gen-cpp2/fan_service_config_types.h"

using namespace ::testing;
using namespace facebook::fboss::platform::fan_service;

namespace constants =
    facebook::fboss::platform::fan_service::fan_service_config_constants;

TEST(ConfigValidatorTest, FanConfig) {
  auto fan = Fan();
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));
  fan.rpmSysfsPath() = "/run/devmap/sensors/ABC/fan0_rpm";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));
  fan.pwmSysfsPath() = "/run/devmap/sensors/ABC/fan0_pwm";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));
  fan.presenceSysfsPath() = "/run/devmap/sensors/ABC/fan0_presence";
  fan.presenceGpio() = Gpio();
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));
  fan.presenceGpio().reset();
  EXPECT_TRUE(ConfigValidator().isValidFanConfig(fan));
  fan.presenceSysfsPath().reset();
  fan.presenceGpio() = Gpio();
  EXPECT_TRUE(ConfigValidator().isValidFanConfig(fan));
}

TEST(ConfigValidatorTest, FanConfigLedPaths) {
  // Create a valid fan configuration first
  auto fan = Fan();
  fan.rpmSysfsPath() = "/run/devmap/sensors/ABC/fan0_rpm";
  fan.pwmSysfsPath() = "/run/devmap/sensors/ABC/fan0_pwm";
  fan.presenceSysfsPath() = "/run/devmap/sensors/ABC/fan0_presence";
  EXPECT_TRUE(ConfigValidator().isValidFanConfig(fan));

  // Test goodLedSysfsPath validation
  // Test with invalid prefix (should fail)
  fan.goodLedSysfsPath() = "/invalid/path/fan0:blue:status";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));

  // Test with invalid suffix (should fail)
  fan.goodLedSysfsPath() = "/sys/class/leds/fan0:invalid:status";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));

  // Test with valid path (should pass)
  fan.goodLedSysfsPath() = "/sys/class/leds/fan0:blue:status";
  EXPECT_TRUE(ConfigValidator().isValidFanConfig(fan));

  // Test failLedSysfsPath validation
  // Test with invalid prefix (should fail)
  fan.failLedSysfsPath() = "/invalid/path/fan0:amber:status";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));

  // Test with invalid suffix (should fail)
  fan.failLedSysfsPath() = "/sys/class/leds/fan0:invalid:status";
  EXPECT_FALSE(ConfigValidator().isValidFanConfig(fan));

  // Test with valid path (should pass)
  fan.failLedSysfsPath() = "/sys/class/leds/fan0:amber:status";
  EXPECT_TRUE(ConfigValidator().isValidFanConfig(fan));
}

TEST(ConfigValidatorTest, OpticConfig) {
  auto optic = Optic();
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.opticName() = "OPTIC_0";
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.aggregationType() = constants::OPTIC_AGGREGATION_TYPE_MAX();
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.tempToPwmMaps() = {{"UNKNOWN_OPTIC", {{34, 50}, {45, 60}}}};
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.tempToPwmMaps() = {{"OPTIC_TYPE_100_GENERIC", {{34, 50}, {45, 60}}}};
  EXPECT_TRUE(ConfigValidator().isValidOpticConfig(optic));
  optic.tempToPwmMaps() = {{"OPTIC_TYPE_1600_GENERIC", {{34, 50}, {45, 60}}}};
  EXPECT_TRUE(ConfigValidator().isValidOpticConfig(optic));
  optic.aggregationType() = constants::OPTIC_AGGREGATION_TYPE_PID();
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.pidSettings() = {{"UNKNOWN_OPTIC", PidSetting()}};
  EXPECT_FALSE(ConfigValidator().isValidOpticConfig(optic));
  optic.pidSettings() = {{"OPTIC_TYPE_100_GENERIC", PidSetting()}};
  EXPECT_TRUE(ConfigValidator().isValidOpticConfig(optic));
}

TEST(ConfigValidatorTest, ZoneConfig) {
  auto zone = Zone();
  EXPECT_FALSE(ConfigValidator().isValidZoneConfig(zone, {}, {}, {}));
  zone.zoneName() = "ZONE_0";
  zone.zoneType() = constants::ZONE_TYPE_MAX();
  EXPECT_TRUE(ConfigValidator().isValidZoneConfig(zone, {}, {}, {}));
  zone.fanNames() = {"FAN_0"};
  auto fan = Fan();
  fan.fanName() = "FAN_1";
  EXPECT_FALSE(ConfigValidator().isValidZoneConfig(zone, {fan}, {}, {}));
  fan.fanName() = "FAN_0";
  EXPECT_TRUE(ConfigValidator().isValidZoneConfig(zone, {fan}, {}, {}));
  zone.sensorNames() = {"SENSOR_0"};
  EXPECT_FALSE(ConfigValidator().isValidZoneConfig(zone, {fan}, {}, {}));
  auto sensor = Sensor();
  sensor.sensorName() = "SENSOR_0";
  EXPECT_TRUE(ConfigValidator().isValidZoneConfig(zone, {fan}, {sensor}, {}));
  zone.sensorNames() = {"SENSOR_0", "OPTIC_0"};
  EXPECT_FALSE(ConfigValidator().isValidZoneConfig(zone, {fan}, {sensor}, {}));
  auto optic = Optic();
  optic.opticName() = "OPTIC_0";
  EXPECT_TRUE(
      ConfigValidator().isValidZoneConfig(zone, {fan}, {sensor}, {optic}));
}
