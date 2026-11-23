// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "fboss/lib/HwWriteBehavior.h"

#include <gtest/gtest.h>
#include <future>
#include <thread>

namespace facebook::fboss {

// Default with no scope installed is WRITE.
TEST(HwWriteBehaviorTest, defaultIsWrite) {
  EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
}

// A scope installs its behavior and restores the previous one on exit,
// including nesting.
TEST(HwWriteBehaviorTest, nestedScopesRestore) {
  EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
  {
    HwWriteBehaviorRAII outer{HwWriteBehavior::FAIL};
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::FAIL);
    {
      HwWriteBehaviorRAII inner{HwWriteBehavior::LOG_FAIL};
      EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::LOG_FAIL);
    }
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::FAIL);
  }
  EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
}

// Regression test: one thread's FAIL window must not leak into another
// thread. qsfp_service inits one SaiSwitch per xphy concurrently; with a
// process-global flag, thread A's FAIL window trips thread B's legitimate
// SAI calls (e.g. the mandatory warmboot switch-reclaim create) and can
// leave the flag stuck at FAIL after interleaved save/restore.
TEST(HwWriteBehaviorTest, scopesAreThreadLocal) {
  EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
  // Barrier 1: main signals the child only after FAIL is installed, so the
  // child's first read happens strictly inside main's FAIL window. Without
  // this, the child may read before main installs FAIL and the test would
  // pass even on a process-global implementation.
  std::promise<void> failInstalledPromise;
  std::shared_future<void> failInstalled{failInstalledPromise.get_future()};
  // Barrier 2: child signals main when done, so main checks its own scope
  // only after the child ran.
  std::promise<void> childDonePromise;
  auto childDone = childDonePromise.get_future();
  std::thread other([failInstalled, &childDonePromise] {
    failInstalled.wait();
    // Must observe the default, not the FAIL installed on the main thread.
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
    {
      HwWriteBehaviorRAII inner{HwWriteBehavior::FAIL};
      EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::FAIL);
    }
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
    childDonePromise.set_value();
  });
  {
    HwWriteBehaviorRAII outer{HwWriteBehavior::FAIL};
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::FAIL);
    failInstalledPromise.set_value();
    childDone.wait();
    // The other thread's scope must not disturb this thread's scope...
    EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::FAIL);
  }
  other.join();
  // ...nor leak FAIL past scope exit (the stuck-at-FAIL teardown crash).
  EXPECT_EQ(getHwWriteBehavior(), HwWriteBehavior::WRITE);
}

} // namespace facebook::fboss
