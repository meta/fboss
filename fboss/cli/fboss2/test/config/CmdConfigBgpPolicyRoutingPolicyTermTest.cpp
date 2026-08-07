// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// The term dispatcher only touches the BGP side of ConfigSession, which seeds
// from thrift schema defaults when neither a staged session nor a system
// bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyRoutingPolicyTest).
class CmdConfigBgpPolicyRoutingPolicyTermTestFixture
    : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyRoutingPolicyTermTestFixture()
      : CmdConfigTestBase("bgp_routing_policy_term_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  // Invoke the term handler the way the framework does: the parent's parsed
  // args plus the term's own tokens.
  std::string runTerm(
      const std::vector<std::string>& policyTokens,
      const std::vector<std::string>& termTokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicyTerm cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyConfig(policyTokens),
        BgpRoutingPolicyTermConfig(termTokens));
  }

  std::string runPolicy(const std::vector<std::string>& tokens) {
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

  const std::vector<bgp::bgp_policy::BgpPolicyTerm>& terms(size_t policyIdx) {
    return *policies()[policyIdx].policy_entries();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpRoutingPolicyTermConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermTestFixture, argValidation) {
  // Bare create term.
  auto bareTerm = BgpRoutingPolicyTermConfig({"10"});
  EXPECT_EQ(bareTerm.seqNum(), 10);
  EXPECT_TRUE(bareTerm.attr().empty());

  // Term-level attribute.
  auto termDesc = BgpRoutingPolicyTermConfig({"10", "description", "a", "b"});
  EXPECT_EQ(termDesc.attr(), "description");
  EXPECT_EQ(termDesc.values(), std::vector<std::string>({"a", "b"}));

  // Invalid: empty, non-numeric/negative seq-num, unknown attribute.
  EXPECT_THROW(BgpRoutingPolicyTermConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermConfig({"ten"}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermConfig({"-1"}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermConfig({"10", "no-such-attr", "1"}),
      std::invalid_argument);
}

// ==============================================================================
// Term-level handlers
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermTestFixture, bareCreateTerm) {
  auto result = runTerm({"RM100"}, {"10"});
  EXPECT_THAT(
      result,
      HasSubstr("Successfully created BGP routing-policy RM100 term 10"));
  // The policy is implicitly created for its term.
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
  ASSERT_EQ(terms(0).size(), 1);
  EXPECT_EQ(*terms(0)[0].sequence_number(), 10);
  // The name is seeded from the seq-num (next_term_id references terms by
  // name, so every term needs a stable identity).
  EXPECT_EQ(*terms(0)[0].name(), "10");
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermTestFixture, setTermDescription) {
  auto result =
      runTerm({"RM100"}, {"10", "description", "deny", "private", "asns"});
  EXPECT_THAT(result, HasSubstr("Successfully set description"));
  EXPECT_THAT(result, HasSubstr("for routing-policy RM100 term 10"));
  ASSERT_EQ(terms(0).size(), 1);
  // Multi-token description is re-joined.
  EXPECT_EQ(*terms(0)[0].description(), "deny private asns");
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermTestFixture, termsAreKeyedBySeqNum) {
  runTerm({"RM100"}, {"10", "description", "one"});
  runTerm({"RM100"}, {"20", "description", "two"});
  ASSERT_EQ(policies().size(), 1);
  ASSERT_EQ(terms(0).size(), 2);

  // Re-referencing an existing term by seq-num updates it, not appends.
  runTerm({"RM100"}, {"10", "description", "one-updated"});
  ASSERT_EQ(terms(0).size(), 2);
  EXPECT_EQ(*terms(0)[0].description(), "one-updated");
  EXPECT_EQ(*terms(0)[1].description(), "two");
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    reReferenceReportsExisting) {
  runTerm({"RM100"}, {"10"});
  // A second bare reference must not claim to have created it again.
  EXPECT_THAT(runTerm({"RM100"}, {"10"}), HasSubstr("already exists"));
  ASSERT_EQ(terms(0).size(), 1);
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    termOnExistingPolicyKeepsPolicyAttributes) {
  runPolicy({"RM100", "description", "keep-me"});
  runTerm({"RM100"}, {"10", "description", "term-desc"});
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].description(), "keep-me");
  ASSERT_EQ(terms(0).size(), 1);
  EXPECT_EQ(*terms(0)[0].description(), "term-desc");
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    emptyDescriptionRejected) {
  auto result = runTerm({"RM100"}, {"10", "description"});
  EXPECT_THAT(result, HasSubstr("Error: description requires <string>"));
  // The rejected value must not leave a phantom term or policy behind.
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    rejectedValueOnExistingPolicyKeepsPolicy) {
  runPolicy({"RM100", "description", "keep-me"});
  ASSERT_EQ(policies().size(), 1);

  auto result = runTerm({"RM100"}, {"10", "description"});
  EXPECT_THAT(result, HasSubstr("Error: description requires <string>"));
  // The pre-existing policy survives; only the phantom term is rolled back.
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].description(), "keep-me");
  EXPECT_TRUE(terms(0).empty());
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    rejectedValueOnExistingTermKeepsTerm) {
  runTerm({"RM100"}, {"10", "description", "keep-me"});
  ASSERT_EQ(terms(0).size(), 1);

  auto result = runTerm({"RM100"}, {"10", "description"});
  EXPECT_THAT(result, HasSubstr("Error: description requires <string>"));
  // The pre-existing term survives untouched.
  ASSERT_EQ(terms(0).size(), 1);
  EXPECT_EQ(*terms(0)[0].description(), "keep-me");
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermTestFixture,
    policyAttributeAlongsideTermRejected) {
  // The CLI parse accepts `routing-policy RM100 description x term 10`, but
  // only the term leaf runs — the policy attribute must be rejected, not
  // silently dropped.
  auto result = runTerm({"RM100", "description", "x"}, {"10"});
  EXPECT_THAT(result, HasSubstr("Error: configure routing-policy attributes"));
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

} // namespace facebook::fboss
