/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/PowerCycleChecks.h"

#include <functional>
#include <thread>

#include <folly/Conv.h>
#include <folly/String.h>

namespace facebook::fboss::platform::platform_checks {

namespace {

constexpr std::chrono::seconds kProbeTimeout{10};
// Resets drop the connection; the reset command itself returns quickly, and a
// long wait here would inflate the uptime measured afterwards.
constexpr std::chrono::seconds kResetCommandTimeout{30};
constexpr auto kLocalX86Reason =
    "Would power cycle the machine running fixmyfboss; run it from another "
    "machine with --hostname";
constexpr auto kMachineIdPath = "/etc/machine-id";

std::optional<double> uptimeSeconds(const Host& host, CommandLog& log) {
  auto result = log.run(host, "cat /proc/uptime");
  if (!result.ok()) {
    return std::nullopt;
  }
  std::vector<folly::StringPiece> fields;
  folly::split(' ', folly::trimWhitespace(result.standardOut), fields);
  if (fields.empty()) {
    return std::nullopt;
  }
  auto uptime = folly::tryTo<double>(fields.front());
  if (!uptime) {
    return std::nullopt;
  }
  return *uptime;
}

} // namespace

bool waitForHost(
    const Host& host,
    const RebootTiming& timing,
    CommandLog& log) {
  // Polls a rebooting remote host, which has no event to wait on.
  // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
  std::this_thread::sleep_for(timing.settle);
  auto deadline = std::chrono::steady_clock::now() + timing.timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (host.run("true", kProbeTimeout).ok()) {
      log.note(host.name() + " is reachable");
      return true;
    }
    // NOLINTNEXTLINE(facebook-hte-BadCall-sleep_for)
    std::this_thread::sleep_for(timing.pollInterval);
  }
  log.note(host.name() + " did not come back");
  return false;
}

std::optional<CheckResult> SecondaryBmcBootCheck::expectBootSource(
    CommandLog& log,
    const std::string& flash) {
  auto result = log.run(host(), "boot_info.sh bmc");
  auto expected = "Current Boot Code Source: " + flash;
  if (!result.ok() || result.standardOut.find(expected) == std::string::npos) {
    return log.attachTo(makeProblem(
        "Expected '" + expected + "' from boot_info.sh",
        RemediationType::MANUAL_REMEDIATION,
        "Check the BMC flash state with boot_info.sh bmc"));
  }
  return std::nullopt;
}

std::optional<CheckResult> SecondaryBmcBootCheck::rebootFrom(
    CommandLog& log,
    const std::string& flash) {
  // The connection drops while the BMC resets, so the exit code is not
  // meaningful; reaching the expected flash afterwards is.
  log.run(host(), "boot_info.sh bmc reset " + flash, kResetCommandTimeout);
  if (!waitForHost(host(), timing_, log)) {
    return log.attachTo(makeProblem(
        "BMC did not come back after reset to " + flash + " flash",
        RemediationType::MANUAL_REMEDIATION,
        "Recover the BMC from the x86 or via console"));
  }
  return std::nullopt;
}

CheckResult SecondaryBmcBootCheck::run() {
  CommandLog log;
  if (auto failure = expectBootSource(log, "Master Flash")) {
    return *failure;
  }
  if (auto failure = rebootFrom(log, "slave")) {
    return *failure;
  }
  if (auto failure = expectBootSource(log, "Slave Flash")) {
    // Do not leave the BMC on its secondary flash.
    log.note("Restoring master flash");
    if (rebootFrom(log, "master") || expectBootSource(log, "Master Flash")) {
      failure->remediationMessage() =
          "The BMC may be left on its slave flash or unreachable: check "
          "boot_info.sh bmc and recover it from the x86 or via console";
    }
    return log.attachTo(*failure);
  }
  if (auto failure = rebootFrom(log, "master")) {
    return *failure;
  }
  if (auto failure = expectBootSource(log, "Master Flash")) {
    return *failure;
  }
  return log.attachTo(makeOK());
}

std::optional<std::string> WedgePowerCheck::getSkipReason() const {
  if (!bmc_.host) {
    return bmc_.unavailableReason;
  }
  if (auto reason = PlatformCheck::getSkipReason()) {
    return reason;
  }
  if (host().isLocal()) {
    return kLocalX86Reason;
  }
  // --hostname may name the machine running fixmyfboss.
  auto x86Id = host().readTrimmedFile(kMachineIdPath);
  auto selfId = self_->readTrimmedFile(kMachineIdPath);
  if (!x86Id || !selfId || x86Id->empty() || selfId->empty()) {
    return "Could not read " + std::string(kMachineIdPath) +
        " to confirm the x86 is not the machine running fixmyfboss";
  }
  if (*x86Id == *selfId) {
    return kLocalX86Reason;
  }
  return std::nullopt;
}

std::optional<CheckResult> WedgePowerCheck::setPower(
    CommandLog& log,
    const std::string& state) {
  const auto& bmc = *bmc_.host;
  // Some wedge_power.sh versions fail "on" when already on; the resulting
  // status is what matters.
  log.run(bmc, "wedge_power.sh " + state);
  auto status = log.run(bmc, "wedge_power.sh status");
  auto expected = "Microserver power is " + state;
  if (!status.ok() || status.standardOut.find(expected) == std::string::npos) {
    return log.attachTo(makeProblem(
        "wedge_power.sh " + state + " did not result in '" + expected + "'",
        RemediationType::MANUAL_REMEDIATION,
        "Inspect wedge_power.sh output in the details (-v)"));
  }
  return std::nullopt;
}

std::optional<CheckResult> WedgePowerCheck::expectFreshBoot(
    CommandLog& log,
    const Host& host,
    std::chrono::steady_clock::time_point poweredAt) {
  if (!waitForHost(host, timing_, log)) {
    bool isBmc = &host == bmc_.host.get();
    return log.attachTo(makeProblem(
        host.name() + " did not come back within " +
            std::to_string(timing_.timeout.count()) + "s",
        RemediationType::MANUAL_REMEDIATION,
        isBmc ? "Recover the BMC via its console or a power cycle"
              : "Power on the x86 with wedge_power.sh on from the BMC"));
  }
  auto uptime = uptimeSeconds(host, log);
  if (!uptime) {
    return log.attachTo(makeError("Could not read uptime of " + host.name()));
  }
  auto sincePowered = std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - poweredAt)
                          .count();
  if (*uptime > sincePowered) {
    return log.attachTo(makeProblem(
        host.name() + " did not reboot: uptime is " +
            std::to_string(static_cast<int>(*uptime)) + "s, " +
            std::to_string(static_cast<int>(sincePowered)) +
            "s after it was power cycled",
        RemediationType::MANUAL_REMEDIATION,
        "Check that wedge_power.sh controls power of " + host.name()));
  }
  return std::nullopt;
}

CheckResult WedgePowerCheck::run() {
  CommandLog log;
  if (auto failure = setPower(log, "on")) {
    return *failure;
  }
  auto poweredAt = std::chrono::steady_clock::now();
  for (const auto& state : {"off", "on"}) {
    if (auto failure = setPower(log, state)) {
      // Never leave the x86 powered off.
      log.note("Trying to power the x86 back on");
      if (setPower(log, "on")) {
        failure->remediationMessage() =
            "The x86 may be left powered off: power it on with "
            "wedge_power.sh on from the BMC";
      }
      return log.attachTo(*failure);
    }
  }
  if (auto failure = expectFreshBoot(log, host(), poweredAt)) {
    return *failure;
  }
  // Resets the BMC and x86 together; the BMC connection drops.
  poweredAt = std::chrono::steady_clock::now();
  log.run(*bmc_.host, "wedge_power.sh reset -s", kResetCommandTimeout);
  for (const Host& rebooted : {std::cref(*bmc_.host), std::cref(host())}) {
    if (auto failure = expectFreshBoot(log, rebooted, poweredAt)) {
      return *failure;
    }
  }
  return log.attachTo(makeOK());
}

} // namespace facebook::fboss::platform::platform_checks
