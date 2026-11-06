// Copyright (c) Meta Platforms, Inc. and affiliates.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include <sstream>
#include <stdexcept>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h"
#include "fboss/cli/fboss2/commands/show/bgp/policy/CmdShowBgpPolicy.h"
#include "fboss/cli/fboss2/test/CmdHandlerTestBase.h"

using namespace ::testing;

namespace facebook::fboss {
namespace {

facebook::bgp::thrift::BgpPolicyConfig makeSamplePolicyConfig() {
  facebook::bgp::bgp_policy::BgpPolicyStatement ingress;
  ingress.name() = "SAMPLE_UPLINK_IN";
  ingress.policy_version() = "15";
  facebook::bgp::bgp_policy::BgpPolicyStatement egress;
  egress.name() = "SAMPLE_UPLINK_OUT";
  egress.policy_version() = "16";
  facebook::bgp::bgp_policy::BgpPolicies policies;
  policies.bgp_policy_statements() = {ingress, egress};

  facebook::bgp::thrift::BgpCommunity community;
  community.name() = "SAMPLE_COMMUNITY";
  community.communities() = {"65108:337"};
  facebook::bgp::thrift::BgpLocalPref localPref;
  localPref.name() = "SAMPLE_LOCALPREF";
  localPref.localpref() = 100;

  facebook::bgp::thrift::BgpPolicyConfig config;
  config.communities() = {community};
  config.localprefs() = {localPref};
  config.policies() = policies;
  return config;
}

class CmdShowBgpPolicyQueryTest : public CmdHandlerTestBase {
 protected:
  void expectArtifact() {
    setupMockedBgpServer();
    EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
    const auto json =
        apache::thrift::SimpleJSONSerializer::serialize<std::string>(
            policyConfig_);
    EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
        .WillOnce([json](std::string& result) { result = json; });
    EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);
  }

  void expectBgpConfig() {
    setupMockedBgpServer();
    EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(false));
    EXPECT_CALL(getMockBgp(), getPolicyConfig(_)).Times(0);
    EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
        .WillOnce([policy = policyConfig_](
                      facebook::bgp::thrift::BgpConfig& running) {
          running.communities().copy_from(policy.communities());
          running.localprefs().copy_from(policy.localprefs());
          running.policies().copy_from(policy.policies());
        });
  }

  facebook::bgp::thrift::BgpPolicyConfig policyConfig_ =
      makeSamplePolicyConfig();
};

TEST_F(CmdShowBgpPolicyQueryTest, BareCommandShowsFullArtifact) {
  expectArtifact();
  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {});

  EXPECT_EQ("BGP policy", result["source"].asString());
  const auto& policy = result["policy"];
  EXPECT_EQ("SAMPLE_COMMUNITY", policy["communities"][0]["name"].asString());
  EXPECT_EQ("SAMPLE_LOCALPREF", policy["localprefs"][0]["name"].asString());
  EXPECT_EQ(2, policy["policies"]["bgp_policy_statements"].size());

  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_THAT(output.str(), StartsWith("Policy source: BGP policy\n"));
}

TEST_F(CmdShowBgpPolicyQueryTest, BareCommandShowsOnlyPolicyFromBgpConfig) {
  expectBgpConfig();
  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {});

  EXPECT_EQ("BGP config", result["source"].asString());
  EXPECT_TRUE(result["policy"].count("policies"));
  EXPECT_FALSE(result["policy"].count("bgp_setting_config"));
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_THAT(output.str(), StartsWith("Policy source: BGP config\n"));
}

TEST_F(CmdShowBgpPolicyQueryTest, BareCommandHandlesEmptyPolicyDocument) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(false));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_)).Times(0);
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_))
      .WillOnce([](facebook::bgp::thrift::BgpConfig&) {});

  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {});

  EXPECT_EQ("BGP config", result["source"].asString());
  EXPECT_TRUE(result["policy"].isObject());
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_THAT(output.str(), StartsWith("Policy source: BGP config\n"));
}

TEST_F(CmdShowBgpPolicyQueryTest, NamedLookupUsesArtifactSource) {
  expectArtifact();
  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {"SAMPLE_UPLINK_OUT"});

  EXPECT_EQ("BGP policy", result["source"].asString());
  EXPECT_EQ("SAMPLE_UPLINK_OUT", result["policy"]["name"].asString());
  EXPECT_FALSE(result["policy"].count("communities"));
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_THAT(output.str(), StartsWith("Policy source: BGP policy\n"));
  EXPECT_THAT(output.str(), Not(HasSubstr("SAMPLE_UPLINK_IN")));
}

TEST_F(CmdShowBgpPolicyQueryTest, NamedLookupUsesBgpConfigSource) {
  expectBgpConfig();
  const auto result =
      CmdShowBgpPolicy().queryClient(localhost(), {"SAMPLE_UPLINK_IN"});

  EXPECT_EQ("BGP config", result["source"].asString());
  EXPECT_EQ("SAMPLE_UPLINK_IN", result["policy"]["name"].asString());
}

TEST_F(CmdShowBgpPolicyQueryTest, MissingArtifactDoesNotUseBgpConfig) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& result) { result.clear(); });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  EXPECT_THROW(
      CmdShowBgpPolicy().queryClient(localhost(), {}), std::runtime_error);
}

TEST_F(CmdShowBgpPolicyQueryTest, MissingArtifactStatementListPrintsNoPolicy) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& result) { result = R"({"policies":{}})"; });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {"DOES_NOT_EXIST"});

  EXPECT_TRUE(result.empty());
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_TRUE(output.str().empty());
}

TEST_F(CmdShowBgpPolicyQueryTest, EmptyArtifactStatementListPrintsNoPolicy) {
  setupMockedBgpServer();
  EXPECT_CALL(getMockBgp(), hasPolicySymlink()).WillOnce(Return(true));
  EXPECT_CALL(getMockBgp(), getPolicyConfig(_))
      .WillOnce([](std::string& result) {
        result = R"({"policies":{"bgp_policy_statements":[]}})";
      });
  EXPECT_CALL(getMockBgp(), getRunningConfigStruct(_)).Times(0);

  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {"DOES_NOT_EXIST"});

  EXPECT_TRUE(result.empty());
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_TRUE(output.str().empty());
}

TEST_F(CmdShowBgpPolicyQueryTest, UnknownNamePrintsNoPolicy) {
  expectArtifact();
  auto command = CmdShowBgpPolicy();
  const auto result = command.queryClient(localhost(), {"DOES_NOT_EXIST"});

  EXPECT_TRUE(result.empty());
  std::ostringstream output;
  command.printOutput(result, output);
  EXPECT_TRUE(output.str().empty());
}

} // namespace
} // namespace facebook::fboss
