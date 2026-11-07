// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/sensor_service/Utils.h"

#include <chrono>
#include <stdexcept>

#include <exprtk.hpp>
#include <folly/logging/xlog.h>
#include <re2/re2.h>

#include "fboss/platform/config_lib/ConfigLib.h"
#include "fboss/platform/helpers/PlatformNameLib.h"
#include "fboss/platform/sensor_service/ConfigValidator.h"

namespace fs = std::filesystem;

namespace facebook::fboss::platform::sensor_service {

namespace {
// Strict weak ordering of VersionedSensorComparator(lhs,rhs)
// Returns true if lhs > rhs, false otherwise.
struct {
  bool operator()(
      const platform_manager::PmUnitVersion& l1,
      const platform_manager::PmUnitVersion& l2) {
    if (*l1.productionState() != *l2.productionState()) {
      return *l1.productionState() > *l2.productionState();
    }
    if (*l1.productionSubState() != *l2.productionSubState()) {
      return *l1.productionSubState() > *l2.productionSubState();
    }
    return *l1.respinVariantIndicator() > *l2.respinVariantIndicator();
  }
  bool operator()(
      const VersionedPmSensor& vSensor1,
      const VersionedPmSensor& vSensor2) {
    if (*vSensor1.productionState() != *vSensor2.productionState()) {
      return *vSensor1.productionState() > *vSensor2.productionState();
    }
    if (*vSensor1.productionSubState() != *vSensor2.productionSubState()) {
      return *vSensor1.productionSubState() > *vSensor2.productionSubState();
    }
    return *vSensor1.respinVariantIndicator() >
        *vSensor2.respinVariantIndicator();
  }
} VersionedSensorComparator;
} // namespace

uint64_t Utils::nowInSecs() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

float Utils::computeExpression(
    const std::string& equation,
    float input,
    const std::string& symbol) {
  std::string temp_equation = equation;

  // Replace "@" with a valid symbol
  static const re2::RE2 atRegex("@");

  re2::RE2::GlobalReplace(&temp_equation, atRegex, symbol);

  exprtk::symbol_table<float> symbolTable;

  symbolTable.add_variable(symbol, input);

  exprtk::expression<float> expr;
  expr.register_symbol_table(symbolTable);

  exprtk::parser<float> parser;
  if (!parser.compile(temp_equation, expr)) {
    XLOG(ERR) << "Failed to compile PM sensor equation: " << temp_equation;
    throw std::runtime_error(
        "Failed to compile PM sensor equation: " + temp_equation);
  }

  return expr.value();
}

std::optional<VersionedPmSensor> Utils::resolveVersionedSensors(
    const std::optional<platform_manager::PmUnitInfo>& pmUnitInfo,
    const std::string& slotPath,
    std::vector<VersionedPmSensor> versionedSensors) {
  if (versionedSensors.empty()) {
    return std::nullopt;
  }

  // Select the entries for this hardware, in priority order:
  //   1. entries whose productName matches the hardware's EEPROM product;
  //   2. else the "DEFAULT" block, if this unit defines one;
  //   3. else the plain numerically-versioned entries (no productName).
  // The chosen set is then narrowed by version below.
  constexpr auto kDefaultProductName = "DEFAULT";
  auto hwProd = (pmUnitInfo && pmUnitInfo->eepromProductName())
      ? pmUnitInfo->eepromProductName().to_optional()
      : std::nullopt;
  auto nameMatchesHardware = [&](const auto& vs) {
    return hwProd && vs.productName().to_optional() == hwProd;
  };
  auto isDefaultBlock = [&](const auto& vs) {
    return vs.productName().has_value() &&
        *vs.productName() == kDefaultProductName;
  };
  auto hasNoProductName = [](const auto& vs) {
    return !vs.productName().has_value();
  };
  bool haveExactMatch = std::any_of(
      versionedSensors.begin(), versionedSensors.end(), nameMatchesHardware);
  bool haveDefaultBlock = std::any_of(
      versionedSensors.begin(), versionedSensors.end(), isDefaultBlock);
  std::vector<VersionedPmSensor> kept;
  for (auto& vs : versionedSensors) {
    bool keep = false;
    if (haveExactMatch) {
      keep = nameMatchesHardware(vs); // 1. product-specific match
    } else if (haveDefaultBlock) {
      keep = isDefaultBlock(vs); // 2. explicit DEFAULT block
    } else {
      keep = hasNoProductName(vs); // 3. plain numerically-versioned entries
    }
    if (keep) {
      kept.push_back(std::move(vs));
    }
  }
  versionedSensors = std::move(kept);

  if (haveExactMatch) {
    XLOG(DBG1) << fmt::format(
        "Using product-name-specific VersionedPmSensor for '{}' at {}",
        *hwProd,
        slotPath);
  } else if (hwProd) {
    XLOG(DBG1) << fmt::format(
        "No product-name-specific VersionedPmSensor for '{}' at {}. "
        "Falling back to universal entries.",
        *hwProd,
        slotPath);
  }

  if (versionedSensors.empty()) {
    return std::nullopt;
  }

  // Sort in descending order by version.
  std::sort(
      versionedSensors.begin(),
      versionedSensors.end(),
      VersionedSensorComparator);
  // Use the latest PmUnitInfo as the best effort because eventually the latest
  // respins will only be deployed to DC. So more merits to tailor towards them
  // with an assumption that latest respins will mainly be in the DC.
  if (!pmUnitInfo || !pmUnitInfo->version()) {
    XLOG(INFO) << fmt::format(
        "No version available for PmUnit at {}. "
        "Fall back to the latest VersionedPmSensor",
        slotPath);
    return versionedSensors.front();
  }
  const auto& fetchedVersion = *pmUnitInfo->version();
  for (const auto& versionedSensor : versionedSensors) {
    // Find a VersionedSensor that satisfies fetched PmUnitInfo version.
    // i.e. PmUnitInfo version >= VersionedSensor sensor
    platform_manager::PmUnitVersion sensorVersion;
    sensorVersion.productionState() = *versionedSensor.productionState();
    sensorVersion.productionSubState() = *versionedSensor.productionSubState();
    sensorVersion.respinVariantIndicator() =
        *versionedSensor.respinVariantIndicator();
    if (!VersionedSensorComparator(sensorVersion, fetchedVersion)) {
      XLOG(DBG1) << fmt::format(
          "Resolved to VersionedPmSensor of version {}.{}.{} (productName: {})",
          *versionedSensor.productionState(),
          *versionedSensor.productionSubState(),
          *versionedSensor.respinVariantIndicator(),
          versionedSensor.productName().value_or("UNSET"));
      return versionedSensor;
    }
  }
  return std::nullopt;
}

SensorConfig Utils::getConfig() {
  auto platformName = helpers::PlatformNameLib().getPlatformName();
  SensorConfig sensorConfig =
      apache::thrift::SimpleJSONSerializer::deserialize<SensorConfig>(
          ConfigLib().getSensorServiceConfig(platformName));
  ConfigLib::verifyPlatformNameMatches(
      *sensorConfig.platformName(), platformName.value_or(""));
  if (!ConfigValidator().isValid(sensorConfig)) {
    throw std::runtime_error("Invalid sensor config");
  }
  return sensorConfig;
}

std::optional<std::string> Utils::getPciAddress(
    const std::string& vendorId,
    const std::string& deviceId) {
  std::optional<std::string> sbdf;
  for (const auto& dirEntry : fs::directory_iterator("/sys/bus/pci/devices")) {
    std::string vendor, device;
    auto deviceFilePath = dirEntry.path() / "device";
    auto vendorFilePath = dirEntry.path() / "vendor";
    if (!folly::readFile(vendorFilePath.c_str(), vendor)) {
      XLOG(ERR) << "Failed to read vendor file from " << dirEntry.path();
    }
    if (!folly::readFile(deviceFilePath.c_str(), device)) {
      XLOG(ERR) << "Failed to read device file from " << dirEntry.path();
    }
    if (folly::trimWhitespace(vendor).str() == vendorId &&
        folly::trimWhitespace(device).str() == deviceId) {
      sbdf = dirEntry.path().filename().string();
      break;
    }
  }
  return sbdf;
}
} // namespace facebook::fboss::platform::sensor_service
