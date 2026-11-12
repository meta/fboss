// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "fboss/agent/test/utils/AgentDropReasonReporting.h"

#include "fboss/agent/FbossError.h"

#include <gtest/gtest.h>

namespace facebook::fboss {
namespace {

TEST(DropReasonReportingTest, accumulatesWrappedLinesAndResetsCaptureWindow) {
  std::vector<std::string> logs{"prefix DROP reasons ingress: RDROP"};
  DropReasonReportingContext context;
  context.readLogMessages = [&]() { return logs; };
  auto reporting = createDropReasonReportingImpl(
      cfg::AsicType::ASIC_TYPE_TOMAHAWK6, context);
  reporting->collectDropObservation();
  EXPECT_FALSE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
  logs = {"prefix DROP reasons ingress: SRC_ROUTE_DROP, SRC_ROUTE_DROP"};
  reporting->collectDropObservation();
  EXPECT_TRUE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
  reporting->verifyOnlyExpectedDropReasonReported(DropScenario::MacSaMulticast);
  reporting->prepareDropObservation();
  EXPECT_FALSE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
  reporting->collectDropObservation();
  EXPECT_TRUE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
}

TEST(DropReasonReportingTest, retainsPartialBitmapObservationAcrossAgents) {
  for (const uint16_t presentIndex : {0, 1}) {
    int reads = 0;
    DropReasonReportingContext context;
    context.switchIndices = {0, 1};
    context.readSwitchStats = [&]() {
      std::map<uint16_t, multiswitch::HwSwitchStats> stats;
      if (++reads == 1) {
        // The next stats cycle clears this bit before the missing agent
        // arrives.
        stats.emplace(presentIndex, multiswitch::HwSwitchStats{});
        stats.at(presentIndex)
            .switchDropBitmapStats()
            ->pipelineMacDropBitmap() = 0x800;
      } else {
        stats = {{0, {}}, {1, {}}};
      }
      return stats;
    };
    auto reporting = createBitmapDropReasonReportingImpl(context);
    reporting->collectDropObservation();
    EXPECT_TRUE(
        reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast))
        << "Missing drop from switch index " << presentIndex;
    reporting->verifyOnlyExpectedDropReasonReported(
        DropScenario::MacSaMulticast);
  }
}

TEST(
    DropReasonReportingTest,
    drainsStaleBitmapsAndRequiresFreshClearSnapshots) {
  int64_t timestamp = 0;
  int reads = 0;
  int staleReads = 0;
  bool inject = true;
  DropReasonReportingContext context;
  context.switchIndices = {0};
  context.readSwitchStats = [&]() {
    multiswitch::HwSwitchStats stats;
    ++reads;
    if (staleReads > 0) {
      --staleReads;
      stats.timestamp() = timestamp;
    } else {
      stats.timestamp() = ++timestamp;
    }
    if (inject) {
      stats.switchDropBitmapStats()->pipelineMacDropBitmap() = 0x800;
    }
    return std::map<uint16_t, multiswitch::HwSwitchStats>{{0, stats}};
  };
  auto reporting = createBitmapDropReasonReportingImpl(context);
  reporting->collectDropObservation();
  ASSERT_TRUE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
  inject = false;
  // Baseline plus two clear-but-stale reads must not count as fresh clears.
  staleReads = 3;
  const auto beforeDrain = reads;
  reporting->prepareDropObservation();
  EXPECT_GE(reads - beforeDrain, 6);
  EXPECT_FALSE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
  inject = true;
  reporting->collectDropObservation();
  EXPECT_TRUE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
}

TEST(DropReasonReportingTest, selectsObservationSourceForSupportedAsics) {
  for (const auto asicType :
       {cfg::AsicType::ASIC_TYPE_TOMAHAWK5,
        cfg::AsicType::ASIC_TYPE_TOMAHAWK6,
        cfg::AsicType::ASIC_TYPE_CHENAB,
        cfg::AsicType::ASIC_TYPE_CHENAB2}) {
    int logReads = 0;
    int statsReads = 0;
    DropReasonReportingContext context;
    context.switchIndices = {0};
    context.readLogMessages = [&]() {
      ++logReads;
      return std::vector<std::string>{};
    };
    context.readSwitchStats = [&]() {
      ++statsReads;
      return std::map<uint16_t, multiswitch::HwSwitchStats>{{0, {}}};
    };
    auto reporting = createDropReasonReportingImpl(asicType, context);
    reporting->collectDropObservation();
    const bool usesBitmap = asicType == cfg::AsicType::ASIC_TYPE_CHENAB ||
        asicType == cfg::AsicType::ASIC_TYPE_CHENAB2;
    EXPECT_EQ(logReads > 0, !usesBitmap);
    EXPECT_EQ(statsReads > 0, usesBitmap);
  }
}

TEST(DropReasonReportingTest, rejectsUnsupportedAsics) {
  EXPECT_THROW(
      createDropReasonReportingImpl(cfg::AsicType::ASIC_TYPE_JERICHO3, {}),
      FbossError);
}

TEST(DropReasonReportingTest, observesExpectedReasonWithAdditionalBits) {
  multiswitch::HwSwitchStats stats;
  stats.switchDropBitmapStats()->pipelineMacDropBitmap() = 0x810;
  DropReasonReportingContext context;
  context.switchIndices = {0};
  context.readSwitchStats = [&]() {
    return std::map<uint16_t, multiswitch::HwSwitchStats>{{0, stats}};
  };
  auto reporting = createBitmapDropReasonReportingImpl(context);
  reporting->collectDropObservation();
  ASSERT_TRUE(
      reporting->expectedDropReasonObserved(DropScenario::MacSaMulticast));
}

} // namespace
} // namespace facebook::fboss
