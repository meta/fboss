// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonListReporting.h"

#include "fboss/lib/CommonUtils.h"

#include <folly/String.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <set>

namespace facebook::fboss {

// Reasons are read back from the HwAgent log rather than from stats: they are
// decoded and logged in the process that owns the ASIC and never cross into
// SwSwitch. installLogCapture() resets the capture window, so each test starts
// from a clean slate, and the window then accumulates every reason the ASIC
// reported. The lists are cleared on read, so each verify loop below re-sends
// the drop triggering traffic on every WITH_RETRIES iteration -- a drop missed
// in one collection window is covered by the next.
namespace {

// Reasons the ASIC reports alongside a specific one rather than instead of
// it. RDROP is "Port bitmap zero drop condition", which comes up for
// essentially any dropped packet. It is deliberately still reported and
// logged, since it is a genuine drop indicator, but a test checking that
// nothing unexpected also fired has to discount it.
const std::set<std::string>& genericReasons() {
  static const std::set<std::string> reasons{"RDROP"};
  return reasons;
}

std::set<std::string> specificOnly(const std::set<std::string>& reasons) {
  std::set<std::string> out;
  std::set_difference(
      reasons.begin(),
      reasons.end(),
      genericReasons().begin(),
      genericReasons().end(),
      std::inserter(out, out.end()));
  return out;
}

} // namespace

ListDropReasonReportingImpl::ListDropReasonReportingImpl(
    DropReasonReportingContext context)
    : context_(std::move(context)) {}

// Union src into dst. Reason lists are cleared on read, so reasons observed
// in an earlier collection must be preserved across retries.
void ListDropReasonReportingImpl::accumulate(
    DropReasons& dst,
    const DropReasons& src) {
  dst.ingress.insert(src.ingress.begin(), src.ingress.end());
  dst.egress.insert(src.egress.begin(), src.egress.end());
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
  for (const auto& match : context_.readLogMessages()) {
    parseReasons(match, "DROP reasons ingress: ", result.ingress);
    parseReasons(match, "DROP reasons egress: ", result.egress);
  }
  return result;
}

std::string ListDropReasonReportingImpl::toString(const DropReasons& reasons) {
  return folly::to<std::string>(
      "ingress=[",
      folly::join(", ", reasons.ingress),
      "] egress=[",
      folly::join(", ", reasons.egress),
      "]");
}

// Assert the expected reason fired on the named stage and that nothing
// unexpected fired, on either stage. Generic reasons are discounted:
// they accompany any drop and say nothing about which one.
void ListDropReasonReportingImpl::verifyOnlyExpectedReason(
    const DropReasons& reasons,
    DropStage dropStage,
    const std::string& expected,
    const char* desc) {
  const auto& reported =
      dropStage == DropStage::Ingress ? reasons.ingress : reasons.egress;
  const auto& other =
      dropStage == DropStage::Ingress ? reasons.egress : reasons.ingress;
  EXPECT_TRUE(reported.contains(expected))
      << desc << ": expected reason " << expected << " not reported";
  auto unexpected = specificOnly(reported);
  unexpected.erase(expected);
  EXPECT_TRUE(unexpected.empty())
      << desc << ": unexpected drop reasons also reported: "
      << folly::join(", ", unexpected);
  auto otherStage = specificOnly(other);
  EXPECT_TRUE(otherStage.empty())
      << desc << ": unexpected drop reasons in the other stage: "
      << folly::join(", ", otherStage);
}

} // namespace facebook::fboss
