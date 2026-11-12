// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"

#include <vector>

namespace facebook::fboss {

class AgentEnsemble;

// Calls recorded by the test-only definitions of the instrumentation hooks.
struct InstrumentationHookCalls {
  int starts{0};
  int stops{0};
  AgentEnsemble* notifiedEnsemble{nullptr};
  std::vector<utility::TestLifecycleEvent> notifiedEvents;
  // Returned by the notification hook.
  bool notificationAccepted{true};
};

// Defined by the test that links the definer.
InstrumentationHookCalls& instrumentationHookCalls();

} // namespace facebook::fboss
