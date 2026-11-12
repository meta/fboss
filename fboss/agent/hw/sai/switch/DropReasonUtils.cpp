// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/switch/DropReasonUtils.h"

#include <bit>
#include <span>

#include <fmt/format.h>
#include <folly/logging/xlog.h>

namespace facebook::fboss {

namespace {
constexpr size_t kMaxReasonStringLen = 256;
constexpr std::string_view kSeparator = ", ";
} // namespace

std::string_view extractDropReasonName(
    const char* raw,
    std::string_view prefix,
    std::string_view suffix) {
  if (!raw) {
    return {};
  }
  std::string_view sv(raw);
  if (sv.starts_with(prefix)) {
    sv.remove_prefix(prefix.size());
  }
  auto eqPos = sv.find(" = ");
  if (eqPos != std::string_view::npos) {
    sv = sv.substr(0, eqPos);
  }
  if (sv.ends_with(suffix)) {
    sv.remove_suffix(suffix.size());
  }
  return sv;
}

namespace {
template <typename Names>
std::vector<std::string> formatLines(const Names& names) {
  std::vector<std::string> lines;
  std::string current;
  for (const auto& name : names) {
    if (name.empty()) {
      continue;
    }
    // Only wrap onto a non-empty line, so an oversized name overruns rather
    // than looping on a line it can never fit.
    if (!current.empty() &&
        current.size() + kSeparator.size() + name.size() >
            kMaxReasonStringLen) {
      lines.push_back(std::move(current));
      current.clear();
    }
    if (!current.empty()) {
      current += kSeparator;
    }
    current += name;
  }
  if (!current.empty()) {
    lines.push_back(std::move(current));
  }
  return lines;
}
} // namespace

std::vector<std::string> formatDropReasonLines(
    const std::vector<std::string>& names) {
  return formatLines(names);
}

std::vector<std::string> formatDropReasonLines(
    const std::vector<std::string_view>& names) {
  return formatLines(names);
}

std::vector<std::string> formatDropReasonLogLines(
    std::string_view stage,
    const std::vector<std::string>& names,
    std::optional<uint64_t> bitmap) {
  const auto header = bitmap.has_value()
      ? fmt::format("DROP reasons {}: ({:#x}) ", stage, *bitmap)
      : fmt::format("DROP reasons {}: ", stage);
  auto lines = formatDropReasonLines(names);
  for (auto& line : lines) {
    line.insert(0, header);
  }
  return lines;
}

std::vector<std::string> decodeDropBitmap(
    int64_t bitmap,
    std::span<const char* const> enumNames,
    std::string_view prefix,
    std::string_view suffix) {
  if (enumNames.empty()) {
    // Rate-limited: an empty table is a persistent programming error and
    // decodeDropBitmap is called once per bitmap field per collection cycle,
    // so cap logging to avoid flooding.
    XLOG_EVERY_MS(ERR, 60000)
        << "decodeDropBitmap called with an empty enum table";
    return {};
  }
  std::vector<std::string> names;
  auto bits = static_cast<uint64_t>(bitmap);
  while (bits != 0) {
    // Brian Kernighan's algorithm to find set bits in the bitmap.
    uint64_t rightmostSetBit = bits & -bits;
    const auto i = static_cast<size_t>(std::countr_zero(rightmostSetBit));
    bits -= rightmostSetBit;
    if (i >= enumNames.size()) {
      break;
    }
    auto name = extractDropReasonName(enumNames[i], prefix, suffix);
    if (!name.empty()) {
      names.emplace_back(name);
    }
  }
  return names;
}

} // namespace facebook::fboss
