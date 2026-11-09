/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/KernelVersionCheck.h"

#include <folly/String.h>
#include <folly/logging/xlog.h>

#include "common/config/ConfigeratorConfig.h"
#include "configerator/structs/fboss/fboss_platform/gen-cpp2/kernel_versions_types.h"

namespace facebook::fboss::platform::platform_checks {

CheckResult KernelVersionCheck::run() {
  auto supportedKernelVersions = getSupportedKernelVersions();
  if (!supportedKernelVersions) {
    return makeError("Failed to get supported kernel versions");
  }

  auto uname = host().run("uname -r");
  if (!uname.ok()) {
    return makeError("Failed to get kernel version information");
  }
  std::string kernelRelease = folly::trimWhitespace(uname.standardOut).str();

  if (std::find(
          supportedKernelVersions->begin(),
          supportedKernelVersions->end(),
          kernelRelease) != supportedKernelVersions->end()) {
    return makeOK();
  }

  std::string errorMsg =
      "Kernel version " + kernelRelease + " is not supported";
  std::string remediationMsg = "Reprovision to install correct kernel";
  return makeProblem(
      errorMsg, RemediationType::REPROVISION_REQUIRED, remediationMsg);
}

std::optional<std::vector<std::string>>
KernelVersionCheck::getSupportedKernelVersions() {
  config::ConfigeratorConfig<::fboss::platform::KernelVersionsConfig> cfg(
      "fboss/fboss_platform/kernel_versions");
  auto cfgPtr = cfg.get();
  if (!cfgPtr) {
    return std::nullopt;
  }
  return *cfgPtr->kernel_versions();
}

} // namespace facebook::fboss::platform::platform_checks
