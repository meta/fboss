// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/InstrumentationHooks.h"

#include <gtest/gtest.h>

// Linked without a library that defines the hooks, as in a normal build.

namespace facebook::fboss {

// Calling a wrapper that skipped its null check would crash here.
TEST(InstrumentationHooksDefaultTest, WrappersAreSafeWithoutHooks) {
  startCoverageCollection();
  stopCoverageCollection();
}

} // namespace facebook::fboss
