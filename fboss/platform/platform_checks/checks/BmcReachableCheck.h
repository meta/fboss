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
#include <string>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * Reports whether the BMC could be reached. Without it, an unreachable BMC
 * would only skip every BMC check, and the run would look healthy.
 */
class BmcReachableCheck : public PlatformCheck {
 public:
  // `connectError` is set if the BMC was expected but could not be reached.
  BmcReachableCheck(CheckTarget bmc, std::optional<std::string> connectError)
      : PlatformCheck(std::move(bmc)), connectError_(std::move(connectError)) {}

  CheckResult run() override {
    if (connectError_) {
      return makeProblem(
          *connectError_,
          RemediationType::MANUAL_REMEDIATION,
          "Check that the BMC is up and reachable over SSH; if this switch "
          "has no BMC, pass --no-bmc");
    }
    return makeOK();
  }

  std::optional<std::string> getSkipReason() const override {
    if (connectError_) {
      return std::nullopt;
    }
    return PlatformCheck::getSkipReason();
  }

  CheckType getType() const override {
    return CheckType::BMC_REACHABILITY_CHECK;
  }

  std::string getName() const override {
    return "BMC Reachable";
  }

  std::string getDescription() const override {
    return "Checks that the BMC can be reached over SSH";
  }

 private:
  std::optional<std::string> connectError_;
};

} // namespace facebook::fboss::platform::platform_checks
