// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/agent/hw/test/HwTestLifecycleEvent.h"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>

namespace facebook::fboss::utility {
namespace {

constexpr auto kEvent = TestLifecycleEvent::AGENT_TEARDOWN_START;

TestLifecycleEventResult makeResult(
    bool handled,
    bool success,
    int32_t switchIndex,
    std::optional<std::string> error = std::nullopt) {
  TestLifecycleEventResult result;
  result.handled() = handled;
  result.success() = success;
  result.switchIndex() = switchIndex;
  if (error) {
    result.error() = *error;
  }
  return result;
}

struct ObservedEvent {
  std::string attemptUuid;
  TestLifecycleEvent event;
  int32_t switchIndex;
  bool operator==(const ObservedEvent&) const = default;
};

TEST(HwTestLifecycleEventTest, UnhandledWithoutObservers) {
  TestLifecycleObservers observers;

  EXPECT_EQ(
      observers.notify("attempt", kEvent, 7),
      makeResult(false, false, 7, "no test-lifecycle observer is registered"));
}

TEST(HwTestLifecycleEventTest, DeliversEventToEveryObserver) {
  TestLifecycleObservers observers;
  std::vector<ObservedEvent> observed;
  for (int i = 0; i < 2; ++i) {
    observers.add([&](const std::string& attemptUuid,
                      TestLifecycleEvent event,
                      int32_t switchIndex) {
      observed.push_back({attemptUuid, event, switchIndex});
      return true;
    });
  }

  EXPECT_EQ(
      observers.notify("attempt", kEvent, 11), makeResult(true, true, 11));
  const std::vector<ObservedEvent> expected(2, {"attempt", kEvent, 11});
  EXPECT_EQ(observed, expected);
}

TEST(HwTestLifecycleEventTest, ObserverKeepsStateAcrossNotifications) {
  TestLifecycleObservers observers;
  std::vector<int> seenCounts;
  observers.add([&seenCounts, count = 0](
                    const std::string&, TestLifecycleEvent, int32_t) mutable {
    seenCounts.push_back(++count);
    return true;
  });

  observers.notify("attempt", kEvent, 0);
  observers.notify("attempt", kEvent, 0);

  const std::vector<int> expected{1, 2};
  EXPECT_EQ(seenCounts, expected);
}

TEST(HwTestLifecycleEventTest, FailsIfAnyObserverRejectsOrThrows) {
  TestLifecycleObservers observers;
  int calls = 0;
  observers.add([&](const std::string&, TestLifecycleEvent, int32_t) {
    ++calls;
    return false;
  });
  observers.add([&](const std::string&, TestLifecycleEvent, int32_t) -> bool {
    ++calls;
    throw std::runtime_error("observer failure");
  });
  observers.add([&](const std::string&, TestLifecycleEvent, int32_t) -> bool {
    ++calls;
    // Deliberately not a std::exception, to cover the catch-all path.
    // @lint-ignore CLANGTIDY facebook-hte-ThrowNonStdExceptionIssue
    throw 42;
  });
  observers.add([&](const std::string&, TestLifecycleEvent, int32_t) {
    ++calls;
    return true;
  });

  EXPECT_EQ(
      observers.notify("attempt", kEvent, 5),
      makeResult(
          true,
          false,
          5,
          "3 of 4 test-lifecycle observers rejected the event"));
  EXPECT_EQ(calls, 4);
}

} // namespace
} // namespace facebook::fboss::utility
