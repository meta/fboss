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

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace facebook::fboss::platform {
class ExpectSession;
} // namespace facebook::fboss::platform

namespace facebook::fboss::platform::platform_checks {

// A command could not be executed on a host, as opposed to running and
// failing.
class HostCommandError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct CommandResult {
  int exitCode{-1};
  std::string standardOut;
  std::string standardErr;

  bool ok() const {
    return exitCode == 0;
  }
};

/**
 * A machine that checks run against: the local system, or a remote x86 or BMC.
 * Checks must perform all I/O through a Host so that the same check works both
 * on-device and remotely.
 */
class Host {
 public:
  static constexpr std::chrono::seconds kDefaultTimeout{30};

  virtual ~Host() = default;

  virtual std::string name() const = 0;

  virtual bool isLocal() const = 0;

  // Runs `cmd` with /bin/sh. A timed out command has exit code 124.
  virtual CommandResult run(
      const std::string& cmd,
      std::chrono::seconds timeout = kDefaultTimeout) const = 0;

  // Copies a local file or directory (recursively) to `hostPath` on this host.
  // Throws std::runtime_error on failure.
  virtual void copyTo(
      const std::filesystem::path& localPath,
      const std::filesystem::path& hostPath,
      std::chrono::seconds timeout) const = 0;

  // Starts an interactive login shell in a PTY, for flows that need a
  // terminal (e.g. serial consoles).
  virtual std::unique_ptr<ExpectSession> spawnInteractive() const = 0;

  // The defaults below are implemented with run(), which works on any host.
  // They throw HostCommandError if the command itself could not be run (e.g.
  // the connection dropped), so that this is not mistaken for a missing file.

  virtual std::optional<std::string> readFile(
      const std::filesystem::path& path) const;

  virtual bool exists(const std::filesystem::path& path) const;

  // Returns full paths of the entries of `path`, or an empty list on failure.
  virtual std::vector<std::filesystem::path> listDirectory(
      const std::filesystem::path& path) const;

  // readFile() for many files in one round trip, which matters on remote
  // hosts. Unreadable files are nullopt.
  virtual std::vector<std::optional<std::string>> readFiles(
      const std::vector<std::filesystem::path>& paths) const;

  // readFile() without surrounding whitespace, e.g. for sysfs attributes.
  std::optional<std::string> readTrimmedFile(
      const std::filesystem::path& path) const;

 private:
  // run(), throwing HostCommandError if `cmd` could not be executed.
  CommandResult runQuery(const std::string& cmd) const;
};

// Runs `argv` locally, killing it after `timeout`. Shared by Host
// implementations, which all end up spawning a local process.
CommandResult runLocalCommand(
    const std::vector<std::string>& argv,
    std::chrono::seconds timeout);

} // namespace facebook::fboss::platform::platform_checks
