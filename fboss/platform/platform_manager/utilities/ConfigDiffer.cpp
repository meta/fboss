// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/platform_manager/utilities/ConfigDiffer.h"

#include <iostream>

#include <fmt/core.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/op/Get.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss::platform::platform_manager {

// Unicode right arrow for showing direction of changes
constexpr const char* kArrow = " \u2192 ";

void ConfigDiffer::addDifference(
    const std::string& pmUnitName,
    const std::string& versionInfo,
    DiffType type,
    const std::string& deviceName,
    const std::string& fieldName,
    const std::optional<std::string>& oldValue,
    const std::optional<std::string>& newValue) {
  diffs_.push_back(
      ConfigDifference{
          .type = type,
          .pmUnitName = pmUnitName,
          .versionInfo = versionInfo,
          .deviceName = deviceName,
          .fieldName = fieldName,
          .oldValue = oldValue,
          .newValue = newValue,
      });
}

void ConfigDiffer::compareI2cDeviceConfigs(
    const std::vector<I2cDeviceConfig>& configs1,
    const std::vector<I2cDeviceConfig>& configs2,
    const std::string& pmUnitName,
    const std::string& versionInfo) {
  // Create maps keyed by pmUnitScopedName for easier comparison
  std::map<std::string, I2cDeviceConfig> map1;
  std::map<std::string, I2cDeviceConfig> map2;

  for (const auto& config : configs1) {
    map1[*config.pmUnitScopedName()] = config;
  }

  for (const auto& config : configs2) {
    map2[*config.pmUnitScopedName()] = config;
  }

  // Find removed devices
  for (const auto& [name, config] : map1) {
    if (map2.find(name) == map2.end()) {
      addDifference(
          pmUnitName, versionInfo, DiffType::REMOVED, name, "[REMOVED]");
    }
  }

  // Find added devices
  for (const auto& [name, config] : map2) {
    if (map1.find(name) == map1.end()) {
      addDifference(pmUnitName, versionInfo, DiffType::ADDED, name, "[ADDED]");
    }
  }

  // Find modified devices - compare field by field using Thrift reflection
  for (const auto& [name, config1] : map1) {
    auto it = map2.find(name);
    if (it == map2.end()) {
      XLOG(ERR) << fmt::format("ALERT: Inconsistent information for {}", name);
      continue;
    }

    const auto& config2 = it->second;

    apache::thrift::op::for_each_field_id<I2cDeviceConfig>(
        [&pmUnitName, &versionInfo, &name, &config1, &config2, this]<class Id>(
            Id) {
          const auto fieldName =
              apache::thrift::op::get_name_v<I2cDeviceConfig, Id>;

          if (fieldName == "pmUnitScopedName") {
            return;
          }

          const auto& ref1 = apache::thrift::op::get<Id>(config1);
          const auto& ref2 = apache::thrift::op::get<Id>(config2);

          if (ref1 != ref2) {
            using FieldType =
                apache::thrift::op::get_native_type<I2cDeviceConfig, Id>;

            auto convertToString = [](const auto& ref) -> std::string {
              if (const auto* val =
                      apache::thrift::op::get_value_or_null(ref)) {
                if constexpr (std::is_same_v<FieldType, std::string>) {
                  return *val;
                } else if constexpr (std::is_same_v<FieldType, bool>) {
                  return *val ? "true" : "false";
                } else if constexpr (std::is_arithmetic_v<FieldType>) {
                  return std::to_string(*val);
                } else {
                  return apache::thrift::SimpleJSONSerializer::serialize<
                      std::string>(*val);
                }
              }
              return "null";
            };

            addDifference(
                pmUnitName,
                versionInfo,
                DiffType::MODIFIED,
                name,
                std::string(fieldName),
                convertToString(ref1),
                convertToString(ref2));
          }
        });
  }
}

void ConfigDiffer::comparePmUnitConfigs(
    const PmUnitConfig& config1,
    const PmUnitConfig& config2,
    const std::string& pmUnitName,
    const std::string& versionInfo) {
  // Only compare i2cDeviceConfigs, as other fields are guaranteed to be same
  // (validated by ConfigValidator)

  compareI2cDeviceConfigs(
      *config1.i2cDeviceConfigs(),
      *config2.i2cDeviceConfigs(),
      pmUnitName,
      versionInfo);
}

void ConfigDiffer::compareVersionedConfigs(const PlatformConfig& config) {
  XLOGF(INFO, "Comparing versioned configs for all PmUnits");

  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          config.versionedPmUnitConfigs())) {
    XLOGF(INFO, "No versioned configs found in platform config");
    return;
  }

  const auto& versionedPmUnitConfigs = config.versionedPmUnitConfigs().value();
  for (const auto& [pmUnit, versionedConfigs] : versionedPmUnitConfigs) {
    compareVersionedConfigs(config, pmUnit);
  }
}

void ConfigDiffer::compareVersionedConfigs(
    const PlatformConfig& config,
    const std::string& pmUnitName) {
  XLOGF(INFO, "Comparing versioned configs for PmUnit: {}", pmUnitName);

  const auto& pmUnitConfigs = config.pmUnitConfigs().value();
  auto defaultIt = pmUnitConfigs.find(pmUnitName);
  if (defaultIt == pmUnitConfigs.end()) {
    XLOGF(INFO, "ALERT: No default config found for PmUnit '{}'", pmUnitName);
    return;
  }
  const auto& defaultConfig = defaultIt->second;

  const auto& versionedPmUnitConfigs = config.versionedPmUnitConfigs().value();
  auto versionedIt = versionedPmUnitConfigs.find(pmUnitName);
  if (versionedIt == versionedPmUnitConfigs.end()) {
    XLOGF(ERR, "ALERT: No versioned configs found for PmUnit '{}'", pmUnitName);
    return;
  }
  const auto& versionedConfigs = versionedIt->second;

  // Compare each versioned config against the default config
  for (const auto& versionedConfig : versionedConfigs) {
    std::string versionInfo;
    if (auto pmUvs = versionedConfig.pmUnitVersions();
        pmUvs && !pmUvs->empty()) {
      std::string versions;
      for (const auto& pmUv : *pmUvs) {
        if (!versions.empty()) {
          versions += ",";
        }
        versions += fmt::format(
            "v{}.{}.{}",
            *pmUv.productionState(),
            *pmUv.productionSubState(),
            *pmUv.respinVariantIndicator());
      }
      versionInfo = fmt::format("default{}{}", kArrow, versions);
    } else if (versionedConfig.productSubVersion()) {
      versionInfo = fmt::format(
          "default{}v{}", kArrow, *versionedConfig.productSubVersion());
    } else {
      XLOGF(
          WARN,
          "VersionedPmUnitConfig for PmUnit '{}' sets neither productSubVersion "
          "nor pmUnitVersions",
          pmUnitName);
      versionInfo = fmt::format("default{}v?", kArrow);
    }

    comparePmUnitConfigs(
        defaultConfig,
        *versionedConfig.pmUnitConfig(),
        pmUnitName,
        versionInfo);
  }
}

void ConfigDiffer::printDifferences() {
  if (diffs_.empty()) {
    std::cout << "No differences found.\n";
    return;
  }

  // Group diffs by (pmUnitName, versionInfo)
  std::map<
      std::pair<std::string, std::string>,
      std::vector<const ConfigDifference*>>
      grouped;
  for (const auto& diff : diffs_) {
    grouped[{diff.pmUnitName, diff.versionInfo}].push_back(&diff);
  }

  // Format each section
  for (const auto& [key, section_diffs] : grouped) {
    const auto& [pmUnitName, versionInfo] = key;

    std::cout << "\n[" << pmUnitName << " (" << versionInfo << ")]\n";

    // Group by device name
    std::map<std::string, std::vector<const ConfigDifference*>> byDevice;
    for (const auto* diff : section_diffs) {
      byDevice[diff->deviceName].push_back(diff);
    }

    // Format each device
    for (const auto& [deviceName, device_diffs] : byDevice) {
      std::cout << "  " << deviceName << ":";

      bool first = true;
      for (const auto* diff : device_diffs) {
        if (!first) {
          std::cout << ",";
        }
        first = false;

        std::cout << " " << diff->fieldName << " (";
        if (diff->oldValue) {
          std::cout << *diff->oldValue;
        }
        if (diff->oldValue && diff->newValue) {
          std::cout << kArrow;
        }
        if (diff->newValue) {
          std::cout << *diff->newValue;
        }
        std::cout << ")";
      }

      std::cout << "\n";
    }
  }

  std::cout << "\n";
}

const std::vector<ConfigDifference>& ConfigDiffer::getDiffs() const {
  return diffs_;
}

} // namespace facebook::fboss::platform::platform_manager
