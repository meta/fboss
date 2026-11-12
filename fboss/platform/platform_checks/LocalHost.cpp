/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/LocalHost.h"

#include <stdexcept>

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>

#include "fboss/platform/helpers/ExpectSession.h"

namespace facebook::fboss::platform::platform_checks {

LocalHost::LocalHost(std::optional<std::filesystem::path> rootDir)
    : rootDir_(std::move(rootDir)) {}

CommandResult LocalHost::run(
    const std::string& cmd,
    std::chrono::seconds timeout) const {
  return runLocalCommand({"/bin/sh", "-c", cmd}, timeout);
}

void LocalHost::copyTo(
    const std::filesystem::path& localPath,
    const std::filesystem::path& hostPath,
    std::chrono::seconds timeout) const {
  auto result = runLocalCommand(
      {"cp", "-r", "--", localPath.string(), hostPath.string()}, timeout);
  if (!result.ok()) {
    throw std::runtime_error(
        "cp " + localPath.string() + " " + hostPath.string() +
        " failed: " + result.standardErr);
  }
}

std::unique_ptr<ExpectSession> LocalHost::spawnInteractive() const {
  return std::make_unique<ExpectSession>("/bin/sh -i");
}

std::optional<std::string> LocalHost::readFile(
    const std::filesystem::path& path) const {
  std::string content;
  if (!folly::readFile(resolve(path).c_str(), content)) {
    return std::nullopt;
  }
  return content;
}

std::vector<std::optional<std::string>> LocalHost::readFiles(
    const std::vector<std::filesystem::path>& paths) const {
  std::vector<std::optional<std::string>> contents;
  contents.reserve(paths.size());
  for (const auto& path : paths) {
    contents.push_back(readFile(path));
  }
  return contents;
}

bool LocalHost::exists(const std::filesystem::path& path) const {
  std::error_code ec;
  return std::filesystem::exists(resolve(path), ec);
}

std::vector<std::filesystem::path> LocalHost::listDirectory(
    const std::filesystem::path& path) const {
  std::vector<std::filesystem::path> entries;
  std::error_code ec;
  // increment(ec) rather than a range-for, whose operator++ throws.
  for (std::filesystem::directory_iterator it(resolve(path), ec);
       !ec && it != std::filesystem::directory_iterator();
       it.increment(ec)) {
    // Report paths as seen by checks, i.e. without rootDir_.
    entries.push_back(path / it->path().filename());
  }
  if (ec) {
    // A partial listing would look like missing entries.
    XLOG(ERR) << "Failed to list " << path << ": " << ec.message();
    return {};
  }
  return entries;
}

std::filesystem::path LocalHost::resolve(
    const std::filesystem::path& path) const {
  return rootDir_ ? *rootDir_ / path.relative_path() : path;
}

} // namespace facebook::fboss::platform::platform_checks
