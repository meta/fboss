/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/ConfigGenerationUtils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include <folly/FileUtil.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/commands/config/gen/PlatformConfigPathUtils.h"
#include "fboss/configs/platforms/generic/forwarding_stack/utils/ConfigGenerationSpecUtils.h"

namespace facebook::fboss::configgen {
namespace fs = std::filesystem;
namespace {

const std::map<std::string_view, ConfigProfileType> kConfigProfiles{
    {"default", ConfigProfileType::DEFAULT},
    {"hw_test", ConfigProfileType::HW_TEST},
    {"benchmark", ConfigProfileType::BENCHMARK},
    {"link_test", ConfigProfileType::LINK_TEST},
};

} // namespace

ConfigProfileType normalizeProfile(std::string_view profile) {
  if (const auto profileIt = kConfigProfiles.find(profile);
      profileIt != kConfigProfiles.end()) {
    return profileIt->second;
  }
  throw FbossError("Unsupported config generation profile: ", profile);
}

std::string getProfileName(ConfigProfileType profile) {
  const auto profileIt = std::find_if(
      kConfigProfiles.begin(),
      kConfigProfiles.end(),
      [profile](const auto& entry) { return entry.second == profile; });
  if (profileIt != kConfigProfiles.end()) {
    return std::string(profileIt->first);
  }
  throw FbossError(
      "Unsupported config generation profile: ",
      apache::thrift::util::enumNameSafe(profile));
}

namespace {

struct PendingConfig {
  fs::path outputPath;
  std::string contents;
};

struct ConfigGenerationPlan {
  std::string serviceName;
  std::optional<PlatformType> platformTypeScope;
  std::optional<fs::path> platformDirectoryScope;
  std::vector<PendingConfig> configs;
  std::set<fs::path> outputPaths;
};

std::string getServiceName(ServiceType service) {
  switch (service) {
    case ServiceType::AGENT:
      return "agent";
  }
  throw FbossError(
      "Unsupported config generation service: ",
      apache::thrift::util::enumNameSafe(service));
}

void validatePathComponent(
    const std::optional<std::string>& value,
    std::string_view field) {
  if (!value) {
    return;
  }
  const fs::path component{*value};
  if (component.empty() || component.has_parent_path() || component == "." ||
      component == "..") {
    throw FbossError("Invalid config generation ", field, ": ", *value);
  }
}

std::string getOutputFileName(
    std::string_view profile,
    const std::optional<std::string>& variant) {
  auto fileName = std::string(profile);
  if (variant) {
    fileName += "_" + *variant;
  }
  fileName += ".conf";
  return fileName;
}

ConfigGenerationPlan createConfigGenerationPlan(
    const fs::path& fbossRoot,
    ServiceType service,
    const ConfigContentsGenerator& generateContents,
    std::optional<std::string_view> selectedPlatform) {
  std::vector<ConfigGenerationTarget> targets;
  std::optional<PlatformType> platformTypeScope;
  std::optional<fs::path> platformDirectoryScope;
  if (selectedPlatform) {
    platformDirectoryScope =
        findPlatformConfigDirectory(fbossRoot, *selectedPlatform).path;
    auto target = loadConfigGenerationTarget(*platformDirectoryScope);
    platformTypeScope = target.platform;
    targets.push_back(std::move(target));
  } else {
    targets = loadConfigGenerationTargets(fbossRoot);
  }
  ConfigGenerationPlan plan{
      .serviceName = getServiceName(service),
      .platformTypeScope = platformTypeScope,
      .platformDirectoryScope = platformDirectoryScope};

  for (const auto& target : targets) {
    const auto optionsIt = target.spec.serviceToOptions()->find(service);
    if (optionsIt == target.spec.serviceToOptions()->end()) {
      continue;
    }

    const auto platformType = target.platform;
    const auto platform = getPlatformName(platformType);
    const auto& platformDirectory = target.platformDirectory;
    for (const auto& option : optionsIt->second) {
      const auto profileType = *option.profile();
      const auto profile = getProfileName(profileType);
      std::optional<std::string> variant;
      if (option.variant()) {
        variant = *option.variant();
      }
      validatePathComponent(variant, "variant");
      std::optional<std::string> asicConfigVariant;
      if (option.asicConfigVariant()) {
        asicConfigVariant = *option.asicConfigVariant();
      }
      validatePathComponent(asicConfigVariant, "asicConfigVariant");
      std::optional<std::string> linkTopologyVariant;
      if (option.linkTopologyVariant()) {
        linkTopologyVariant = *option.linkTopologyVariant();
      }
      validatePathComponent(linkTopologyVariant, "linkTopologyVariant");

      auto outputPath = platformDirectory / "forwarding_stack" / "generated" /
          plan.serviceName / getOutputFileName(profile, variant);
      if (!plan.outputPaths.insert(outputPath).second) {
        throw FbossError(
            "Config generation declarations contain duplicate output ",
            outputPath.string());
      }

      ConfigGenerationRequest request{
          .fbossRoot = fbossRoot,
          .platformType = platformType,
          .platform = platform,
          .profileType = profileType,
          .profile = profile,
          .variant = std::move(variant),
          .asicConfigVariant = std::move(asicConfigVariant),
          .linkTopologyVariant = std::move(linkTopologyVariant),
          .service = service,
          .serviceName = plan.serviceName,
          .platformDirectory = platformDirectory,
          .outputPath = outputPath,
      };
      plan.configs.push_back(
          {.outputPath = std::move(outputPath),
           .contents = generateContents(request)});
    }
  }

  if (plan.configs.empty()) {
    throw FbossError(
        "Config generation declarations have no ",
        plan.serviceName,
        " targets");
  }
  return plan;
}

std::set<fs::path> findGeneratedConfigPaths(
    const fs::path& fbossRoot,
    std::string_view serviceName,
    const std::optional<fs::path>& platformDirectoryScope) {
  const auto addPlatformOutputs = [&](const fs::path& platformDirectory,
                                      std::set<fs::path>& paths) {
    const auto serviceDirectory =
        platformDirectory / "forwarding_stack" / "generated" / serviceName;
    if (!fs::is_directory(serviceDirectory)) {
      return;
    }
    for (const auto& outputEntry : fs::directory_iterator(serviceDirectory)) {
      if (!outputEntry.is_regular_file()) {
        throw FbossError(
            "Unexpected entry in generated config directory: ",
            outputEntry.path().string());
      }
      paths.insert(outputEntry.path());
    }
  };

  std::set<fs::path> paths;
  if (platformDirectoryScope) {
    addPlatformOutputs(*platformDirectoryScope, paths);
    return paths;
  }

  const auto platformsDirectory = fbossRoot / "configs" / "platforms";
  if (!fs::is_directory(platformsDirectory)) {
    throw FbossError(
        "Platform config directory does not exist: ",
        platformsDirectory.string());
  }

  for (const auto& vendorEntry : fs::directory_iterator(platformsDirectory)) {
    if (!vendorEntry.is_directory() ||
        vendorEntry.path().filename() == "generic") {
      continue;
    }
    for (const auto& platformEntry :
         fs::directory_iterator(vendorEntry.path())) {
      if (!platformEntry.is_directory()) {
        continue;
      }
      addPlatformOutputs(platformEntry.path(), paths);
    }
  }
  return paths;
}

std::string readGeneratedConfig(const fs::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw FbossError("Unable to read generated config ", path.string());
  }
  return contents;
}

} // namespace

std::vector<fs::path> generateConfigsFromManifest(
    const fs::path& fbossRoot,
    ServiceType service,
    const ConfigContentsGenerator& generateContents,
    std::optional<std::string_view> platform) {
  auto plan = createConfigGenerationPlan(
      fbossRoot, service, generateContents, platform);
  const auto existingPaths = findGeneratedConfigPaths(
      fbossRoot, plan.serviceName, plan.platformDirectoryScope);

  // Do not create or update outputs until every target has generated
  // successfully. This prevents a bad declaration from leaving a partial
  // regeneration behind.
  for (const auto& pendingConfig : plan.configs) {
    fs::create_directories(pendingConfig.outputPath.parent_path());
  }
  for (const auto& pendingConfig : plan.configs) {
    folly::writeFileAtomic(
        pendingConfig.outputPath.c_str(), pendingConfig.contents, 0644);
  }
  for (const auto& existingPath : existingPaths) {
    if (!plan.outputPaths.contains(existingPath)) {
      fs::remove(existingPath);
    }
  }

  std::vector<fs::path> generatedPaths;
  generatedPaths.reserve(plan.configs.size());
  for (auto& pendingConfig : plan.configs) {
    generatedPaths.push_back(std::move(pendingConfig.outputPath));
  }
  return generatedPaths;
}

ConfigVerificationResult verifyConfigsFromManifest(
    const fs::path& fbossRoot,
    ServiceType service,
    const ConfigContentsGenerator& generateContents,
    std::optional<std::string_view> platform) {
  const auto plan = createConfigGenerationPlan(
      fbossRoot, service, generateContents, platform);
  const auto existingPaths = findGeneratedConfigPaths(
      fbossRoot, plan.serviceName, plan.platformDirectoryScope);
  ConfigVerificationResult result{.platform = plan.platformTypeScope};

  for (const auto& expected : plan.configs) {
    if (!existingPaths.contains(expected.outputPath)) {
      result.missing.push_back(expected.outputPath);
    } else if (readGeneratedConfig(expected.outputPath) != expected.contents) {
      result.stale.push_back(expected.outputPath);
    }
  }
  for (const auto& existingPath : existingPaths) {
    if (!plan.outputPaths.contains(existingPath)) {
      result.obsolete.push_back(existingPath);
    }
  }
  return result;
}

} // namespace facebook::fboss::configgen
