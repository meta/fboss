// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonBitmapReporting.h"

#include "fboss/lib/CommonUtils.h"

#include <gtest/gtest.h>

namespace facebook::fboss {

// Drop bitmaps are READ_AND_CLEAR: each HwAgent stats-collection cycle reads
// and clears the HW bitmap and pushes that one snapshot to the SwSwitch cache
// these tests read, so a single drop is easy to miss — especially post-warmboot
// while the HwAgent->SwSwitch stats sink is re-establishing. Each verify loop
// below re-sends the drop-triggering traffic on every WITH_RETRIES iteration
// (the ~1s collection interval) and OR-accumulates: a miss in one collection
// window is covered by the next, and the first observed drop is latched.

BitmapDropReasonReportingImpl::BitmapDropReasonReportingImpl(
    DropReasonReportingContext context)
    : context_(std::move(context)) {}

// OR-accumulate src into dst. Used to aggregate across switch indices
// and across retry iterations (bitmaps are READ_AND_CLEAR, so drops
// observed in earlier reads must be preserved).
void BitmapDropReasonReportingImpl::orBitmapStats(
    HwSwitchDropBitmapStats& dst,
    const HwSwitchDropBitmapStats& src) {
#define OR_BITMAP(field) \
  dst.field() = dst.field().value_or(0) | src.field().value_or(0);

  OR_BITMAP(ingressMacDropBitmap);
  OR_BITMAP(egressPipeDropBitmap);
  OR_BITMAP(egressMacDropBitmap);
  OR_BITMAP(pipelineGeneralDropBitmap);
  OR_BITMAP(pipelineMacDropBitmap);
  OR_BITMAP(pipelineMplsDropBitmap);
  OR_BITMAP(pipelineTunnelDropBitmap);
  OR_BITMAP(pipelineIpv4DropBitmap);
  OR_BITMAP(pipelineIpv6DropBitmap);
  OR_BITMAP(bufferDropBitmap);
  OR_BITMAP(queueDropBitmap);
  OR_BITMAP(hostDropBitmap);
#undef OR_BITMAP
}

HwSwitchDropBitmapStats
BitmapDropReasonReportingImpl::getAggregatedSwitchDropBitmapStats() {
  HwSwitchDropBitmapStats result;
  checkWithRetry([&result, this]() {
    auto switchIndex2Stats = context_.readSwitchStats();
    bool complete = true;
    for (const auto& switchIndex : context_.switchIndices) {
      auto it = switchIndex2Stats.find(switchIndex);
      if (it == switchIndex2Stats.end()) {
        complete = false;
        continue;
      }
      // OR into the persistent result (not a per-iteration local): a
      // concurrent READ_AND_CLEAR collection may consume a switch's drops
      // between retries, so a partial snapshot seen on an earlier retry
      // must not be discarded or those drops are lost permanently.
      orBitmapStats(result, *it->second.switchDropBitmapStats());
    }
    return complete;
  });
  return result;
}

// True if any drop bitmap field is non-zero.
bool BitmapDropReasonReportingImpl::hasAnyDrop(
    const HwSwitchDropBitmapStats& s) {
  return (s.ingressMacDropBitmap().value_or(0) |
          s.egressPipeDropBitmap().value_or(0) |
          s.egressMacDropBitmap().value_or(0) |
          s.pipelineGeneralDropBitmap().value_or(0) |
          s.pipelineMacDropBitmap().value_or(0) |
          s.pipelineMplsDropBitmap().value_or(0) |
          s.pipelineTunnelDropBitmap().value_or(0) |
          s.pipelineIpv4DropBitmap().value_or(0) |
          s.pipelineIpv6DropBitmap().value_or(0) |
          s.bufferDropBitmap().value_or(0) | s.queueDropBitmap().value_or(0) |
          s.hostDropBitmap().value_or(0)) != 0;
}

// Drain the drop bitmaps before a capture phase so a stale drop (from a prior
// phase or warm-boot iteration) isn't OR-latched into the next phase. A zero
// read alone is ambiguous — value_or(0) can't tell a real zero from stats not
// flowing yet — so require a few consecutive snapshots that are both fresh
// (per-switch timestamp advancing) and all-zero.
void BitmapDropReasonReportingImpl::waitForDropBitmapsToClear() {
  constexpr int kRequiredConsecutiveClearReads = 3;
  auto prevStats = context_.readSwitchStats();
  int consecutiveClearReads = 0;
  WITH_RETRIES({
    auto curStats = context_.readSwitchStats();
    bool freshAndClear = true;
    for (const auto& switchIndex : context_.switchIndices) {
      auto curIt = curStats.find(switchIndex);
      auto prevIt = prevStats.find(switchIndex);
      if (curIt == curStats.end() || prevIt == prevStats.end()) {
        freshAndClear = false;
        continue;
      }
      if (curIt->second.timestamp().value() <=
              prevIt->second.timestamp().value() ||
          hasAnyDrop(*curIt->second.switchDropBitmapStats())) {
        freshAndClear = false;
      }
    }
    prevStats = curStats;
    consecutiveClearReads = freshAndClear ? consecutiveClearReads + 1 : 0;
    EXPECT_EVENTUALLY_TRUE(
        consecutiveClearReads >= kRequiredConsecutiveClearReads);
  });
}

// Verify that only the expected drop reason fires and no other bitmap
// categories report unexpected drops. This catches regressions where a
// code or config change causes collateral drops in unrelated stages.
void BitmapDropReasonReportingImpl::verifyOnlyExpectedDrop(
    const HwSwitchDropBitmapStats& stats,
    DropBitmapField expectedField,
    int expectedBit,
    const char* desc) const {
  auto getBitmap = [&](DropBitmapField f) -> int64_t {
    switch (f) {
      case DropBitmapField::INGRESS_MAC:
        return stats.ingressMacDropBitmap().value_or(0);
      case DropBitmapField::EGRESS_PIPE:
        return stats.egressPipeDropBitmap().value_or(0);
      case DropBitmapField::EGRESS_MAC:
        return stats.egressMacDropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_GENERAL:
        return stats.pipelineGeneralDropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_MAC:
        return stats.pipelineMacDropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_MPLS:
        return stats.pipelineMplsDropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_TUNNEL:
        return stats.pipelineTunnelDropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_IPV4:
        return stats.pipelineIpv4DropBitmap().value_or(0);
      case DropBitmapField::PIPELINE_IPV6:
        return stats.pipelineIpv6DropBitmap().value_or(0);
      case DropBitmapField::BUFFER:
        return stats.bufferDropBitmap().value_or(0);
      case DropBitmapField::QUEUE:
        return stats.queueDropBitmap().value_or(0);
      case DropBitmapField::HOST:
        return stats.hostDropBitmap().value_or(0);
    }
  };

  auto expected = getBitmap(expectedField);
  EXPECT_EQ(expected, 1LL << expectedBit)
      << desc << ": expected only bit " << expectedBit
      << " set in field, got: 0x" << std::hex << expected;

  for (auto f :
       {DropBitmapField::INGRESS_MAC,
        DropBitmapField::EGRESS_PIPE,
        DropBitmapField::EGRESS_MAC,
        DropBitmapField::PIPELINE_GENERAL,
        DropBitmapField::PIPELINE_MAC,
        DropBitmapField::PIPELINE_MPLS,
        DropBitmapField::PIPELINE_TUNNEL,
        DropBitmapField::PIPELINE_IPV4,
        DropBitmapField::PIPELINE_IPV6,
        DropBitmapField::BUFFER,
        DropBitmapField::QUEUE,
        DropBitmapField::HOST}) {
    if (f == expectedField) {
      continue;
    }
    EXPECT_EQ(getBitmap(f), 0)
        << desc << ": unexpected drop in other bitmap field, got: 0x"
        << std::hex << getBitmap(f);
  }
}

} // namespace facebook::fboss
