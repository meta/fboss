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
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/CmdDeleteProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/term/CmdDeleteProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// Deleting only touches the BGP side of ConfigSession, which seeds from thrift
// schema defaults when neither a staged session nor a system bgpcpp.conf
// exists — so no seed agent config is needed (mirrors
// CmdDeleteBgpPolicyRoutingPolicyTest).
class CmdDeleteBgpPolicyRoutingPolicyTermTestFixture
    : public CmdConfigTestBase {
 public:
  CmdDeleteBgpPolicyRoutingPolicyTermTestFixture()
      : CmdConfigTestBase(
            "bgp_routing_policy_term_delete_test_%%%%-%%%%-%%%%",
            "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configureTerm(
      const std::string& policyName,
      const std::vector<std::string>& termTokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicyTerm cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyConfig({policyName}),
        BgpRoutingPolicyTermConfig(termTokens));
  }

  std::string delTerm(
      const std::string& policyName,
      const std::vector<std::string>& termTokens) {
    CmdDeleteProtocolBgpPolicyRoutingPolicyTerm cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyRef({policyName}),
        BgpRoutingPolicyTermRef(termTokens));
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
// BgpRoutingPolicyTermRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyRoutingPolicyTermTestFixture, argValidation) {
  auto ref = BgpRoutingPolicyTermRef({"10"});
  EXPECT_EQ(ref.seqNum(), 10);

  // Invalid: empty, non-numeric/negative seq-num, extra tokens.
  EXPECT_THROW(BgpRoutingPolicyTermRef({}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermRef({"ten"}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermRef({"-1"}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermRef({"10", "extra"}), std::invalid_argument);
}

// ==============================================================================
// queryClient
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyRoutingPolicyTermTestFixture, deleteExistingTerm) {
  configureTerm("RM100", {"10", "description", "delete-me"});
  configureTerm("RM100", {"20", "description", "keep"});
  ASSERT_EQ(terms(0).size(), 2);

  auto result = delTerm("RM100", {"10"});
  EXPECT_THAT(
      result,
      HasSubstr("Successfully deleted BGP routing-policy RM100 term 10"));
  // The policy and its other term survive.
  ASSERT_EQ(policies().size(), 1);
  ASSERT_EQ(terms(0).size(), 1);
  EXPECT_EQ(*terms(0)[0].sequence_number(), 20);
  EXPECT_EQ(*terms(0)[0].description(), "keep");
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTermTestFixture,
    deleteLastTermLeavesPolicy) {
  configureTerm("RM100", {"10"});

  auto result = delTerm("RM100", {"10"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  // Deleting the only term must not delete the policy itself.
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
  EXPECT_TRUE(terms(0).empty());
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTermTestFixture,
    deleteUnknownTermRejected) {
  configureTerm("RM100", {"10"});

  auto result = delTerm("RM100", {"20"});
  EXPECT_THAT(
      result, HasSubstr("Error: BGP routing-policy RM100 term 20 not found"));
  // The existing term is untouched.
  ASSERT_EQ(terms(0).size(), 1);
  EXPECT_EQ(*terms(0)[0].sequence_number(), 10);
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTermTestFixture,
    deleteTermFromUnknownPolicyRejected) {
  auto result = delTerm("NO-SUCH-POLICY", {"10"});
  EXPECT_THAT(
      result, HasSubstr("Error: BGP routing-policy NO-SUCH-POLICY not found"));
  // Nothing was persisted for the failed delete.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected delete";
}

} // namespace facebook::fboss
