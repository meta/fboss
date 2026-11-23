// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/InstrumentationHooks.h"

#include "fboss/agent/InstrumentationHookEntryPoints.h"

// Weak, so that an entry point is null unless a collector defines it.
extern "C" {
void fbossStartCoverageCollection() noexcept __attribute__((weak));
void fbossStopCoverageCollection() noexcept __attribute__((weak));
}

namespace facebook::fboss {

void startCoverageCollection() noexcept {
  if (fbossStartCoverageCollection != nullptr) {
    fbossStartCoverageCollection();
  }
}

void stopCoverageCollection() noexcept {
  if (fbossStopCoverageCollection != nullptr) {
    fbossStopCoverageCollection();
  }
}

} // namespace facebook::fboss
