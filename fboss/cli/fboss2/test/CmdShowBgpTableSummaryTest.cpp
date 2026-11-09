/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTableSummary.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp::thrift;
using facebook::neteng::fboss::bgp_attr::TBgpAfi;

namespace facebook::fboss {

namespace {
TRibSummary makeSummary(
    TBgpAfi afi,
    int64_t totalPrefixes,
    std::map<int16_t, int64_t> lengthCounts) {
  TRibSummary summary;
  summary.afi() = afi;
  summary.total_prefixes() = totalPrefixes;
  summary.prefix_length_counts() = std::move(lengthCounts);
  return summary;
}
} // namespace

class CmdShowBgpTableSummaryTestFixture : public CmdHandlerTestBase {
 public:
  TRibSummary v4_;
  TRibSummary v6_;
  std::optional<std::string> savedLcAll_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    // utils::Table (tabulate) constructs std::locale("") from the environment;
    // pin a valid locale so the test is hermetic where LANG/LC_* are unset.
    // Saved and restored in TearDown so it does not leak into sibling tests.
    if (const char* lcAll = std::getenv("LC_ALL")) {
      savedLcAll_ = lcAll;
    }
    setenv("LC_ALL", "C", 1);
    v4_ = makeSummary(TBgpAfi::AFI_IPV4, 5, {{24, 3}, {32, 2}});
    v4_.ebgp_prefixes() = 4;
    v4_.ibgp_prefixes() = 1;
    // RIB-wide count, identical across AFIs (as the server populates it).
    v4_.unresolvable_nexthops_count() = 3;
    // Per-AFI count of routes with no best path; set explicitly to exercise the
    // per-AFI rendering.
    v4_.routes_with_unresolved_nexthops() = 2;
    // Total paths exceed prefixes when add-path is in play (5 prefixes, 8
    // paths); set explicitly to exercise the per-AFI path rendering.
    v4_.total_paths() = 8;
    // A subset of total_paths was excluded from best-path selection, so the
    // active/inactive split is 5/3 rather than a trivial 8/0.
    v4_.inactive_paths() = 3;
    v6_ = makeSummary(TBgpAfi::AFI_IPV6, 1, {{64, 1}});
    v6_.ibgp_prefixes() = 1;
    v6_.unresolvable_nexthops_count() = 3;
    v6_.routes_with_unresolved_nexthops() = 0;
    v6_.total_paths() = 2;
    v6_.inactive_paths() = 0;
  }

  void TearDown() override {
    if (savedLcAll_.has_value()) {
      setenv("LC_ALL", savedLcAll_->c_str(), 1);
    } else {
      unsetenv("LC_ALL");
    }
    CmdHandlerTestBase::TearDown();
  }
};

TEST_F(CmdShowBgpTableSummaryTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRibSummary(_, TBgpAfi::AFI_IPV4))
      .WillOnce([&](TRibSummary& summary, TBgpAfi) { summary = v4_; });
  EXPECT_CALL(getMockBgp(), getRibSummary(_, TBgpAfi::AFI_IPV6))
      .WillOnce([&](TRibSummary& summary, TBgpAfi) { summary = v6_; });

  auto result = CmdShowBgpTableSummary().queryClient(localhost());
  ASSERT_EQ(2, result.summaries()->size());
  EXPECT_EQ(TBgpAfi::AFI_IPV4, result.summaries()->at(0).afi());
  EXPECT_EQ(5, result.summaries()->at(0).total_prefixes());
  EXPECT_EQ(TBgpAfi::AFI_IPV6, result.summaries()->at(1).afi());
  EXPECT_EQ(1, result.summaries()->at(1).total_prefixes());
  EXPECT_EQ(8, result.summaries()->at(0).total_paths());
  EXPECT_EQ(2, result.summaries()->at(1).total_paths());
  EXPECT_EQ(3, result.summaries()->at(0).inactive_paths());
  EXPECT_EQ(0, result.summaries()->at(1).inactive_paths());
}

TEST_F(CmdShowBgpTableSummaryTestFixture, printOutput) {
  cli::ShowBgpTableSummaryModel model;
  model.summaries() = {v4_, v6_};

  std::stringstream ss;
  CmdShowBgpTableSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Address Family: AFI_IPV4"));
  EXPECT_THAT(output, HasSubstr("Total Prefixes: 5"));
  // The active/inactive split is derived from total_paths - inactive_paths.
  EXPECT_THAT(output, HasSubstr("Total Paths: 8 (Active: 5, Inactive: 3)"));
  EXPECT_THAT(output, HasSubstr("External (eBGP): 4"));
  EXPECT_THAT(output, HasSubstr("Internal (iBGP): 1"));
  EXPECT_THAT(output, HasSubstr("Routes with unresolved next-hops: 2"));
  EXPECT_THAT(output, HasSubstr("/24"));
  EXPECT_THAT(output, HasSubstr("/32"));
  EXPECT_THAT(output, HasSubstr("Address Family: AFI_IPV6"));
  EXPECT_THAT(output, HasSubstr("Total Prefixes: 1"));
  EXPECT_THAT(output, HasSubstr("Total Paths: 2 (Active: 2, Inactive: 0)"));
  EXPECT_THAT(output, HasSubstr("/64"));

  // RIB-wide unresolvable count is rendered once.
  EXPECT_THAT(output, HasSubstr("Unresolvable next-hops: 3"));
}

/*
 * An older bgpd predating inactive_paths leaves the field unset. The CLI must
 * omit the split rather than deriving one from a default, which would report a
 * fully active RIB that was never actually measured.
 */
TEST_F(CmdShowBgpTableSummaryTestFixture, printOutputOmitsSplitWhenUnreported) {
  v4_.inactive_paths().reset();
  cli::ShowBgpTableSummaryModel model;
  model.summaries() = {v4_};

  std::stringstream ss;
  CmdShowBgpTableSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Total Paths: 8"));
  EXPECT_THAT(output, Not(HasSubstr("Active:")));
  EXPECT_THAT(output, Not(HasSubstr("Inactive:")));
}

/*
 * total_paths moves synchronously on announce/withdraw while inactive_paths is
 * only reconciled at the next best-path selection pass, so a summary sampled
 * between the two can report more inactive than total. Clamp the displayed
 * split to the subset invariant rather than printing either count as
 * impossible; the model retains the raw server value.
 */
TEST_F(CmdShowBgpTableSummaryTestFixture, printOutputClampsTransientSkew) {
  v4_.total_paths() = 8;
  v4_.inactive_paths() = 10;
  cli::ShowBgpTableSummaryModel model;
  model.summaries() = {v4_};

  std::stringstream ss;
  CmdShowBgpTableSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Total Paths: 8 (Active: 0, Inactive: 8)"));
  EXPECT_THAT(output, Not(HasSubstr("Active: -")));
  EXPECT_EQ(10, model.summaries()->front().inactive_paths().value());
}

/*
 * Treat all server-provided counts as untrusted. In particular, keep the
 * std::clamp upper bound nonnegative so malformed input cannot violate its
 * precondition, and preserve the raw model values for programmatic callers.
 */
TEST_F(CmdShowBgpTableSummaryTestFixture, printOutputClampsNegativeTotal) {
  v4_.total_paths() = -2;
  v4_.inactive_paths() = 1;
  cli::ShowBgpTableSummaryModel model;
  model.summaries() = {v4_};

  std::stringstream ss;
  CmdShowBgpTableSummary().printOutput(model, ss);
  std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Total Paths: 0 (Active: 0, Inactive: 0)"));
  EXPECT_EQ(-2, model.summaries()->front().total_paths().value());
  EXPECT_EQ(1, model.summaries()->front().inactive_paths().value());
}

TEST_F(CmdShowBgpTableSummaryTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpTableSummaryTraits::description().empty());
  auto model = CmdShowBgpTableSummary::sampleModel();
  EXPECT_FALSE(model.summaries()->empty());
  std::stringstream ss;
  CmdShowBgpTableSummary().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Address Family: AFI_IPV4"));
  EXPECT_THAT(output, HasSubstr("Address Family: AFI_IPV6"));
  EXPECT_THAT(
      output,
      HasSubstr(
          "Total Prefixes: 171  Total Paths: 969 (Active: 969, Inactive: 0)"));
  // The RIB-wide count is printed once, not once per address family.
  EXPECT_EQ(
      output.find("Unresolvable next-hops:"),
      output.rfind("Unresolvable next-hops:"));
}

} // namespace facebook::fboss
