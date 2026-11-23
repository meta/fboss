// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/fw_util/ConfigValidator.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/logging/xlog.h>
#include <re2/re2.h>

namespace {

// Valid command types based on analysis of existing configurations
const std::unordered_set<std::string> kValidCommandTypes = {
    "flashrom",
    "jtag",
    "gpioset",
    "gpioget",
    "writeToPort",
    "createI2cDevice",
    "i2cBusRead",
    "jam",
    "xapp",
    "psu_util"};

// Valid version types based on analysis of existing configurations
const std::unordered_set<std::string> kValidVersionTypes = {
    "sysfs",
    "full_command",
    "Not Applicable"};

// Valid programmer types for flashrom
const std::unordered_set<std::string> kValidProgrammerTypes = {
    "internal",
    "linux_spi:dev="};

const re2::RE2 kSha1SumRegex{"^[a-fA-F0-9]{40}$"};
const re2::RE2 kHexValueRegex{"^(0x)?[a-fA-F0-9]+$"};
const re2::RE2 kDevmapPathRegex{"^/run/devmap/.+"};
const re2::RE2 kSysfsPathRegex{"^/sys/.+"};

// Helper function to check if component is allowed to use sysfs paths
bool isComponentAllowedSysfsAccess(const std::string& deviceName) {
  std::string lowerName = deviceName;
  folly::toLowerAscii(lowerName);
  return lowerName.find("bios") != std::string::npos ||
      lowerName.find("oob") != std::string::npos;
}
} // namespace

namespace facebook::fboss::platform::fw_util {

bool ConfigValidator::isValid(const fw_util_config::FwUtilConfig& config) {
  XLOG(DBG1) << "Validating fw_util config";
  if (config.fwConfigs()->empty()) {
    XLOG(INFO) << "FwUtilConfig is empty. Please check the config file";
  }
  // Validate each firmware configuration
  for (const auto& [deviceName, fwConfig] : *config.fwConfigs()) {
    if (!isValidFwConfig(deviceName, fwConfig)) {
      return false;
    }
  }

  XLOG(DBG1) << "FwUtilConfig validation passed";
  return true;
}

bool ConfigValidator::isValidFwConfig(
    const std::string& deviceName,
    const fw_util_config::FwConfig& fwConfig) {
  XLOG(DBG1) << fmt::format("Validating FwConfig for device: {}", deviceName);

  // Skip validation for oob_eeprom as it's not currently being used
  // and contains unsupported command types like ddDynamicBus
  if (deviceName == "oob_eeprom") {
    XLOG(INFO) << "Skipping validation for oob_eeprom device";
    return true;
  }

  if (deviceName.empty()) {
    XLOG(ERR) << "Device name cannot be empty";
    return false;
  }

  // Priority must be positive
  if (*fwConfig.priority() < 0) {
    XLOG(ERR) << fmt::format(
        "Priority must be positive for device {}, got: {}",
        deviceName,
        *fwConfig.priority());
    return false;
  }

  if (!isValidVersionConfig(deviceName, *fwConfig.version())) {
    return false;
  }

  // Validate desiredVersion if present
  if (fwConfig.desiredVersion() && fwConfig.desiredVersion()->empty()) {
    XLOG(ERR) << fmt::format(
        "Desired version is empty string for device {}", deviceName);
    return false;
  }

  // Validate SHA1 sum if present
  if (fwConfig.sha1sum() && !isValidSha1Sum(*fwConfig.sha1sum())) {
    XLOG(ERR) << fmt::format(
        "Invalid SHA1 sum for device {}: {}", deviceName, *fwConfig.sha1sum());
    return false;
  }

  if (fwConfig.delayAfterUpgradeSec() && *fwConfig.delayAfterUpgradeSec() < 0) {
    XLOG(ERR) << fmt::format(
        "delayAfterUpgradeSec must be non-negative for device {}, got: {}",
        deviceName,
        *fwConfig.delayAfterUpgradeSec());
    return false;
  }

  // Validate pre-upgrade configurations
  if (fwConfig.preUpgrade().has_value()) {
    for (const auto& preUpgradeConfig : *fwConfig.preUpgrade()) {
      if (!isValidPreUpgradeConfig(preUpgradeConfig)) {
        XLOG(ERR) << fmt::format(
            "Invalid pre-upgrade configuration for device {}", deviceName);
        return false;
      }
    }
  }

  // Validate upgrade configurations
  if (fwConfig.upgrade().has_value()) {
    for (const auto& upgradeConfig : *fwConfig.upgrade()) {
      if (!isValidUpgradeConfig(upgradeConfig)) {
        XLOG(ERR) << fmt::format(
            "Invalid upgrade configuration for device {}", deviceName);
        return false;
      }
    }
  }

  // Validate post-upgrade configurations
  if (fwConfig.postUpgrade().has_value()) {
    for (const auto& postUpgradeConfig : *fwConfig.postUpgrade()) {
      if (!isValidPostUpgradeConfig(postUpgradeConfig)) {
        XLOG(ERR) << fmt::format(
            "Invalid post-upgrade configuration for device {}", deviceName);
        return false;
      }
    }
  }

  // Validate verify configuration
  if (fwConfig.verify().has_value() &&
      !isValidVerifyConfig(*fwConfig.verify())) {
    XLOG(ERR) << fmt::format(
        "Invalid verify configuration for device {}", deviceName);
    return false;
  }

  // Validate read configuration
  if (fwConfig.read().has_value() && !isValidReadConfig(*fwConfig.read())) {
    XLOG(ERR) << fmt::format(
        "Invalid read configuration for device {}", deviceName);
    return false;
  }

  return true;
}

bool ConfigValidator::isValidFlashromConfig(
    const fw_util_config::FlashromConfig& flashromConfig) {
  // Programmer type is required
  if (!flashromConfig.programmer_type().has_value() ||
      flashromConfig.programmer_type()->empty()) {
    XLOG(ERR) << "Flashrom programmer_type is required and cannot be empty";
    return false;
  }

  if (!isValidProgrammerType(*flashromConfig.programmer_type())) {
    XLOG(ERR) << fmt::format(
        "Invalid flashrom programmer type: {}",
        *flashromConfig.programmer_type());
    return false;
  }

  // Validate programmer path if specified
  if (flashromConfig.programmer().has_value() &&
      !flashromConfig.programmer()->empty()) {
    if (!isValidPath(*flashromConfig.programmer())) {
      XLOG(ERR) << fmt::format(
          "Invalid flashrom programmer path: {}", *flashromConfig.programmer());
      return false;
    }
  }

  // Validate custom content offset if custom content is specified
  if (flashromConfig.custom_content().has_value() &&
      flashromConfig.custom_content_offset().has_value()) {
    if (*flashromConfig.custom_content_offset() < 0) {
      XLOG(ERR) << "Custom content offset must be non-negative";
      return false;
    }
  }

  return true;
}

bool ConfigValidator::isValidJtagConfig(
    const fw_util_config::JtagConfig& jtagConfig) {
  if (jtagConfig.path()->empty()) {
    XLOG(ERR) << "JTAG path cannot be empty";
    return false;
  }

  if (!isValidPath(*jtagConfig.path())) {
    XLOG(ERR) << fmt::format("Invalid JTAG path: {}", *jtagConfig.path());
    return false;
  }

  return true;
}

bool ConfigValidator::isValidJamConfig(
    const fw_util_config::JamConfig& jamConfig) {
  if (jamConfig.jamExtraArgs()->empty()) {
    XLOG(ERR) << "JAM configuration must have at least one extra argument";
    return false;
  }
  return true;
}

bool ConfigValidator::isValidXappConfig(
    const fw_util_config::XappConfig& xappConfig) {
  if (xappConfig.xappExtraArgs()->empty()) {
    XLOG(ERR) << "XAPP configuration must have at least one extra argument";
    return false;
  }
  return true;
}
bool ConfigValidator::isValidPsuUtilConfig(
    const fw_util_config::PsuUtilConfig& psuUtilConfig) {
  if (psuUtilConfig.psuUtilExtraArgs()->empty()) {
    XLOG(ERR) << "psu_util configuration must have at least one extra argument";
    return false;
  }
  return true;
}

bool ConfigValidator::isValidGpiosetConfig(
    const fw_util_config::GpiosetConfig& gpiosetConfig) {
  if (gpiosetConfig.gpioChip()->empty()) {
    XLOG(ERR) << "GPIO chip cannot be empty";
    return false;
  }

  if (gpiosetConfig.gpioChipPin()->empty()) {
    XLOG(ERR) << "GPIO chip pin cannot be empty";
    return false;
  }

  if (gpiosetConfig.gpioChipValue()->empty()) {
    XLOG(ERR) << "GPIO chip value cannot be empty";
    return false;
  }

  return true;
}

bool ConfigValidator::isValidGpiogetConfig(
    const fw_util_config::GpiogetConfig& gpiogetConfig) {
  if (gpiogetConfig.gpioChip()->empty()) {
    XLOG(ERR) << "GPIO chip cannot be empty";
    return false;
  }

  if (gpiogetConfig.gpioChipPin()->empty()) {
    XLOG(ERR) << "GPIO chip pin cannot be empty";
    return false;
  }

  return true;
}

bool ConfigValidator::isValidWriteToPortConfig(
    const fw_util_config::WriteToPortConfig& writeToPortConfig) {
  if (writeToPortConfig.portFile()->empty()) {
    XLOG(ERR) << "Port file cannot be empty";
    return false;
  }

  if (writeToPortConfig.hexByteValue()->empty()) {
    XLOG(ERR) << "Hex byte value cannot be empty";
    return false;
  }

  if (!re2::RE2::FullMatch(*writeToPortConfig.hexByteValue(), kHexValueRegex)) {
    XLOG(ERR) << fmt::format(
        "Invalid hex byte value: {}", *writeToPortConfig.hexByteValue());
    return false;
  }

  if (writeToPortConfig.hexOffset()->empty()) {
    XLOG(ERR) << "Hex offset cannot be empty";
    return false;
  }

  if (!re2::RE2::FullMatch(*writeToPortConfig.hexOffset(), kHexValueRegex)) {
    XLOG(ERR) << fmt::format(
        "Invalid hex offset: {}", *writeToPortConfig.hexOffset());
    return false;
  }

  return true;
}

bool ConfigValidator::isValidVersionConfig(
    const std::string& deviceName,
    const fw_util_config::VersionConfig& versionConfig) {
  if (versionConfig.versionType()->empty()) {
    XLOG(ERR) << "Version type cannot be empty";
    return false;
  }

  if (!isValidVersionType(*versionConfig.versionType())) {
    XLOG(ERR) << fmt::format(
        "Invalid version type: {}", *versionConfig.versionType());
    return false;
  }

  // Skip path validation for "Not Applicable" version type
  if (*versionConfig.versionType() == "Not Applicable") {
    return true; // No further validation needed
  }

  // For sysfs version types, path is required
  if (*versionConfig.versionType() == "sysfs") {
    if (!versionConfig.path() || versionConfig.path()->empty()) {
      XLOG(ERR) << "Path is required for sysfs version type";
      return false;
    }
    const std::string& path = *versionConfig.path();

    // Always allow /run/devmap paths for any component
    if (isValidPath(path)) {
      return true;
    }

    // Additionally allow sysfs paths for BIOS and OOB components
    if (isComponentAllowedSysfsAccess(deviceName) &&
        re2::RE2::FullMatch(path, kSysfsPathRegex)) {
      return true;
    }

    return false;
  }

  // For full_command version types, getVersionCmd is required
  if (*versionConfig.versionType() == "full_command") {
    if (!versionConfig.getVersionCmd() ||
        versionConfig.getVersionCmd()->empty()) {
      XLOG(ERR) << "getVersionCmd is required for full_command version type";
      return false;
    }
  }

  return true;
}

bool ConfigValidator::isValidPreUpgradeConfig(
    const fw_util_config::PreFirmwareOperationConfig& preUpgradeConfig) {
  if (!isValidCommandType(*preUpgradeConfig.commandType())) {
    XLOG(ERR) << fmt::format(
        "Invalid pre-upgrade command type: {}",
        *preUpgradeConfig.commandType());
    return false;
  }

  const std::string& commandType = *preUpgradeConfig.commandType();

  // Validate specific configurations using early returns
  if (commandType == "flashrom") {
    if (!preUpgradeConfig.flashromArgs().has_value()) {
      XLOG(ERR) << "Flashrom args required for flashrom command type";
      return false;
    }
    return isValidFlashromConfig(*preUpgradeConfig.flashromArgs());
  }

  if (commandType == "jtag") {
    if (!preUpgradeConfig.jtagArgs().has_value()) {
      XLOG(ERR) << "JTAG args required for jtag command type";
      return false;
    }
    return isValidJtagConfig(*preUpgradeConfig.jtagArgs());
  }

  if (commandType == "gpioset") {
    if (!preUpgradeConfig.gpiosetArgs().has_value()) {
      XLOG(ERR) << "GPIO set args required for gpioset command type";
      return false;
    }
    return isValidGpiosetConfig(*preUpgradeConfig.gpiosetArgs());
  }

  if (commandType == "writeToPort") {
    if (!preUpgradeConfig.writeToPortArgs().has_value()) {
      XLOG(ERR) << "Write to port args required for writeToPort command type";
      return false;
    }
    return isValidWriteToPortConfig(*preUpgradeConfig.writeToPortArgs());
  }

  // For command types without specific validation (e.g., createI2cDevice,
  // i2cBusRead)
  return true;
}

bool ConfigValidator::isValidUpgradeConfig(
    const fw_util_config::UpgradeConfig& upgradeConfig) {
  if (!isValidCommandType(*upgradeConfig.commandType())) {
    XLOG(ERR) << fmt::format(
        "Invalid upgrade command type: {}", *upgradeConfig.commandType());
    return false;
  }

  const std::string& commandType = *upgradeConfig.commandType();

  // Validate specific configurations using early returns
  if (commandType == "flashrom") {
    if (!upgradeConfig.flashromArgs().has_value()) {
      XLOG(ERR) << "Flashrom args required for flashrom command type";
      return false;
    }
    return isValidFlashromConfig(*upgradeConfig.flashromArgs());
  }

  if (commandType == "jam") {
    if (!upgradeConfig.jamArgs().has_value()) {
      XLOG(ERR) << "JAM args required for jam command type";
      return false;
    }
    return isValidJamConfig(*upgradeConfig.jamArgs());
  }

  if (commandType == "xapp") {
    if (!upgradeConfig.xappArgs().has_value()) {
      XLOG(ERR) << "XAPP args required for xapp command type";
      return false;
    }
    return isValidXappConfig(*upgradeConfig.xappArgs());
  }

  if (commandType == "psu_util") {
    if (!upgradeConfig.psuUtilArgs().has_value()) {
      XLOG(ERR) << "psu_util args required for psu_util command type";
      return false;
    }
    return isValidPsuUtilConfig(*upgradeConfig.psuUtilArgs());
  }

  // For command types without specific validation
  return true;
}

bool ConfigValidator::isValidPostUpgradeConfig(
    const fw_util_config::PostFirmwareOperationConfig& postUpgradeConfig) {
  if (!isValidCommandType(*postUpgradeConfig.commandType())) {
    XLOG(ERR) << fmt::format(
        "Invalid post-upgrade command type: {}",
        *postUpgradeConfig.commandType());
    return false;
  }

  const std::string& commandType = *postUpgradeConfig.commandType();

  // Validate specific configurations using early returns
  if (commandType == "gpioget") {
    if (!postUpgradeConfig.gpiogetArgs().has_value()) {
      XLOG(ERR) << "GPIO get args required for gpioget command type";
      return false;
    }
    return isValidGpiogetConfig(*postUpgradeConfig.gpiogetArgs());
  }

  if (commandType == "jtag") {
    if (!postUpgradeConfig.jtagArgs().has_value()) {
      XLOG(ERR) << "JTAG args required for jtag command type";
      return false;
    }
    return isValidJtagConfig(*postUpgradeConfig.jtagArgs());
  }

  if (commandType == "writeToPort") {
    if (!postUpgradeConfig.writeToPortArgs().has_value()) {
      XLOG(ERR) << "Write to port args required for writeToPort command type";
      return false;
    }
    return isValidWriteToPortConfig(*postUpgradeConfig.writeToPortArgs());
  }

  // For command types without specific validation
  return true;
}

bool ConfigValidator::isValidVerifyConfig(
    const fw_util_config::VerifyFirmwareOperationConfig& verifyConfig) {
  if (!isValidCommandType(*verifyConfig.commandType())) {
    XLOG(ERR) << fmt::format(
        "Invalid verify command type: {}", *verifyConfig.commandType());
    return false;
  }

  if (*verifyConfig.commandType() == "flashrom") {
    if (!verifyConfig.flashromArgs().has_value()) {
      XLOG(ERR) << "Flashrom args required for flashrom verify command";
      return false;
    }
    return isValidFlashromConfig(*verifyConfig.flashromArgs());
  }

  return true;
}

bool ConfigValidator::isValidReadConfig(
    const fw_util_config::ReadFirmwareOperationConfig& readConfig) {
  if (!isValidCommandType(*readConfig.commandType())) {
    XLOG(ERR) << fmt::format(
        "Invalid read command type: {}", *readConfig.commandType());
    return false;
  }

  if (*readConfig.commandType() == "flashrom") {
    if (!readConfig.flashromArgs().has_value()) {
      XLOG(ERR) << "Flashrom args required for flashrom read command";
      return false;
    }
    return isValidFlashromConfig(*readConfig.flashromArgs());
  }

  return true;
}

bool ConfigValidator::isValidCommandType(const std::string& commandType) {
  return kValidCommandTypes.count(commandType) > 0;
}

bool ConfigValidator::isValidVersionType(const std::string& versionType) {
  return kValidVersionTypes.count(versionType) > 0;
}

bool ConfigValidator::isValidProgrammerType(const std::string& programmerType) {
  return kValidProgrammerTypes.count(programmerType) > 0;
}

bool ConfigValidator::isValidPath(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  // Check for devmap paths (runtime paths)
  if (re2::RE2::FullMatch(path, kDevmapPathRegex)) {
    return true;
  }

  return false;
}

bool ConfigValidator::isValidSha1Sum(const std::string& sha1sum) {
  return re2::RE2::FullMatch(sha1sum, kSha1SumRegex);
}

} // namespace facebook::fboss::platform::fw_util
