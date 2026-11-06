/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/PlatformCheck.h"
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "fboss/platform/config_lib/ConfigLib.h"

namespace facebook::fboss::platform::platform_checks {
platform_manager::PlatformConfig PlatformCheck::getPlatformConfig() const {
  std::string configJson =
      fboss::platform::ConfigLib().getPlatformManagerConfig(platformName());
  fboss::platform::platform_manager::PlatformConfig config;
  try {
    apache::thrift::SimpleJSONSerializer::deserialize<
        fboss::platform::platform_manager::PlatformConfig>(configJson, config);
  } catch (const std::exception& e) {
    XLOG(ERR) << "Failed to deserialize platform config: " << e.what();
    throw;
  }
  return config;
}

} // namespace facebook::fboss::platform::platform_checks
