// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/TracingStackCheckpoint.h"
#include "fboss/agent/test/TestLifecycleNotification.h"

#include <gtest/gtest.h>

// Linked without a library that defines the hooks, as in a normal build.

namespace facebook::fboss {

// Calling a wrapper that skipped its null check would crash here.
TEST(InstrumentationHooksDefaultTest, WrappersAreSafeWithoutHooks) {
  startCoverageCollection();
  stopCoverageCollection();
  const TracingStackCheckpoint checkpoint;
}

TEST(InstrumentationHooksDefaultTest, NotificationIsSafeWithoutHook) {
  EXPECT_NO_THROW(notifyHwAgentsOfTestLifecycleEvent(
      nullptr, utility::TestLifecycleEvent::AGENT_SETUP_COMPLETE));
}

} // namespace facebook::fboss
