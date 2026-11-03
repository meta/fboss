/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/CommandCheck.h"

#include "fboss/platform/platform_checks/CommandLog.h"

namespace facebook::fboss::platform::platform_checks {

CheckResult CommandCheck::run() {
  CommandLog log;
  auto result = log.run(host(), spec_.command, spec_.timeout);
  if (result.ok()) {
    return log.attachTo(makeOK());
  }
  return log.attachTo(makeProblem(
      "`" + spec_.command + "` on " + host().name() + " exited with code " +
          std::to_string(result.exitCode),
      RemediationType::MANUAL_REMEDIATION,
      spec_.remediation));
}

} // namespace facebook::fboss::platform::platform_checks
