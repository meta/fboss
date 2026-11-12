/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/agent/CmdConfigGenAgent.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <folly/FileUtil.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/CmdHandler.cpp"
#include "fboss/cli/fboss2/commands/config/gen/ConfigGenerationUtils.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigComparisonUtils.h"
#include "fboss/cli/fboss2/commands/config/gen/agent/AgentConfigGenUtils.h"

namespace facebook::fboss {
namespace {

std::string readFile(const std::filesystem::path& path) {
  std::string contents;
  if (!folly::readFile(path.c_str(), contents)) {
    throw FbossError("Unable to read reference Agent config ", path.string());
  }
  return contents;
}

std::string formatComparisonResult(
    const std::filesystem::path& generatedPath,
    const std::filesystem::path& referencePath,
    const configgen::AgentConfigComparisonResult& comparison) {
  std::string output =
      "Generated Agent configuration at " + generatedPath.string() +
      "\nReference: " + referencePath.string() + "\nResult: " +
      std::string(
          configgen::getAgentConfigComparisonStatusName(comparison.status));
  if (!comparison.ignoredPaths.empty()) {
    output += "\nIgnored paths:";
    for (const auto& path : comparison.ignoredPaths) {
      output += "\n  " + path;
    }
  }
  if (!comparison.semanticDifferences.empty()) {
    output += "\nSemantic differences:";
    for (const auto& difference : comparison.semanticDifferences) {
      output += "\n  " + difference;
    }
  }
  if (!comparison.differingPaths.empty()) {
    output += "\nDiffering paths:";
    for (const auto& path : comparison.differingPaths) {
      output += "\n  " + path;
    }
  }
  return output;
}

void rejectBatchModeOptions(
    const std::vector<std::pair<std::string_view, std::string_view>>& options,
    std::string_view requiredOption) {
  std::string invalidOptions;
  for (const auto& [name, value] : options) {
    if (value.empty()) {
      continue;
    }
    if (!invalidOptions.empty()) {
      invalidOptions += ", ";
    }
    invalidOptions += name;
  }
  if (!invalidOptions.empty()) {
    throw FbossError(
        "The following options require ", requiredOption, ": ", invalidOptions);
  }
}

std::string formatGeneratedPaths(
    const std::vector<std::filesystem::path>& outputPaths) {
  std::string output = "Generated Agent configurations:";
  for (const auto& outputPath : outputPaths) {
    output += "\n  " + outputPath.string();
  }
  return output;
}

void appendPaths(
    std::string& output,
    std::string_view label,
    const std::vector<std::filesystem::path>& paths) {
  if (paths.empty()) {
    return;
  }
  output += "\n";
  output += label;
  for (const auto& path : paths) {
    output += "\n  " + path.string();
  }
}

std::string formatVerificationFailure(
    const configgen::ConfigVerificationResult& result,
    const std::filesystem::path& fbossRoot,
    std::optional<std::string_view> platform) {
  std::string output = "Agent generated configurations";
  if (result.platform) {
    output += " for " +
        std::string(apache::thrift::util::enumNameSafe(*result.platform));
  }
  output += " are out of date:";
  appendPaths(output, "Missing:", result.missing);
  appendPaths(output, "Stale:", result.stale);
  appendPaths(output, "Obsolete:", result.obsolete);
  output += "\nRegenerate with a freshly built fboss2 from this checkout:\n  ";
  output += kConfigGenerationCommand;
  output += fbossRoot.string();
  if (platform) {
    output += " --platform " + std::string(*platform);
  }
  return output;
}

} // namespace

CmdConfigGenAgent::RetType CmdConfigGenAgent::queryClient(
    const HostInfo& /* hostInfo */) {
  auto options = CmdLocalOptions::getInstance();
  auto platform = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentPlatform);
  auto profile = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentProfile);
  auto fbossRoot = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentFbossRoot);
  auto asicConfigVariant = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentAsicConfigVariant);
  auto asicConfigFile = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentAsicConfigFile);
  auto asicConfigType = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentAsicConfigType);
  auto referenceConfigFile = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentReferenceConfigFile);
  auto outputDirectory = options->getLocalOption(
      std::string(kConfigGenAgentCommand), kConfigGenAgentOutputDirectory);
  const auto verify =
      options->getLocalOption(
          std::string(kConfigGenAgentCommand), kConfigGenAgentVerify) == "true";

  if (platform.empty() || profile.empty()) {
    // Platform declarations own profile selection and canonical output paths.
    // Explicit profile generation retains the single-output override options.
    if (platform.empty()) {
      rejectBatchModeOptions(
          {{kConfigGenAgentProfile, profile},
           {kConfigGenAgentAsicConfigVariant, asicConfigVariant},
           {kConfigGenAgentAsicConfigFile, asicConfigFile},
           {kConfigGenAgentAsicConfigType, asicConfigType},
           {kConfigGenAgentReferenceConfigFile, referenceConfigFile},
           {kConfigGenAgentOutputDirectory, outputDirectory}},
          kConfigGenAgentPlatform);
    } else {
      rejectBatchModeOptions(
          {{kConfigGenAgentAsicConfigVariant, asicConfigVariant},
           {kConfigGenAgentAsicConfigFile, asicConfigFile},
           {kConfigGenAgentAsicConfigType, asicConfigType},
           {kConfigGenAgentReferenceConfigFile, referenceConfigFile},
           {kConfigGenAgentOutputDirectory, outputDirectory}},
          kConfigGenAgentProfile);
    }
    const auto selectedPlatform = platform.empty()
        ? std::nullopt
        : std::optional<std::string_view>(platform);
    if (verify) {
      const auto result = configgen::verifyConfigsFromManifest(
          fbossRoot,
          configgen::ServiceType::AGENT,
          configgen::generateAgentConfigForManifest,
          selectedPlatform);
      if (!result.isFresh()) {
        throw FbossError(
            formatVerificationFailure(result, fbossRoot, selectedPlatform));
      }
      return "Agent generated configurations are up to date.";
    }
    return formatGeneratedPaths(
        configgen::generateConfigsFromManifest(
            fbossRoot,
            configgen::ServiceType::AGENT,
            configgen::generateAgentConfigForManifest,
            selectedPlatform));
  }

  if (verify) {
    throw FbossError("--verify cannot be combined with --profile");
  }
  if (!asicConfigVariant.empty() && !asicConfigFile.empty()) {
    throw FbossError(
        "--asic-config-variant cannot be combined with --asic-config-file");
  }

  std::optional<std::filesystem::path> asicConfigFilePath;
  if (!asicConfigFile.empty()) {
    asicConfigFilePath = asicConfigFile;
  }
  std::optional<cfg::AsicConfigType> parsedAsicConfigType;
  if (!asicConfigType.empty()) {
    parsedAsicConfigType = configgen::parseAsicConfigType(asicConfigType);
  }
  std::optional<std::filesystem::path> outputDirectoryPath;
  if (!outputDirectory.empty()) {
    outputDirectoryPath = outputDirectory;
  }
  const auto parsedAsicConfigVariant = asicConfigVariant.empty()
      ? std::nullopt
      : std::optional<std::string_view>(asicConfigVariant);
  const auto outputPath = configgen::generateAgentConfig(
      platform,
      profile,
      fbossRoot,
      outputDirectoryPath,
      asicConfigFilePath,
      parsedAsicConfigType,
      parsedAsicConfigVariant);
  if (referenceConfigFile.empty()) {
    return "Generated Agent configuration at " + outputPath.string();
  }

  const auto comparison = configgen::compareAgentConfigContents(
      readFile(outputPath), readFile(referenceConfigFile));
  const auto result =
      formatComparisonResult(outputPath, referenceConfigFile, comparison);
  if (comparison.status ==
      configgen::AgentConfigComparisonStatus::CONTENT_MISMATCH) {
    throw FbossError(result);
  }
  return result;
}

void CmdConfigGenAgent::printOutput(const RetType& output) {
  std::cout << output << std::endl;
}

template void CmdHandler<CmdConfigGenAgent, CmdConfigGenAgentTraits>::run();

} // namespace facebook::fboss
