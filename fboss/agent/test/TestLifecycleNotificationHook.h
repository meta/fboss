// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"

namespace facebook::fboss {
class AgentEnsemble;
} // namespace facebook::fboss

// Test-side entry point for an optional test-coverage collector, under the link
// contract in fboss/agent/InstrumentationHookEntryPoints.h. Tests call it only
// through notifyHwAgentsOfTestLifecycleEvent. A collector typically forwards to
// AgentEnsemble::notifyTestLifecycleEvent. It returns whether every hardware
// agent accepted the event and must not let an exception escape.
extern "C" {
bool fbossNotifyHwAgentsOfTestLifecycleEvent(
    facebook::fboss::AgentEnsemble* ensemble,
    facebook::fboss::utility::TestLifecycleEvent event) noexcept;
}
