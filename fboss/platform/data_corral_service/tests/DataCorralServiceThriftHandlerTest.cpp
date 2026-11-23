// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <thread>

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include "fboss/platform/data_corral_service/DataCorralServiceThriftHandler.h"

namespace facebook::fboss::platform::data_corral_service {

namespace {
// V5 EEPROM from fboss/platform/weutil/test/FbossEepromInterfaceTest.cpp
const std::vector<uint8_t> kEepromV5 = {
    0xfb, 0xfb, 0x05, 0xff, 0x01, 0x0d, 0x46, 0x49, 0x52, 0x53, 0x54,
    0x5f, 0x53, 0x51, 0x55, 0x45, 0x45, 0x5a, 0x45, 0x02, 0x08, 0x32,
    0x30, 0x31, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
} // namespace

// Unsynchronized access to the cached fruid is reported by TSAN as a data race
// and by ASAN as a heap-use-after-free.
TEST(
    DataCorralServiceThriftHandlerTest,
    GetFruid_ConcurrentCachedAndUncachedCalls_ReturnFullContents) {
  constexpr int kThreads = 8;
  constexpr int kCallsPerThread = 100;
  folly::test::TemporaryDirectory tmpDir;
  auto eepromPath = tmpDir.path().string() + "/eeprom";
  folly::writeFile(kEepromV5, eepromPath.c_str());
  DataCorralServiceThriftHandler handler(eepromPath);
  DataCorralFruidReadResponse expected;
  handler.getFruid(expected, true);
  ASSERT_FALSE(expected.fruidData()->empty());

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([&, uncached = i % 2 == 0] {
      for (int call = 0; call < kCallsPerThread; ++call) {
        DataCorralFruidReadResponse response;
        handler.getFruid(response, uncached);
        EXPECT_EQ(*response.fruidData(), *expected.fruidData());
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

} // namespace facebook::fboss::platform::data_corral_service
