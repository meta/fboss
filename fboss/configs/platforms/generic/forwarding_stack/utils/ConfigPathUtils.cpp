/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/configs/platforms/generic/forwarding_stack/utils/ConfigPathUtils.h"

#include <algorithm>

#include "fboss/agent/FbossError.h"

namespace facebook::fboss::configgen {
namespace fs = std::filesystem;

std::optional<ResolvedConfigFile> findConfigFile(
    const fs::path& directory,
    std::string_view fileName) {
  auto internalPath = directory / kInternalConfigDirectoryName / fileName;
  auto publicPath = directory / fileName;
  const bool hasInternal = fs::is_regular_file(internalPath);
  const bool hasPublic = fs::is_regular_file(publicPath);
  if (hasInternal && hasPublic) {
    throw FbossError(
        "Config file exists in both open-source and internal locations: ",
        publicPath.string(),
        " and ",
        internalPath.string());
  }
  if (hasInternal) {
    return ResolvedConfigFile{
        .path = std::move(internalPath), .isInternal = true};
  }
  if (hasPublic) {
    return ResolvedConfigFile{
        .path = std::move(publicPath), .isInternal = false};
  }
  return std::nullopt;
}

bool isInternalConfigPath(const fs::path& path, const fs::path& root) {
  const auto relativePath = path.lexically_relative(root);
  if (relativePath.empty() || *relativePath.begin() == "..") {
    return false;
  }
  return std::any_of(
      relativePath.begin(), relativePath.end(), [](const fs::path& component) {
        return component == kInternalConfigDirectoryName;
      });
}

} // namespace facebook::fboss::configgen
