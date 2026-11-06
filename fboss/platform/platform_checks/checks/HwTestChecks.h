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

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fboss/platform/platform_checks/PlatformCheck.h"
#include "fboss/platform/platform_checks/checks/PowerCycleChecks.h"

// Checks that run prebuilt test suites on the switch. Test artifacts live in a
// local directory (--hw-tests-dir) and are copied to the switch first.
namespace facebook::fboss::platform::platform_checks {

struct HwTestSpec {
  // Artifact name in the tests directory: a binary, or a directory holding a
  // binary of the same name.
  std::string name;
  std::chrono::seconds timeout{300};
  // Run after platform services are stopped, before the test.
  std::vector<std::string> preRunCommands;
};

// Whether a cit_runner.py run passed: exit code 0 and unittest's "OK"
// summary.
bool citRunPassed(const CommandResult& result);

// The platform service hardware tests that device_checker ran.
std::vector<HwTestSpec> platformHwTestSpecs();

/**
 * Runs a platform hardware test binary on the x86 with platform services
 * stopped. Services are restarted afterwards, whatever the outcome.
 */
class HwTestCheck : public PlatformCheck {
 public:
  HwTestCheck(
      CheckTarget x86,
      HwTestSpec spec,
      std::optional<std::filesystem::path> testsDir)
      : PlatformCheck(std::move(x86)),
        spec_(std::move(spec)),
        testsDir_(std::move(testsDir)) {}

  CheckResult run() override;

  std::optional<std::string> getSkipReason() const override;

  bool isDisruptive() const override {
    return true;
  }

  CheckType getType() const override {
    return CheckType::PLATFORM_HW_TEST_CHECK;
  }

  std::string getName() const override {
    return spec_.name;
  }

  std::string getDescription() const override {
    return "Runs " + spec_.name +
        " with platform services stopped (disruptive)";
  }

 private:
  std::filesystem::path localPath() const;
  // Stages and runs the test; may throw. The caller attaches `log`.
  CheckResult runTest(CommandLog& log) const;

  HwTestSpec spec_;
  std::optional<std::filesystem::path> testsDir_;
};

/**
 * Runs the OpenBMC CIT suite (tests2/cit_runner.py) on the BMC. The suite may
 * reboot the x86, so the check waits for it to come back.
 */
class BmcCitTestCheck : public PlatformCheck {
 public:
  BmcCitTestCheck(
      CheckTarget bmc,
      CheckTarget x86,
      std::optional<std::filesystem::path> testsDir,
      std::string bmcPlatform,
      RebootTiming timing = {})
      : PlatformCheck(std::move(bmc)),
        x86_(std::move(x86)),
        testsDir_(std::move(testsDir)),
        bmcPlatform_(std::move(bmcPlatform)),
        timing_(timing) {}

  CheckResult run() override;

  std::optional<std::string> getSkipReason() const override;

  bool isDisruptive() const override {
    return true;
  }

  CheckType getType() const override {
    return CheckType::BMC_CIT_TEST_CHECK;
  }

  std::string getName() const override {
    return "BMC CIT tests";
  }

  std::string getDescription() const override {
    return "Runs the OpenBMC CIT suite on the BMC (disruptive)";
  }

 private:
  CheckTarget x86_;
  std::optional<std::filesystem::path> testsDir_;
  std::string bmcPlatform_;
  RebootTiming timing_;
};

} // namespace facebook::fboss::platform::platform_checks
