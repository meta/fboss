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
#include <optional>
#include <string_view>

namespace facebook::fboss::configgen {

// Meta-internal config files live in a directory with this name, directly
// beside where the open-source file would be. See fboss/configs/README.md.
inline constexpr std::string_view kInternalConfigDirectoryName = "facebook";

struct ResolvedConfigFile {
  std::filesystem::path path;
  bool isInternal;
};

// Looks for <directory>/facebook/<fileName> and <directory>/<fileName>.
// Returns std::nullopt when neither exists and throws when both exist.
std::optional<ResolvedConfigFile> findConfigFile(
    const std::filesystem::path& directory,
    std::string_view fileName);

// Returns whether path is below root and passes through a facebook/
// directory.
bool isInternalConfigPath(
    const std::filesystem::path& path,
    const std::filesystem::path& root);

} // namespace facebook::fboss::configgen
