// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/test/TestLifecycleNotification.h"

#include "fboss/agent/test/TestLifecycleNotificationHook.h"

#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

// Weak, so that the entry point is null unless a collector defines it.
extern "C" {
bool fbossNotifyHwAgentsOfTestLifecycleEvent(
    facebook::fboss::AgentEnsemble* ensemble,
    facebook::fboss::utility::TestLifecycleEvent event) noexcept
    __attribute__((weak));
}

namespace facebook::fboss {

void notifyHwAgentsOfTestLifecycleEvent(
    AgentEnsemble* ensemble,
    utility::TestLifecycleEvent event) {
  if (fbossNotifyHwAgentsOfTestLifecycleEvent != nullptr &&
      !fbossNotifyHwAgentsOfTestLifecycleEvent(ensemble, event)) {
    XLOG(ERR) << "Test-lifecycle notification "
              << apache::thrift::util::enumNameSafe(event)
              << " to hardware agents failed";
  }
}

} // namespace facebook::fboss
