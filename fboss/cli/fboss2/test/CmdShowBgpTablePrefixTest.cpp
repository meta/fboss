/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include <fmt/core.h>
#include <folly/json.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/TApplicationException.h>
#include <thrift/lib/cpp2/reflection/testing.h> // NOLINT(misc-include-cleaner)
#include <initializer_list>
#include <memory>
#include <vector>
#include "fboss/agent/FbossError.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/CanonicalRibResolver.h"
#include "fboss/cli/fboss2/commands/show/bgp/table/CmdShowBgpTablePrefix.h"
#include "fboss/cli/fboss2/test/CmdBgpTestUtils.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"
#ifndef IS_OSS
// Avoid EXPECT_THRIFT_EQ clash with <thrift/lib/cpp2/reflection/testing.h>
#undef EXPECT_THRIFT_EQ
#include "nettools/common/TestUtils.h"
#endif

using namespace ::testing;
using namespace facebook::neteng::fboss::bgp::thrift;
namespace facebook::fboss {
class CmdShowBgpTablePrefixTestFixture : public CmdHandlerTestBase {
 public:
  std::vector<TRibEntry> queriedEntry_;
  const std::string kPrefixToQuery = "8.0.0.0/32";

  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    queriedEntry_ = {
        buildEntry(kPrefixToQuery, kNextHop, kPeerId, kPeerDescription)};
  }

  void setupConfig() {
    EXPECT_CALL(getMockBgp(), getRunningConfig(_))
        .WillRepeatedly(Invoke([&](std::string& config) {
          // clang-format off
        folly::dynamic value = folly::dynamic::object
          ("communities",
          folly::dynamic::array(
          folly::dynamic::object("name", "SAMPLE_LOOPBACK_COM")
          ("description", "rsw loopback")
          ("communities", folly::dynamic::array("65221:28734"))
          )
        )
        ("localprefs",
        folly::dynamic::array(
          folly::dynamic::object("localpref", 20)
          ("name", "LOCALPREF_SAMPLE_BKUP")
          ("description", "low-priority supplementary/backup routes from bgp controller"),
          folly::dynamic::object("localpref", 25)
          ("name", "LOCALPREF_SAMPL1")
          ("description", "deprioritized local preference value"))
        );
          // clang-format on
          config = folly::toPrettyJson(value);
        }));
  }

  TBgpPath buildFixedPath(
      const std::string& peerId,
      const std::string& nextHop,
      const bool isBestPath = false) {
    // Path created will have peerId as the peerDescription too.
    return buildPath(
        kPrefixToQuery,
        nextHop,
        peerId, // peerId
        peerId, // peerDescription
        true, // setCommunity
        true, // setAsPath
        true, // setExtCommunity
        std::nullopt, // clusterList
        kOriginatorId, // originatorId
        false, // setPolicy
        std::nullopt, // pathNextHopWeight
        isBestPath,
        true, // setMed
        true, // setRecvPathId
        true, // setWeight
        true // setIgpCost
    );
  }
};

/** Build one compact programmed FIB-out row for prefix command tests. */
TFibOutEntry makeFibOutEntryForPrefixTest(
    const std::string& prefix,
    const std::string& nextHop) {
  TFibOutNextHop thriftNextHop;
  thriftNextHop.next_hop() = getPrefix(nextHop);
  thriftNextHop.weight() = 17;
  thriftNextHop.role() = TFibOutNextHopRole::PRIMARY;
  TFibOutRoute route;
  route.operation() = TFibOutOperation::PROGRAM;
  route.next_hops() = {std::move(thriftNextHop)};
  TFibOutEntry entry;
  entry.prefix() = getPrefix(prefix);
  entry.fib_out() = std::move(route);
  return entry;
}

TEST_F(CmdShowBgpTablePrefixTestFixture, queryClient) {
  setupMockedBgpServer();
  auto canonical = buildCanonicalRibState(kPrefixToQuery);
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .WillOnce([&](TCanonicalRibState& state, std::unique_ptr<std::string>) {
        state = canonical;
      });
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .WillOnce([](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
        table.enabled() = false;
      });

  auto result =
      CmdShowBgpTablePrefix().queryClient(localhost(), {kPrefixToQuery});
  EXPECT_THRIFT_EQ_VECTOR(
      *result.tRibEntries(), resolveCanonicalRibState(canonical));
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryIncludesFibOutWhenEnabled) {
  setupMockedBgpServer();
  auto canonical = buildCanonicalRibState(kPrefixToQuery);
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .WillOnce([&](TCanonicalRibState& state, std::unique_ptr<std::string>) {
        state = canonical;
      });
  const auto expectedFibOut =
      makeFibOutEntryForPrefixTest(kPrefixToQuery, kNextHop);
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .WillOnce([&](TFibOutTable& table,
                    std::unique_ptr<TFibOutPrefixRequest> request) {
        EXPECT_EQ(*request->prefix(), kPrefixToQuery);
        table.enabled() = true;
        table.entries() = {expectedFibOut};
      });

  const auto result =
      CmdShowBgpTablePrefix().queryClient(localhost(), {kPrefixToQuery});

  ASSERT_EQ(result.tRibEntries()->size(), 1);
  const auto& entry = result.tRibEntries()->front();
  ASSERT_TRUE(entry.fib_out().has_value());
  EXPECT_EQ(entry.fib_out(), expectedFibOut.fib_out());
  EXPECT_FALSE(entry.fib_out_pending().has_value());
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryAttachesFibOutToDuplicateRows) {
  setupMockedBgpServer();
  const auto canonical = buildCanonicalRibState(kPrefixToQuery);
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .Times(2)
      .WillRepeatedly([&](TCanonicalRibState& state,
                          std::unique_ptr<std::string>) { state = canonical; });
  const auto expectedFibOut =
      makeFibOutEntryForPrefixTest(kPrefixToQuery, kNextHop);
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .Times(2)
      .WillRepeatedly(
          [&](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
            table.enabled() = true;
            table.entries() = {expectedFibOut};
          });

  const auto result = CmdShowBgpTablePrefix().queryClient(
      localhost(), {kPrefixToQuery, kPrefixToQuery});

  ASSERT_EQ(result.tRibEntries()->size(), 2);
  for (const auto& entry : *result.tRibEntries()) {
    ASSERT_TRUE(entry.fib_out().has_value());
    EXPECT_EQ(entry.fib_out(), expectedFibOut.fib_out());
    EXPECT_FALSE(entry.fib_out_pending().has_value());
  }
}

TEST_F(
    CmdShowBgpTablePrefixTestFixture,
    QueryDuplicatePrefixUsesLatestFibOutState) {
  setupMockedBgpServer();
  const std::string normalizedPrefix = "8.0.0.0/24";
  const std::string equivalentPrefix = "8.0.0.42/24";
  const auto canonical = buildCanonicalRibState(normalizedPrefix);
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .Times(2)
      .WillRepeatedly([&](TCanonicalRibState& state,
                          std::unique_ptr<std::string>) { state = canonical; });
  const auto submitted =
      makeFibOutEntryForPrefixTest(normalizedPrefix, kNextHop);
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .WillOnce(
          [&](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
            table.enabled() = true;
            table.entries() = {submitted};
          })
      .WillOnce(
          [&](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
            table.enabled() = true;
            table.entries()->clear();
          });

  const auto result = CmdShowBgpTablePrefix().queryClient(
      localhost(), {normalizedPrefix, equivalentPrefix});

  ASSERT_EQ(result.tRibEntries()->size(), 2);
  for (const auto& entry : *result.tRibEntries()) {
    EXPECT_FALSE(entry.fib_out().has_value());
    EXPECT_FALSE(entry.fib_out_pending().has_value());
  }
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryHidesFibOutWhenDisabled) {
  setupMockedBgpServer();
  auto canonical = buildCanonicalRibState(kPrefixToQuery);
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .WillOnce([&](TCanonicalRibState& state, std::unique_ptr<std::string>) {
        state = canonical;
      });
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .WillOnce([](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
        table.enabled() = false;
      });

  const auto result =
      CmdShowBgpTablePrefix().queryClient(localhost(), {kPrefixToQuery});

  ASSERT_EQ(result.tRibEntries()->size(), 1);
  EXPECT_FALSE(result.tRibEntries()->front().fib_out().has_value());
  EXPECT_FALSE(result.tRibEntries()->front().fib_out_pending().has_value());
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryHidesFibOutForOlderBinary) {
  MockBgpClient client;
  EXPECT_CALL(client, getFibOutPrefix(_, _))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::UNKNOWN_METHOD,
              "Method name getFibOutPrefix not found")));

  EXPECT_FALSE(
      queryFibOutPrefixIfSupported(client, kPrefixToQuery).has_value());
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryPreservesRealFibOutFailure) {
  MockBgpClient client;
  EXPECT_CALL(client, getFibOutPrefix(_, _))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::INTERNAL_ERROR,
              "RIB event base is unresponsive")));

  EXPECT_THROW(
      queryFibOutPrefixIfSupported(client, kPrefixToQuery),
      apache::thrift::TApplicationException);
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryClientWithAbsentPrefix) {
  const std::string absentPrefix = "1.1.1.1/32";
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getRibPrefixCanonical(_, _))
      .WillOnce([&](TCanonicalRibState& state, std::unique_ptr<std::string>) {
        state = TCanonicalRibState();
      });
  EXPECT_CALL(getMockBgp(), getFibOutPrefix(_, _))
      .WillOnce([](TFibOutTable& table, std::unique_ptr<TFibOutPrefixRequest>) {
        table.enabled() = true;
      });

  auto result =
      CmdShowBgpTablePrefix().queryClient(localhost(), {absentPrefix});
  EXPECT_THAT(*result.tRibEntries(), IsEmpty());
}

TEST_F(CmdShowBgpTablePrefixTestFixture, QueryClientRejectsMalformedPrefix) {
  setupMockedBgpServer();

  EXPECT_THROW(
      CmdShowBgpTablePrefix().queryClient(
          localhost(), {kPrefixToQuery, "invalid"}),
      FbossError);
}

TEST_F(CmdShowBgpTablePrefixTestFixture, printOutput) {
  // Setup.
  setupMockedBgpServer();
  setupConfig();

  // Create TRibEntry (mocked output @queriedEntry_) from getRibEntries.
  TRibEntryWithHost tRibEntryWithHost;
  tRibEntryWithHost.tRibEntries() = queriedEntry_;
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();
  // Expect one selected path.
  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: [1.1.1.2]\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutputIncludesFibOutWhenPresent) {
  setupMockedBgpServer();
  setupConfig();
  auto fibOutEntry = makeFibOutEntryForPrefixTest(kPrefixToQuery, kNextHop);
  fibOutEntry.fib_out()->admin_distance() = 20;
  fibOutEntry.fib_out()->class_id() = 9;
  fibOutEntry.fib_out()->nexthop_set_ref_count() = 3;
  queriedEntry_.front().fib_out() = std::move(*fibOutEntry.fib_out());
  TRibEntryWithHost data;
  data.tRibEntries() = queriedEntry_;
  data.host() = localhost().getName();
  data.oobName() = localhost().getOobName();
  data.ip() = localhost().getIpStr();
  std::stringstream output;

  CmdShowBgpTablePrefix().printOutput(data, output);

  EXPECT_THAT(
      output.str(),
      HasSubstr(
          "  FIB-out: PROGRAM | Route References: 3 | Admin Distance: 20 | Class ID: 9\n"
          "    8.0.0.1 | Weight: 17 | Role: PRIMARY"));
  EXPECT_THAT(output.str(), Not(HasSubstr(" | Pending: true")));
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutputHidesAbsentFibOutState) {
  setupMockedBgpServer();
  setupConfig();
  TRibEntryWithHost data;
  data.tRibEntries() = queriedEntry_;
  data.host() = localhost().getName();
  data.oobName() = localhost().getOobName();
  data.ip() = localhost().getIpStr();
  std::stringstream output;

  CmdShowBgpTablePrefix().printOutput(data, output);

  EXPECT_THAT(output.str(), Not(HasSubstr("FIB-out:")));
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutputShowsEmptyFibOutState) {
  setupMockedBgpServer();
  setupConfig();
  TFibOutRoute fibOut;
  fibOut.operation() = TFibOutOperation::NONE;
  queriedEntry_.front().fib_out() = std::move(fibOut);
  TRibEntryWithHost data;
  data.tRibEntries() = queriedEntry_;
  data.host() = localhost().getName();
  data.oobName() = localhost().getOobName();
  data.ip() = localhost().getIpStr();
  std::stringstream output;

  CmdShowBgpTablePrefix().printOutput(data, output);

  EXPECT_THAT(output.str(), HasSubstr("  FIB-out: NONE\n"));
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutputHidesFibOutWhenDisabled) {
  setupMockedBgpServer();
  setupConfig();
  TRibEntryWithHost data;
  data.tRibEntries() = queriedEntry_;
  data.host() = localhost().getName();
  data.oobName() = localhost().getOobName();
  data.ip() = localhost().getIpStr();
  std::stringstream output;

  CmdShowBgpTablePrefix().printOutput(data, output);

  EXPECT_THAT(output.str(), Not(HasSubstr("FIB-out:")));
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutput_OnlyDefaultPaths) {
  // Setup.
  setupMockedBgpServer();
  setupConfig();

  // Create TRibEntry with 2 default paths.
  TRibEntryWithHost tRibEntryWithHost;
  TRibEntry entry = buildEntry(
      kPrefixToQuery,
      kNextHop, // nextHop
      {
          {kDefaultGroupName,
           {
               buildFixedPath(
                   "1.2.3.2", // peerId
                   kNextHop),
               buildFixedPath(
                   "1.2.3.3", // peerId
                   kNextHop),
           }},
      }, // paths map
      true /* omitBestPath */);
  tRibEntryWithHost.tRibEntries()->push_back(std::move(entry));
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();

  // Show output.
  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  // No paths should be marked with * or @ as we do not have any best paths.
  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 0/2 paths (2 active, 0 inactive)\n"
      // path 1
      "    from 1.2.3.2 (1.2.3.2) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n"
      // path 2
      "    from 1.2.3.3 (1.2.3.3) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(
    CmdShowBgpTablePrefixTestFixture,
    PrintOutput_SelectBestPathWithIsBestPath) {
  // Setup.
  setupMockedBgpServer();
  setupConfig();

  // Create TRibEntry with 3 paths; 2 as best paths, 1 as default path.
  TRibEntryWithHost tRibEntryWithHost;
  TRibEntry entry = buildEntry(
      kPrefixToQuery,
      kNextHop, // nextHop
      {
          {kBestGroupName,
           {
               buildFixedPath(
                   "1.2.3.2", // peerId
                   kNextHop,
                   true), // isBestPath
               buildFixedPath("1.2.3.3" /* peerId */, kNextHop),
           }},
          {kDefaultGroupName,
           {
               buildFixedPath("1.2.3.4" /* peerId */, kNextHop),
           }},
      }, // paths map
      false); // omitBestPath
  tRibEntryWithHost.tRibEntries()->push_back(std::move(entry));
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();

  // Show output.
  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  // When is_best_path is set on TBgpPath, we indicate this path is selected.
  // Only 1.2.3.2 should be selected as the very best entry (@).
  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n> 8.0.0.0/32, Selected 2/3 paths (3 active, 0 inactive)\n"
      // SELECTED path 1 (best path group)
      "*@  from 1.2.3.2 (1.2.3.2) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n"
      // path 2 (best path group)
      "*   from 1.2.3.3 (1.2.3.3) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n"
      // path 3 (default path group)
      "    from 1.2.3.4 (1.2.3.4) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

/*
 * A path the server marked inactive gets the "!" marker and is excluded from
 * the active count on the "Selected" line -- it never entered the comparison.
 */
TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutput_InactivePath) {
  // Setup.
  setupMockedBgpServer();
  setupConfig();

  // One selected best path plus one path excluded before selection ran.
  auto inactivePath = buildFixedPath("1.2.3.3" /* peerId */, kNextHop);
  inactivePath.is_inactive() = true;

  TRibEntryWithHost tRibEntryWithHost;
  TRibEntry entry = buildEntry(
      kPrefixToQuery,
      kNextHop, // nextHop
      {
          {kBestGroupName,
           {
               buildFixedPath(
                   "1.2.3.2", // peerId
                   kNextHop,
                   true), // isBestPath
           }},
          {kDefaultGroupName, {inactivePath}},
      }, // paths map
      false); // omitBestPath
  tRibEntryWithHost.tRibEntries()->push_back(std::move(entry));
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();

  // Show output.
  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  std::string expectedOutput = kRibEntryMarkersHeader +
      // 2 total paths, but only 1 was a candidate, so 1 active / 1 inactive.
      "\n> 8.0.0.0/32, Selected 1/2 paths (1 active, 1 inactive)\n"
      // Selected best path: no "!" in the third marker column.
      "*@  from 1.2.3.2 (1.2.3.2) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n"
      // Inactive path: "!" marker.
      "  ! from 1.2.3.3 (1.2.3.3) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: []\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpTablePrefixTestFixture, PrintOutput_PathSelectionPending) {
  // Test that path_selection_pending indicator is displayed when set
  setupMockedBgpServer();
  setupConfig();

  // Create TRibEntry with path_selection_pending = true
  TRibEntryWithHost tRibEntryWithHost;
  TRibEntry entry =
      buildEntry(kPrefixToQuery, kNextHop, kPeerId, kPeerDescription);
  entry.path_selection_pending() = true;
  tRibEntryWithHost.tRibEntries()->push_back(std::move(entry));
  tRibEntryWithHost.host() = localhost().getName();
  tRibEntryWithHost.oobName() = localhost().getOobName();
  tRibEntryWithHost.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(tRibEntryWithHost, ss);
  std::string output = ss.str();

  // Verify that the % marker is in the output when path selection is pending.
  // This indicates the path-selection results displayed are in a transient
  // state - wait for the final result after the marker is cleared.
  std::string expectedOutput = kRibEntryMarkersHeader +
      "\n>% 8.0.0.0/32, Selected 1/1 paths (1 active, 0 inactive)\n"
      "*@  from 1.2.3.4 (one.two.three.four) via 8.0.0.1 | LBW: None | Origin: INCOMPLETE | LP: SAMPL1/25 | ASP: 64712 | LM: # | NH Weight: N/A | MED: 10 | ID: 5 (rcvd) 6 (sent) | Weight: 20 | IgpCost: 100"
      "\n    Router/Originator: 2.2.2.3 | ClusterList: [1.1.1.2]\n"
      "    Communities: SAMPLE_LOOPBACK_COM/65221:28734\n"
      "    ExtCommunities: Type(64):SubType(2):AS(3):Value(4)\n"
      "    BestPath Rejection Reason: Router-Id, Filter Criterion: Choose Lowest Value\n";

  maskDateInOutput(output);
  EXPECT_EQ(output, expectedOutput);
}

TEST_F(CmdShowBgpTablePrefixTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpTablePrefixTraits::description().empty());

  /*
   * printRIBEntries reaches getLocalBgpConfig for the community/local-pref
   * mnemonics and builds the HostInfo it connects to from the MODEL's own
   * host/ip fields, so point the copy under test at the mocked server rather
   * than at the canned documentation host.
   */
  setupMockedBgpServer();
  resetBgpMnemonicCaches();
  EXPECT_CALL(getMockBgp(), getRunningConfig(_))
      .WillRepeatedly([](std::string& config) { config = "{}"; });

  // Exact match returns the one prefix, never a covering or covered one.
  auto model = CmdShowBgpTablePrefix::sampleModel();
  ASSERT_EQ(model.tRibEntries()->size(), 1);
  model.host() = localhost().getName();
  model.oobName() = localhost().getOobName();
  model.ip() = localhost().getIpStr();

  std::stringstream ss;
  CmdShowBgpTablePrefix().printOutput(model, ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("> 0.0.0.0/0, Selected 2/3 paths"));
  // printOutput hard-codes detail=true, so the detail-only lines are present.
  EXPECT_THAT(output, HasSubstr("Router/Originator:"));
  EXPECT_THAT(output, HasSubstr("Communities:"));
}

} // namespace facebook::fboss
