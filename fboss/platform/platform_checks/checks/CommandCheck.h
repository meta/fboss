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
#include <string>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

struct CommandCheckSpec {
  CheckType type;
  std::string name;
  std::string description;
  std::string command;
  std::chrono::seconds timeout{Host::kDefaultTimeout};
  // Shown when the command fails.
  std::string remediation;
};

/**
 * Passes if a shell command exits with 0 on the target host. Covers the many
 * health checks that boil down to "this tool works".
 */
class CommandCheck : public PlatformCheck {
 public:
  CommandCheck(CheckTarget target, CommandCheckSpec spec)
      : PlatformCheck(std::move(target)), spec_(std::move(spec)) {}

  CheckResult run() override;

  CheckType getType() const override {
    return spec_.type;
  }

  std::string getName() const override {
    return spec_.name;
  }

  std::string getDescription() const override {
    return spec_.description;
  }

 private:
  CommandCheckSpec spec_;
};

} // namespace facebook::fboss::platform::platform_checks
