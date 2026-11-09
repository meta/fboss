/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fboss/configs/platforms/generic/forwarding_stack/gen-cpp2/config_generation_types.h"
#include "fboss/lib/if/gen-cpp2/fboss_common_types.h"

namespace facebook::fboss::configgen {

ConfigProfileType normalizeProfile(std::string_view profile);
std::string getProfileName(ConfigProfileType profile);

// Service-neutral inputs resolved from one declaration option. Service-specific
// generators consume this request to produce serialized config contents.
struct ConfigGenerationRequest {
  std::filesystem::path fbossRoot;
  PlatformType platformType;
  std::string platform;
  ConfigProfileType profileType;
  std::string profile;
  std::optional<std::string> variant;
  std::optional<std::string> asicConfigVariant;
  std::optional<std::string> linkTopologyVariant;
  ServiceType service;
  std::string serviceName;
  std::filesystem::path platformDirectory;
  std::filesystem::path outputPath;
};

using ConfigContentsGenerator =
    std::function<std::string(const ConfigGenerationRequest&)>;

struct ConfigVerificationResult {
  std::optional<PlatformType> platform;
  std::vector<std::filesystem::path> missing;
  std::vector<std::filesystem::path> stale;
  std::vector<std::filesystem::path> obsolete;

  bool isFresh() const {
    return missing.empty() && stale.empty() && obsolete.empty();
  }
};

// Generates every declared target for one service. When platform is set, only
// that platform's declaration and generated outputs are owned by this call.
// Otherwise every per-platform declaration and generated output is owned. All
// contents are generated before any output is written, then canonical
// forwarding_stack/generated/<service> outputs are atomically refreshed and
// obsolete owned outputs are removed.
std::vector<std::filesystem::path> generateConfigsFromManifest(
    const std::filesystem::path& fbossRoot,
    ServiceType service,
    const ConfigContentsGenerator& generateContents,
    std::optional<std::string_view> platform = std::nullopt);

// Generates expected contents in memory and compares them byte-for-byte with
// the service-owned output directory without modifying the filesystem.
ConfigVerificationResult verifyConfigsFromManifest(
    const std::filesystem::path& fbossRoot,
    ServiceType service,
    const ConfigContentsGenerator& generateContents,
    std::optional<std::string_view> platform = std::nullopt);

} // namespace facebook::fboss::configgen
