// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/platform/reboot_cause_finder/RebootHistory.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

#include <fmt/format.h>
#include <folly/FileUtil.h>
#include <folly/json.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>

namespace facebook::fboss::platform::reboot_cause_finder {

namespace {
// A record is a few KiB; anything far larger is not one of ours and is not
// worth reading into memory.
constexpr uintmax_t kMaxRecordBytes = 1 << 20; // 1 MiB
// reboot_cause_finder writes one record per boot and never prunes, so the
// directory grows without bound. Read only the newest few.
constexpr size_t kMaxRecords = 64;
constexpr size_t kMaxWarnings = 8;
// Matches the filenames reboot_cause_finder writes:
// reboot-cause-<stamp>-<bootId>.json
constexpr std::string_view kRecordPrefix = "reboot-cause-";
constexpr std::string_view kRecordSuffix = ".json";

// Records written before the *Sec fields existed carry only the *Ms ones.
void fillSecFromMs(reboot_cause_config::RebootCause& cause) {
  if (*cause.occurredAtSec() == 0 && *cause.occurredAtMs() > 0) {
    cause.occurredAtSec() = *cause.occurredAtMs() / 1000;
  }
}

void fillSecFromMs(reboot_cause_config::RebootCauseRecord& record) {
  if (*record.bootTimeSec() == 0 && *record.bootTimeMs() > 0) {
    record.bootTimeSec() = *record.bootTimeMs() / 1000;
  }
  if (*record.detectedAtSec() == 0 && *record.detectedAtMs() > 0) {
    record.detectedAtSec() = *record.detectedAtMs() / 1000;
  }
  if (record.determinedCause()) {
    fillSecFromMs(*record.determinedCause()->cause());
  }
  for (auto& attempt : *record.providersAttempted()) {
    for (auto& cause : *attempt.causes()) {
      fillSecFromMs(cause);
    }
  }
}

} // namespace

void addWarning(RebootHistory& history, const std::string& warning) {
  auto& warnings = history.warnings;
  if (warnings.size() >= kMaxWarnings) {
    return;
  }
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(warning);
  }
}

void readRebootHistory(
    const std::string& historyDirPath,
    RebootHistory& history) {
  namespace fs = std::filesystem;
  const fs::path historyDir(historyDirPath);

  std::error_code ec;
  if (!fs::is_directory(historyDir, ec) || ec) {
    // Expected on any switch that has not yet run reboot_cause_finder.
    addWarning(
        history,
        fmt::format("No reboot history directory at '{}'", historyDirPath));
    return;
  }

  // Collect paths first so they can be ordered before any file is read, and
  // the read capped to kMaxRecords.
  //
  // `ec` has to be tested after construction and after each increment: on
  // failure the iterator is reset to end, so the loop condition would
  // otherwise exit first and a partial scan would look like a complete one.
  std::vector<fs::path> paths;
  fs::directory_iterator it(historyDir, ec), end;
  if (ec) {
    addWarning(history, "Reboot history directory could not be read");
    return;
  }
  // Stepped by hand rather than with a range-for: increment() resets the
  // iterator to end on failure, so a for-loop would exit on the condition
  // before any check in the body could run, and a partial scan would look
  // like a complete one.
  while (it != end) {
    if (it->is_regular_file(ec) && !ec) {
      // Only our own records. Anything else dropped here is deliberately
      // not counted as skipped: it was never a record to begin with.
      const auto name = it->path().filename().string();
      if (name.starts_with(kRecordPrefix) && name.ends_with(kRecordSuffix)) {
        paths.push_back(it->path());
      }
    }
    it.increment(ec);
    if (ec) {
      addWarning(history, "Reboot history directory could not be fully read");
      break;
    }
  }

  // Filenames embed a local-time stamp, so a lexicographic sort is only a
  // heuristic for "newest" -- reboot_cause_finder may stamp a record before
  // NTP has corrected the clock. It is good enough to decide which files to
  // read when there are more than the cap; the ordering actually returned is
  // by bootTimeSec below.
  std::sort(paths.begin(), paths.end());
  if (paths.size() > kMaxRecords) {
    history.skippedCount += static_cast<int32_t>(paths.size() - kMaxRecords);
    addWarning(
        history,
        fmt::format(
            "More than {} records present; read the {} newest by filename",
            kMaxRecords,
            kMaxRecords));
    paths.erase(paths.begin(), paths.end() - kMaxRecords);
  }

  for (const auto& path : paths) {
    // Bound the read itself rather than stat-then-read: the file could grow
    // between the two. Reading one byte past the cap is what distinguishes
    // "exactly at the limit" from "truncated by it".
    std::string contents;
    if (!folly::readFile(path.c_str(), contents, kMaxRecordBytes + 1) ||
        contents.empty()) {
      history.skippedCount += 1;
      addWarning(history, "A record could not be read");
      continue;
    }
    if (contents.size() > kMaxRecordBytes) {
      history.skippedCount += 1;
      addWarning(history, "A record was too large to read");
      continue;
    }
    try {
      // Lenient so enums written as numbers, by finders older than the
      // switch to names, still read.
      reboot_cause_config::RebootCauseRecord record;
      facebook::thrift::from_dynamic(
          record,
          folly::parseJson(contents),
          facebook::thrift::dynamic_format::PORTABLE,
          facebook::thrift::format_adherence::LENIENT);
      fillSecFromMs(record);
      history.records.push_back(std::move(record));
    } catch (const std::exception& ex) {
      // A record still being written, or truncated by a full disk.
      history.skippedCount += 1;
      addWarning(
          history, fmt::format("A record could not be parsed: {}", ex.what()));
    }
  }

  // Newest boot first, like `last reboot`. Ordering by detectedAtSec would
  // move a boot whose cause was determined late (a manual or redo run) above
  // later boots. A record with no boot time sorts last.
  std::sort(
      history.records.begin(),
      history.records.end(),
      [](const auto& a, const auto& b) {
        if (*a.bootTimeSec() != *b.bootTimeSec()) {
          return *a.bootTimeSec() > *b.bootTimeSec();
        }
        return *a.detectedAtSec() > *b.detectedAtSec();
      });
}
} // namespace facebook::fboss::platform::reboot_cause_finder
