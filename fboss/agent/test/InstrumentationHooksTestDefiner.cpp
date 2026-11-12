// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/InstrumentationHooksTestDefiner.h"

#include "fboss/agent/InstrumentationHookEntryPoints.h"

extern "C" {
void fbossStartCoverageCollection() noexcept {
  ++facebook::fboss::instrumentationHookCalls().starts;
}

void fbossStopCoverageCollection() noexcept {
  ++facebook::fboss::instrumentationHookCalls().stops;
}
}
