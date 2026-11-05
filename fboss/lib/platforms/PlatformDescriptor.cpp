/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/platforms/PlatformDescriptor.h"

#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <utility>

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "fboss/agent/FbossError.h"
#include "fboss/agent/gen-cpp2/platform_config_types.h"
#include "fboss/lib/platforms/PlatformMappingUtils.h"

DEFINE_string(
    platform_descriptor_config_path,
    "",
    "Path to a platform descriptor config root. Directories containing "
    "platform_descriptor.json are discovered recursively and must also contain "
    "platform_mapping.json.");

// Defined beside the registry rather than in AgentFeatures so every registry
// user registers it (qsfp_service does not link the agent flag library).
DEFINE_bool(
    hwrev_state1_substate1_respin0,
    false,
    "Chassis hardware revision: production state 1, sub-state 1, re-spin 0");

namespace fs = std::filesystem;

namespace facebook::fboss {
namespace {

constexpr auto kPlatformDescriptorFileName = "platform_descriptor.json";
constexpr auto kPlatformMappingFileName = "platform_mapping.json";
constexpr auto kRawPlatformMappingFileName = "raw_platform_mapping.json";

std::string normalize(std::string_view value) {
  return boost::algorithm::to_lower_copy(std::string(value));
}

bool startsWithNormalized(
    std::string_view candidate,
    const std::string& normalizedPrefix) {
  auto normalizedCandidate = normalize(candidate);
  return normalizedCandidate.rfind(normalizedPrefix, 0) == 0;
}

bool equalsNormalized(
    std::string_view candidate,
    const std::string& normalizedExpected) {
  return normalize(candidate) == normalizedExpected;
}

std::optional<bool> getBoolFlagValue(
    std::string_view flagName,
    const PlatformDescriptor& descriptor) {
  gflags::CommandLineFlagInfo flagInfo;
  auto flagNameStr = std::string{flagName};
  if (!gflags::GetCommandLineFlagInfo(flagNameStr.c_str(), &flagInfo) ||
      flagInfo.type != "bool") {
    XLOG(ERR) << "Platform descriptor for platform type "
              << static_cast<int>(*descriptor.platformType())
              << " references unknown or non-bool variant attribute gflag "
              << flagName;
    return std::nullopt;
  }
  return flagInfo.current_value == "true";
}

bool matchesVariantAttributes(const PlatformDescriptor& descriptor) {
  const auto& variantAttributes = descriptor.variantAttributes().value();
  return std::all_of(
      variantAttributes.begin(),
      variantAttributes.end(),
      [&descriptor](const auto& item) {
        auto flagValue = getBoolFlagValue(item.first, descriptor);
        return flagValue.has_value() && *flagValue == item.second;
      });
}

void validatePlatformDescriptor(
    const PlatformDescriptor& descriptor,
    const std::string& path) {
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          descriptor.platformType())) {
    throw FbossError("Platform descriptor ", path, " is missing platformType");
  }
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          descriptor.productNamePrefixes())) {
    throw FbossError(
        "Platform descriptor ", path, " is missing productNamePrefixes");
  }
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          descriptor.modeNames())) {
    throw FbossError("Platform descriptor ", path, " is missing modeNames");
  }
  if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          descriptor.asicType())) {
    throw FbossError("Platform descriptor ", path, " is missing asicType");
  }
}

bool matchesSystemVendor(
    const fs::path& descriptorDir,
    const fs::path& platformDir,
    const std::string& normalizedVendor) {
  if (normalizedVendor.empty()) {
    return true;
  }

  auto relativePath = fs::relative(platformDir, descriptorDir);
  auto component = relativePath.begin();
  if (component != relativePath.end() &&
      normalize(component->string()) == "platforms") {
    ++component;
  }
  return component != relativePath.end() &&
      normalize(component->string()) == normalizedVendor;
}

std::vector<fs::path> getPlatformDirs(
    const fs::path& descriptorDir,
    const std::string& path,
    std::string_view systemVendor) {
  std::vector<fs::path> platformDirs;
  auto normalizedVendor = normalize(systemVendor);

  for (const auto& entry : fs::recursive_directory_iterator(descriptorDir)) {
    if (entry.is_regular_file() &&
        entry.path().filename() == kPlatformDescriptorFileName) {
      auto platformDir = entry.path().parent_path();
      if (matchesSystemVendor(descriptorDir, platformDir, normalizedVendor)) {
        platformDirs.push_back(std::move(platformDir));
      }
    }
  }
  std::sort(platformDirs.begin(), platformDirs.end());

  if (platformDirs.empty()) {
    if (!normalizedVendor.empty()) {
      return platformDirs;
    }
    throw FbossError(
        "Platform descriptor directory ",
        path,
        " does not contain any platform descriptors");
  }
  return platformDirs;
}

fs::path getRequiredPlatformFile(
    const fs::path& platformDir,
    std::string_view fileName) {
  auto file = platformDir / fileName;
  if (!fs::exists(file)) {
    throw FbossError(
        "Platform directory ", platformDir.string(), " is missing ", fileName);
  }
  return file;
}

} // namespace

PlatformDescriptorRegistry::PlatformDescriptorRegistry(
    std::vector<PlatformDescriptorEntry> descriptorEntries)
    : descriptorEntries_(std::move(descriptorEntries)) {}

const PlatformDescriptorRegistry& PlatformDescriptorRegistry::get() {
  static PlatformDescriptorRegistry instance(
      FLAGS_platform_descriptor_config_path.empty()
          ? std::vector<PlatformDescriptorEntry>{}
          : loadPlatformDescriptorEntriesFromDirectory(
                FLAGS_platform_descriptor_config_path));
  return instance;
}

const PlatformDescriptor* FOLLY_NULLABLE
PlatformDescriptorRegistry::getDescriptor(PlatformType type) const {
  auto entry = getDescriptorEntry(type);
  return entry ? &entry->descriptor : nullptr;
}

const PlatformDescriptorRegistry::PlatformDescriptorEntry* FOLLY_NULLABLE
PlatformDescriptorRegistry::getDescriptorEntry(PlatformType type) const {
  const PlatformDescriptorEntry* defaultEntry = nullptr;
  for (const auto& entry : descriptorEntries_) {
    if (entry.descriptor.platformType().value() != type) {
      continue;
    }
    const auto& variantAttributes =
        entry.descriptor.variantAttributes().value();
    if (!variantAttributes.empty()) {
      if (matchesVariantAttributes(entry.descriptor)) {
        return &entry;
      }
      continue;
    }
    if (!defaultEntry) {
      defaultEntry = &entry;
    }
  }
  return defaultEntry;
}

std::optional<PlatformType> PlatformDescriptorRegistry::findPlatformType(
    std::string_view productName,
    std::string_view mode) const {
  if (!mode.empty()) {
    for (const auto& entry : descriptorEntries_) {
      for (const auto& modeName : entry.descriptor.modeNames().value()) {
        if (equalsNormalized(mode, normalize(modeName))) {
          const auto platformType = entry.descriptor.platformType().value();
          XLOG(DBG2) << "Using platform descriptor file "
                     << fs::path(entry.platformMappingPath)
                            .replace_filename(kPlatformDescriptorFileName)
                            .string()
                     << " for mode '" << mode << "' (platform type "
                     << apache::thrift::util::enumNameSafe(platformType) << ")";
          return platformType;
        }
      }
    }
    return std::nullopt;
  }

  std::optional<PlatformType> bestMatch;
  std::optional<PlatformType> conflictingBestMatch;
  std::string bestMatchPrefix;
  const PlatformDescriptorEntry* bestMatchEntry{nullptr};
  size_t bestMatchLength = 0;
  for (const auto& entry : descriptorEntries_) {
    for (const auto& productNamePrefix :
         entry.descriptor.productNamePrefixes().value()) {
      const auto normalizedPrefix = normalize(productNamePrefix);
      if (!startsWithNormalized(productName, normalizedPrefix)) {
        continue;
      }
      const auto platformType = entry.descriptor.platformType().value();
      if (!bestMatch || normalizedPrefix.size() > bestMatchLength) {
        bestMatch = platformType;
        conflictingBestMatch.reset();
        bestMatchPrefix = productNamePrefix;
        bestMatchEntry = &entry;
        bestMatchLength = normalizedPrefix.size();
      } else if (
          normalizedPrefix.size() == bestMatchLength &&
          *bestMatch != platformType) {
        conflictingBestMatch = platformType;
      }
    }
  }
  if (conflictingBestMatch) {
    throw FbossError(
        "Product name '",
        productName,
        "' ambiguously matches platform types ",
        static_cast<int>(*bestMatch),
        " and ",
        static_cast<int>(*conflictingBestMatch));
  }
  if (bestMatch) {
    XLOG(DBG2) << "Using platform descriptor file "
               << fs::path(bestMatchEntry->platformMappingPath)
                      .replace_filename(kPlatformDescriptorFileName)
                      .string()
               << " for product name '" << productName << "' using prefix '"
               << bestMatchPrefix << "' (platform type "
               << apache::thrift::util::enumNameSafe(*bestMatch) << ")";
  }
  return bestMatch;
}

std::optional<std::string> PlatformDescriptorRegistry::loadPlatformMapping(
    PlatformType type) const {
  auto entry = getDescriptorEntry(type);
  if (!entry) {
    return std::nullopt;
  }
  if (entry->platformMappingPath.empty()) {
    return std::nullopt;
  }

  std::string mappingJson;
  if (!folly::readFile(entry->platformMappingPath.c_str(), mappingJson)) {
    throw FbossError(
        "Unable to read platform mapping file ", entry->platformMappingPath);
  }

  try {
    cfg::PlatformMapping mapping;
    apache::thrift::SimpleJSONSerializer::deserialize<cfg::PlatformMapping>(
        mappingJson, mapping);
  } catch (const std::exception& ex) {
    throw FbossError(
        "Unable to parse platform mapping file ",
        entry->platformMappingPath,
        ": ",
        ex.what());
  }
  return mappingJson;
}

cfg::PlatformMapping PlatformDescriptorRegistry::loadPlatformMappingFromRaw(
    PlatformType type,
    const cfg::PlatformConfig& platformConfig) const {
  if (FLAGS_platform_descriptor_config_path.empty()) {
    throw FbossError(
        "--platform_descriptor_config_path must be set when --use_raw_platform_mapping is enabled");
  }
  const auto& portIdToPortAssignment = platformConfig.portIdToPortAssignment();
  if (!portIdToPortAssignment.has_value()) {
    throw FbossError(
        "portIdToPortAssignment must be set when loading a raw platform mapping");
  }
  if (portIdToPortAssignment->empty()) {
    throw FbossError(
        "portIdToPortAssignment must not be empty when loading a raw platform mapping");
  }
  const auto entry = getDescriptorEntry(type);
  if (!entry || entry->rawPlatformMappingPath.empty()) {
    throw FbossError(
        "Selected platform descriptor is missing sibling raw_platform_mapping.json");
  }

  return reconstructPlatformMapping(
      readRawPlatformMapping(
          resolveRawPlatformMappingPath(entry->rawPlatformMappingPath)),
      *portIdToPortAssignment);
}

PlatformDescriptor PlatformDescriptorRegistry::loadPlatformDescriptorFromFile(
    const std::string& path) {
  if (!fs::exists(path)) {
    throw FbossError("Unable to find platform descriptor file ", path);
  }

  std::string descriptorJson;
  if (!folly::readFile(path.c_str(), descriptorJson)) {
    throw FbossError("Unable to read platform descriptor file ", path);
  }

  PlatformDescriptor descriptor;
  try {
    apache::thrift::SimpleJSONSerializer::deserialize<PlatformDescriptor>(
        descriptorJson, descriptor);
  } catch (const std::exception& ex) {
    throw FbossError(
        "Unable to parse platform descriptor file ", path, ": ", ex.what());
  }
  validatePlatformDescriptor(descriptor, path);
  return descriptor;
}

PlatformDescriptorRegistry
PlatformDescriptorRegistry::loadPlatformDescriptorRegistryFromDirectory(
    const std::string& path,
    std::string_view systemVendor) {
  return PlatformDescriptorRegistry(
      loadPlatformDescriptorEntriesFromDirectory(path, systemVendor));
}

std::vector<PlatformDescriptorRegistry::PlatformDescriptorEntry>
PlatformDescriptorRegistry::loadPlatformDescriptorEntriesFromDirectory(
    const std::string& path,
    std::string_view systemVendor) {
  const fs::path descriptorDir(path);
  if (!fs::is_directory(descriptorDir)) {
    throw FbossError(
        "Platform descriptor config path ", path, " is not a directory");
  }

  auto platformDirs = getPlatformDirs(descriptorDir, path, systemVendor);
  std::vector<PlatformDescriptorEntry> descriptorEntries;
  descriptorEntries.reserve(platformDirs.size());
  for (const auto& platformDir : platformDirs) {
    auto descriptorFile =
        getRequiredPlatformFile(platformDir, kPlatformDescriptorFileName);
    auto mappingFile =
        getRequiredPlatformFile(platformDir, kPlatformMappingFileName);
    auto rawMappingFile = platformDir / kRawPlatformMappingFileName;

    descriptorEntries.push_back(
        PlatformDescriptorEntry{
            loadPlatformDescriptorFromFile(descriptorFile.string()),
            mappingFile.string(),
            rawMappingFile.string()});
  }
  return descriptorEntries;
}

} // namespace facebook::fboss
