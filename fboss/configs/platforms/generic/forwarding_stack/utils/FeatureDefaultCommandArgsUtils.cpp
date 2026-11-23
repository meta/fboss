/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/configs/platforms/generic/forwarding_stack/utils/FeatureDefaultCommandArgsUtils.h"

#include <algorithm>
#include <optional>
#include <string>

#include <folly/FileUtil.h>

#include "fboss/agent/FbossError.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/ThriftConfigUtils.h"

namespace facebook::fboss::configgen {
namespace fs = std::filesystem;
namespace {

fs::path getConfigPath(ServiceType serviceType) {
  constexpr std::string_view kBasePath =
      "configs/platforms/generic/forwarding_stack";
  constexpr std::string_view kJsonFile =
      "feature_default_command_args_config.json";
  switch (serviceType) {
    case ServiceType::AGENT:
      return fs::path(kBasePath) / "agent" / kJsonFile;
  }
  throw FbossError("Unsupported service type");
}

// The remaining validation helpers enforce semantic constraints that the
// Thrift type system cannot express, such as nonempty condition sets,
// recognized ASIC names, and conflict-free argument composition.
void validateConditionValues(
    const FeatureConditionValues& values,
    std::string_view context) {
  if (values.included()->empty() && values.excluded()->empty()) {
    throw FbossError(context, " must contain at least one value");
  }
  const auto validateNonEmpty = [context](const auto& entries) {
    for (const auto& entry : entries) {
      if (entry.empty()) {
        throw FbossError(context, " must not contain an empty value");
      }
    }
  };
  validateNonEmpty(*values.included());
  validateNonEmpty(*values.excluded());
  for (const auto& value : *values.included()) {
    if (values.excluded()->contains(value)) {
      throw FbossError(
          context, " contains '", value, "' in both included and excluded");
    }
  }
}

template <typename Enum>
void validateEnumNames(
    const FeatureConditionValues& values,
    std::string_view typeName) {
  const auto validate = [typeName](const auto& names) {
    for (const auto& name : names) {
      parseThriftEnumName<Enum>(name, typeName);
    }
  };
  validate(*values.included());
  validate(*values.excluded());
}

void validateEnableConditions(
    const FeatureEnableConditions& conditions,
    std::string_view featureName) {
  if (!conditions.configProfiles() && !conditions.asicTypes() &&
      !conditions.platforms()) {
    throw FbossError(
        "Feature '",
        featureName,
        "' has autoEnableWhen entry without any conditions");
  }
  if (conditions.configProfiles()) {
    validateConditionValues(
        *conditions.configProfiles(), "configProfiles condition");
  }
  if (conditions.asicTypes()) {
    validateConditionValues(*conditions.asicTypes(), "asicTypes condition");
    validateEnumNames<cfg::AsicType>(*conditions.asicTypes(), "ASIC type");
  }
  if (conditions.platforms()) {
    validateConditionValues(*conditions.platforms(), "platforms condition");
    validateEnumNames<PlatformType>(*conditions.platforms(), "platform type");
  }
}

void validateConfig(const FeatureDefaultCommandArgsConfig& config) {
  for (const auto& profileAndArgs : *config.profileDefaultArgs()) {
    if (profileAndArgs.first.empty()) {
      throw FbossError("Profile names must not be empty");
    }
    for (const auto& argument : profileAndArgs.second) {
      if (argument.first.empty()) {
        throw FbossError("Command-line argument names must not be empty");
      }
    }
  }
  for (const auto& [featureName, feature] : *config.features()) {
    if (featureName.empty()) {
      throw FbossError("Feature names must not be empty");
    }
    if (feature.args()->empty()) {
      throw FbossError("Feature '", featureName, "' has no arguments");
    }
    for (const auto& argument : *feature.args()) {
      if (argument.first.empty()) {
        throw FbossError(
            "Feature '", featureName, "' has an empty argument name");
      }
    }
    if (!feature.autoEnableWhen()) {
      continue;
    }
    const auto& conditionAlternatives = *feature.autoEnableWhen();
    if (conditionAlternatives.empty()) {
      throw FbossError("Feature '", featureName, "' has empty autoEnableWhen");
    }
    for (const auto& conditions : conditionAlternatives) {
      validateEnableConditions(conditions, featureName);
    }
  }
}

bool matches(
    const std::optional<FeatureConditionValues>& condition,
    std::string_view value) {
  if (!condition) {
    return true;
  }
  const auto stringValue = std::string(value);
  if (condition->excluded()->contains(stringValue)) {
    return false;
  }
  return condition->included()->empty() ||
      condition->included()->contains(stringValue);
}

template <typename Enum>
bool matchesEnum(
    const std::optional<FeatureConditionValues>& condition,
    Enum value) {
  if (!condition) {
    return true;
  }
  const auto matchesValue = [value](const auto& names) {
    return std::any_of(names.begin(), names.end(), [value](const auto& name) {
      return thriftEnumNameMatches(name, value);
    });
  };
  if (matchesValue(*condition->excluded())) {
    return false;
  }
  return condition->included()->empty() || matchesValue(*condition->included());
}

bool matches(
    const std::optional<FeatureConditionValues>& condition,
    std::optional<cfg::AsicType> asicType) {
  if (!condition) {
    return true;
  }
  return asicType && matchesEnum(condition, *asicType);
}

bool matchesAllConditions(
    const FeatureEnableConditions& conditions,
    std::string_view profile,
    std::optional<cfg::AsicType> asicType,
    PlatformType platformType) {
  return matches(conditions.configProfiles().to_optional(), profile) &&
      matches(conditions.asicTypes().to_optional(), asicType) &&
      matchesEnum(conditions.platforms().to_optional(), platformType);
}

void mergeArgs(
    std::map<std::string, std::string>& resolved,
    const std::map<std::string, std::string>& additions,
    std::string_view source) {
  for (const auto& [name, value] : additions) {
    const auto [it, inserted] = resolved.emplace(name, value);
    if (!inserted && it->second != value) {
      throw FbossError(
          "Command-line argument '",
          name,
          "' has conflicting values '",
          it->second,
          "' and '",
          value,
          "' from ",
          source);
    }
  }
}

} // namespace

FeatureDefaultCommandArgsConfig parseFeatureDefaultCommandArgsConfig(
    std::string_view contents) {
  auto config =
      parseStrictSimpleJson<FeatureDefaultCommandArgsConfig>(contents);
  validateConfig(config);
  return config;
}

FeatureDefaultCommandArgsConfig loadFeatureDefaultCommandArgsConfig(
    const fs::path& fbossRoot,
    ServiceType serviceType) {
  const auto path = fbossRoot / getConfigPath(serviceType);
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw FbossError(
        "Unable to read feature default command arguments from ",
        path.string());
  }

  try {
    return parseFeatureDefaultCommandArgsConfig(contents);
  } catch (const FbossError&) {
    throw;
  } catch (const std::exception& error) {
    throw FbossError(
        "Unable to parse feature default command arguments from ",
        path.string(),
        ": ",
        error.what());
  }
}

std::map<std::string, std::string> resolveFeatureDefaultCommandArgs(
    const FeatureDefaultCommandArgsConfig& config,
    std::string_view profile,
    std::optional<cfg::AsicType> asicType,
    PlatformType platformType) {
  // Callers may construct the Thrift object directly instead of using the JSON
  // parser, so semantic validation also belongs at this public entry point.
  validateConfig(config);
  std::map<std::string, std::string> resolved;
  const auto profileName = std::string(profile);
  if (const auto profileIt = config.profileDefaultArgs()->find(profileName);
      profileIt != config.profileDefaultArgs()->end()) {
    mergeArgs(resolved, profileIt->second, "profile defaults");
  }

  for (const auto& [featureName, feature] : *config.features()) {
    if (!feature.autoEnableWhen()) {
      continue;
    }
    const auto& conditionAlternatives = *feature.autoEnableWhen();
    if (std::any_of(
            conditionAlternatives.begin(),
            conditionAlternatives.end(),
            [&](const auto& conditions) {
              return matchesAllConditions(
                  conditions, profile, asicType, platformType);
            })) {
      mergeArgs(resolved, *feature.args(), featureName);
    }
  }
  return resolved;
}

std::map<std::string, std::string> generateFeatureDefaultCommandArgs(
    const fs::path& fbossRoot,
    ServiceType serviceType,
    std::string_view profile,
    std::optional<cfg::AsicType> asicType,
    PlatformType platformType) {
  return resolveFeatureDefaultCommandArgs(
      loadFeatureDefaultCommandArgsConfig(fbossRoot, serviceType),
      profile,
      asicType,
      platformType);
}

} // namespace facebook::fboss::configgen
