/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#ifndef IS_OSS
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sstream>
#include <string_view>
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsPolicy.h"
#include "neteng/fboss/bgp/if/gen-cpp2/policy_thrift_types.h"

using namespace ::testing;
using facebook::neteng::routing::policy::thrift::TPolicyStatementStats;
using facebook::neteng::routing::policy::thrift::TPolicyStats;
using facebook::neteng::routing::policy::thrift::TPolicyTermStats;
namespace facebook::fboss {

const std::string_view kStatementName = "RSW-TEST";
const int kStatementPrefixHitCount = 300;
const int kStatementAvgTime = 12;
const int kStatementMaxTime = 200;
const int kStatementNumRuns = 300;
const std::string_view kTermName = "term-test";
const std::string_view kTermDescription = "backup anycast path";
const int kTermPrefixHitCount = 200;

class CmdShowBgpStatsPolicyTestFixture : public CmdHandlerTestBase {
 public:
  TPolicyStats stats_;

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    stats_ = getStats();
  }

  TPolicyStats getStats() {
    TPolicyStatementStats statement_stats;
    statement_stats.name() = kStatementName;
    statement_stats.prefix_hit_count() = kStatementPrefixHitCount;
    statement_stats.avg_time() = kStatementAvgTime;
    statement_stats.max_time() = kStatementMaxTime;
    statement_stats.num_of_runs() = kStatementNumRuns;

    TPolicyTermStats term_stats;
    term_stats.name() = kTermName;
    term_stats.description() = kTermDescription;
    term_stats.prefix_hit_count() = kTermPrefixHitCount;

    statement_stats.term_stats() = {term_stats};

    TPolicyStats queriedSats;
    queriedSats.policy_statement_stats() = {statement_stats};

    return queriedSats;
  }
};

TEST_F(CmdShowBgpStatsPolicyTestFixture, queryClient) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPolicyStats(_))
      .WillOnce(Invoke([&](auto& entries) { entries = stats_; }));

  auto results = CmdShowBgpStatsPolicy().queryClient(localhost());

  const auto& statements = results.policy_statement_stats().value();
  ASSERT_EQ(statements.size(), 1);

  const auto& st_stats = statements[0];
  EXPECT_EQ(st_stats.name().value(), kStatementName);
  EXPECT_EQ(st_stats.prefix_hit_count().value(), kStatementPrefixHitCount);
  EXPECT_EQ(st_stats.avg_time().value(), kStatementAvgTime);
  EXPECT_EQ(st_stats.max_time().value(), kStatementMaxTime);
  EXPECT_EQ(st_stats.num_of_runs().value(), kStatementNumRuns);

  const auto& term_stats = st_stats.term_stats().value();
  ASSERT_EQ(term_stats.size(), 1);

  const auto& tr_stats = term_stats[0];
  EXPECT_EQ(tr_stats.name().value(), kTermName);
  EXPECT_EQ(tr_stats.description().value(), kTermDescription);
  EXPECT_EQ(tr_stats.prefix_hit_count().value(), kTermPrefixHitCount);
}

TEST_F(CmdShowBgpStatsPolicyTestFixture, printOutput) {
  std::stringstream ss;
  CmdShowBgpStatsPolicy().printOutput(stats_, ss);

  std::string output = ss.str();
  std::string expectedOutput =
      "BGP policy statistics:"
      "\n Policy Name: RSW-TEST"
      "\n\t Number of executions: 300"
      "\n\t Number of hits: 300 prefixes"
      "\n\t Average time of execution: 12us"
      "\n\t Maximum time of execution: 200us"
      "\n\t Term  Description                                        Hits/Misses"
      "\n\t 1     backup anycast path                                 200/100   "
      "\n\t 2     Default deny (Implicit)                             100/0\n";

  EXPECT_EQ(expectedOutput, output);
}

TEST_F(CmdShowBgpStatsPolicyTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpStatsPolicyTraits::description().empty());
  EXPECT_FALSE(
      CmdShowBgpStatsPolicy::sampleModel().policy_statement_stats()->empty());
  std::stringstream ss;
  CmdShowBgpStatsPolicy().printOutput(CmdShowBgpStatsPolicy::sampleModel(), ss);
  const std::string output = ss.str();

  // Misses accumulate down the term list, which the description calls out as
  // the easy thing to misread; pin it so the example keeps demonstrating it.
  EXPECT_THAT(output, HasSubstr("1     Accept default route"));
  EXPECT_THAT(output, HasSubstr("1/1463"));
  EXPECT_THAT(output, HasSubstr("1341/122"));
  EXPECT_THAT(output, HasSubstr("Default deny (Implicit)"));
}
} // namespace facebook::fboss
#endif // IS_OSS
