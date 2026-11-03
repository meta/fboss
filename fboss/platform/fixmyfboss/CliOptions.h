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

#include <CLI/CLI.hpp>

#include "fboss/platform/fixmyfboss/CheckEnvironment.h"
#include "fboss/platform/fixmyfboss/CheckRunner.h"

namespace facebook::fboss::platform::fixmyfboss {

struct CliOptions {
  bool listChecks{false};
  bool json{false};
  bool verbose{false};
  bool debug{false};
  // "auto", "ssh" or "sush2"; resolved into connect.transport by
  // resolveTransport().
  std::string transport{"auto"};
  ConnectOptions connect;
  RunOptions run;
  std::optional<std::string> hwTestsDir;
  std::optional<std::string> bmcPlatform;
};

void addOptions(CLI::App& app, CliOptions& options);

// Sets options.connect.transport from options.transport.
void resolveTransport(CliOptions& options);

// Applies the options that describe what is available to the checks.
void applyToEnvironment(const CliOptions& options, CheckEnvironment& env);

} // namespace facebook::fboss::platform::fixmyfboss
