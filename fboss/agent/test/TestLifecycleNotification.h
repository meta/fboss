// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"

namespace facebook::fboss {

class AgentEnsemble;

// Passes the event to a linked test-coverage collector, if any, and logs an
// event it rejects without failing the test.
void notifyHwAgentsOfTestLifecycleEvent(
    AgentEnsemble* ensemble,
    utility::TestLifecycleEvent event);

} // namespace facebook::fboss
