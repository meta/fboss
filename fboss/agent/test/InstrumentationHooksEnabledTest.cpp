// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/InstrumentationHooks.h"
#include "fboss/agent/test/InstrumentationHooksTestDefiner.h"
#include "fboss/agent/test/TestLifecycleNotification.h"

#include <gtest/gtest.h>

#include <vector>

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

TEST_F(InstrumentationHooksEnabledTest, NotificationForwardsEnsembleAndEvents) {
  // Only compared, never dereferenced.
  int storage{0};
  auto* ensemble = reinterpret_cast<AgentEnsemble*>(&storage);

  notifyHwAgentsOfTestLifecycleEvent(
      ensemble, utility::TestLifecycleEvent::AGENT_SETUP_COMPLETE);
  notifyHwAgentsOfTestLifecycleEvent(
      ensemble, utility::TestLifecycleEvent::AGENT_TEARDOWN_START);

  const std::vector<utility::TestLifecycleEvent> expected{
      utility::TestLifecycleEvent::AGENT_SETUP_COMPLETE,
      utility::TestLifecycleEvent::AGENT_TEARDOWN_START};
  EXPECT_EQ(instrumentationHookCalls().notifiedEvents, expected);
  EXPECT_EQ(instrumentationHookCalls().notifiedEnsemble, ensemble);
}

TEST_F(InstrumentationHooksEnabledTest, RejectedNotificationDoesNotThrow) {
  instrumentationHookCalls().notificationAccepted = false;

  EXPECT_NO_THROW(notifyHwAgentsOfTestLifecycleEvent(
      nullptr, utility::TestLifecycleEvent::AGENT_TEARDOWN_START));

  const std::vector<utility::TestLifecycleEvent> expected{
      utility::TestLifecycleEvent::AGENT_TEARDOWN_START};
  EXPECT_EQ(instrumentationHookCalls().notifiedEvents, expected);
}

} // namespace facebook::fboss
