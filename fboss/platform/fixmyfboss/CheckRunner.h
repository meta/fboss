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

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace re2 {
class RE2;
} // namespace re2

namespace facebook::fboss::platform::fixmyfboss {

struct RunOptions {
  // Run checks that reboot or power cycle parts of the switch.
  bool includeDisruptive{false};
  // Only run checks whose name matches this regex (case-insensitive).
  std::optional<std::string> nameFilter;
};

struct PlannedCheck {
  std::reference_wrapper<platform_checks::PlatformCheck> check;
  // False for checks of other platforms; they are listed but not reported.
  bool appliesToPlatform{false};
  // Set if the check will be reported as SKIPPED.
  std::optional<std::string> skipReason;
  // Set if computing skipReason threw; the check is reported as ERROR.
  std::optional<std::string> skipReasonError;
};

/**
 * Runs checks applicable to a platform and turns exceptions into ERROR
 * results, so one broken check cannot abort the whole run. Disruptive checks
 * run last, so that they cannot affect the other checks.
 */
class CheckRunner {
 public:
  // Throws std::invalid_argument for an invalid RunOptions::nameFilter.
  explicit CheckRunner(std::string platformName, RunOptions options = {});

  // The checks selected by the name filter, in run order, with what will
  // happen to each. Used both to run and to list checks.
  std::vector<PlannedCheck> plan(
      const std::vector<std::unique_ptr<platform_checks::PlatformCheck>>&
          checks) const;

  // Called before each check that will actually run, with its 1-based
  // position among them.
  using ProgressCallback = std::function<
      void(int index, int total, const platform_checks::PlatformCheck& check)>;

  std::vector<platform_checks::CheckResult> run(
      const std::vector<PlannedCheck>& plan,
      const ProgressCallback& onProgress = nullptr) const;

  std::vector<platform_checks::CheckResult> run(
      const std::vector<std::unique_ptr<platform_checks::PlatformCheck>>&
          checks) const {
    return run(plan(checks));
  }

 private:
  bool appliesToPlatform(const platform_checks::PlatformCheck& check) const;
  bool isSelected(const platform_checks::PlatformCheck& check) const;
  platform_checks::CheckResult runOne(const PlannedCheck& planned) const;
  std::optional<std::string> getSkipReason(
      const platform_checks::PlatformCheck& check) const;

  std::string platformName_;
  RunOptions options_;
  std::shared_ptr<const re2::RE2> nameFilter_;
};

} // namespace facebook::fboss::platform::fixmyfboss
