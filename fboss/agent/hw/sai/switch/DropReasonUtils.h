// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::fboss {

// Helpers shared by the per ASIC drop reason reporting paths. Vendors expose
// active drop reasons differently -- Chenab as per pipeline stage bitmaps,
// Broadcom XGS as flat lists of enum values -- but both end up turning
// stringified SDK enumerators into human readable log lines. Sharing that here
// keeps the log format identical across ASICs.

// Strip `prefix` and `suffix` from a stringified SDK enumerator, along with any
// " = <value>" tail left behind by stringifying an enum entry. Returns empty
// for a null input.
std::string_view extractDropReasonName(
    const char* raw,
    std::string_view prefix,
    std::string_view suffix);

// Join names with ", " into lines of at most a fixed length, so a long list
// wraps across lines instead of being truncated. A name longer than the cap
// gets its own line and overruns it. The string_view overload is for names
// pointing into the SDK enumerator tables, which outlive the call.
std::vector<std::string> formatDropReasonLines(
    const std::vector<std::string>& names);
std::vector<std::string> formatDropReasonLines(
    const std::vector<std::string_view>& names);

std::vector<std::string> formatDropReasonLogLines(
    std::string_view stage,
    const std::vector<std::string>& names,
    std::optional<uint64_t> bitmap = std::nullopt);

// Return individual reason names, without formatting. enumNames is indexed
// by bit position. Bits past its end, or past the bitmap width, are ignored.
std::vector<std::string> decodeDropBitmap(
    int64_t bitmap,
    std::span<const char* const> enumNames,
    std::string_view prefix,
    std::string_view suffix);

} // namespace facebook::fboss
