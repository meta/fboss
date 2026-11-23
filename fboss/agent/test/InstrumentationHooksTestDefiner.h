// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"

#include <cstdint>
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
  // The next token to return. It starts at 0, which is a valid token.
  uint64_t captures{0};
  std::vector<uint64_t> restoredTokens;
};

// Defined by the test that links the definer.
InstrumentationHookCalls& instrumentationHookCalls();

} // namespace facebook::fboss
