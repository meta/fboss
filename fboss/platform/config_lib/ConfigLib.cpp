// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
#include "fboss/platform/config_lib/ConfigLib.h"

#include <algorithm>
#include <unordered_map>

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/helpers/PlatformNameLib.h"

#include "fboss/platform/config_lib/GeneratedConfig.h"

DEFINE_string(
    config_file,
    "",
    "Optional configuration file. "
    "If empty we pick the platform default config");

DEFINE_bool(
    run_in_netos,
    false,
    "Setup platform services to run within netos environment");

using namespace facebook::fboss::platform;

namespace {

// Distinct hardware platforms that reuse another platform's config. Keys and
// values are lowercase config names, matching the generated config map keys.
const std::unordered_map<std::string, std::string> kConfigAliases = {
    {"wedge800cnhp", "wedge800cact"},
    {"wedge800bnhp", "wedge800bact"},
    {"meru800biab", "meru800bia"},
    {"meru800biac", "meru800bia"},
    {"minipack3bta", "minipack3ba"},
    {"m4052acti", "minipack3ba"},
    {"m4052actm", "minipack3bam"},
};

std::string toLower(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), ::tolower);
  return str;
}

std::string toUpper(std::string str) {
  std::transform(str.begin(), str.end(), str.begin(), ::toupper);
  return str;
}

std::string resolveConfigAlias(const std::string& platformNameLower) {
  auto it = kConfigAliases.find(platformNameLower);
  return it != kConfigAliases.end() ? it->second : platformNameLower;
}

std::string getPlatformName(const std::optional<std::string>& platformName) {
  auto platformStr = toLower(
      platformName ? *platformName
                   : *helpers::PlatformNameLib().getPlatformName());
  if (platformStr == "darwin" && FLAGS_run_in_netos) {
    platformStr =
        "darwin_netos"; // Bypass Darwin config for netos, Remove after all
                        // Darwin platforms are onboarded to netos
  }
  platformStr = resolveConfigAlias(platformStr);
  XLOG(DBG1) << "The inferred platform is " << platformStr;
  return platformStr;
}

} // namespace

namespace facebook::fboss::platform {

std::optional<std::string> ConfigLib::getConfigFromFile() const {
  // Use the member variable if provided, otherwise fall back to gflags
  std::string configFile =
      !configFilePath_.empty() ? configFilePath_ : FLAGS_config_file;
  if (!configFile.empty()) {
    XLOG(INFO) << "Using config file: " << configFile;
    std::string configJson;
    if (!folly::readFile(configFile.c_str(), configJson)) {
      throw std::runtime_error("Can not read config file: " + configFile);
    }
    return configJson;
  }
  return std::nullopt;
}

std::string ConfigLib::getSensorServiceConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::sensor_service.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "sensor_service configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::getFanServiceConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::fan_service.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "fan_service configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::getFwUtilConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::fw_util.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "fw_util configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::getPlatformManagerConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::platform_manager.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "platform_manager configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::getLedManagerConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::led_manager.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "led_manager configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::getBspTestConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  return configs::bsp_tests.at(getPlatformName(platformName));
}

std::string ConfigLib::getShowtechConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  return configs::rma_showtech.at(getPlatformName(platformName));
}

std::string ConfigLib::getRebootCauseFinderConfig(
    const std::optional<std::string>& platformName) const {
  if (auto configJson = getConfigFromFile()) {
    return *configJson;
  }
  try {
    return configs::reboot_cause_finder.at(getPlatformName(platformName));
  } catch (const std::out_of_range&) {
    throw std::runtime_error(
        "reboot_cause_finder configuration not found for platform: " +
        getPlatformName(platformName));
  }
}

std::string ConfigLib::canonicalConfigPlatformName(
    const std::string& platformName) {
  return toUpper(resolveConfigAlias(toLower(platformName)));
}

void ConfigLib::verifyPlatformNameMatches(
    const std::string& platformNameInConfig,
    const std::string& inferredPlatformName) {
  if (toUpper(platformNameInConfig) ==
      canonicalConfigPlatformName(inferredPlatformName)) {
    return;
  }
  throw std::runtime_error(
      "platformName in config '" + platformNameInConfig +
      "' does not match the inferred platform name '" + inferredPlatformName +
      "'");
}

} // namespace facebook::fboss::platform
