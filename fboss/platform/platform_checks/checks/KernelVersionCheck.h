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

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * Validates that the running kernel version is a qualified version
 */
class KernelVersionCheck : public PlatformCheck {
 public:
  explicit KernelVersionCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::KERNEL_VERSION_CHECK;
  }

  std::string getName() const override {
    return "Kernel Version Check";
  }

  std::string getDescription() const override {
    return "Validates kernel version compatibility with FBOSS";
  }

 protected:
  virtual std::optional<std::vector<std::string>> getSupportedKernelVersions();
};

} // namespace facebook::fboss::platform::platform_checks
