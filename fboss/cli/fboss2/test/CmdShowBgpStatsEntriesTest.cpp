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
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsEntries.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TEntryStats;
namespace facebook::fboss {

const int kTotalUcastRoutes = 30;
const int kTotalRibPaths = 20;
const int kTotalAdjRibs = 15;
const int kTotalOriginatedRoutes = 2;
const int kTotalShadowRibEntries = 10;
const int kTotalNetlinkInterfaces = 32;
const int kTotalNetlinkHoldsActive = 3;

class CmdShowBgpStatsEntriesTestFixture : public CmdHandlerTestBase {
 public:
  TEntryStats stats_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    stats_ = getStats();
  }

  TEntryStats getStats() {
    TEntryStats queriedStats;

    queriedStats.total_ucast_routes() = kTotalUcastRoutes;
    queriedStats.total_rib_paths() = kTotalRibPaths;
    queriedStats.total_adj_ribs() = kTotalAdjRibs;
    queriedStats.total_originated_routes() = kTotalOriginatedRoutes;
    queriedStats.total_shadow_rib_entries() = kTotalShadowRibEntries;
    queriedStats.total_netlink_wrapper_interfaces() = kTotalNetlinkInterfaces;
    queriedStats.total_netlink_wrapper_holds_active() =
        kTotalNetlinkHoldsActive;

    return queriedStats;
  }
};

TEST_F(CmdShowBgpStatsEntriesTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getEntryStats(_))
      .WillOnce(Invoke([&](auto& entries) { entries = stats_; }));

  auto results = CmdShowBgpStatsEntries().queryClient(localhost());
  EXPECT_EQ(kTotalUcastRoutes, results.total_ucast_routes());
  EXPECT_EQ(kTotalRibPaths, results.total_rib_paths());
  EXPECT_EQ(kTotalAdjRibs, results.total_adj_ribs());
  EXPECT_EQ(kTotalOriginatedRoutes, results.total_originated_routes());
  EXPECT_EQ(kTotalShadowRibEntries, results.total_shadow_rib_entries());
  EXPECT_EQ(
      kTotalNetlinkInterfaces, results.total_netlink_wrapper_interfaces());
  EXPECT_EQ(
      kTotalNetlinkHoldsActive, results.total_netlink_wrapper_holds_active());
}

TEST_F(CmdShowBgpStatsEntriesTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowBgpStatsEntries().printOutput(stats_, ss);
  std::string output = ss.str();

  std::string expectedOutput =
      "BGP entry statistics:\n"
      " Total number of unicast routes: 30\n"
      " Total number of rib paths: 20\n"
      " Total number of adjribs: 15\n"
      " Total number of originated routes: 2\n"
      " Total number of shadow rib entries: 10\n"
      " Total number of tracked netlink wrapper interfaces: 32\n"
      " Total number of active netlink wrapper holds: 3\n";

  EXPECT_EQ(expectedOutput, output);
}

TEST_F(CmdShowBgpStatsEntriesTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpStatsEntriesTraits::description().empty());
  std::stringstream ss;
  CmdShowBgpStatsEntries().printOutput(
      CmdShowBgpStatsEntries::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("Total number of unicast routes: 1515"));
  // The description tells readers these two should track each other, so the
  // sample must not contradict it.
  EXPECT_THAT(output, HasSubstr("Total number of shadow rib entries: 1515"));
}

} // namespace facebook::fboss
