/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/utils/ConfigFileUtils.h"

#include <fcntl.h>
#include <stdlib.h>

#include <cerrno>
#include <stdexcept>
#include <system_error>

#include <fmt/format.h>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include "fboss/lib/CommonFileUtils.h"

namespace facebook::fboss::utils {
namespace {

namespace fs = std::filesystem;

const fs::path kDefaultOutputRoot{"/tmp/fboss2/config-gen"};
const fs::path kActiveConfigRoot{"/etc/coop"};

fs::path normalizePath(const fs::path& path) {
  std::error_code error;
  auto absolutePath = fs::absolute(path, error);
  if (error) {
    throw std::runtime_error(
        fmt::format(
            "Failed to resolve output directory {}: {}",
            path.string(),
            error.message()));
  }
  auto normalizedPath = fs::weakly_canonical(absolutePath, error);
  if (error) {
    throw std::runtime_error(
        fmt::format(
            "Failed to normalize output directory {}: {}",
            path.string(),
            error.message()));
  }
  return normalizedPath;
}

bool isWithin(const fs::path& path, const fs::path& root) {
  auto pathIt = path.begin();
  for (auto rootIt = root.begin(); rootIt != root.end(); ++rootIt, ++pathIt) {
    if (pathIt == path.end() || *pathIt != *rootIt) {
      return false;
    }
  }
  return true;
}

fs::path ensureOutputDirectory(const fs::path& requestedDirectory) {
  auto outputDirectory = normalizePath(requestedDirectory);
  if (isWithin(outputDirectory, normalizePath(kActiveConfigRoot))) {
    throw std::invalid_argument(
        fmt::format(
            "Refusing to generate configuration under {}",
            kActiveConfigRoot.string()));
  }

  createDir(outputDirectory.string());
  std::error_code error;
  if (!fs::is_directory(outputDirectory, error) || error) {
    throw std::runtime_error(
        fmt::format(
            "Output path is not a directory: {}", outputDirectory.string()));
  }
  return outputDirectory;
}

fs::path createDefaultOutputDirectory() {
  const auto outputRoot = ensureOutputDirectory(kDefaultOutputRoot);
  auto directoryTemplate = (outputRoot / "XXXXXX").string();
  if (::mkdtemp(directoryTemplate.data()) == nullptr) {
    throw std::system_error(
        errno,
        std::generic_category(),
        fmt::format(
            "Failed to create a unique output directory under {}",
            outputRoot.string()));
  }
  return directoryTemplate;
}

} // namespace

fs::path prepareOutputDirectory(
    const std::optional<fs::path>& outputDirectory) {
  return outputDirectory.has_value() ? ensureOutputDirectory(*outputDirectory)
                                     : createDefaultOutputDirectory();
}

void writeFileWithoutOverwrite(
    const fs::path& path,
    std::string_view contents) {
  folly::File file(
      path.string(),
      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
      0644);
  try {
    const auto bytesWritten =
        folly::writeFull(file.fd(), contents.data(), contents.size());
    if (bytesWritten < 0) {
      throw std::system_error(
          errno, std::generic_category(), "Failed to write " + path.string());
    }
    if (static_cast<size_t>(bytesWritten) != contents.size()) {
      throw std::runtime_error(
          "Failed to write complete file " + path.string());
    }
    file.close();
  } catch (...) {
    file.closeNoThrow();
    std::error_code removeError;
    fs::remove(path, removeError);
    throw;
  }
}

} // namespace facebook::fboss::utils
