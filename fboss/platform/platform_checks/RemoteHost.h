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

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "fboss/platform/platform_checks/Host.h"
#include "fboss/platform/platform_checks/SshAskPass.h"

namespace facebook::fboss::platform {
class ExpectSession;
} // namespace facebook::fboss::platform

namespace facebook::fboss::platform::platform_checks {

/**
 * A switch (x86 or BMC) reached over SSH, authenticating with keys or
 * certificates, or with a password (SSH transport only).
 *
 * Opening a connection per command costs about a second, which adds up to
 * minutes for checks that read many sysfs files, so commands share one
 * long-lived remote shell driven through an ExpectSession. If that shell
 * cannot be started, each command opens its own connection instead.
 *
 * Thread-safe: concurrent commands on one host are serialized through its
 * shell session.
 */
class RemoteHost : public Host {
 public:
  enum class Transport {
    // OpenSSH ssh/scp.
    SSH,
    // Meta's sush2/suscp2 wrappers, which handle authentication to switches.
    SUSH2,
  };

  // SUSH2 when sush2 is on PATH, SSH otherwise.
  static Transport detectTransport();

  // A password forces the SSH transport: sush2 does its own authentication.
  RemoteHost(
      std::string hostname,
      Transport transport,
      std::string user = "root",
      std::optional<std::string> password = std::nullopt);
  ~RemoteHost() override;

  std::string name() const override {
    return hostname_;
  }

  bool isLocal() const override {
    return false;
  }

  CommandResult run(
      const std::string& cmd,
      std::chrono::seconds timeout = kDefaultTimeout) const override;

  void copyTo(
      const std::filesystem::path& localPath,
      const std::filesystem::path& hostPath,
      std::chrono::seconds timeout) const override;

  std::unique_ptr<ExpectSession> spawnInteractive() const override;

  // Local argv that runs `cmd` on the host. Virtual for tests.
  virtual std::vector<std::string> execArgv(const std::string& cmd) const;

  // Local argv that opens an interactive login shell on the host. Virtual for
  // tests.
  virtual std::vector<std::string> interactiveArgv() const;

  std::vector<std::string> copyArgv(
      const std::filesystem::path& localPath,
      const std::filesystem::path& hostPath) const;

  // Asks the host's SSH server, without offering keys, whether it accepts a
  // password login. False also if the host cannot be reached.
  bool acceptsPassword(std::chrono::seconds timeout) const;

  // Local argv for acceptsPassword().
  std::vector<std::string> authMethodsArgv() const;

 private:
  std::string destination() const;
  // ssh/scp options plus, for password logins, the askpass environment.
  std::vector<std::string> sshPrefix(const std::string& program) const;
  // nullopt iff the shell could not be started, i.e. `cmd` was never sent.
  std::optional<CommandResult> runInSession(
      const std::string& cmd,
      std::chrono::seconds timeout) const;
  // A shell ready for commands, or nullptr if it could not be started.
  std::unique_ptr<ExpectSession> startSession(
      std::chrono::steady_clock::time_point deadline) const;
  // Ends the session after a timeout or lost connection while `cmd` was in
  // flight.
  CommandResult abandonSession(
      const std::string& cmd,
      std::chrono::seconds timeout) const;
  // Ends the session and reports `error`; the command is not retried.
  CommandResult endSession(int exitCode, std::string error) const;

  std::string hostname_;
  Transport transport_;
  std::string user_;
  std::unique_ptr<SshAskPass> askPass_;
  mutable std::mutex sessionMutex_;
  mutable std::unique_ptr<ExpectSession> session_;
  mutable uint64_t nextCommandId_{0};
};

// Whether ssh's "Permission denied (<methods>)." in `sshStderr` lists a
// password method.
bool offersPasswordAuth(const std::string& sshStderr);

} // namespace facebook::fboss::platform::platform_checks
