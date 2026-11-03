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

#include "fboss/platform/platform_checks/Host.h"
#include "fboss/platform/platform_checks/gen-cpp2/check_types_types.h"

namespace facebook::fboss::platform::platform_checks {

/**
 * Runs commands for a check and records them with their output, so a failure
 * can be debugged from CheckResult.details.
 */
class CommandLog {
 public:
  CommandResult run(
      const Host& host,
      const std::string& cmd,
      std::chrono::seconds timeout = Host::kDefaultTimeout);

  void note(const std::string& message);

  // Returns `result` with its details set to everything logged so far.
  CheckResult attachTo(CheckResult result) const;

 private:
  std::string log_;
};

} // namespace facebook::fboss::platform::platform_checks
