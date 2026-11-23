/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/lib/FunctionCallTimeReporter.h"

#include <fmt/format.h>
#include <folly/logging/xlog.h>

#include <folly/Singleton.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <sstream>
#include <thread>

DEFINE_bool(enable_call_timing, false, "Enable call time reporting");

namespace {

struct singleton_tag_type {};
} // namespace

using facebook::fboss::FunctionCallTimeReporter;
static folly::Singleton<FunctionCallTimeReporter, singleton_tag_type>
    functionCallTimeReporterSingleton;

std::shared_ptr<FunctionCallTimeReporter>
FunctionCallTimeReporter::getInstance() {
  return functionCallTimeReporterSingleton.try_get();
}

namespace facebook::fboss {

thread_local FunctionCallTimeReporter::CallTimeTracker
    FunctionCallTimeReporter::tracker_;

FunctionCallTimeReporter::CallTimeTracker::~CallTimeTracker() {
  std::string threadIdStr;
  std::stringstream ss;
  ss << std::this_thread::get_id();
  threadIdStr = ss.str();
  std::string threadStr;
  auto threadName = folly::getCurrentThreadName();
  if (threadName) {
    threadStr = fmt::format("{} ({})", *threadName, threadIdStr);
  } else {
    threadStr = threadIdStr;
  }
  XLOG(INFO) << " Thread: " << threadStr
             << " function time msecs: " << (cumalativeUsecs_.count() / 1000.0);
}

void FunctionCallTimeReporter::start() {
  CHECK(!isOn_);
  isOn_ = true;
  startTime_ = std::chrono::steady_clock::now();
}

void FunctionCallTimeReporter::end() {
  CHECK(isOn_);
  std::chrono::duration<double, std::micro> durationUsecs =
      std::chrono::steady_clock::now() - startTime_;
  XLOG(INFO) << "Total time, msecs: " << (durationUsecs.count() / 1000.0);
  isOn_ = false;
}

ScopedCallTimer::ScopedCallTimer() {
  if (FLAGS_enable_call_timing) {
    FunctionCallTimeReporter::getInstance()->start();
  }
}
ScopedCallTimer::~ScopedCallTimer() {
  if (FLAGS_enable_call_timing) {
    FunctionCallTimeReporter::getInstance()->end();
  }
}

} // namespace facebook::fboss
