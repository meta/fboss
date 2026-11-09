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
#include <thrift/lib/cpp/TApplicationException.h>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

#include "fboss/cli/fboss2/commands/show/bgp/stats/CmdShowBgpStatsAttrs.h"
#include "neteng/fboss/bgp/if/gen-cpp2/bgp_thrift_types.h"

using namespace ::testing;
using facebook::neteng::fboss::bgp::thrift::TAttributeStats;
using facebook::neteng::fboss::bgp::thrift::TAttributeStatsPayloadKind;
using facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsRequest;
using facebook::neteng::fboss::bgp::thrift::TGetDeduplicatorStatsResponse;

namespace facebook::fboss {

constexpr int64_t kBgpPathEntries = 800000;
constexpr int64_t kBgpAttributesEntries = 300001;
constexpr int64_t kAsPathEntries = 101;
constexpr int64_t kCommunitiesEntries = 200;
constexpr int64_t kClusterListEntries = 0;
constexpr int64_t kExtCommunitiesEntries = 100;
constexpr int64_t kTotalNumberOfAttributes = 1;
constexpr int64_t kTotalUniqueAttributes = 2;
constexpr double kAvgAttrRefCount = 1.3578;
constexpr double kAvgCommunityListLen = 0.0;
constexpr double kAvgExtCommunityListLen = 3.4567;
constexpr double kAvgASPathLen = 1.064;
constexpr double kAvgClusterListLen = 3.1415;
constexpr double kAvgTopologyInfoLen = 1.618;

static_assert(
    std::is_same_v<CmdShowBgpStatsAttrsTraits::RetType, TAttributeStats>);

class MockBgpStatsRpcClient {
 public:
  MOCK_METHOD(
      void,
      sync_getDeduplicatorStats,
      (TGetDeduplicatorStatsResponse&, const TGetDeduplicatorStatsRequest&));
  MOCK_METHOD(void, sync_getAttributeStats, (TAttributeStats&));
};

void expectRowContains(
    const std::string& output,
    const std::string& collection,
    const std::string& entries) {
  std::istringstream lines(output);
  for (std::string row; std::getline(lines, row);) {
    if (row.find(collection) != std::string::npos &&
        row.find(entries) != std::string::npos) {
      return;
    }
  }
  ADD_FAILURE() << "No row paired " << collection << " with " << entries;
}

class CmdShowBgpStatsAttrsTestFixture : public CmdHandlerTestBase {
 public:
  void SetUp() override {
    CmdHandlerTestBase::SetUp();
    /**
     * utils::Table constructs std::locale("") from the environment. Pin a
     * valid locale and restore it in TearDown so this test remains hermetic.
     */
    if (const char* lcAll = std::getenv("LC_ALL")) {
      savedLcAll_ = lcAll;
    }
    ASSERT_EQ(0, ::setenv("LC_ALL", "C", /*overwrite=*/1));
    deduplicatorStats_ = getDeduplicatorStats();
    deduplicatorModel_ = getDeduplicatorModel();
    legacyStats_ = getLegacyStats();
  }

  void TearDown() override {
    if (savedLcAll_.has_value()) {
      ASSERT_EQ(0, ::setenv("LC_ALL", savedLcAll_->c_str(), /*overwrite=*/1));
    } else {
      ASSERT_EQ(0, ::unsetenv("LC_ALL"));
    }
    CmdHandlerTestBase::TearDown();
  }

  static TGetDeduplicatorStatsResponse getDeduplicatorStats() {
    TGetDeduplicatorStatsResponse stats;
    stats.bgp_path()->entry_count() = kBgpPathEntries;
    stats.bgp_attributes()->entry_count() = kBgpAttributesEntries;
    stats.as_path()->entry_count() = kAsPathEntries;
    stats.communities()->entry_count() = kCommunitiesEntries;
    stats.cluster_list()->entry_count() = kClusterListEntries;
    stats.ext_communities()->entry_count() = kExtCommunitiesEntries;
    return stats;
  }

  static TAttributeStats getLegacyStats() {
    TAttributeStats stats;
    stats.total_num_of_attributes() = kTotalNumberOfAttributes;
    stats.total_unique_attributes() = kTotalUniqueAttributes;
    stats.avg_attribute_refcount() = kAvgAttrRefCount;
    stats.avg_community_list_len() = kAvgCommunityListLen;
    stats.avg_extcommunity_list_len() = kAvgExtCommunityListLen;
    stats.avg_as_path_len() = kAvgASPathLen;
    stats.avg_cluster_list_len() = kAvgClusterListLen;
    stats.avg_topology_info_len() = kAvgTopologyInfoLen;
    stats.payload_kind() = TAttributeStatsPayloadKind::LEGACY_ATTRIBUTE_STATS;
    return stats;
  }

  static TAttributeStats getDeduplicatorModel() {
    TAttributeStats stats;
    stats.dedup_bgp_path() = kBgpPathEntries;
    stats.dedup_bgp_attributes() = kBgpAttributesEntries;
    stats.dedup_as_path() = kAsPathEntries;
    stats.dedup_communities() = kCommunitiesEntries;
    stats.dedup_cluster_list() = kClusterListEntries;
    stats.dedup_ext_communities() = kExtCommunitiesEntries;
    stats.payload_kind() = TAttributeStatsPayloadKind::DEDUPLICATOR_STATS;
    return stats;
  }

  TGetDeduplicatorStatsResponse deduplicatorStats_;
  TAttributeStats deduplicatorModel_;
  TAttributeStats legacyStats_;
  std::optional<std::string> savedLcAll_;
};

TEST_F(CmdShowBgpStatsAttrsTestFixture, QueryClientUsesDeduplicatorStatsRpc) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getDeduplicatorStats(_, _))
      .WillOnce([&](auto& response, auto request) {
        EXPECT_NE(nullptr, request);
        response = deduplicatorStats_;
      });
  EXPECT_CALL(getMockBgp(), getAttributeStats(_)).Times(0);

  const auto results = CmdShowBgpStatsAttrs().queryClient(localhost());
  EXPECT_EQ(kBgpPathEntries, results.dedup_bgp_path().value());
  EXPECT_EQ(kBgpAttributesEntries, results.dedup_bgp_attributes().value());
  EXPECT_EQ(kAsPathEntries, results.dedup_as_path().value());
  EXPECT_EQ(kCommunitiesEntries, results.dedup_communities().value());
  EXPECT_EQ(kClusterListEntries, results.dedup_cluster_list().value());
  EXPECT_EQ(kExtCommunitiesEntries, results.dedup_ext_communities().value());
  EXPECT_EQ(
      TAttributeStatsPayloadKind::DEDUPLICATOR_STATS,
      results.payload_kind().value());
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, QueryClientFallsBackForOldBgpd) {
  MockBgpStatsRpcClient client;
  EXPECT_CALL(client, sync_getDeduplicatorStats(_, _))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::UNKNOWN_METHOD,
              "Method name getDeduplicatorStats not found")));
  EXPECT_CALL(client, sync_getAttributeStats(_)).WillOnce([&](auto& response) {
    response = legacyStats_;
  });

  const auto stats = queryBgpStatsAttrsWithFallback(client);

  EXPECT_EQ(kTotalNumberOfAttributes, stats.total_num_of_attributes().value());
  EXPECT_EQ(kTotalUniqueAttributes, stats.total_unique_attributes().value());
  EXPECT_FALSE(stats.dedup_bgp_path().has_value());
  EXPECT_EQ(
      TAttributeStatsPayloadKind::LEGACY_ATTRIBUTE_STATS,
      stats.payload_kind().value());
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, QueryClientPropagatesOtherErrors) {
  MockBgpStatsRpcClient client;
  EXPECT_CALL(client, sync_getDeduplicatorStats(_, _))
      .WillOnce(Throw(
          apache::thrift::TApplicationException(
              apache::thrift::TApplicationException::INTERNAL_ERROR,
              "deduplicator stats failed")));
  EXPECT_CALL(client, sync_getAttributeStats(_)).Times(0);

  EXPECT_THROW(
      queryBgpStatsAttrsWithFallback(client),
      apache::thrift::TApplicationException);
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, PrintOutputRendersEveryCollection) {
  std::stringstream output;
  CmdShowBgpStatsAttrs().printOutput(deduplicatorModel_, output);

  EXPECT_THAT(output.str(), HasSubstr("BGP attribute deduplicator statistics"));
  expectRowContains(output.str(), "bgp_path", "800000");
  expectRowContains(output.str(), "bgp_attributes", "300001");
  expectRowContains(output.str(), "as_path", "101");
  expectRowContains(output.str(), "communities", "200");
  expectRowContains(output.str(), "cluster_list", "0");
  expectRowContains(output.str(), "ext_communities", "100");
  EXPECT_THAT(output.str(), HasSubstr("L1"));
  EXPECT_THAT(output.str(), HasSubstr("L2"));
  EXPECT_THAT(output.str(), HasSubstr("L3"));
  EXPECT_THAT(output.str(), Not(HasSubstr("Total number of attributes")));
  EXPECT_THAT(output.str(), Not(HasSubstr("Average attribute")));
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, PrintOutputPreservesZeroEntries) {
  std::stringstream output;
  CmdShowBgpStatsAttrs().printOutput(deduplicatorModel_, output);

  expectRowContains(output.str(), "cluster_list", "0");
  EXPECT_THAT(output.str(), Not(HasSubstr("n/a")));
  EXPECT_THAT(output.str(), Not(HasSubstr("unavailable")));
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, PrintOutputRendersLegacyFallback) {
  std::stringstream output;
  CmdShowBgpStatsAttrs().printOutput(legacyStats_, output);

  const std::string expectedOutput =
      "BGP attribute statistics:\n"
      " Total number of attributes: 1\n"
      " Total number of unique attributes: 2\n"
      " Average attribute reference count: 1.36\n"
      " Average community list length: 0.00\n"
      " Average extended community list length: 3.46\n"
      " Average as path length: 1.06\n"
      " Average cluster list length: 3.14\n"
      " Average topology info length: 1.62\n";
  EXPECT_EQ(expectedOutput, output.str());
  EXPECT_THAT(
      output.str(), Not(HasSubstr("BGP attribute deduplicator statistics")));
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, PrintOutputRejectsUnknownPayloadKind) {
  std::stringstream output;

  EXPECT_THROW(
      CmdShowBgpStatsAttrs().printOutput(TAttributeStats{}, output),
      std::runtime_error);
}

TEST_F(
    CmdShowBgpStatsAttrsTestFixture,
    PrintOutputRejectsIncompleteDeduplicatorPayload) {
  TAttributeStats stats;
  stats.payload_kind() = TAttributeStatsPayloadKind::DEDUPLICATOR_STATS;
  stats.dedup_bgp_path() = kBgpPathEntries;
  std::stringstream output;

  EXPECT_THROW(
      CmdShowBgpStatsAttrs().printOutput(stats, output), std::runtime_error);
}

TEST_F(CmdShowBgpStatsAttrsTestFixture, wikiDocHooks) {
  EXPECT_FALSE(CmdShowBgpStatsAttrsTraits::description().empty());
  std::stringstream ss;
  CmdShowBgpStatsAttrs().printOutput(CmdShowBgpStatsAttrs::sampleModel(), ss);
  const std::string output = ss.str();

  EXPECT_THAT(output, HasSubstr("L1     bgp_path"));
  EXPECT_THAT(output, HasSubstr("L2     bgp_attributes"));
  EXPECT_THAT(output, HasSubstr("L3     ext_communities"));
}

} // namespace facebook::fboss
