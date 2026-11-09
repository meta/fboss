// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <fboss/fsdb/if/gen-cpp2/fsdb_oper_types.h>
#include <fmt/core.h>
#include <folly/Conv.h>
#include <thrift/lib/cpp2/FieldRef.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace facebook::fboss {

void dumpFsdbMetadata(const facebook::fboss::fsdb::OperMetadata& meta);

namespace fsdb_cli_format {

// Format an epoch-seconds timestamp as "YYYY-MM-DD HH:MM:SS" local time.
// Returns "--" if the input is non-positive.
inline std::string epochSecondsAsLocalTime(int64_t epochSeconds) {
  if (epochSeconds <= 0) {
    return "--";
  }
  time_t timestamp = static_cast<time_t>(epochSeconds);
  std::tm tm{};
  localtime_r(&timestamp, &tm);
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d %T");
  return oss.str();
}

// Format an epoch-milliseconds timestamp (FSDB's getCurrentTimeMs() unit).
// Returns "--" if the input is non-positive.
inline std::string epochMillisAsLocalTime(int64_t millis) {
  if (millis <= 0) {
    return "--";
  }
  return epochSecondsAsLocalTime(millis / 1000LL);
}

// Render "Xs ago" given a past event in epoch-milliseconds; "--" if unset
// or in the future.
inline std::string stalenessFromMillis(int64_t lastEventMillis) {
  if (lastEventMillis <= 0) {
    return "--";
  }
  auto nowMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  auto deltaSec = (nowMillis - lastEventMillis) / 1000LL;
  if (deltaSec < 0) {
    return "--";
  }
  return fmt::format("{}s ago", deltaSec);
}

// Render an optional thrift field as "--" when unset, else its value.
template <typename T>
inline std::string optFieldToString(
    const apache::thrift::optional_field_ref<const T&> field) {
  if (!field.has_value()) {
    return "--";
  }
  return folly::to<std::string>(field.value());
}

// An absent interval is the server's default cadence, not missing data.
inline std::string serveIntervalToString(
    apache::thrift::optional_field_ref<const int32_t&> field) {
  if (!field.has_value()) {
    return "default";
  }
  return fmt::format("{}s", *field);
}

} // namespace fsdb_cli_format

} // namespace facebook::fboss
