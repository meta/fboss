// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/test/InstrumentationHooksTestDefiner.h"

#include <gtest/gtest.h>

// Linked with a library that defines the hooks and records their calls.

namespace facebook::fboss {

// Defined here rather than in the definer, so that reading the recorder does
// not pull the hook definitions out of the archive; only link_whole does.
InstrumentationHookCalls& instrumentationHookCalls() {
  static InstrumentationHookCalls calls;
  return calls;
}

class InstrumentationHooksEnabledTest : public ::testing::Test {
 protected:
  void SetUp() override {
    instrumentationHookCalls() = {};
  }
};

TEST_F(InstrumentationHooksEnabledTest, WrappersCallDefinedHooks) {
  startCoverageCollection();
  stopCoverageCollection();

  EXPECT_EQ(instrumentationHookCalls().starts, 1);
  EXPECT_EQ(instrumentationHookCalls().stops, 1);
}

} // namespace facebook::fboss
