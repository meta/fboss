// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "fboss/platform/reboot_cause_finder/if/gen-cpp2/reboot_cause_config_types.h"

namespace facebook::fboss::platform::reboot_cause_finder {

// What could be read of the per-boot records reboot_cause_finder writes under
// REBOOT_HISTORY_DIR.
struct RebootHistory {
  // Newest boot first, by bootTimeMs; a record with no boot time sorts last.
  std::vector<reboot_cause_config::RebootCauseRecord> records;
  // Records that exist but were not returned: unreadable, oversized,
  // unparseable, or beyond the cap on how many are read.
  int32_t skippedCount{0};
  // Why anything was skipped or could not be read. Deduplicated and capped.
  std::vector<std::string> warnings;
};

// Fills history from historyDir. The directory is written by another process
// on its own schedule, so nothing found there -- absent, truncated, malformed,
// oversized, or too numerous -- is reported other than through history.
// Anything unexpected propagates; history keeps what was read before it, so a
// caller that catches can still return that.
void readRebootHistory(const std::string& historyDir, RebootHistory& history);

// Adds a warning unless it is already present or the cap is reached.
void addWarning(RebootHistory& history, const std::string& warning);

} // namespace facebook::fboss::platform::reboot_cause_finder
