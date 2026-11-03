// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "fboss/platform/platform_checks/PlatformCheck.h"

namespace facebook::fboss::platform::platform_checks {

// Helper functions shared across power reset checks
namespace power_reset_utils {

std::vector<std::string> filterLogsByDate(
    const std::vector<std::string>& logs,
    const std::string& timeFormat,
    std::chrono::seconds lookbackTime);

// Lines of `content` matching `pattern`, case-insensitively.
std::vector<std::string> grepLines(
    const std::string& content,
    const std::string& pattern);

std::optional<std::chrono::system_clock::time_point> parseDateTime(
    const std::string& dateStr,
    const std::string& format);

} // namespace power_reset_utils

/**
 * Check for recent manual reboots (within the last 1 day).
 */
class RecentManualRebootCheck : public PlatformCheck {
 public:
  explicit RecentManualRebootCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}
  ~RecentManualRebootCheck() override = default;

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::RECENT_MANUAL_REBOOT_CHECK;
  }

  std::string getName() const override {
    return "Recent Manual Reboot Check";
  }

  std::string getDescription() const override {
    return "Checks if a manual reboot has been performed recently (within last 1 day)";
  }

 private:
  static constexpr const char* VAR_SECURE_LOGFILE = "/var/log/secure";
  static constexpr const char* VAR_SECURE_DATE_FORMAT = "%b %d %H:%M:%S";
};

/**
 * Check for recent kernel panics (within the last 7 days).
 */
class RecentKernelPanicCheck : public PlatformCheck {
 public:
  explicit RecentKernelPanicCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}
  ~RecentKernelPanicCheck() override = default;

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::RECENT_KERNEL_PANIC_CHECK;
  }

  std::string getName() const override {
    return "Recent Kernel Panic Check";
  }

  std::string getDescription() const override {
    return "Checks if a kernel panic has occurred recently (within last 7 days)";
  }

 private:
  static constexpr const char* PROCESSED_DIR_DATE_FORMAT = "%Y-%m-%dT%H:%M:%S";
  static constexpr const char* CRASH_DIR = "/var/crash/processed";
};

/**
 * Check for watchdog failures (within the last 3 hours).
 */
class WatchdogDidNotStopCheck : public PlatformCheck {
 public:
  explicit WatchdogDidNotStopCheck(CheckTarget target = {})
      : PlatformCheck(std::move(target)) {}
  ~WatchdogDidNotStopCheck() override = default;

  CheckResult run() override;

  CheckType getType() const override {
    return CheckType::WATCHDOG_DID_NOT_STOP_CHECK;
  }

  std::string getName() const override {
    return "Watchdog Did Not Stop Check";
  }

  std::string getDescription() const override {
    return "Checks dmesg for logs indicating that a watchdog did not stop (within last 3 hours)";
  }

 private:
  static constexpr const char* DMESG_DATE_FORMAT = "[%a %b %d %H:%M:%S %Y]";
  static constexpr int WATCHDOG_LOG_WARNING_LIMIT = 2;
};

} // namespace facebook::fboss::platform::platform_checks
