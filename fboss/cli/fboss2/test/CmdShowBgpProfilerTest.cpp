/*
 *  Copyright (c) 2004-present, Meta Platforms, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/cli/fboss2/commands/show/bgp/profiler/CmdShowBgpProfiler.h"

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TBgpProfilerStat;

namespace facebook::fboss {

TEST(CmdShowBgpProfilerTest, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpProfilerTraits::description().empty());

  /*
   * printOutput() is defined in CmdShowBgpProfiler.cpp, which belongs only to
   * the fboss2-routing-protocol target that neither test target links, so the
   * sample is asserted directly rather than rendered.
   *
   * The assertions are invariants rather than echoes of the sample's literals:
   * every row must be internally coherent, and the set must span the readings
   * description() contrasts - a hot cheap function and one whose tail is far
   * above its median.
   */
  const auto model = CmdShowBgpProfiler::sampleModel();
  ASSERT_FALSE(model.empty());

  for (const auto& stat : model) {
    EXPECT_FALSE(stat.name()->empty());
    EXPECT_GT(*stat.count(), 0);
    // Percentiles are monotonic in every row, so a transposed column shows up.
    EXPECT_LE(*stat.p50_ms(), *stat.p90_ms());
    EXPECT_LE(*stat.p90_ms(), *stat.p99_ms());
    EXPECT_LE(*stat.p99_ms(), *stat.max_ms());
    // A row cannot spend less time in total than its slowest single call.
    EXPECT_GE(*stat.total_ms(), *stat.max_ms());
  }

  const bool hasHotCheapRow =
      std::any_of(model.begin(), model.end(), [](const TBgpProfilerStat& stat) {
        return *stat.count() > 1000 && *stat.p99_ms() <= 5;
      });
  // Requires a non-zero p50: with p50 == 0 the ratio holds for any row at all,
  // so the intended stall row could be removed without failing this.
  const bool hasTailStallRow =
      std::any_of(model.begin(), model.end(), [](const TBgpProfilerStat& stat) {
        return *stat.p50_ms() > 0 && *stat.p99_ms() > *stat.p50_ms() * 10;
      });
  EXPECT_TRUE(hasHotCheapRow);
  EXPECT_TRUE(hasTailStallRow);
}

} // namespace facebook::fboss
