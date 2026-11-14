// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.
#include "fboss/platform/weutil/ConfigUtils.h"

#include <folly/String.h>
#include <re2/re2.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/platform/config_lib/ConfigLib.h"

namespace {
using facebook::fboss::platform::platform_manager::PlatformConfig;
using facebook::fboss::platform::weutil::FruEeprom;

// Extract EEPROM name from symbolicPath
std::string getEepromName(const std::string& symbolicPath) {
  // Extracts the eeprom device name from the symbolic path.
  // Example: /run/devmap/eeproms/CHASSIS_EEPROM -> CHASSIS
  // Example: /run/devmap/eeproms/SCM_EEPROM -> SCM
  // Example: /run/devmap/eeproms/PDB_L_EEPROM -> PDB_L
  // Edge case: /run/devmap/eeproms/COME_FRU -> COME_FRU
  // Edge case: /run/devmap/eeproms/MERU800BFA_SCM_EEPROM -> SCM

  static const re2::RE2 re(
      "^/run/devmap/eeproms/([A-Za-z0-9_]+?)(?:_EEPROM)?$");
  re2::StringPiece match;
  if (!re2::RE2::FullMatch(symbolicPath, re, &match)) {
    throw std::runtime_error(
        fmt::format(
            "Unable to find eeprom name from symbolic path: {}", symbolicPath));
  }

  std::string eepromName = std::string(match);
  // Special handling for MERU platforms: extract text after underscore
  folly::StringPiece sp(eepromName);
  if (sp.removePrefix("MERU800BFA_") || sp.removePrefix("MERU_") ||
      sp.removePrefix("MERU800BIA_")) {
    eepromName = sp.str();
  }

  return eepromName;
}

// Extract EEPROM symbolicPath from devicePath
std::string getEepromPath(
    const PlatformConfig& platformConfig,
    const std::string& devicePath) {
  std::string eepromPath;
  for (const auto& [symbolicLink, path] :
       *platformConfig.symbolicLinkToDevicePath()) {
    if (path == devicePath) {
      eepromPath = symbolicLink;
      break;
    }
  }
  if (eepromPath.empty()) {
    throw std::runtime_error(
        fmt::format(
            "Unable to find symbolic link for device path: {}", devicePath));
  }
  return eepromPath;
}

// Extract EEPROM offset
int getEepromOffset(
    const PlatformConfig& platformConfig,
    const std::string& eepromName) {
  int eepromOffset = 0;
  for (const auto& [__, slotConfig] : *platformConfig.slotTypeConfigs()) {
    if (slotConfig.pmUnitName()) {
      if (*slotConfig.pmUnitName() == eepromName) {
        if (slotConfig.idpromConfig()) {
          eepromOffset = *slotConfig.idpromConfig()->offset();
        }
        break;
      }
    }
  }

  for (const auto& [__, pmUnitConfig] : *platformConfig.pmUnitConfigs()) {
    for (const auto& i2cDeviceConfig : *pmUnitConfig.i2cDeviceConfigs()) {
      if (*i2cDeviceConfig.pmUnitScopedName() == eepromName + "_EEPROM" &&
          *i2cDeviceConfig.isEeprom() && i2cDeviceConfig.eepromOffset()) {
        eepromOffset = *i2cDeviceConfig.eepromOffset();
        break;
      }
    }
  }

  return eepromOffset;
}

std::vector<std::string> getEepromNames(
    const std::unordered_map<std::string, FruEeprom>& fruEepromList) {
  std::vector<std::string> eepromNames;
  for (const auto& [eepromName, eepromConfig] : fruEepromList) {
    std::string fruName = eepromName;
    eepromNames.push_back(fruName);
  }

  return eepromNames;
}

// Gets the FruEeprom based on the eeprom name specified.
FruEeprom getFruEepromByName(
    const std::string& eepromName,
    const std::unordered_map<std::string, FruEeprom>& fruEepromList) {
  auto itr = fruEepromList.find(eepromName);
  if (itr == fruEepromList.end()) {
    throw std::runtime_error(
        fmt::format(
            "Invalid EEPROM name {}. Valid EEPROM names are: {}",
            eepromName,
            folly::join(", ", getEepromNames(fruEepromList))));
  }
  return itr->second;
}
} // namespace

namespace facebook::fboss::platform::weutil {

ConfigUtils::ConfigUtils(const std::optional<std::string>& platformName) {
  std::string configJson = ConfigLib().getPlatformManagerConfig(platformName);

  try {
    apache::thrift::SimpleJSONSerializer::deserialize<
        platform_manager::PlatformConfig>(configJson, config_);
  } catch (const std::exception& e) {
    throw std::runtime_error(
        fmt::format("Failed to deserialize platform config: {}", e.what()));
  }
}

std::unordered_map<std::string, FruEeprom> ConfigUtils::getFruEepromList() {
  // Create FruEeprom list from the platform manager config
  std::unordered_map<std::string, FruEeprom> fruEepromList;

  // Create FruEepromList
  for (const auto& [symbolicLink, devicePath] :
       *config_.symbolicLinkToDevicePath()) {
    if (symbolicLink.starts_with("/run/devmap/eeproms/")) {
      std::string eepromName = getEepromName(symbolicLink);
      auto& fruEeprom = fruEepromList[eepromName];
      fruEeprom.path = symbolicLink;
      fruEeprom.offset = getEepromOffset(config_, eepromName);
    }
  }

  // Because of upstream kernel issues, we have to manually read the
  // SCM/SMB EEPROM for certain platforms. It is read directly
  // with ioctl and written to the /run/devmap file.
  // See: https://github.com/facebookexternal/fboss.bsp.arista/pull/31/files
  // The EEPROM is not a symlink created by PlatformManager (instead it is
  // a regular file). So it is not present in `symbolicLinkToDevicePath`, and we
  // have to add it explicitly. ```
  if (config_.platformName().value() == "MERU800BFA" ||
      config_.platformName().value() == "MERU800BIA" ||
      config_.platformName().value() == "BLACKWOLF800BANW") {
    std::string eepromName = "SCM";
    FruEeprom fruEeprom;
    fruEeprom.path = "/run/devmap/eeproms/MERU_SCM_EEPROM";
    fruEeprom.offset = 0;
    fruEepromList[eepromName] = fruEeprom;
  } else if (config_.platformName().value() == "ICECUBE800BANW") {
    std::string eepromName = "SMB";
    FruEeprom fruEeprom;
    fruEeprom.path = "/run/devmap/eeproms/SMB_EEPROM";
    fruEeprom.offset = 0;
    fruEepromList[eepromName] = fruEeprom;
  } else if (config_.platformName().value() == "DARWIN") {
    // Darwin Platform special case that doesn't have a chassis EEPROM path
    std::string eepromName = "CHASSIS";
    FruEeprom fruEeprom;
    fruEeprom.path = "";
    fruEeprom.offset = 0;
    fruEepromList[eepromName] = fruEeprom;
  }

  return fruEepromList;
}

std::string ConfigUtils::getChassisEepromName() {
  // Get Chassis Eeprom Device Name
  // Darwin Platform special case that doesn't have a chassis EEPROM path
  if (config_.platformName().value() == "DARWIN") {
    return "CHASSIS";
  } else if (!config_.chassisEepromDevicePath()->empty()) {
    std::string chassisEepromPath =
        getEepromPath(config_, *config_.chassisEepromDevicePath());
    return getEepromName(chassisEepromPath);
  } else {
    throw std::runtime_error("Chassis EEPROM device path not found");
  }
}

FruEeprom ConfigUtils::getFruEeprom(const std::string& eepromName) {
  auto fruEepromList = getFruEepromList();
  return getFruEepromByName(eepromName, fruEepromList);
}
} // namespace facebook::fboss::platform::weutil
