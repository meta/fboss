/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/HwTestChecks.h"

#include <fmt/format.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/system/Shell.h>
#include <re2/re2.h>

#include "fboss/platform/platform_checks/CommandLog.h"

namespace facebook::fboss::platform::platform_checks {

namespace {

constexpr auto kPlatformServices =
    "platform_manager fan_service sensor_service data_corral_service";
constexpr std::chrono::seconds kServiceTimeout{75};
constexpr std::chrono::seconds kCopyTimeout{600};
// mktemp -d creates it 0700, under a name other users cannot predict.
constexpr auto kMakeStagingDir = "mktemp -d /tmp/fixmyfboss.XXXXXX";
constexpr auto kRemoteCitDir = "/usr/local/bin/tests2";
constexpr auto kTestsDirFlag = "pass --hw-tests-dir to run";

std::optional<std::string> missingArtifact(
    const std::optional<std::filesystem::path>& testsDir,
    const std::string& artifact) {
  if (!testsDir) {
    return std::string("Needs test artifacts; ") + kTestsDirFlag;
  }
  if (!std::filesystem::exists(*testsDir / artifact)) {
    return "No " + artifact + " in " + testsDir->string();
  }
  return std::nullopt;
}

// Copies and records how long it took: copying many small files can take
// minutes. Throws like Host::copyTo().
void copyAndLog(
    const Host& host,
    const std::filesystem::path& localPath,
    const std::filesystem::path& hostPath,
    CommandLog& log) {
  auto start = std::chrono::steady_clock::now();
  host.copyTo(localPath, hostPath, kCopyTimeout);
  log.note(
      fmt::format(
          "Copied {} to {}:{} in {}s",
          localPath.string(),
          host.name(),
          hostPath.string(),
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - start)
              .count()));
}

} // namespace

bool citRunPassed(const CommandResult& result) {
  // unittest ends with "OK", "OK (skipped=N)" or "FAILED (failures=N, ...)",
  // on stderr.
  static const RE2 kOk(R"((?m)^OK( \(.*\))?\s*$)");
  static const RE2 kFailed(R"((?m)^FAILED \()");
  const auto output = result.standardOut + result.standardErr;
  return result.ok() && RE2::PartialMatch(output, kOk) &&
      !RE2::PartialMatch(output, kFailed);
}

std::vector<HwTestSpec> platformHwTestSpecs() {
  return {
      {.name = "platform_manager_hw_test"},
      {.name = "bsp_tests", .timeout = std::chrono::seconds(600)},
      {.name = "data_corral_service_hw_test"},
      {.name = "fan_service_hw_test",
       .preRunCommands = {"systemctl start sensor_service"}},
      {.name = "fw_util_hw_test"},
      {.name = "sensor_service_hw_test"},
      {.name = "weutil_hw_test"},
  };
}

std::optional<std::string> HwTestCheck::getSkipReason() const {
  if (auto reason = PlatformCheck::getSkipReason()) {
    return reason;
  }
  return missingArtifact(testsDir_, spec_.name);
}

std::filesystem::path HwTestCheck::localPath() const {
  return *testsDir_ / spec_.name;
}

CheckResult HwTestCheck::run() {
  CommandLog log;
  const auto startServices =
      std::string("systemctl start ") + kPlatformServices;
  log.run(
      host(),
      std::string("systemctl stop ") + kPlatformServices,
      kServiceTimeout);
  // Caught here so the services are restarted and the log is kept.
  CheckResult result;
  try {
    result = runTest(log);
  } catch (const std::exception& ex) {
    result = makeError(ex.what());
  }

  if (!log.run(host(), startServices, kServiceTimeout).ok()) {
    auto problem = "Platform services did not restart after " + spec_.name;
    if (*result.status() != CheckStatus::OK) {
      problem += ", which also failed: " + result.errorMessage().value_or("");
    }
    return log.attachTo(makeProblem(
        problem,
        RemediationType::MANUAL_REMEDIATION,
        "Start them with: " + startServices));
  }
  return log.attachTo(std::move(result));
}

CheckResult HwTestCheck::runTest(CommandLog& log) const {
  auto staging = log.run(host(), kMakeStagingDir);
  const auto stagingDir = folly::trimWhitespace(staging.standardOut).str();
  if (!staging.ok() || stagingDir.empty()) {
    return makeError(
        "Could not create a staging directory on " + host().name());
  }
  // Also runs while unwinding, so it must not throw.
  SCOPE_EXIT {
    try {
      log.run(host(), "rm -rf " + folly::shellQuote(stagingDir));
    } catch (const std::exception& ex) {
      log.note("Could not remove " + stagingDir + ": " + ex.what());
    }
  };
  const auto remotePath = std::filesystem::path(stagingDir) / spec_.name;
  auto binary = remotePath;
  if (std::filesystem::is_directory(localPath())) {
    binary /= spec_.name;
  }

  copyAndLog(host(), localPath(), remotePath, log);
  for (const auto& cmd : spec_.preRunCommands) {
    log.run(host(), cmd);
  }
  auto quoted = folly::shellQuote(binary.string());
  auto result =
      log.run(host(), "chmod 0700 " + quoted + " && " + quoted, spec_.timeout);
  if (result.ok()) {
    return makeOK();
  }
  return makeProblem(
      spec_.name + " exited with code " + std::to_string(result.exitCode),
      RemediationType::MANUAL_REMEDIATION,
      "See the failing test cases in the details (-v)");
}

std::optional<std::string> BmcCitTestCheck::getSkipReason() const {
  if (auto reason = PlatformCheck::getSkipReason()) {
    return reason;
  }
  if (!x86_.host) {
    return x86_.unavailableReason;
  }
  // The suite may reboot the x86.
  if (x86_.host->isLocal()) {
    return "May reboot the machine running fixmyfboss; run it from another "
           "machine with --hostname";
  }
  return missingArtifact(testsDir_, "tests2");
}

CheckResult BmcCitTestCheck::run() {
  CommandLog log;
  log.run(host(), std::string("rm -rf ") + kRemoteCitDir);
  try {
    copyAndLog(host(), *testsDir_ / "tests2", kRemoteCitDir, log);
  } catch (const std::exception& ex) {
    return log.attachTo(makeError(ex.what()));
  }
  auto result = log.run(
      host(),
      std::string("cd ") + kRemoteCitDir +
          " && python cit_runner.py --platform " +
          folly::shellQuote(bmcPlatform_),
      std::chrono::seconds(600));
  bool passed = citRunPassed(result);

  // Tests may reboot the x86; later checks and the user need it back.
  bool x86Back = waitForHost(*x86_.host, timing_, log);

  // An x86 left down is the more urgent problem; report it even if tests
  // failed.
  if (!x86Back) {
    return log.attachTo(makeProblem(
        passed ? "x86 did not come back after the CIT tests"
               : "CIT tests failed and the x86 did not come back",
        RemediationType::MANUAL_REMEDIATION,
        passed ? "Power on the x86 with wedge_power.sh on from the BMC"
               : "Power on the x86 with wedge_power.sh on from the BMC, then "
                 "see the failing test cases in the details (-v)"));
  }
  if (!passed) {
    return log.attachTo(makeProblem(
        "CIT tests failed",
        RemediationType::MANUAL_REMEDIATION,
        "See the failing test cases in the details (-v)"));
  }
  return log.attachTo(makeOK());
}

} // namespace facebook::fboss::platform::platform_checks
