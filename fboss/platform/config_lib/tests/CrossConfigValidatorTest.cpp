// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/config_lib/CrossConfigValidator.h"

using namespace ::testing;
using namespace facebook::fboss::platform;
namespace {
sensor_config::PmSensor createPmSensor(
    const std::string& name,
    const std::string& sysfsPath) {
  sensor_config::PmSensor pmSensor;
  pmSensor.name() = name;
  pmSensor.sysfsPath() = sysfsPath;
  return pmSensor;
}
} // namespace

class CrossConfigValidatorTest : public testing::Test {
 public:
  void SetUp() override {
    // PM config set up
    platform_manager::PlatformConfig platformConfig;
    platform_manager::SlotTypeConfig bcbSlotTypeConfig;
    platformConfig.rootSlotType() = "MCB_SLOT";
    bcbSlotTypeConfig.idpromConfig() = platform_manager::IdpromConfig();
    platformConfig.slotTypeConfigs() = {
        {"MCB_SLOT", platform_manager::SlotTypeConfig()},
        {"BCB_SLOT", bcbSlotTypeConfig}};

    platform_manager::PmUnitConfig mcbPmUnitConfig, bcbPmUnitConfig;
    mcbPmUnitConfig.pluggedInSlotType() = "MCB_SLOT";
    platform_manager::SlotConfig bcbSlotConfig;
    bcbSlotConfig.slotType() = "BCB_SLOT";
    mcbPmUnitConfig.outgoingSlotConfigs() = {{"BCB_SLOT@0", bcbSlotConfig}};
    bcbPmUnitConfig.pluggedInSlotType() = "BCB_SLOT";

    platformConfig.pmUnitConfigs() = {
        {"MCB", mcbPmUnitConfig}, {"BCB", bcbPmUnitConfig}};
    platformConfig.symbolicLinkToDevicePath() = {
        {"/run/devmap/sensors/CPU_CORE_TEMP", "/[CPU_CORE_TEMP]"},
        {"/run/devmap/sensors/BCB_FAN_CPLD", "/BCB_SLOT@0/[BCB_FAN_CPLD]"},
        {"/run/devmap/gpiochips/MCB_GPIO_CHIP_1", "/[MCB_GPIO_CHIP_1]"},
        {"/run/devmap/cplds/MCB_CPLD", "/[MCB_CPLD]"},
        {"/run/devmap/eeproms/SMB_EEPROM", "/SMB_FRU_SLOT@0/[IDPROM]"},
        {"/run/devmap/eeproms/COME_EEPROM", "/[IDPROM]"}};

    crossConfigValidator_ = CrossConfigValidator(platformConfig);
  }

  std::optional<CrossConfigValidator> crossConfigValidator_{std::nullopt};
};

TEST_F(CrossConfigValidatorTest, ValidSensorConfig) {
  auto config = sensor_config::SensorConfig();
  sensor_config::PmUnitSensors pmUnitSensors1, pmUnitSensors2;
  pmUnitSensors1.slotPath() = "/";
  pmUnitSensors1.pmUnitName() = "MCB";
  pmUnitSensors1.sensors() = {
      createPmSensor("sensor1", "/run/devmap/sensors/CPU_CORE_TEMP/input1"),
      createPmSensor("sensor4", "/run/devmap/cplds/MCB_CPLD/pdb_psu_r_alert")};
  pmUnitSensors2.slotPath() = "/BCB_SLOT@0";
  pmUnitSensors2.pmUnitName() = "BCB";
  pmUnitSensors2.sensors() = {
      createPmSensor("sensor2", "/run/devmap/sensors/BCB_FAN_CPLD/pwm3")};
  sensor_config::VersionedPmSensor versionedSensors2;
  versionedSensors2.sensors() = {
      createPmSensor("sensor3", "/run/devmap/sensors/BCB_FAN_CPLD/pwm4")};
  pmUnitSensors2.versionedSensors() = {versionedSensors2};
  config.pmUnitSensorsList() = {pmUnitSensors1, pmUnitSensors2};

  EXPECT_TRUE(crossConfigValidator_->isValidSensorConfig(config));
}

TEST_F(CrossConfigValidatorTest, InvalidSensorConfig) {
  auto config = sensor_config::SensorConfig();
  sensor_config::PmUnitSensors pmUnitSensors;

  // Undefined runtime path in PM config.
  pmUnitSensors.slotPath() = "/";
  pmUnitSensors.pmUnitName() = "MCB";
  pmUnitSensors.sensors() = {
      createPmSensor("sensor1", "/run/devmap/sensors/SMB_E1S_SSD_TEMP/input1")};
  config.pmUnitSensorsList() = {pmUnitSensors};
  EXPECT_FALSE(crossConfigValidator_->isValidSensorConfig(config));

  // Invalid PmUnit at SlotPath
  pmUnitSensors.slotPath() = "/";
  pmUnitSensors.pmUnitName() = "SMB";
  pmUnitSensors.sensors() = {};
  config.pmUnitSensorsList() = {pmUnitSensors};
  EXPECT_FALSE(crossConfigValidator_->isValidSensorConfig(config));

  // Undefined SlotPath
  pmUnitSensors.slotPath() = "/SMB_SLOT@0";
  pmUnitSensors.pmUnitName() = "SMB";
  pmUnitSensors.sensors() = {};
  config.pmUnitSensorsList() = {pmUnitSensors};
  EXPECT_FALSE(crossConfigValidator_->isValidSensorConfig(config));
}

TEST_F(CrossConfigValidatorTest, ValidFanConfig) {
  auto config = fan_service::FanServiceConfig();
  fan_service::Fan fan1, fan2;
  fan1.rpmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/fan1_input";
  fan1.pwmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/pwm3";
  fan1.presenceSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/fan1_present";
  fan2.rpmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/fan2_input";
  fan2.pwmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/pwm3";
  fan_service::Gpio gpio;
  gpio.path() = "/run/devmap/gpiochips/MCB_GPIO_CHIP_1";
  fan2.presenceGpio() = gpio;
  config.fans() = {fan1, fan2};

  // Test sysfs paths
  EXPECT_TRUE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Test sensor names
  sensor_config::SensorConfig sensorConfig;
  sensor_config::PmUnitSensors pmUnitSensors;
  pmUnitSensors.sensors() = {
      createPmSensor("sensor1", "/run/devmap/sensors/SMB_E1S_SSD_TEMP/input1")};
  sensorConfig.pmUnitSensorsList() = {pmUnitSensors};
  fan_service::Sensor fanSensor;
  fanSensor.sensorName() = "sensor1";
  config.sensors() = {fanSensor};
  EXPECT_TRUE(
      crossConfigValidator_->isValidFanServiceConfig(config, sensorConfig));

  // Test agent ASIC temperature sensor names
  fan_service::Sensor asicSensor;
  asicSensor.sensorName() = "asic_temp_0_15";
  config.sensors() = {fanSensor, asicSensor};
  fan_service::OvertempEntry overtempEntry;
  overtempEntry.sensorName() = "asic_temp_0_0";
  fan_service::ShutdownCondition shutdownCondition;
  shutdownCondition.conditions() = {overtempEntry};
  config.shutdownCondition() = shutdownCondition;
  EXPECT_TRUE(
      crossConfigValidator_->isValidFanServiceConfig(config, sensorConfig));
}

TEST_F(CrossConfigValidatorTest, InvalidFanConfig) {
  auto config = fan_service::FanServiceConfig();

  fan_service::Fan validFan;
  validFan.rpmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/fan1_input";
  validFan.pwmSysfsPath() = "/run/devmap/sensors/BCB_FAN_CPLD/pwm3";
  validFan.presenceSysfsPath() =
      "/run/devmap/sensors/BCB_FAN_CPLD/fan1_present";
  EXPECT_TRUE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  fan_service::Fan fan;
  // Undefined rpm path
  fan.rpmSysfsPath() = "/run/devmap/sensors/FAN_CPLD1/fan1_input";
  config.fans() = {fan};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Undefined pwm path
  fan.rpmSysfsPath() = *validFan.rpmSysfsPath();
  fan.pwmSysfsPath() = "/run/devmap/sensors/FAN_CPLD1/pwm";
  config.fans() = {fan};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Undefined presence path
  fan.pwmSysfsPath() = *validFan.pwmSysfsPath();
  fan.presenceSysfsPath() = "/run/devmap/sensors/FAN_CPLD1/fan1_present";
  config.fans() = {fan};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Undefined gpio path
  fan.presenceSysfsPath().reset();
  fan_service::Gpio gpio;
  gpio.path() = "/run/devmap/gpiochips/MCB_GPIO_CHIP_UNDEFINED";
  fan.presenceGpio() = gpio;
  config.fans() = {fan};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Undefined SensorConfig
  fan_service::Sensor fanSensor;
  fanSensor.sensorName() = "sensor1";
  config.sensors() = {fanSensor};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, std::nullopt));

  // Nonexistent sensor name
  sensor_config::SensorConfig sensorConfig;
  sensor_config::PmUnitSensors pmUnitSensors;
  pmUnitSensors.sensors() = {};
  sensorConfig.pmUnitSensorsList() = {pmUnitSensors};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, sensorConfig));

  // Malformed agent ASIC temperature sensor name
  config.fans() = {validFan};
  fanSensor.sensorName() = "asic_temp_0";
  config.sensors() = {fanSensor};
  EXPECT_FALSE(
      crossConfigValidator_->isValidFanServiceConfig(config, sensorConfig));
}

TEST_F(CrossConfigValidatorTest, ValidWeutilConfig) {
  weutil_config::WeutilConfig weutilConfig;
  weutilConfig.chassisEepromName() = "SMB";
  weutilConfig.fruEepromList()["SMB"] = {};
  weutilConfig.fruEepromList()["SMB"].path() = "/run/devmap/eeproms/SMB_EEPROM";
  weutilConfig.fruEepromList()["COME"] = {};
  weutilConfig.fruEepromList()["COME"].path() =
      "/run/devmap/eeproms/COME_EEPROM";
  EXPECT_TRUE(
      crossConfigValidator_->isValidWeutilConfig(weutilConfig, "tahan800bc"));
  // It is expected for MERU_SCM_EEPROM to be missing in platform_manager config
  weutilConfig.fruEepromList()["MERU_SCM"] = {};
  weutilConfig.fruEepromList()["MERU_SCM"].path() =
      "/run/devmap/eeproms/MERU_SCM_EEPROM";
  EXPECT_TRUE(
      crossConfigValidator_->isValidWeutilConfig(weutilConfig, "meru800bfa"));
  // TODO: T226259767 Remove this check once PM is onboarded in Darwin
  // Darwin is not migrated to platform_manager config yet, so any config should
  // be valid
  weutilConfig.fruEepromList()["MCB"] = {};
  weutilConfig.fruEepromList()["MCB"].path() =
      "/run/devmap/eeproms/INVALID_PATH";
  EXPECT_TRUE(
      crossConfigValidator_->isValidWeutilConfig(weutilConfig, "darwin"));
}

TEST_F(CrossConfigValidatorTest, InvalidWeutilConfig) {
  weutil_config::WeutilConfig weutilConfig;
  weutilConfig.chassisEepromName() = "SMB";
  weutilConfig.fruEepromList()["SMB"] = {};
  weutilConfig.fruEepromList()["SMB"].path() =
      "/run/devmap/eeproms/INVALID_PATH";
  EXPECT_FALSE(
      crossConfigValidator_->isValidWeutilConfig(weutilConfig, "tahan800bc"));
}
