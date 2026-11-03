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

#include <memory>
#include <vector>

#include "fboss/platform/fixmyfboss/CheckEnvironment.h"
#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::fixmyfboss {

// Every check fixmyfboss knows about, wired to the hosts of `env`.
std::vector<std::unique_ptr<platform_checks::PlatformCheck>> createAllChecks(
    const CheckEnvironment& env);

} // namespace facebook::fboss::platform::fixmyfboss
