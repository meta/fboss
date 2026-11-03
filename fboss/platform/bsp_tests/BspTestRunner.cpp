// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <folly/logging/xlog.h>
#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include "fboss/platform/bsp_tests/BspTestEnvironment.h"
#include "fboss/platform/bsp_tests/utils/IdpromUtils.h"

#include "fboss/platform/helpers/Init.h"

DEFINE_bool(
    enable_stress_tests,
    false,
    "Enable stress tests that may take a long time to run");

using namespace facebook;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_manager;
using namespace facebook::fboss::platform::bsp_tests;

// Custom test filter to exclude stress tests unless enabled
class StressTestFilter : public ::testing::EmptyTestEventListener {
 public:
  void OnTestStart(const ::testing::TestInfo& test_info) override {
    // Skip stress tests if not enabled
    if (!FLAGS_enable_stress_tests &&
        std::string(test_info.test_suite_name()).find("Stress") !=
            std::string::npos) {
      GTEST_SKIP() << "Skipping stress test. Use --enable_stress_tests to run.";
    }
  }
};

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  helpers::init(&argc, &argv);

  ::testing::UnitTest::GetInstance()->listeners().Append(new StressTestFilter);
  BspTestEnvironment::GetInstance()->setPmUnitVersionDetector(
      &IdpromUtils::detectPmUnitVersions);
  ::testing::AddGlobalTestEnvironment(BspTestEnvironment::GetInstance());

  auto ret = RUN_ALL_TESTS();
  auto env = BspTestEnvironment::GetInstance();
  env->printAllRecordedErrors();
  return ret;
}
