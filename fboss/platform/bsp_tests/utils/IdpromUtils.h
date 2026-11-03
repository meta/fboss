// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/platform/bsp_tests/RuntimeConfigBuilder.h"
#include "fboss/platform/bsp_tests/gen-cpp2/bsp_tests_runtime_config_types.h"
#include "fboss/platform/platform_manager/gen-cpp2/platform_manager_config_types.h"

namespace facebook::fboss::platform::bsp_tests {

class IdpromUtils {
 public:
  // Reads every IDPROM placed in `runtimeConfig` and returns the version of
  // each PmUnit that reports one. Reloads the kmods and brings up the adapters
  // itself, so it does not depend on platform_manager having run. Deletes the
  // FPGA adapters and IDPROM devices it created, and leaves the kmods loaded,
  // the state every test suite expects to start from.
  static PmUnitVersionMap detectPmUnitVersions(
      const platform_manager::PlatformConfig& pmConfig,
      const RuntimeConfig& runtimeConfig);
};

} // namespace facebook::fboss::platform::bsp_tests
