// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/test/HwTestThriftHandler.h"

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/test/HwTestLifecycleEvent.h"

namespace facebook::fboss::utility {

void HwTestThriftHandler::notifyTestLifecycleEvent(
    TestLifecycleEventResult& result,
    std::unique_ptr<std::string> attemptUuid,
    TestLifecycleEvent event) {
  result =
      testLifecycleObservers().notify(*attemptUuid, event, FLAGS_switchIndex);
}

} // namespace facebook::fboss::utility
