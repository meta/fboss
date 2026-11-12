// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include "fboss/lib/CommonUtils.h"

#include <fmt/format.h>
#include <folly/String.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>

namespace facebook::fboss {

ListDropReasonReportingImpl::ListDropReasonReportingImpl(
    DropReasonReportingContext context)
    : context_(std::move(context)) {}

// Union src into dst. Reason lists are cleared on read, so reasons observed
// in an earlier collection must be preserved across retries.
void ListDropReasonReportingImpl::accumulate(
    DropReasons& dst,
    const DropReasons& src) {
  for (const auto& [stage, reasons] : src) {
    dst[stage].insert(reasons.begin(), reasons.end());
  }
}

// Pull the reason names out of a "DROP reasons <stage>: A, B, C" line.
void ListDropReasonReportingImpl::parseReasons(
    const std::string& message,
    std::string_view marker,
    std::set<std::string>& out) {
  auto pos = message.find(marker);
  if (pos == std::string::npos) {
    return;
  }
  // View into message rather than a substr() temporary: the pieces below
  // point back into whatever is split, so a temporary would dangle.
  std::string_view tail(message);
  tail.remove_prefix(pos + marker.size());
  std::vector<std::string_view> names;
  folly::split(", ", tail, names);
  for (auto name : names) {
    auto trimmed = folly::trimWhitespace(name);
    if (!trimmed.empty()) {
      out.emplace(trimmed);
    }
  }
}

ListDropReasonReportingImpl::DropReasons
ListDropReasonReportingImpl::getAggregatedDropReasons() {
  DropReasons result;
  const std::string marker = "DROP reasons ";
  for (const auto& message : context_.readLogMessages()) {
    const auto start = message.find(marker);
    if (start == std::string::npos) {
      continue;
    }
    const auto end = message.find(": ", start + marker.size());
    if (end == std::string::npos) {
      continue;
    }
    const auto stage =
        message.substr(start + marker.size(), end - start - marker.size());
    parseReasons(message, marker + stage + ": ", result[stage]);
  }
  return result;
}

bool ListDropReasonReportingImpl::expectedDropReasonsObserved(
    const DropReasons& reasons,
    const DropReasons& expected) {
  for (const auto& [stage, names] : expected) {
    const auto it = reasons.find(stage);
    if (it == reasons.end() ||
        !std::includes(
            it->second.begin(), it->second.end(), names.begin(), names.end())) {
      return false;
    }
  }
  return true;
}

std::string ListDropReasonReportingImpl::toString(const DropReasons& reasons) {
  std::vector<std::string> stages;
  stages.reserve(reasons.size());
  for (const auto& [stage, names] : reasons) {
    stages.push_back(fmt::format("{}=[{}]", stage, folly::join(", ", names)));
  }
  return folly::join(" ", stages);
}

// Assert every expected reason fired. Allow optional reasons and reject all
// other reasons across every stage.
void ListDropReasonReportingImpl::verifyOnlyExpectedReason(
    const DropReasons& reasons,
    const DropReasons& expected,
    const DropReasons& optional,
    const char* desc) const {
  auto observed = reasons;
  for (const auto& [stage, names] : optional) {
    for (const auto& name : names) {
      observed[stage].erase(name);
    }
  }
  std::erase_if(
      observed, [](const auto& entry) { return entry.second.empty(); });
  EXPECT_EQ(observed, expected)
      << desc << ": unexpected drop reasons: " << toString(reasons);
}

} // namespace facebook::fboss
