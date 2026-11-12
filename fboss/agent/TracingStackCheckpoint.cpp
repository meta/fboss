// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/TracingStackCheckpoint.h"

#include "fboss/agent/InstrumentationHookEntryPoints.h"

// Weak, so that an entry point is null unless a collector defines it.
extern "C" {
uint64_t fbossCaptureCurrentThreadTracingStackCheckpoint() noexcept
    __attribute__((weak));
void fbossRestoreCurrentThreadTracingStackCheckpoint(uint64_t token) noexcept
    __attribute__((weak));
}

namespace facebook::fboss {

TracingStackCheckpoint::TracingStackCheckpoint() noexcept {
  if (fbossCaptureCurrentThreadTracingStackCheckpoint != nullptr &&
      fbossRestoreCurrentThreadTracingStackCheckpoint != nullptr) {
    token_ = fbossCaptureCurrentThreadTracingStackCheckpoint();
    captured_ = true;
  }
}

TracingStackCheckpoint::~TracingStackCheckpoint() {
  if (captured_) {
    fbossRestoreCurrentThreadTracingStackCheckpoint(token_);
  }
}

} // namespace facebook::fboss
