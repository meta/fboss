// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "fboss/agent/if/gen-cpp2/agent_hw_test_ctrl_types.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace facebook::fboss::utility {

using TestLifecycleObserver = std::function<bool(
    const std::string& attemptUuid,
    TestLifecycleEvent event,
    int32_t switchIndex)>;

class TestLifecycleObservers {
 public:
  // Register observers before the first notification. They cannot be removed,
  // so they must outlive the process, and must not call add() or notify(),
  // which would deadlock.
  void add(TestLifecycleObserver observer);

  // Delivers the event to every observer, one notification at a time; it
  // succeeds only if all accept. An observer may see the same event more than
  // once, for example when the caller retries a timed-out request.
  TestLifecycleEventResult notify(
      const std::string& attemptUuid,
      TestLifecycleEvent event,
      int32_t switchIndex) const;

 private:
  std::vector<TestLifecycleObserver> observers_;
  mutable std::mutex mutex_;
};

TestLifecycleObservers& testLifecycleObservers();

} // namespace facebook::fboss::utility
