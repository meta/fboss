/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/platform/platform_checks/checks/PowerResetCheck.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/platform_checks/tests/MockHost.h"

using namespace ::testing;
using namespace facebook::fboss::platform;
using namespace facebook::fboss::platform::platform_checks;

// ============================================================================
// RecentKernelPanicCheck Tests
// ============================================================================

class RecentKernelPanicCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temporary directory for each test
    auto testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string uniqueName =
        std::string("test_crash_") + testInfo->test_suite_name() + "_" +
        testInfo->name() + "_" +
        std::to_string(
            std::chrono::system_clock::now().time_since_epoch().count());
    tmpDir_ = std::filesystem::temp_directory_path() / uniqueName;
    std::filesystem::create_directories(tmpDir_);

    check_ = std::make_unique<RecentKernelPanicCheck>(
        CheckTarget{.host = std::make_shared<LocalHost>(tmpDir_)});
  }

  void TearDown() override {
    std::filesystem::remove_all(tmpDir_);
  }

  std::filesystem::path tmpDir_;
  std::unique_ptr<RecentKernelPanicCheck> check_;
};

TEST_F(RecentKernelPanicCheckTest, NoCrashDirectory) {
  // Don't create /var/crash/processed in tmpDir
  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::RECENT_KERNEL_PANIC_CHECK);
}

TEST_F(RecentKernelPanicCheckTest, EmptyCrashDirectory) {
  // Create empty directory
  auto crashDir = tmpDir_ / "var" / "crash" / "processed";
  std::filesystem::create_directories(crashDir);

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::RECENT_KERNEL_PANIC_CHECK);
}

TEST_F(RecentKernelPanicCheckTest, RecentPanicsFound) {
  // Create crash directory with recent panic file
  auto crashDir = tmpDir_ / "var" / "crash" / "processed";
  std::filesystem::create_directories(crashDir);

  // Create file with recent timestamp
  auto now = std::chrono::system_clock::now();
  auto recentTime = now - std::chrono::hours(24); // 1 day ago
  auto time_t_recent = std::chrono::system_clock::to_time_t(recentTime);
  struct tm tm_storage{};
  struct tm* tm_info = gmtime_r(&time_t_recent, &tm_storage);

  char buffer[256];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  std::string recentFilename = std::string(buffer) + "PST";

  auto recentFile = crashDir / recentFilename;
  std::ofstream(recentFile).close();

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  EXPECT_EQ(*result.checkType(), CheckType::RECENT_KERNEL_PANIC_CHECK);
  EXPECT_EQ(*result.remediation(), RemediationType::MANUAL_REMEDIATION);
}

TEST_F(RecentKernelPanicCheckTest, OldPanicsIgnored) {
  // Create crash directory with old panic file
  auto crashDir = tmpDir_ / "var" / "crash" / "processed";
  std::filesystem::create_directories(crashDir);

  // Create file with old timestamp (>7 days ago)
  auto now = std::chrono::system_clock::now();
  auto oldTime = now - std::chrono::hours(24 * 10); // 10 days ago
  auto time_t_old = std::chrono::system_clock::to_time_t(oldTime);
  struct tm tm_storage{};
  struct tm* tm_info = gmtime_r(&time_t_old, &tm_storage);

  char buffer[256];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", tm_info);
  std::string oldFilename = std::string(buffer) + "PST";

  auto oldFile = crashDir / oldFilename;
  std::ofstream(oldFile).close();

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::RECENT_KERNEL_PANIC_CHECK);
}

// ============================================================================
// WatchdogDidNotStopCheck Tests
// ============================================================================

class WatchdogDidNotStopCheckTest : public ::testing::Test {
 protected:
  void SetUp() override {
    host_ = std::make_shared<MockHost>();
    check_ =
        std::make_unique<WatchdogDidNotStopCheck>(CheckTarget{.host = host_});
  }

  void expectDmesg(int exitCode, const std::string& output) {
    EXPECT_CALL(*host_, run("dmesg -T", _))
        .WillOnce(
            Return(CommandResult{.exitCode = exitCode, .standardOut = output}));
  }

  std::shared_ptr<MockHost> host_;
  std::unique_ptr<WatchdogDidNotStopCheck> check_;
};

TEST_F(WatchdogDidNotStopCheckTest, DmesgCommandFails) {
  expectDmesg(1, "");

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::ERROR);
  EXPECT_EQ(*result.checkType(), CheckType::WATCHDOG_DID_NOT_STOP_CHECK);
}

TEST_F(WatchdogDidNotStopCheckTest, NoWatchdogLogs) {
  std::string dmesgOutput =
      "[Mon Oct 21 10:00:00 2024] Normal log entry\n"
      "[Mon Oct 21 10:01:00 2024] Another normal entry\n";

  expectDmesg(0, dmesgOutput);

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::WATCHDOG_DID_NOT_STOP_CHECK);
}

TEST_F(WatchdogDidNotStopCheckTest, FewWatchdogLogsWarning) {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  struct tm tm_storage{};
  struct tm* tm_info = gmtime_r(&time_t_now, &tm_storage);

  char buffer[256];
  std::strftime(buffer, sizeof(buffer), "[%a %b %d %H:%M:%S %Y]", tm_info);

  std::string dmesgOutput = std::string(buffer) + " watchdog did not stop\n" +
      std::string(buffer) + " watchdog did not stop\n";

  expectDmesg(0, dmesgOutput);

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  ASSERT_TRUE(result.remediation().has_value());
  EXPECT_EQ(*result.remediation(), RemediationType::NONE);
  EXPECT_EQ(*result.checkType(), CheckType::WATCHDOG_DID_NOT_STOP_CHECK);
}

TEST_F(WatchdogDidNotStopCheckTest, ManyWatchdogLogsError) {
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  struct tm tm_storage{};
  struct tm* tm_info = gmtime_r(&time_t_now, &tm_storage);

  char buffer[256];
  std::strftime(buffer, sizeof(buffer), "[%a %b %d %H:%M:%S %Y]", tm_info);

  std::string dmesgOutput;
  for (int i = 0; i < 5; i++) {
    dmesgOutput += std::string(buffer) + " watchdog did not stop\n";
  }

  expectDmesg(0, dmesgOutput);

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
  ASSERT_TRUE(result.remediation().has_value());
  EXPECT_EQ(*result.remediation(), RemediationType::MANUAL_REMEDIATION);
  EXPECT_EQ(*result.checkType(), CheckType::WATCHDOG_DID_NOT_STOP_CHECK);
}

TEST_F(WatchdogDidNotStopCheckTest, OldWatchdogLogsIgnored) {
  // Create logs from 5 hours ago (outside 3-hour window)
  auto oldTime = std::chrono::system_clock::now() - std::chrono::hours(5);
  auto time_t_old = std::chrono::system_clock::to_time_t(oldTime);
  struct tm tm_storage{};
  struct tm* tm_info = gmtime_r(&time_t_old, &tm_storage);

  char buffer[256];
  std::strftime(buffer, sizeof(buffer), "[%a %b %d %H:%M:%S %Y]", tm_info);

  std::string dmesgOutput;
  for (int i = 0; i < 5; i++) {
    dmesgOutput += std::string(buffer) + " watchdog did not stop\n";
  }

  expectDmesg(0, dmesgOutput);

  auto result = check_->run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
  EXPECT_EQ(*result.checkType(), CheckType::WATCHDOG_DID_NOT_STOP_CHECK);
}

// ============================================================================
// RecentManualRebootCheck Tests
// ============================================================================

TEST(RecentManualRebootCheckTest, UnreadableLogFileIsError) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, readFile(std::filesystem::path("/var/log/secure")))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*host, exists(std::filesystem::path("/var/log/secure")))
      .WillOnce(Return(true));
  RecentManualRebootCheck check(CheckTarget{.host = host});

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::ERROR);
}

TEST(RecentManualRebootCheckTest, MissingLogFileIsOk) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, readFile(std::filesystem::path("/var/log/secure")))
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(*host, exists(std::filesystem::path("/var/log/secure")))
      .WillOnce(Return(false));
  RecentManualRebootCheck check(CheckTarget{.host = host});

  auto result = check.run();

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

namespace {

std::string secureLogTimestamp(std::chrono::system_clock::time_point time) {
  auto timeT = std::chrono::system_clock::to_time_t(time);
  struct tm tmStorage{};
  gmtime_r(&timeT, &tmStorage);
  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S", &tmStorage);
  return buffer;
}

CheckResult runManualRebootCheck(const std::string& secureLog) {
  auto host = std::make_shared<MockHost>();
  EXPECT_CALL(*host, readFile(std::filesystem::path("/var/log/secure")))
      .WillOnce(Return(secureLog));
  return RecentManualRebootCheck(CheckTarget{.host = host}).run();
}

} // namespace

TEST(RecentManualRebootCheckTest, RecentRebootIsReported) {
  auto line = secureLogTimestamp(
                  std::chrono::system_clock::now() - std::chrono::hours(1)) +
      " rsw1 systemd-logind[1]: System is rebooting.\n";

  auto result = runManualRebootCheck(line);

  EXPECT_EQ(*result.status(), CheckStatus::PROBLEM);
}

TEST(RecentManualRebootCheckTest, OldRebootIsIgnored) {
  auto line = secureLogTimestamp(
                  std::chrono::system_clock::now() - std::chrono::hours(72)) +
      " rsw1 systemd-logind[1]: System is rebooting.\n";

  auto result = runManualRebootCheck(line);

  EXPECT_EQ(*result.status(), CheckStatus::OK);
}

TEST(PowerResetUtilsTest, YearlessTimestampAfterNowIsLastYear) {
  // A yearless timestamp two days ahead can only be from last year.
  auto future = std::chrono::system_clock::now() + std::chrono::hours(48);

  auto parsed = power_reset_utils::parseDateTime(
      secureLogTimestamp(future), "%b %d %H:%M:%S");

  ASSERT_TRUE(parsed.has_value());
  EXPECT_LT(*parsed, std::chrono::system_clock::now());
}
