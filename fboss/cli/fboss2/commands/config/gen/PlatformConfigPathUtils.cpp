/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/commands/config/gen/PlatformConfigPathUtils.h"

#include <optional>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss::configgen {
namespace fs = std::filesystem;

PlatformConfigDirectory findPlatformConfigDirectory(
    const fs::path& fbossRoot,
    std::string_view platform) {
  if (fbossRoot.empty()) {
    throw FbossError("FBOSS source root must not be empty");
  }
  if (platform.empty()) {
    throw FbossError("Platform name must not be empty");
  }

  const auto platformsDirectory = fbossRoot / "configs" / "platforms";
  if (!fs::is_directory(platformsDirectory)) {
    throw FbossError(
        "Platform config directory does not exist: ",
        platformsDirectory.string());
  }

  std::optional<PlatformConfigDirectory> match;
  for (const auto& vendorEntry : fs::directory_iterator(platformsDirectory)) {
    if (!vendorEntry.is_directory() ||
        vendorEntry.path().filename() == "generic") {
      continue;
    }
    for (const auto& platformEntry :
         fs::directory_iterator(vendorEntry.path())) {
      if (!platformEntry.is_directory() ||
          platformEntry.path().filename().string() != platform) {
        continue;
      }

      PlatformConfigDirectory candidate{
          .systemVendor = vendorEntry.path().filename().string(),
          .path = platformEntry.path(),
      };
      if (match) {
        throw FbossError(
            "Duplicate platform '",
            platform,
            "' found under system vendors '",
            match->systemVendor,
            "' and '",
            candidate.systemVendor,
            "'");
      }
      match = std::move(candidate);
    }
  }

  if (!match) {
    throw FbossError(
        "Platform '",
        platform,
        "' was not found under ",
        platformsDirectory.string());
  }
  return std::move(*match);
}

fs::path findPlatformConfigComponentDirectory(
    const PlatformConfigDirectory& platformDirectory,
    std::string_view component) {
  const fs::path componentName{component};
  if (componentName.empty() || componentName.has_parent_path()) {
    throw FbossError("Invalid platform config component: ", component);
  }

  const auto componentDirectory = platformDirectory.path / componentName;
  if (!fs::is_directory(componentDirectory)) {
    throw FbossError(
        "Platform config component '",
        component,
        "' does not exist under ",
        platformDirectory.path.string());
  }
  return componentDirectory;
}

} // namespace facebook::fboss::configgen
