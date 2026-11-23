// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <thrift/lib/cpp2/protocol/Serializer.h>
#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/cli/fboss2/commands/show/bgp/CmdShowUtils.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {
namespace {

facebook::bgp::bgp_policy::BgpPolicies makePolicies(std::string name) {
  facebook::bgp::bgp_policy::BgpPolicyStatement statement;
  statement.name() = std::move(name);
  statement.policy_version() = "1";

  facebook::bgp::bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements()->push_back(std::move(statement));
  return policies;
}

std::string serializePolicyConfig(
    const facebook::bgp::bgp_policy::BgpPolicies& policies) {
  facebook::bgp::thrift::BgpPolicyConfig config;
  config.policies() = policies;
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(config);
}

class CmdShowBgpPolicyConfigTest : public CmdHandlerTestBase {};

TEST_F(CmdShowBgpPolicyConfigTest, UsesStandalonePolicySource) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& config) {
        config = serializePolicyConfig(makePolicies("standalone"));
      });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  const auto loaded = getLoadedBgpPolicy(localhost());

  EXPECT_EQ(BgpPolicySource::POLICY_ARTIFACT, loaded.source);
  ASSERT_TRUE(loaded.config.policies().has_value());
  EXPECT_EQ(
      "standalone",
      loaded.config.policies()->bgp_policy_statements()->front().name());
}

TEST_F(CmdShowBgpPolicyConfigTest, UsesBgpConfigWhenNoArtifact) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(false));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_)).Times(0);
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
      .WillOnce([](facebook::bgp::thrift::BgpConfig& config) {
        config.policies() = makePolicies("embedded");
      });

  const auto loaded = getLoadedBgpPolicy(localhost());

  EXPECT_EQ(BgpPolicySource::BGP_CONFIG, loaded.source);
  ASSERT_TRUE(loaded.config.policies().has_value());
  EXPECT_EQ(
      "embedded",
      loaded.config.policies()->bgp_policy_statements()->front().name());
}

TEST_F(CmdShowBgpPolicyConfigTest, EmptyStandalonePolicyDoesNotFallBack) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& config) {
        config = apache::thrift::SimpleJSONSerializer::serialize<std::string>(
            facebook::bgp::thrift::BgpPolicyConfig{});
      });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  const auto loaded = getLoadedBgpPolicy(localhost());
  EXPECT_EQ(BgpPolicySource::POLICY_ARTIFACT, loaded.source);
  EXPECT_FALSE(loaded.config.policies().has_value());
}

TEST_F(CmdShowBgpPolicyConfigTest, UnreadableArtifactNeverUsesBgpConfig) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& config) { config.clear(); });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  EXPECT_THROW(getLoadedBgpPolicy(localhost()), std::runtime_error);
}

TEST_F(CmdShowBgpPolicyConfigTest, RouteDetailsFallsBackOnEmptyArtifactRpc) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& config) { config.clear(); });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
      .WillOnce([](facebook::bgp::thrift::BgpConfig& config) {
        config.policies() = makePolicies("merged");
      });

  const auto policies = getRunningBgpPolicies(localhost());
  ASSERT_TRUE(policies.has_value());
  EXPECT_EQ("merged", policies->bgp_policy_statements()->front().name());
}

TEST_F(CmdShowBgpPolicyConfigTest, RouteDetailsFallsBackOnArtifactRpcError) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce(Throw(std::runtime_error("artifact RPC unavailable")));
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
      .WillOnce([](facebook::bgp::thrift::BgpConfig& config) {
        config.policies() = makePolicies("merged");
      });

  const auto policies = getRunningBgpPolicies(localhost());
  ASSERT_TRUE(policies.has_value());
  EXPECT_EQ("merged", policies->bgp_policy_statements()->front().name());
}

} // namespace
} // namespace facebook::fboss
