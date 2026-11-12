// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/sensor_service/ConfigValidator.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include "fboss/platform/sensor_service/utilities/PowerConfigUtils.h"

namespace facebook::fboss::platform::sensor_service {
using namespace sensor_config;

const std::unordered_set<std::string> kTempThresholdViolators = {
    "LEH800BCLS",
    "MONTBLANC",
};

bool ConfigValidator::isValid(const SensorConfig& sensorConfig) {
  XLOG(INFO) << "Validating sensor_service config";
  if (!isValidPlatformName(sensorConfig)) {
    return false;
  }
  if (!isValidPmUnitSensorsList(*sensorConfig.pmUnitSensorsList())) {
    return false;
  }
  if (!isValidTemperatureSensorThresholds(sensorConfig)) {
    return false;
  }
  if (!isValidPowerConfig(sensorConfig)) {
    return false;
  }
  if (!isValidTemperatureConfig(sensorConfig)) {
    return false;
  }
  if (!isValidAsicCommand(sensorConfig)) {
    return false;
  }
  if (!isValidLoggedSensorNames(sensorConfig)) {
    return false;
  }
  return true;
}

bool ConfigValidator::isValidPlatformName(const SensorConfig& sensorConfig) {
  XLOG(DBG1) << "Validating platform name";
  if (sensorConfig.platformName()->empty()) {
    XLOG(ERR) << "platformName must be non-empty";
    return false;
  }
  const auto& name = *sensorConfig.platformName();
  for (char c : name) {
    if (!std::isupper(c) && c != '_' && !std::isdigit(c)) {
      XLOG(ERR) << fmt::format(
          "platformName '{}' must contain only uppercase letters, digits,"
          " and underscores",
          name);
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidPmSensor(const sensor_config::PmSensor& pmSensor) {
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          pmSensor.name()) ||
      pmSensor.name()->empty()) {
    XLOG(ERR) << "PmSensor name must be non-empty";
    return false;
  }

  // Check if sensor name is all uppercase with underscores and digits
  const auto& name = *pmSensor.name();
  for (char c : name) {
    if (!std::isupper(c) && c != '_' && !std::isdigit(c)) {
      XLOG(ERR) << fmt::format(
          "PmSensor name '{}' must contain only uppercase letters, digits, and "
          " underscores ",
          name);
      return false;
    }
  }

  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          pmSensor.sysfsPath()) ||
      pmSensor.sysfsPath()->empty()) {
    XLOG(ERR) << "PmSensor sysfsPath must be non-empty";
    return false;
  }
  return true;
}

bool ConfigValidator::isValidPmUnitSensorsList(
    const std::vector<sensor_config::PmUnitSensors>& pmUnitSensorsList) {
  std::unordered_set<std::pair<std::string, std::string>> usedSlotPaths;
  for (const auto& pmUnitSensors : pmUnitSensorsList) {
    if (usedSlotPaths.contains(
            {*pmUnitSensors.slotPath(), *pmUnitSensors.pmUnitName()})) {
      XLOG(ERR) << fmt::format(
          "(SlotPath {}, PmUnitName {}) is a duplicate",
          *pmUnitSensors.slotPath(),
          *pmUnitSensors.pmUnitName());
      return false;
    }
    usedSlotPaths.insert(
        {*pmUnitSensors.slotPath(), *pmUnitSensors.pmUnitName()});
    if (!isValidPmSensors(*pmUnitSensors.sensors())) {
      return false;
    }
    for (const auto& versionedPmSensor : *pmUnitSensors.versionedSensors()) {
      if (!isValidPmSensors(*versionedPmSensor.sensors())) {
        return false;
      }
    }
  }
  return true;
}

bool ConfigValidator::isValidPmSensors(const std::vector<PmSensor>& pmSensors) {
  std::unordered_set<std::string> usedSensorNames;
  for (const auto& pmSensor : pmSensors) {
    if (!isValidPmSensor(pmSensor)) {
      return false;
    }
    if (usedSensorNames.contains(*pmSensor.name())) {
      XLOG(ERR) << fmt::format(
          "SensorName {} is a duplicate", *pmSensor.name());
      return false;
    }
    usedSensorNames.emplace(*pmSensor.name());
    if (!pmSensor.sysfsPath()->starts_with("/run/devmap/sensors/") &&
        !pmSensor.sysfsPath()->starts_with("/run/devmap/cplds/")) {
      XLOG(ERR)
          << "PmSensor sysfsPath must start with /run/devmap/sensors/ or /run/devmap/cplds/";
      return false;
    }
  }
  return true;
}

bool ConfigValidator::isValidPowerConfig(
    const sensor_config::SensorConfig& sensorConfig) {
  re2::RE2 psuPattern("(PSU|PEM|HSC|PWRBRK)([1-9][0-9]*)");

  XLOG(DBG1) << "Validating Power Config";

  const auto& powerConfig = *sensorConfig.powerConfig();
  auto sensorNames = getAllSensorNames(sensorConfig);
  auto universalSensorNames = getAllUniversalSensorNames(sensorConfig);
  std::unordered_set<std::string> perSlotPowerConfigNames;

  // sensorName → slotPath of its declaring PmUnitSensors entry. Used to
  // validate that any sensor referenced from a PerSlotPowerConfig with an
  // explicit slotPath actually lives at that same slot path.
  std::unordered_map<std::string, std::string> sensorNameToSlotPath;
  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    const auto& slotPath = *pmUnitSensors.slotPath();
    for (const auto& sensor : *pmUnitSensors.sensors()) {
      sensorNameToSlotPath[*sensor.name()] = slotPath;
    }
    for (const auto& versionedPmSensor : *pmUnitSensors.versionedSensors()) {
      for (const auto& sensor : *versionedPmSensor.sensors()) {
        sensorNameToSlotPath[*sensor.name()] = slotPath;
      }
    }
  }

  // perSlotPowerConfigs is mandatory and must not be empty
  if (powerConfig.perSlotPowerConfigs()->empty()) {
    XLOG(ERR) << "perSlotPowerConfigs must be defined and non-empty";
    return false;
  }

  for (const auto& perSlotConfig : *powerConfig.perSlotPowerConfigs()) {
    // Check for duplicate per-slot power config names
    if (perSlotPowerConfigNames.find(*perSlotConfig.name()) !=
        perSlotPowerConfigNames.end()) {
      XLOG(ERR) << fmt::format(
          "perSlotPowerConfig name {} is a duplicate", *perSlotConfig.name());
      return false;
    }
    perSlotPowerConfigNames.insert(*perSlotConfig.name());

    // Check if per-slot power config name conflicts with existing sensor names
    if (sensorNames.find(*perSlotConfig.name()) != sensorNames.end()) {
      XLOG(ERR) << fmt::format(
          "perSlotPowerConfig name {} conflicts with existing sensor name",
          *perSlotConfig.name());
      return false;
    }

    // Validate name pattern (PSU/PEM/HSC/PWRBRK)
    if (!RE2::FullMatch(*perSlotConfig.name(), psuPattern)) {
      XLOG(ERR) << fmt::format(
          "perSlotPowerConfig name {} should be "
          "PSU[number], PEM[number], HSC[number], or PWRBRK[number]",
          *perSlotConfig.name());
      return false;
    }

    // Validate sensor references
    if (perSlotConfig.powerSensorName().has_value()) {
      if (universalSensorNames.count(*perSlotConfig.powerSensorName()) == 0) {
        XLOG(ERR) << fmt::format(
            "perSlotPowerConfig powerSensorName {} is not defined in"
            " SensorConfig",
            *perSlotConfig.powerSensorName());
        return false;
      }
    } else if (
        perSlotConfig.voltageSensorName().has_value() &&
        perSlotConfig.currentSensorName().has_value()) {
      if (universalSensorNames.count(*perSlotConfig.voltageSensorName()) == 0) {
        XLOG(ERR) << fmt::format(
            "perSlotPowerConfig voltageSensorName {} is not defined in"
            " SensorConfig",
            *perSlotConfig.voltageSensorName());
        return false;
      }

      if (universalSensorNames.count(*perSlotConfig.currentSensorName()) == 0) {
        XLOG(ERR) << fmt::format(
            "perSlotPowerConfig currentSensorName {} is not defined in"
            " SensorConfig",
            *perSlotConfig.currentSensorName());
        return false;
      }
    } else {
      XLOG(ERR) << fmt::format(
          "perSlotPowerConfig {}: Either powerSensorName, or voltageSensorName"
          " and currentSensorName should be defined",
          *perSlotConfig.name());
      return false;
    }

    // PSU/PEM PerSlotPowerConfigs MUST set a non-empty slotPath: presence
    // counting and absent-PSU sensor suppression both look up the slot path
    // in platform_manager. HSC/PWRBRK are not field-replaceable and may
    // omit slotPath.
    if (isPsuOrPem(perSlotConfig) &&
        (!perSlotConfig.slotPath().has_value() ||
         perSlotConfig.slotPath()->empty())) {
      XLOG(ERR) << fmt::format(
          "perSlotPowerConfig {} (PSU/PEM) must set a non-empty slotPath",
          *perSlotConfig.name());
      return false;
    }

    // When slotPath is set on this PerSlotPowerConfig, every referenced
    // sensor must live at that same slot path — otherwise the runtime
    // presence check (sensor_service queries platform_manager at the
    // configured slotPath) would be reading presence for a different
    // PmUnit than the sensor it pairs with.
    if (perSlotConfig.slotPath().has_value()) {
      const auto& declaredSlotPath = *perSlotConfig.slotPath();
      for (const auto& sensorNameRef :
           {perSlotConfig.powerSensorName(),
            perSlotConfig.voltageSensorName(),
            perSlotConfig.currentSensorName()}) {
        auto sensorName = sensorNameRef.to_optional();
        if (!sensorName) {
          continue;
        }
        auto it = sensorNameToSlotPath.find(*sensorName);
        if (it == sensorNameToSlotPath.end()) {
          // Already caught by the universalSensorNames checks above; skip.
          continue;
        }
        if (it->second != declaredSlotPath) {
          XLOG(ERR) << fmt::format(
              "perSlotPowerConfig {} declares slotPath {} but its sensor"
              " {} is defined under PmUnitSensors slotPath {}",
              *perSlotConfig.name(),
              declaredSlotPath,
              *sensorName,
              it->second);
          return false;
        }
      }
    }
  }

  // Validate otherPowerSensorNames
  for (const auto& sensorName : *powerConfig.otherPowerSensorNames()) {
    if (universalSensorNames.count(sensorName) == 0) {
      XLOG(ERR) << fmt::format(
          "otherPowerSensorName {} is not defined in SensorConfig", sensorName);
      return false;
    }
  }

  // inputVoltageSensors is mandatory; each name must be a universally
  // available sensor.
  if (powerConfig.inputVoltageSensors()->empty()) {
    XLOG(ERR) << "inputVoltageSensors must be defined and non-empty";
    return false;
  }

  if (*powerConfig.minAcPsuCount() < 0 || *powerConfig.minDcPsuCount() < 0) {
    XLOG(ERR) << "minAcPsuCount and minDcPsuCount must be non-negative";
    return false;
  }

  for (const auto& sensorName : *powerConfig.inputVoltageSensors()) {
    if (universalSensorNames.count(sensorName) == 0) {
      XLOG(ERR) << fmt::format(
          "inputVoltageSensor {} is not defined in SensorConfig", sensorName);
      return false;
    }
  }

  if (hasPsuOrPem(powerConfig) && *powerConfig.minAcPsuCount() == 0 &&
      *powerConfig.minDcPsuCount() == 0) {
    XLOG(ERR) << "Platform has PSU/PEM slots but both minAcPsuCount and"
              << " minDcPsuCount are 0. At least one must be set.";
    return false;
  }

  // If no PSU/PEM slots exist (only HSC/PWRBRK), minAcPsuCount and
  // minDcPsuCount must both be 0.
  if (!hasPsuOrPem(powerConfig) &&
      (*powerConfig.minAcPsuCount() != 0 ||
       *powerConfig.minDcPsuCount() != 0)) {
    XLOG(ERR)
        << "Platform has no PSU/PEM slots but minAcPsuCount or minDcPsuCount"
        << " is non-zero. Min PSU counts only apply to platforms with"
        << " field-replaceable PSU/PEM.";
    return false;
  }

  return true;
}

bool ConfigValidator::isValidTemperatureConfig(
    const sensor_config::SensorConfig& sensorConfig) {
  re2::RE2 asicPattern("ASIC([1-9][0-9]*)?");

  XLOG(DBG1) << "Validating Temperature Config";

  // Validate that temperatureConfigs is not empty
  if (sensorConfig.temperatureConfigs()->empty()) {
    XLOG(ERR) << "temperatureConfigs must not be empty";
    return false;
  }

  auto sensorNames = getAllSensorNames(sensorConfig);
  auto universalSensorNames = getAllUniversalSensorNames(sensorConfig);
  std::unordered_set<std::string> temperatureConfigNames;

  for (const auto& tempConfig : *sensorConfig.temperatureConfigs()) {
    // Check for duplicate temperature config names
    if (temperatureConfigNames.find(*tempConfig.name()) !=
        temperatureConfigNames.end()) {
      XLOG(ERR) << fmt::format(
          "temperatureConfig name `{}` is a duplicate", *tempConfig.name());
      return false;
    }
    temperatureConfigNames.insert(*tempConfig.name());

    // Check if temperature config name conflicts with existing sensor names
    if (sensorNames.find(*tempConfig.name()) != sensorNames.end()) {
      XLOG(ERR) << fmt::format(
          "temperatureConfig name `{}` conflicts with existing sensor name",
          *tempConfig.name());
      return false;
    }

    // Validate temperature config name pattern (ASIC or ASIC[number])
    if (!RE2::FullMatch(*tempConfig.name(), asicPattern)) {
      XLOG(ERR) << fmt::format(
          "temperatureConfig name `{}` should be ASIC or ASIC[number]",
          *tempConfig.name());
      return false;
    }

    // Validate that temperatureSensorNames list is non-empty
    if (tempConfig.temperatureSensorNames()->empty()) {
      XLOG(ERR) << fmt::format(
          "temperatureConfig `{}` must have a non-empty temperatureSensorNames"
          " list ",
          *tempConfig.name());
      return false;
    }

    // Each temperatureSensorName must be a universally available sensor.
    for (const auto& sensorName : *tempConfig.temperatureSensorNames()) {
      if (universalSensorNames.count(sensorName) == 0) {
        XLOG(ERR) << fmt::format(
            "temperatureConfig temperatureSensorName `{}` is not defined in"
            " SensorConfig",
            sensorName);
        return false;
      }
    }
  }

  return true;
}

bool ConfigValidator::isValidAsicCommand(
    const sensor_config::SensorConfig& sensorConfig) {
  if (!sensorConfig.asicCommand().has_value()) {
    // AsicCommand is optional, so it's valid if not present
    return true;
  }

  XLOG(DBG1) << "Validating AsicCommand config";

  const auto& asicCommand = *sensorConfig.asicCommand();

  // Check if sensorName is non-empty
  if (asicCommand.sensorName()->empty()) {
    XLOG(ERR) << "AsicCommand sensorName must be non-empty";
    return false;
  }

  // Check if cmd is non-empty
  if (asicCommand.cmd()->empty()) {
    XLOG(ERR) << "AsicCommand cmd must be non-empty";
    return false;
  }

  // Check if sensor name conflicts with existing sensor names (excluding the
  // asicCmd struct)
  auto sensorConfigCopy = sensorConfig;
  sensorConfigCopy.asicCommand().reset();
  auto sensorNames = getAllSensorNames(sensorConfigCopy);
  if (sensorNames.count(*asicCommand.sensorName()) != 0) {
    XLOG(ERR) << fmt::format(
        "AsicCommand sensorName {} conflicts with existing sensor name",
        *asicCommand.sensorName());
    return false;
  }

  return true;
}

bool ConfigValidator::isValidLoggedSensorNames(
    const sensor_config::SensorConfig& sensorConfig) {
  const auto& loggedSensorNames = *sensorConfig.loggedSensorNames();
  if (loggedSensorNames.empty()) {
    return true;
  }

  XLOG(DBG1) << "Validating loggedSensorNames";

  // Validate against the universal set (base + asicCommand + the per-PmUnit
  // intersection of versioned entries) — the same contract as
  // inputVoltageSensors / temperatureSensorNames. A logged sensor must resolve
  // on every hardware version; a name present on only some respins would
  // otherwise log "sensor data missing" every cycle on the others.
  auto sensorNames = getAllUniversalSensorNames(sensorConfig);
  std::unordered_set<std::string> seen;
  for (const auto& sensorName : loggedSensorNames) {
    if (!seen.insert(sensorName).second) {
      XLOG(ERR) << fmt::format(
          "loggedSensorName {} is a duplicate", sensorName);
      return false;
    }
    if (sensorNames.count(sensorName) == 0) {
      XLOG(ERR) << fmt::format(
          "loggedSensorName {} is not defined in SensorConfig", sensorName);
      return false;
    }
  }
  return true;
}

std::unordered_set<std::string> ConfigValidator::getAllSensorNames(
    const sensor_config::SensorConfig& sensorConfig) {
  std::unordered_set<std::string> sensorNames;
  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    for (const auto& pmSensor : *pmUnitSensors.sensors()) {
      sensorNames.emplace(*pmSensor.name());
    }
    for (const auto& versionedPmSensor : *pmUnitSensors.versionedSensors()) {
      for (const auto& pmSensor : *versionedPmSensor.sensors()) {
        sensorNames.emplace(*pmSensor.name());
      }
    }
  }
  if (const auto& asicCmd = sensorConfig.asicCommand()) {
    sensorNames.emplace(*asicCmd->sensorName());
  }
  return sensorNames;
}

std::unordered_set<std::string> ConfigValidator::getAllUniversalSensorNames(
    const sensor_config::SensorConfig& sensorConfig) {
  std::unordered_set<std::string> sensorNames;
  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    for (const auto& pmSensor : *pmUnitSensors.sensors()) {
      sensorNames.emplace(*pmSensor.name());
    }
    // Per-PmUnit intersection: include only names present in every
    // versionedSensors entry, so resolution can never miss them.
    const auto& versioned = *pmUnitSensors.versionedSensors();
    if (versioned.empty()) {
      continue;
    }
    std::unordered_set<std::string> intersection;
    for (const auto& pmSensor : *versioned.front().sensors()) {
      intersection.emplace(*pmSensor.name());
    }
    for (size_t i = 1; i < versioned.size() && !intersection.empty(); ++i) {
      std::unordered_set<std::string> next;
      for (const auto& pmSensor : *versioned[i].sensors()) {
        if (intersection.count(*pmSensor.name())) {
          next.emplace(*pmSensor.name());
        }
      }
      intersection = std::move(next);
    }
    sensorNames.insert(intersection.begin(), intersection.end());
  }
  if (auto asicCmd = sensorConfig.asicCommand()) {
    sensorNames.emplace(*asicCmd->sensorName());
  }
  return sensorNames;
}

bool ConfigValidator::isValidTemperatureSensorThresholds(
    const SensorConfig& sensorConfig) {
  const auto& platformName = *sensorConfig.platformName();
  if (kTempThresholdViolators.count(platformName)) {
    XLOG(WARN) << fmt::format(
        "Platform '{}' is a known temperature threshold violator,"
        " skipping threshold validation",
        platformName);
    return true;
  }

  XLOG(DBG1) << "Validating temperature sensor thresholds";

  auto checkSensors = [](const std::vector<PmSensor>& sensors) -> bool {
    for (const auto& sensor : sensors) {
      if (*sensor.type() != SensorType::TEMPERATURE) {
        continue;
      }
      if (!sensor.thresholds().has_value()) {
        XLOG(ERR) << fmt::format(
            "Temperature sensor '{}' must have thresholds defined",
            *sensor.name());
        return false;
      }
    }
    return true;
  };

  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    if (!checkSensors(*pmUnitSensors.sensors())) {
      return false;
    }
    for (const auto& versionedPmSensor : *pmUnitSensors.versionedSensors()) {
      if (!checkSensors(*versionedPmSensor.sensors())) {
        return false;
      }
    }
  }
  return true;
}

bool ConfigValidator::isValidSensorName(
    const sensor_config::SensorConfig& sensorConfig,
    const std::string& sensorName) {
  for (const auto& pmUnitSensors : *sensorConfig.pmUnitSensorsList()) {
    for (const auto& pmSensor : *pmUnitSensors.sensors()) {
      if (sensorName == *pmSensor.name()) {
        return true;
      }
    }
  }
  if (auto asicCmd = sensorConfig.asicCommand()) {
    if (sensorName == asicCmd->sensorName()) {
      return true;
    }
  }
  XLOG(ERR) << fmt::format(
      "Sensor `{}` is not defined in SensorConfig", sensorName);
  return false;
}

} // namespace facebook::fboss::platform::sensor_service
