// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/InstrumentationHooksTestDefiner.h"

#include "fboss/agent/InstrumentationHookEntryPoints.h"
#include "fboss/agent/test/TestLifecycleNotificationHook.h"

extern "C" {
void fbossStartCoverageCollection() noexcept {
  ++facebook::fboss::instrumentationHookCalls().starts;
}

void fbossStopCoverageCollection() noexcept {
  ++facebook::fboss::instrumentationHookCalls().stops;
}

bool fbossNotifyHwAgentsOfTestLifecycleEvent(
    facebook::fboss::AgentEnsemble* ensemble,
    facebook::fboss::utility::TestLifecycleEvent event) noexcept {
  auto& calls = facebook::fboss::instrumentationHookCalls();
  calls.notifiedEnsemble = ensemble;
  calls.notifiedEvents.push_back(event);
  return calls.notificationAccepted;
}
}
