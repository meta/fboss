/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <optional>

#include "fboss/platform/platform_checks/Host.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * The machine fixmyfboss runs on.
 */
class LocalHost : public Host {
 public:
  // If `rootDir` is set, file access treats every path (even absolute ones) as
  // relative to it, like PlatformFsUtils. Commands are not affected.
  explicit LocalHost(
      std::optional<std::filesystem::path> rootDir = std::nullopt);

  std::string name() const override {
    return "localhost";
  }

  bool isLocal() const override {
    return true;
  }

  CommandResult run(
      const std::string& cmd,
      std::chrono::seconds timeout = kDefaultTimeout) const override;

  void copyTo(
      const std::filesystem::path& localPath,
      const std::filesystem::path& hostPath,
      std::chrono::seconds timeout) const override;

  std::unique_ptr<ExpectSession> spawnInteractive() const override;

  std::optional<std::string> readFile(
      const std::filesystem::path& path) const override;

  std::vector<std::optional<std::string>> readFiles(
      const std::vector<std::filesystem::path>& paths) const override;

  bool exists(const std::filesystem::path& path) const override;

  std::vector<std::filesystem::path> listDirectory(
      const std::filesystem::path& path) const override;

 private:
  std::filesystem::path resolve(const std::filesystem::path& path) const;

  std::optional<std::filesystem::path> rootDir_;
};

} // namespace facebook::fboss::platform::platform_checks
