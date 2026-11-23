// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/test/HwTestLifecycleEvent.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss::utility {

void TestLifecycleObservers::add(TestLifecycleObserver observer) {
  const std::lock_guard<std::mutex> guard(mutex_);
  observers_.push_back(std::move(observer));
}

TestLifecycleEventResult TestLifecycleObservers::notify(
    const std::string& attemptUuid,
    TestLifecycleEvent event,
    int32_t switchIndex) const {
  const std::lock_guard<std::mutex> guard(mutex_);
  TestLifecycleEventResult result;
  result.handled() = !observers_.empty();
  result.success() = false;
  result.switchIndex() = switchIndex;
  if (observers_.empty()) {
    result.error() = "no test-lifecycle observer is registered";
    return result;
  }
  size_t rejected = 0;
  for (const auto& observer : observers_) {
    try {
      rejected += observer(attemptUuid, event, switchIndex) ? 0 : 1;
    } catch (const std::exception& ex) {
      XLOG(ERR) << "Test-lifecycle observer threw: " << ex.what();
      ++rejected;
    } catch (...) {
      XLOG(ERR) << "Test-lifecycle observer threw a non-standard exception";
      ++rejected;
    }
  }
  result.success() = rejected == 0;
  if (rejected > 0) {
    result.error() = fmt::format(
        "{} of {} test-lifecycle observers rejected the event",
        rejected,
        observers_.size());
  }
  return result;
}

TestLifecycleObservers& testLifecycleObservers() {
  // Leaked so that it outlives RPCs served during process exit.
  static auto* observers = new TestLifecycleObservers();
  return *observers;
}

} // namespace facebook::fboss::utility
