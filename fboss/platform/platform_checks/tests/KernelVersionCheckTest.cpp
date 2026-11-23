/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/KernelVersionCheck.h"

#include <sys/utsname.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace ::testing;
using namespace facebook::fboss::platform::platform_checks;

class MockKernelVersionCheck : public KernelVersionCheck {
 public:
  MOCK_METHOD(
      std::optional<std::vector<std::string>>,
      getSupportedKernelVersions,
      (),
      (override));
};

class KernelVersionCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    check_ = std::make_unique<MockKernelVersionCheck>();
  }

  std::string getCurrentKernelVersion() {
    struct utsname unameData{};
    if (uname(&unameData) == 0) {
      return std::string(unameData.release);
    }
    return "unknown";
  }

  std::unique_ptr<MockKernelVersionCheck> check_;
};

TEST_F(KernelVersionCheckTest, SupportedKernelVersion) {
  std::string currentKernel = getCurrentKernelVersion();
  std::vector<std::string> supportedVersions = {currentKernel};

  EXPECT_CALL(*check_, getSupportedKernelVersions())
      .WillOnce(Return(supportedVersions));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::KERNEL_VERSION_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::OK);
  EXPECT_THAT(*result.details(), HasSubstr("$ uname -r\n" + currentKernel));
}

TEST_F(KernelVersionCheckTest, UnsupportedKernelVersion) {
  std::vector<std::string> supportedVersions = {"1.0.0-noexist"};

  EXPECT_CALL(*check_, getSupportedKernelVersions())
      .WillOnce(Return(supportedVersions));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::KERNEL_VERSION_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(result.remediation(), RemediationType::REPROVISION_REQUIRED);
}

TEST_F(KernelVersionCheckTest, FailedToGetSupportedVersions) {
  EXPECT_CALL(*check_, getSupportedKernelVersions())
      .WillOnce(Return(std::nullopt));

  auto result = check_->run();

  EXPECT_EQ(result.checkType(), CheckType::KERNEL_VERSION_CHECK);
  EXPECT_EQ(result.status(), CheckStatus::ERROR);
}
