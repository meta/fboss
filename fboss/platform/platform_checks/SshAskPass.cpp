/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/SshAskPass.h"

#include <fcntl.h>
#include <stdlib.h>

#include <stdexcept>
#include <vector>

#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <folly/system/Shell.h>

namespace facebook::fboss::platform::platform_checks {

namespace fs = std::filesystem;

namespace {

// The umask filters creation modes and may strip bits ssh needs, such as the
// helper's execute bit, so the mode is set explicitly after creating the file
// owner-only.
bool writeFileWithMode(
    const fs::path& path,
    const std::string& content,
    fs::perms perms) {
  if (!folly::writeFile(
          content, path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)) {
    return false;
  }
  std::error_code ec;
  fs::permissions(path, perms, ec);
  return !ec;
}

} // namespace

SshAskPass::SshAskPass(const std::string& password) {
  std::string dirTemplate =
      (fs::temp_directory_path() / "fixmyfboss-askpass-XXXXXX").string();
  if (!mkdtemp(dirTemplate.data())) {
    throw std::runtime_error("Failed to create askpass directory");
  }
  dir_ = dirTemplate;
  auto passwordFile = dir_ / "password";
  script_ = dir_ / "askpass";
  std::error_code ec;
  fs::permissions(dir_, fs::perms::owner_all, ec);
  if (ec ||
      !writeFileWithMode(
          passwordFile,
          password,
          fs::perms::owner_read | fs::perms::owner_write) ||
      !writeFileWithMode(
          script_,
          "#!/bin/sh\ncat " + folly::shellQuote(passwordFile.string()) + "\n",
          fs::perms::owner_all)) {
    fs::remove_all(dir_, ec);
    throw std::runtime_error("Failed to write askpass helper");
  }
}

SshAskPass::~SshAskPass() {
  std::error_code ec;
  fs::remove_all(dir_, ec);
  if (ec) {
    XLOG(WARN) << "Could not remove " << dir_
               << ", which holds an SSH password: " << ec.message();
  }
}

std::vector<std::string> SshAskPass::envPrefix() const {
  return {
      "env",
      "SSH_ASKPASS=" + script_.string(),
      "SSH_ASKPASS_REQUIRE=force",
  };
}

} // namespace facebook::fboss::platform::platform_checks
