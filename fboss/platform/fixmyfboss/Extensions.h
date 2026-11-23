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
#include <vector>

#include <CLI/CLI.hpp>

#include "fboss/platform/platform_checks/gen-cpp2/check_types_types.h"

namespace facebook::fboss::platform::fixmyfboss {

// Called with the report once all checks have finished.
using ReportHook = std::function<void(const platform_checks::CheckReport&)>;

// Adds options to `app`; returns what to do with the report.
using Extension = std::function<ReportHook(CLI::App&)>;

// For optional libraries linked into the binary, from a static initializer.
// Always returns true, so it can initialize a namespace-scope constant.
bool registerExtension(Extension extension);

// Lets every registered extension add its options to `app`.
std::vector<ReportHook> addExtensionOptions(CLI::App& app);

} // namespace facebook::fboss::platform::fixmyfboss
