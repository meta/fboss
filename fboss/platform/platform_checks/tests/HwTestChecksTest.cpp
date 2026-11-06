/*
 *  Copyright (c) Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/HwTestChecks.h"

#include <folly/FileUtil.h>
#include <folly/testing/TestUtil.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace facebook::fboss::platform::platform_checks;
using ::testing::_;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::StartsWith;

namespace {

CommandResult ok(std::string out = "") {
  return CommandResult{.exitCode = 0, .standardOut = std::move(out)};
}

constexpr RebootTiming kNoWait{
    .settle = std::chrono::seconds(0),
    .pollInterval = std::chrono::seconds(0),
    .timeout = std::chrono::seconds(0)};

class HwTestCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    folly::writeFile(
        std::string("bin"), (testsDir_ / "weutil_hw_test").c_str());
  }

  folly::test::TemporaryDirectory tmpDir_;
  std::filesystem::path testsDir_{tmpDir_.path().string()};
  std::shared_ptr<MockHost> x86_ = std::make_shared<MockHost>("x86");
};

} // namespace

TEST_F(HwTestCheckTest, StopsServicesRunsTestAndRestartsServices) {
  {
    InSequence seq;
    EXPECT_CALL(*x86_, run(StartsWith("systemctl stop"), _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*x86_, run(StartsWith("mktemp -d"), _))
        .WillOnce(Return(ok("/tmp/fixmyfboss.abc123\n")));
    EXPECT_CALL(
        *x86_,
        copyTo(
            testsDir_ / "weutil_hw_test",
            std::filesystem::path("/tmp/fixmyfboss.abc123/weutil_hw_test"),
            _));
    EXPECT_CALL(
        *x86_,
        run(StartsWith("chmod 0700 '/tmp/fixmyfboss.abc123/weutil_hw_test'"),
            _))
        .WillOnce(Return(CommandResult{.exitCode = 1}));
    EXPECT_CALL(*x86_, run("rm -rf '/tmp/fixmyfboss.abc123'", _))
        .WillOnce(Return(ok()));
    EXPECT_CALL(*x86_, run(StartsWith("systemctl start"), _))
        .WillOnce(Return(ok()));
  }
  HwTestCheck check(
      CheckTarget{.host = x86_}, {.name = "weutil_hw_test"}, testsDir_);

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(*result.errorMessage(), "weutil_hw_test exited with code 1");
}

TEST_F(HwTestCheckTest, FailedRestartIsReported) {
  EXPECT_CALL(*x86_, run(StartsWith("systemctl stop"), _))
      .WillOnce(Return(ok()));
  EXPECT_CALL(*x86_, run(StartsWith("mktemp -d"), _))
      .WillOnce(Return(ok("/tmp/fixmyfboss.abc123\n")));
  EXPECT_CALL(*x86_, copyTo(_, _, _));
  EXPECT_CALL(*x86_, run(StartsWith("chmod"), _)).WillOnce(Return(ok()));
  EXPECT_CALL(*x86_, run(StartsWith("rm -rf"), _)).WillOnce(Return(ok()));
  EXPECT_CALL(*x86_, run(StartsWith("systemctl start"), _))
      .WillOnce(Return(CommandResult{.exitCode = 1}));
  HwTestCheck check(
      CheckTarget{.host = x86_}, {.name = "weutil_hw_test"}, testsDir_);

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(
      *result.errorMessage(),
      "Platform services did not restart after weutil_hw_test");
  EXPECT_THAT(*result.remediationMessage(), HasSubstr("systemctl start"));
}

TEST_F(HwTestCheckTest, NoStagingDirIsAnErrorAndServicesRestart) {
  EXPECT_CALL(*x86_, run(StartsWith("systemctl stop"), _))
      .WillOnce(Return(ok()));
  EXPECT_CALL(*x86_, run(StartsWith("mktemp -d"), _))
      .WillOnce(Return(CommandResult{.exitCode = 1}));
  EXPECT_CALL(*x86_, copyTo(_, _, _)).Times(0);
  EXPECT_CALL(*x86_, run(StartsWith("systemctl start"), _))
      .WillOnce(Return(ok()));
  HwTestCheck check(
      CheckTarget{.host = x86_}, {.name = "weutil_hw_test"}, testsDir_);

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::ERROR);
}

TEST_F(HwTestCheckTest, ThrowIsAnErrorWithLogAndServicesRestart) {
  EXPECT_CALL(*x86_, run(StartsWith("systemctl stop"), _))
      .WillOnce(Return(ok()));
  EXPECT_CALL(*x86_, run(StartsWith("mktemp -d"), _))
      .WillOnce(Return(ok("/tmp/fixmyfboss.abc123\n")));
  EXPECT_CALL(*x86_, copyTo(_, _, _));
  EXPECT_CALL(*x86_, run(StartsWith("chmod"), _))
      .WillOnce(::testing::Throw(std::runtime_error("forkpty failed")));
  EXPECT_CALL(*x86_, run(StartsWith("rm -rf"), _))
      .WillOnce(::testing::Throw(std::runtime_error("forkpty failed")));
  EXPECT_CALL(*x86_, run(StartsWith("systemctl start"), _))
      .WillOnce(Return(ok()));
  HwTestCheck check(
      CheckTarget{.host = x86_}, {.name = "weutil_hw_test"}, testsDir_);

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::ERROR);
  EXPECT_EQ(*result.errorMessage(), "forkpty failed");
  EXPECT_THAT(*result.details(), HasSubstr("systemctl stop"));
  EXPECT_THAT(
      *result.details(), HasSubstr("Could not remove /tmp/fixmyfboss.abc123"));
}

TEST_F(HwTestCheckTest, SkippedWithoutArtifacts) {
  HwTestCheck noDir(CheckTarget{.host = x86_}, {.name = "bsp_tests"}, {});
  HwTestCheck missing(
      CheckTarget{.host = x86_}, {.name = "bsp_tests"}, testsDir_);

  EXPECT_THAT(*noDir.getSkipReason(), HasSubstr("--hw-tests-dir"));
  EXPECT_THAT(*missing.getSkipReason(), HasSubstr("No bsp_tests in"));
}

TEST(CitRunPassedTest, UnittestSummary) {
  const std::string ran =
      "test_x (tests.t.T.test_x) ... ok\n"
      "----------------------------------------------------------------------\n"
      "Ran 89 tests in 224.881s\n\n";

  EXPECT_TRUE(citRunPassed({.exitCode = 0, .standardErr = ran + "OK\n"}));
  EXPECT_TRUE(
      citRunPassed({.exitCode = 0, .standardErr = ran + "OK (skipped=10)\n"}));
  EXPECT_FALSE(citRunPassed(
      {.exitCode = 0, .standardErr = ran + "FAILED (failures=1, errors=2)\n"}));
  EXPECT_FALSE(citRunPassed({.exitCode = 1, .standardErr = ran + "OK\n"}));
  EXPECT_FALSE(citRunPassed({.exitCode = 0, .standardErr = "Traceback ...\n"}));
}

TEST_F(HwTestCheckTest, CitSkippedWhenX86IsLocal) {
  std::filesystem::create_directories(testsDir_ / "tests2");
  BmcCitTestCheck check(
      CheckTarget{.host = std::make_shared<MockHost>("bmc")},
      CheckTarget{.host = std::make_shared<MockHost>("localhost", true)},
      testsDir_,
      "montblanc");

  EXPECT_THAT(*check.getSkipReason(), HasSubstr("--hostname"));
}

TEST_F(HwTestCheckTest, CitReportsX86DownEvenWhenTestsFailed) {
  std::filesystem::create_directories(testsDir_ / "tests2");
  auto bmc = std::make_shared<MockHost>("bmc");
  EXPECT_CALL(*bmc, run(StartsWith("rm -rf"), _)).WillOnce(Return(ok()));
  EXPECT_CALL(*bmc, copyTo(_, _, _));
  EXPECT_CALL(*bmc, run(HasSubstr("cit_runner.py"), _))
      .WillOnce(Return(
          CommandResult{
              .exitCode = 1, .standardErr = "FAILED (failures=1)\n"}));
  EXPECT_CALL(*x86_, run("true", _))
      .WillRepeatedly(Return(CommandResult{.exitCode = 255}));
  BmcCitTestCheck check(
      CheckTarget{.host = bmc},
      CheckTarget{.host = x86_},
      testsDir_,
      "montblanc",
      kNoWait);

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(
      *result.errorMessage(), "CIT tests failed and the x86 did not come back");
  EXPECT_THAT(*result.remediationMessage(), HasSubstr("wedge_power.sh on"));
}
