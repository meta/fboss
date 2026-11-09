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
#include <string>
#include <vector>

namespace facebook::fboss::platform::platform_checks {

/**
 * Lets non-interactive OpenSSH (no TTY, e.g. a piped shell session) log in
 * with a password: ssh runs SSH_ASKPASS to obtain it. The password is kept in
 * a private temporary file read by a helper script, so it never appears in
 * any process's command line or environment.
 */
class SshAskPass {
 public:
  explicit SshAskPass(const std::string& password);
  ~SshAskPass();

  SshAskPass(const SshAskPass&) = delete;
  SshAskPass& operator=(const SshAskPass&) = delete;
  SshAskPass(SshAskPass&&) = delete;
  SshAskPass& operator=(SshAskPass&&) = delete;

  // Prefix for an ssh/scp argv, e.g. {"env", "SSH_ASKPASS=...", ...}.
  std::vector<std::string> envPrefix() const;

  const std::filesystem::path& script() const {
    return script_;
  }

 private:
  std::filesystem::path dir_;
  std::filesystem::path script_;
};

} // namespace facebook::fboss::platform::platform_checks
