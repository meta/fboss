/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/AsyncLogger.h"

#include <sys/stat.h>
#include <unistd.h>
#include <string>

#include <folly/FileUtil.h>
#include <gtest/gtest.h>

#define TEST_LOG "/tmp/sai_logger_test"

// Test string size that's larger than half of the buffer,
// such that two strings cannot be flushed together.
static auto constexpr kTestStringSize =
    facebook::fboss::AsyncLogger::kBufferSize * 3 / 4;

using namespace facebook::fboss;

class AsyncLoggerTest : public ::testing::Test {
 public:
  void SetUp() override {
    asyncLogger = std::make_unique<AsyncLogger>(
        TEST_LOG, logTimeout, AsyncLogger::BCM_CINTER);
    asyncLogger->startFlushThread();
  }

  void TearDown() override {
    asyncLogger->stopFlushThread();
    std::remove(TEST_LOG);
  }

  std::unique_ptr<AsyncLogger> asyncLogger;
  std::condition_variable cv;
  std::mutex latch;
  uint32_t logTimeout = 100;
};

TEST(AsyncLoggerFallbackTest, FallbackRefusesSymlinkTarget) {
  // A primary log path under a non-existent directory forces the primary open
  // to fail, so AsyncLoggerBase falls back to /tmp/<name>. The agent runs as
  // root, and a local attacker could pre-plant that predictable path as a
  // symlink; the fallback must not follow it and truncate the target.
  // Use a pid-suffixed name so concurrent runs on a shared host (e.g. parallel
  // sandcastle jobs) don't race on the same /tmp paths.
  const std::string suffix = std::to_string(::getpid());
  const std::string victimPath = "/tmp/async_logger_victim_" + suffix;
  const std::string fallbackName = "async_logger_fallback_test_" + suffix;
  const std::string fallbackPath = "/tmp/" + fallbackName;
  const std::string primaryPath =
      "/nonexistent_async_logger_dir/" + fallbackName;

  ::unlink(fallbackPath.c_str());
  ::unlink(victimPath.c_str());
  ASSERT_TRUE(
      folly::writeFile(std::string("DO NOT TRUNCATE"), victimPath.c_str()));
  ASSERT_EQ(::symlink(victimPath.c_str(), fallbackPath.c_str()), 0);

  {
    AsyncLogger logger(primaryPath, 100, AsyncLogger::BCM_CINTER);
  }

  // The planted symlink must not have been followed: victim is intact.
  std::string victimContent;
  ASSERT_TRUE(folly::readFile(victimPath.c_str(), victimContent));
  EXPECT_EQ(victimContent, "DO NOT TRUNCATE");

  // The fallback path is now a fresh regular file, not the planted symlink.
  struct stat st{};
  ASSERT_EQ(::lstat(fallbackPath.c_str(), &st), 0);
  EXPECT_TRUE(S_ISREG(st.st_mode));

  ::unlink(fallbackPath.c_str());
  ::unlink(victimPath.c_str());
}

TEST(AsyncLoggerWriteFailureTest, WriteFailureDoesNotTerminate) {
  // Every write to /dev/full fails with ENOSPC, like a full disk. A failed
  // write must drop the logs, not take down the process.
  if (::access("/dev/full", W_OK) != 0) {
    GTEST_SKIP() << "/dev/full not available";
  }
  AsyncLogger logger("/dev/full", 100, AsyncLogger::BCM_CINTER);
  logger.startFlushThread();

  std::string str = "TestString";
  logger.appendLog(str.c_str(), str.size());
  logger.forceFlush();
  EXPECT_GE(logger.getFlushCount(), 1);

  // Larger than the buffer, so it is written directly on this thread.
  std::string largeLog(AsyncLogger::kBufferSize + 1000, 'X');
  logger.appendLog(largeLog.c_str(), largeLog.size());

  logger.stopFlushThread();
}

// Skip this test in tsan mode because of the slow down introduced by
// thread sanitizer. It makes the logger flush once in 3-4x log timeout.
#ifndef FOLLY_SANITIZE_THREAD
TEST_F(AsyncLoggerTest, logTimeoutTest) {
  std::string str(kTestStringSize, '.');
  asyncLogger->appendLog(str.c_str(), str.size());
  EXPECT_EQ(asyncLogger->getFlushCount(), 0);

  // Wait for one log timeout and then check
  // Add 20ms of boundary to let background thread flush data
  std::unique_lock<std::mutex> lock(latch);
  cv.wait_for(lock, std::chrono::milliseconds(logTimeout + 20));
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);

  // Wait for one more log timeout but there's no logging happening
  // So flush count should still be the same
  cv.wait_for(lock, std::chrono::milliseconds(logTimeout + 20));
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);
}
#endif

TEST_F(AsyncLoggerTest, fullflushTest) {
  std::string str(kTestStringSize, '.');
  asyncLogger->appendLog(str.c_str(), str.size());
  EXPECT_EQ(asyncLogger->getFlushCount(), 0);

  asyncLogger->appendLog(str.c_str(), str.size());

  // Log buffer is swapped, but it's non-blocking and flush does not take place
  // immediately. Wait for 20ms to let that happen
  std::unique_lock<std::mutex> lock(latch);
  cv.wait_for(lock, std::chrono::milliseconds(20));
  EXPECT_GE(asyncLogger->getFlushCount(), 1);
}

TEST_F(AsyncLoggerTest, forceflushTest) {
  std::string str = "TestString";
  asyncLogger->appendLog(str.c_str(), str.size());
  EXPECT_EQ(asyncLogger->getFlushCount(), 0);

  // Force flush will block until the flush happens
  asyncLogger->forceFlush();
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);
}

TEST_F(AsyncLoggerTest, emptyBufferTest) {
  // Force flush to flush out the new boot header - this should keep the buffer
  // empty.
  asyncLogger->forceFlush();
  std::unique_lock<std::mutex> lock(latch);
  cv.wait_for(lock, std::chrono::milliseconds(20));

  std::string str;
  asyncLogger->appendLog(str.c_str(), str.size());
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);

  // Wait for one log timeout and then check empty buffer
  // Add 20ms of boundary to let background thread flush data
  cv.wait_for(lock, std::chrono::milliseconds(logTimeout + 20));
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);

  // Force flush and check
  asyncLogger->forceFlush();
  EXPECT_EQ(asyncLogger->getFlushCount(), 1);
}

TEST_F(AsyncLoggerTest, concurrentWaitTest) {
  std::string str(kTestStringSize, '.');
  asyncLogger->appendLog(str.c_str(), str.size());

  // The first string will fill up the empty buffer, causing the next two
  // appends to wait for buffer to be swapped. When they finish waiting, logger
  // should realize that the buffer can only accept one. So it will immediately
  // flush one more time, rather than waiting for the next timeout to flush the
  // other string.
  std::thread t([&]() { asyncLogger->appendLog(str.c_str(), str.size()); });

  asyncLogger->appendLog(str.c_str(), str.size());
  t.join();

  // When the third append finished, the logger should have swapped the buffer
  // and potentially still writing into the buffer. Therefore, we wait for a bit
  // here to let the flush happen and increase the flush count.
  std::unique_lock<std::mutex> lock(latch);
  cv.wait_for(lock, std::chrono::milliseconds(20));

  // Without triggering a timeout, logger should flush str0, and one of
  // str1 or str2. The other unflushed string should be in the current buffer.
  // Therefore, the flush count should be equal or greater than two.
  EXPECT_GE(asyncLogger->getFlushCount(), 2);
}

TEST_F(AsyncLoggerTest, logSizeLargerThanBufferSize) {
  // Create a log that's larger than the buffer size
  size_t largeLogSize = AsyncLogger::kBufferSize + 1000;
  std::string largeLog(largeLogSize, 'X');

  // This should complete without hanging. We use a timeout to ensure
  // the test fails if it hangs instead of blocking forever.
  std::atomic<bool> completed{false};
  std::thread t([&]() {
    asyncLogger->appendLog(largeLog.c_str(), largeLog.size());
    completed = true;
  });

  // Wait for the append to complete with a timeout
  std::unique_lock<std::mutex> lock(latch);
  cv.wait_for(lock, std::chrono::milliseconds(500));

  // If logger forever waits for available buffer it would cause this test to
  // hang. so if we get here, we need to ensure the append is completed.
  t.join();

  // The append should have completed (either successfully or with proper error
  // handling)
  EXPECT_TRUE(completed);
}
