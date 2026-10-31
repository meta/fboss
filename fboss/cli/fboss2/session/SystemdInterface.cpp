/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/cli/fboss2/session/SystemdInterface.h"

#include <chrono>
#include <thread>

#include <fmt/format.h>
#include <folly/String.h>
#include <folly/Subprocess.h>
#include <folly/logging/xlog.h>
#include <folly/small_vector.h>
#include <unistd.h>

namespace facebook::fboss {

namespace {
// Helper function for systemctl commands that return boolean status
bool runSystemctlCheck(
    folly::StringPiece command,
    const std::string& serviceName) {
  try {
    folly::Subprocess proc(
        {"/usr/bin/systemctl", command.str(), "--quiet", serviceName});
    proc.waitChecked();
    return true;
  } catch (const folly::CalledProcessError&) {
    return false;
  }
}

// Helper function for systemctl commands that perform actions
void runSystemctlAction(
    folly::StringPiece action,
    const std::string& serviceName) {
  try {
    std::vector<std::string> cmd;
    if (getuid() != 0) {
      cmd.emplace_back("/usr/bin/sudo");
    }
    cmd.insert(cmd.end(), {"/usr/bin/systemctl", action.str(), serviceName});
    folly::Subprocess proc(cmd);
    proc.waitChecked();
  } catch (const folly::CalledProcessError& ex) {
    throw std::runtime_error(
        fmt::format("Failed to {} {}: {}", action, serviceName, ex.what()));
  }
}
} // namespace

bool SystemdInterface::isServiceEnabled(const std::string& serviceName) {
  return runSystemctlCheck("is-enabled", serviceName);
}

std::vector<std::string> SystemdInterface::getMatchingServices(
    const std::string& pattern) {
  folly::Subprocess proc(
      {"/usr/bin/systemctl",
       "list-units",
       "--all",
       "--type=service",
       "--state=loaded",
       "--plain",
       "--no-legend",
       "--no-pager",
       pattern},
      folly::Subprocess::Options().pipeStdout().pipeStderr());
  const auto [standardOut, standardErr] = proc.communicate();
  const auto returnCode = proc.wait();
  if (!returnCode.exited() || returnCode.exitStatus() != 0) {
    throw std::runtime_error(
        fmt::format(
            "Failed to query systemd services matching {}: {}",
            pattern,
            folly::trimWhitespace(standardErr)));
  }

  std::vector<std::string> services;
  folly::small_vector<folly::StringPiece, 8> lines;
  folly::split('\n', folly::StringPiece(standardOut), lines, true);
  for (const auto& line : lines) {
    const auto trimmedLine = folly::trimWhitespace(line).str();
    if (trimmedLine.empty()) {
      continue;
    }
    services.emplace_back(
        trimmedLine.substr(0, trimmedLine.find_first_of(" \t")));
  }
  return services;
}

void SystemdInterface::stopService(const std::string& serviceName) {
  runSystemctlAction("stop", serviceName);
}

void SystemdInterface::startService(const std::string& serviceName) {
  runSystemctlAction("start", serviceName);
}

void SystemdInterface::restartService(const std::string& serviceName) {
  runSystemctlAction("restart", serviceName);
}

bool SystemdInterface::isServiceActive(const std::string& serviceName) {
  return runSystemctlCheck("is-active", serviceName);
}

void SystemdInterface::waitForServiceActive(
    const std::string& serviceName,
    int maxWaitSeconds,
    int pollIntervalMs) {
  int waitedMs = 0;

  while (waitedMs < maxWaitSeconds * 1000) {
    if (isServiceActive(serviceName)) {
      XLOG(INFO) << serviceName << " is now active";
      return;
    }
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    waitedMs += pollIntervalMs;
  }

  throw std::runtime_error(
      fmt::format(
          "{} did not become active within {} seconds",
          serviceName,
          maxWaitSeconds));
}

void SystemdInterface::waitForServiceInactive(
    const std::string& serviceName,
    int maxWaitSeconds,
    int pollIntervalMs) {
  int waitedMs = 0;

  while (waitedMs < maxWaitSeconds * 1000) {
    if (!isServiceActive(serviceName)) {
      XLOG(INFO) << serviceName << " is no longer active";
      return;
    }
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
    waitedMs += pollIntervalMs;
  }

  throw std::runtime_error(
      fmt::format(
          "{} was still active {} seconds after being stopped",
          serviceName,
          maxWaitSeconds));
}

} // namespace facebook::fboss
