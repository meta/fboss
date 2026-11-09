// Copyright (c) 2004-present, Meta Platforms, Inc. and affiliates.

#include "fboss/platform/platform_checks/checks/PowerResetCheck.h"

#include <ctime>
#include <iomanip>
#include <sstream>

#include <re2/re2.h>

namespace facebook::fboss::platform::platform_checks {

namespace power_reset_utils {

std::optional<std::chrono::system_clock::time_point> parseDateTime(
    const std::string& dateStr,
    const std::string& format) {
  struct tm tm = {};

  std::istringstream ss(dateStr);
  ss >> std::get_time(&tm, format.c_str());

  if (ss.fail()) {
    return std::nullopt;
  }

  // syslog-style timestamps (e.g. /var/log/secure) have no year. Assume the
  // most recent one that does not put the timestamp in the future.
  bool hasYear = format.find("%Y") != std::string::npos ||
      format.find("%y") != std::string::npos;
  if (!hasYear) {
    auto now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm nowTm{};
    gmtime_r(&now, &nowTm);
    tm.tm_year = nowTm.tm_year;
    struct tm candidate = tm;
    if (timegm(&candidate) > now + 24 * 60 * 60) {
      tm.tm_year -= 1;
    }
  }

  auto time = timegm(&tm);
  if (time == -1) {
    return std::nullopt;
  }

  return std::chrono::system_clock::from_time_t(time);
}

std::vector<std::string> grepLines(
    const std::string& content,
    const std::string& pattern) {
  std::vector<std::string> matchingLines;
  std::istringstream file(content);

  RE2::Options options;
  options.set_case_sensitive(false);
  RE2 regex(pattern, options);
  std::string line;

  while (std::getline(file, line)) {
    if (RE2::PartialMatch(line, regex)) {
      matchingLines.push_back(line);
    }
  }

  return matchingLines;
}

std::vector<std::string> filterLogsByDate(
    const std::vector<std::string>& logs,
    const std::string& timeFormat,
    std::chrono::seconds lookbackTime) {
  auto cutoffTime =
      std::chrono::system_clock::now() - std::chrono::seconds(lookbackTime);
  std::vector<std::string> filteredLogs;

  // Get time string length for parsing
  auto now = std::chrono::system_clock::now();
  auto nowTime = std::chrono::system_clock::to_time_t(now);
  struct tm tm_storage{};
  struct tm* tm_info = localtime_r(&nowTime, &tm_storage);
  char buffer[256];
  std::strftime(buffer, sizeof(buffer), timeFormat.c_str(), tm_info);
  size_t timeStrLen = std::strlen(buffer);

  for (const auto& log : logs) {
    if (log.length() < timeStrLen) {
      continue;
    }

    std::string dateStr = log.substr(0, timeStrLen);

    auto logTime = parseDateTime(dateStr, timeFormat);
    if (logTime && *logTime >= cutoffTime) {
      filteredLogs.push_back(log);
    }
  }

  return filteredLogs;
}

} // namespace power_reset_utils

// ============================================================================
// RecentManualRebootCheck Implementation
// ============================================================================

CheckResult RecentManualRebootCheck::run() {
  const std::string pattern = "System is rebooting";
  const std::string reason = "System was rebooted with `reboot` command";

  auto secureLog = host().readFile(VAR_SECURE_LOGFILE);
  if (!secureLog) {
    // Not every system logs to /var/log/secure; failing to read it is
    // different, e.g. a dropped connection to a remote host.
    if (host().exists(VAR_SECURE_LOGFILE)) {
      return makeError(
          std::string("Failed to read ") + VAR_SECURE_LOGFILE + " on " +
          host().name());
    }
    return makeOK();
  }
  auto rebootLogs = power_reset_utils::grepLines(*secureLog, pattern);
  auto filteredLogs = power_reset_utils::filterLogsByDate(
      rebootLogs, VAR_SECURE_DATE_FORMAT, std::chrono::hours(24)); // 1 day

  if (filteredLogs.empty()) {
    return makeOK();
  }

  std::ostringstream desc;
  desc << "\n" << reason << "\n";
  for (const auto& log : filteredLogs) {
    desc << " " << log << "\n";
  }

  return makeProblem(
      desc.str(), RemediationType::NONE, "Monitor for additional reboots");
}

// ============================================================================
// RecentKernelPanicCheck Implementation
// ============================================================================

CheckResult RecentKernelPanicCheck::run() {
  std::vector<std::string> recentPanics;
  auto cutoffTime =
      std::chrono::system_clock::now() - std::chrono::hours(24 * 7); // 7 days

  // Check if crash directory exists
  if (!host().exists(CRASH_DIR)) {
    return makeOK();
  }

  // Iterate through crash directory
  try {
    for (const auto& path : host().listDirectory(CRASH_DIR)) {
      std::string entryName = path.filename().string();

      // Extract just the timestamp portion (before timezone suffix like PST)
      // Format: "2017-10-19T01:15:29PST" -> "2017-10-19T01:15:29"
      std::string dateStr =
          entryName.substr(0, 19); // Length of "YYYY-MM-DDTHH:MM:SS"

      auto entryTime =
          power_reset_utils::parseDateTime(dateStr, PROCESSED_DIR_DATE_FORMAT);
      if (entryTime && *entryTime >= cutoffTime) {
        recentPanics.push_back(entryName);
      }
    }
  } catch (const std::exception& e) {
    return makeError(
        std::string("Failed to read crash directory: ") + e.what());
  }

  if (recentPanics.empty()) {
    return makeOK();
  }

  std::string reason =
      std::string("Recent kernel panic") + (recentPanics.size() > 1 ? "s" : "");
  std::ostringstream desc;
  desc << "\n" << reason << "\n";
  for (const auto& panic : recentPanics) {
    desc << " " << panic << "\n";
  }

  return makeProblem(
      desc.str(),
      RemediationType::MANUAL_REMEDIATION,
      "Investigate the kernel panics by looking at the logs in the /var/crash/processed directory.");
}

// ============================================================================
// WatchdogDidNotStopCheck Implementation
// ============================================================================

CheckResult WatchdogDidNotStopCheck::run() {
  // Get dmesg output with timestamps
  auto dmesg = host().run("dmesg -T");

  if (!dmesg.ok()) {
    // Unable to read dmesg, skip this check
    return makeError("Unable to read dmesg output");
  }

  // Split into lines
  std::vector<std::string> dmesgLines;
  std::istringstream iss(dmesg.standardOut);
  std::string line;
  while (std::getline(iss, line)) {
    dmesgLines.push_back(line);
  }

  // Filter for watchdog logs
  std::vector<std::string> watchdogLogs;
  RE2::Options options;
  options.set_case_sensitive(false);
  RE2 watchdogRegex("watchdog did not stop", options);

  for (const auto& logLine : dmesgLines) {
    if (RE2::PartialMatch(logLine, watchdogRegex)) {
      watchdogLogs.push_back(logLine);
    }
  }

  // Filter by date (last 3 hours)
  auto filteredLogs = power_reset_utils::filterLogsByDate(
      watchdogLogs, DMESG_DATE_FORMAT, std::chrono::hours(3));

  size_t logCount = filteredLogs.size();
  if (logCount == 0) {
    return makeOK();
  }

  if (logCount <= WATCHDOG_LOG_WARNING_LIMIT) {
    std::ostringstream desc;
    desc << "Watchdog did not stop " << logCount
         << " time(s) in the last 3 hours. "
         << "This usually indicates someone manually kicked the watchdog. "
         << "Continue monitoring the system to see if the issue persists.";
    return makeProblem(
        desc.str(), RemediationType::NONE, "Continue monitoring the system");
  }

  return makeProblem(
      "Watchdog did not stop. This could indicate a process managing watchdog "
      "(like fan_service in fboss switch) exited without closing the watchdog in the "
      "proper way (using MAGICCLOSE feature). If no one is kicking the watchdog "
      "before the watchdog timer counts down, it can start its corrective "
      "action (like running fan at high speed for fan_watchdog).",
      RemediationType::MANUAL_REMEDIATION,
      "Investigate watchdog management processes and ensure proper shutdown");
}

} // namespace facebook::fboss::platform::platform_checks
