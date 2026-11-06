/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/CommandLog.h"

#include <fmt/format.h>

namespace facebook::fboss::platform::platform_checks {

namespace {

void appendBlock(std::string& log, const std::string& block) {
  log += block;
  if (!block.empty() && block.back() != '\n') {
    log += '\n';
  }
}

} // namespace

CommandResult CommandLog::run(
    const Host& host,
    const std::string& cmd,
    std::chrono::seconds timeout) {
  auto result = host.run(cmd, timeout);
  log_ += fmt::format("[{}]$ {}\n", host.name(), cmd);
  appendBlock(log_, result.standardOut);
  if (!result.standardErr.empty()) {
    log_ += "[stderr]\n";
    appendBlock(log_, result.standardErr);
  }
  log_ += fmt::format("[exit code {}]\n", result.exitCode);
  return result;
}

void CommandLog::note(const std::string& message) {
  appendBlock(log_, message);
}

CheckResult CommandLog::attachTo(CheckResult result) const {
  result.details() = log_;
  return result;
}

} // namespace facebook::fboss::platform::platform_checks
