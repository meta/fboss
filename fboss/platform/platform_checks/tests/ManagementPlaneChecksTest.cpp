/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/ManagementPlaneChecks.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::Return;

class ManagementPlaneChecksTest : public ::testing::Test {
 protected:
  std::shared_ptr<MockHost> host_ = std::make_shared<MockHost>("rsw1-oob");
  CheckTarget target_{.host = host_};
};

TEST_F(ManagementPlaneChecksTest, PassesWhenCommandSucceeds) {
  EXPECT_CALL(*host_, run("ifconfig eth0.4088", _))
      .WillOnce(Return(CommandResult{.exitCode = 0, .standardOut = "UP\n"}));

  auto result = makeBmcX86LinkCheck(target_)->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::BMC_X86_LINK_CHECK);
  EXPECT_THAT(*result.details(), HasSubstr("[rsw1-oob]$ ifconfig eth0.4088"));
}

TEST_F(ManagementPlaneChecksTest, ProblemWhenCommandFails) {
  EXPECT_CALL(*host_, run("ipmitool mc info", _))
      .WillOnce(Return(
          CommandResult{
              .exitCode = 1, .standardErr = "Could not open device"}));

  auto result = makeIpmiMcInfoCheck(target_)->run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_THAT(*result.errorMessage(), HasSubstr("exited with code 1"));
  EXPECT_THAT(*result.details(), HasSubstr("Could not open device"));
}

TEST(ManagementPlaneChecksSkipTest, SkippedWithoutBmc) {
  auto check = makeBmcRestApiCheck(
      CheckTarget{.host = nullptr, .unavailableReason = "no BMC"});

  EXPECT_EQ(check->getSkipReason(), "no BMC");
}
