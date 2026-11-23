// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

using namespace ::testing;

namespace facebook::fboss {

// The routing-policy dispatcher only touches the BGP side of ConfigSession,
// which seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyCommunityListTest).
class CmdConfigBgpPolicyRoutingPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyRoutingPolicyTestFixture()
      : CmdConfigTestBase("bgp_routing_policy_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicy cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpRoutingPolicyConfig(tokens));
  }

  const std::vector<bgp::bgp_policy::BgpPolicyStatement>& policies() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .bgp_policy_statements();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpRoutingPolicyConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, argValidation) {
  // Bare create policy.
  auto barePolicy = BgpRoutingPolicyConfig({"RM100"});
  EXPECT_EQ(barePolicy.policyName(), "RM100");
  EXPECT_TRUE(barePolicy.attr().empty());

  // Policy-level attribute.
  auto policyAttr =
      BgpRoutingPolicyConfig({"RM100", "description", "my", "policy"});
  EXPECT_EQ(policyAttr.policyName(), "RM100");
  EXPECT_EQ(policyAttr.attr(), "description");
  EXPECT_EQ(policyAttr.values(), std::vector<std::string>({"my", "policy"}));

  // Invalid: empty, empty name, unknown attribute.
  EXPECT_THROW(BgpRoutingPolicyConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyConfig({""}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyConfig({"RM100", "no-such-attr", "1"}),
      std::invalid_argument);
  // term is a follow-up, not yet a valid second token.
  EXPECT_THROW(
      BgpRoutingPolicyConfig({"RM100", "term", "10"}), std::invalid_argument);
}

// ==============================================================================
// Policy-level handlers
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, bareCreatePolicy) {
  auto result = run({"RM100"});
  EXPECT_THAT(
      result, HasSubstr("Successfully created BGP routing-policy RM100"));
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
  EXPECT_FALSE(policies()[0].description().has_value());
  EXPECT_TRUE(sessionFileExists());

  run({"RM200"});
  ASSERT_EQ(policies().size(), 2);
  EXPECT_EQ(*policies()[1].name(), "RM200");
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, setDescription) {
  auto result = run({"RM100", "description", "spine", "export", "policy"});
  EXPECT_THAT(result, HasSubstr("Successfully set description"));
  EXPECT_THAT(result, HasSubstr("for routing-policy RM100"));
  ASSERT_EQ(policies().size(), 1);
  // Multi-token description is re-joined.
  EXPECT_EQ(*policies()[0].description(), "spine export policy");
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, namedPoliciesAreDistinct) {
  run({"RM100", "description", "one"});
  run({"RM200", "description", "two"});
  ASSERT_EQ(policies().size(), 2);
  // Re-referencing an existing policy by name updates it, not appends.
  run({"RM100", "description", "one-updated"});
  ASSERT_EQ(policies().size(), 2);
  EXPECT_EQ(*policies()[0].description(), "one-updated");
  EXPECT_EQ(*policies()[1].description(), "two");
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, reReferenceReportsExisting) {
  run({"RM100"});
  // A second bare reference must not claim to have created it again.
  EXPECT_THAT(run({"RM100"}), HasSubstr("already exists"));
  ASSERT_EQ(policies().size(), 1);
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTestFixture, emptyDescriptionRejected) {
  auto result = run({"RM100", "description"});
  EXPECT_THAT(result, HasSubstr("Error: description requires <string>"));
  // The rejected value must not leave a phantom policy behind.
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTestFixture,
    rejectedValueOnExistingPolicyKeepsPolicy) {
  // Create the policy first, then reject a value on it.
  run({"RM100", "description", "keep-me"});
  ASSERT_EQ(policies().size(), 1);

  auto result = run({"RM100", "description"});
  EXPECT_THAT(result, HasSubstr("Error: description requires <string>"));
  // The pre-existing policy survives untouched.
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].description(), "keep-me");
}

} // namespace facebook::fboss
