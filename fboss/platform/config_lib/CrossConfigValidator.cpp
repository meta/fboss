// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/config_lib/CrossConfigValidator.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>
#include <numeric>

#include "fboss/platform/platform_manager/ConfigValidator.h"
#include "fboss/platform/sensor_service/ConfigValidator.h"

namespace {
const re2::RE2 kRuntimePathRegex{
    "(?P<Path>/run/devmap/(sensors|gpiochips|fpgas|cplds)/[^/]+)($|(/.+)+)"};
const re2::RE2 kAgentAsicTempSensorRegex{"asic_temp_\\d+_\\d+"};
}; // namespace

namespace facebook::fboss::platform {
CrossConfigValidator::CrossConfigValidator(
    const platform_manager::PlatformConfig& config)
    : pmConfig_(config) {}

bool CrossConfigValidator::isValidSensorConfig(
    const sensor_config::SensorConfig& sensorConfig) {
  XLOG(INFO) << "Cross validating sensor_service config";
  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    if (!platform_manager::ConfigValidator().isValidSlotPath(
            pmConfig_, *pmUnitSensors.slotPath())) {
      return false;
    }
    if (!platform_manager::ConfigValidator().isValidPmUnitName(
            pmConfig_,
            *pmUnitSensors.slotPath(),
            *pmUnitSensors.pmUnitName())) {
      return false;
    }
    if (!isValidPmSensors(*pmUnitSensors.sensors())) {
      return false;
    }
    if (!pmUnitSensors.versionedSensors()->empty() &&
        !isValidVersionedPmSensors(
            *pmUnitSensors.slotPath(),
            *pmUnitSensors.pmUnitName(),
            *pmUnitSensors.versionedSensors())) {
      return false;
    }
  }
  return true;
}

bool CrossConfigValidator::isValidFanServiceConfig(
    const fan_service::FanServiceConfig& fanConfig,
    const std::optional<sensor_config::SensorConfig>& sensorConfig) {
  XLOG(INFO) << "Cross validating fan_service config";
  for (const auto& fan : *fanConfig.fans()) {
    if (!isValidRuntimePath(*fan.rpmSysfsPath()) ||
        !isValidRuntimePath(*fan.pwmSysfsPath())) {
      return false;
    }
    if (fan.presenceSysfsPath() &&
        !isValidRuntimePath(*fan.presenceSysfsPath())) {
      return false;
    }
    if (fan.presenceGpio() &&
        !isValidRuntimePath(*fan.presenceGpio()->path())) {
      return false;
    }
  }
  if (!fanConfig.sensors()->empty() && !sensorConfig) {
    XLOG(ERR) << fmt::format(
        "Failed validate SensorNames in FanServiceConfig. "
        "Undefined SensorConfig in {}",
        *pmConfig_.platformName());
    return false;
  }
  auto isValidSensorName = [&](const std::string& sensorName) {
    return re2::RE2::FullMatch(sensorName, kAgentAsicTempSensorRegex) ||
        sensor_service::ConfigValidator().isValidSensorName(
            *sensorConfig, sensorName);
  };
  for (const auto& sensor : *fanConfig.sensors()) {
    if (!isValidSensorName(*sensor.sensorName())) {
      return false;
    }
  }
  // add the sensor name validation check for shutdownCondition config
  if (fanConfig.shutdownCondition()) {
    for (const auto& condition : *fanConfig.shutdownCondition()->conditions()) {
      if (!isValidSensorName(*condition.sensorName())) {
        return false;
      }
    }
  }
  return true;
}

bool CrossConfigValidator::isValidWeutilConfig(
    const weutil_config::WeutilConfig& weutilConfig,
    const std::string& platformName) {
  // TODO: T226259767 Remove this check once PM is onboarded in Darwin
  if (platformName == "darwin") {
    return true;
  }
  XLOG(INFO) << "Cross validating weutil config";
  std::unordered_set<std::string> weutilEepromPaths{};
  for (const auto& [eepromName, eepromConfig] : *weutilConfig.fruEepromList()) {
    // Meru SCM EEPROM is calculated runtime and not present in pm config
    // https://fburl.com/code/ifvtduk2
    if (*eepromConfig.path() == "/run/devmap/eeproms/MERU_SCM_EEPROM") {
      XLOG(INFO)
          << "Skipping '/run/devmap/eeproms/MERU_SCM_EEPROM' as it's not being created by PlatformManager";
      continue;
    }
    weutilEepromPaths.insert(*eepromConfig.path());
    XLOG(DBG1) << fmt::format(
        "Added {} to weutilEepromPaths", *eepromConfig.path());
  }
  // PlatformManager should have all the EEPROMs defined in WeutilConfig
  for (const auto& [link, path] : *pmConfig_.symbolicLinkToDevicePath()) {
    weutilEepromPaths.erase(link);
    XLOG(DBG1) << fmt::format("Found {} in PlatformManager config", path);
    // Exit early if the set is empty
    if (weutilEepromPaths.empty()) {
      break;
    }
  }
  XLOG(DBG1) << fmt::format(
      "weutilEepromPaths after removing all symbolic links: [{}]",
      // Format it to json array to make it easier to read
      std::accumulate(
          weutilEepromPaths.begin(),
          weutilEepromPaths.end(),
          std::string(""),
          [](std::string a, const std::string& b) {
            bool comma_needed = !a.empty();
            return std::move(a) + (comma_needed ? ", " : "") +
                fmt::format("\"{}\"", b);
          }));
  return weutilEepromPaths.empty();
}

bool CrossConfigValidator::isValidPmSensors(
    const std::vector<sensor_config::PmSensor>& pmSensors) {
  for (const auto& pmSensor : pmSensors) {
    if (!isValidRuntimePath(*pmSensor.sysfsPath())) {
      return false;
    }
  }
  return true;
}

bool CrossConfigValidator::isValidVersionedPmSensors(
    const std::string& slotPath,
    const std::string& pmUnitName,
    const std::vector<sensor_config::VersionedPmSensor>& versionedPmSensors) {
  const auto& pmUnitConfig = pmConfig_.pmUnitConfigs()->at(pmUnitName);
  const auto& slotTypeConfig =
      pmConfig_.slotTypeConfigs()->at(*pmUnitConfig.pluggedInSlotType());
  if (!slotTypeConfig.idpromConfig() && !versionedPmSensors.empty()) {
    XLOG(ERR) << fmt::format(
        "Unexpected VersionedSensors definition for PmUnit {} at {} "
        "where IDPROM is not present.",
        pmUnitName,
        slotPath);
    return false;
  }
  for (const auto& versionedPmSensor : versionedPmSensors) {
    if (!isValidPmSensors(*versionedPmSensor.sensors())) {
      return false;
    }
  }
  return true;
}

bool CrossConfigValidator::isValidRuntimePath(const std::string& path) {
  std::string symlink;
  if (!re2::RE2::FullMatch(path, kRuntimePathRegex, &symlink)) {
    XLOG(ERR) << fmt::format("Fail to extract runtime path from {}", path);
    return false;
  }
  if (!pmConfig_.symbolicLinkToDevicePath()->contains(symlink)) {
    XLOG(ERR) << fmt::format(
        "{} is not defined in PlatformConfig::symbolicLinkToDevicePath",
        symlink);
    return false;
  }
  return true;
}
} // namespace facebook::fboss::platform
