// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/global/CmdConfigProtocolBgpGlobal.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/neighbor/CmdConfigProtocolBgpNeighbor.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/peer-group/CmdConfigProtocolBgpPeerGroup.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/CmdConfigProtocolBgpPolicyRoutingPolicy.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/routing-policy/CmdDeleteProtocolBgpPolicyRoutingPolicy.h"
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

// Deleting only touches the BGP side of ConfigSession, which seeds from thrift
// schema defaults when neither a staged session nor a system bgpcpp.conf
// exists — so no seed agent config is needed (mirrors
// CmdDeleteBgpPolicyCommunityListTest).
class CmdDeleteBgpPolicyRoutingPolicyTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteBgpPolicyRoutingPolicyTestFixture()
      : CmdConfigTestBase("bgp_routing_policy_delete_test_%%%%-%%%%-%%%%", "") {
  }

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configure(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicy cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpRoutingPolicyConfig(tokens));
  }

  std::string del(const std::vector<std::string>& tokens) {
    CmdDeleteProtocolBgpPolicyRoutingPolicy cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpRoutingPolicyRef(tokens));
  }

  const std::vector<bgp::bgp_policy::BgpPolicyStatement>& policies() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .bgp_policy_statements();
  }

  // The referrers are staged through the real CLIs that live below this
  // level, so the guard is exercised against exactly what those commands
  // write.
  std::string configureNeighbor(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpNeighbor cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpNeighborConfig(tokens));
  }

  std::string configurePeerGroup(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPeerGroup cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPeerGroupConfig(tokens));
  }

  std::string configureGlobal(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpGlobal cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpGlobalConfig(tokens));
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpRoutingPolicyRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyRoutingPolicyTestFixture, argValidation) {
  auto ref = BgpRoutingPolicyRef({"RM100"});
  EXPECT_EQ(ref.policyName(), "RM100");

  // Invalid: empty, empty name, extra tokens.
  EXPECT_THROW(BgpRoutingPolicyRef({}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyRef({""}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyRef({"RM100", "RM200"}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyRef({"RM100", "description"}), std::invalid_argument);
}

// ==============================================================================
// queryClient
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyRoutingPolicyTestFixture, deleteExistingPolicy) {
  configure({"RM100", "description", "delete-me"});
  configure({"RM200", "description", "keep"});
  ASSERT_EQ(policies().size(), 2);

  auto result = del({"RM100"});
  EXPECT_THAT(
      result, HasSubstr("Successfully deleted BGP routing-policy RM100"));
  // The other policy survives untouched.
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM200");
  EXPECT_EQ(*policies()[0].description(), "keep");
  EXPECT_TRUE(sessionFileExists());
}

// Delete mirrors add: an absent target is a success with a warning, never an
// error, so a replayed script stays idempotent.
TEST_F(CmdDeleteBgpPolicyRoutingPolicyTestFixture, deleteUnknownPolicyWarns) {
  auto result = del({"NO-SUCH-POLICY"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP routing-policy NO-SUCH-POLICY does not exist; nothing "
          "to delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  // Nothing changed, so nothing is staged.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

TEST_F(CmdDeleteBgpPolicyRoutingPolicyTestFixture, deleteTwiceIsIdempotent) {
  configure({"RM100", "description", "delete-me"});
  EXPECT_THAT(del({"RM100"}), HasSubstr("Successfully deleted"));
  EXPECT_TRUE(policies().empty());

  auto again = del({"RM100"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP routing-policy RM100 does not exist; nothing to "
          "delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  EXPECT_TRUE(policies().empty());
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTestFixture,
    deleteUnknownLeavesOthersIntact) {
  configure({"RM100", "description", "one"});
  auto result = del({"RM200"});
  EXPECT_THAT(result, HasSubstr("does not exist"));
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
}

// ==============================================================================
// Reference guard — a policy a neighbor, peer-group or network6 still names is
// not deletable
// ==============================================================================

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTestFixture,
    deleteReferencedPolicyRejected) {
  configure({"RM100", "description", "in-use"});
  configureNeighbor({"10.0.0.2", "ingress-policy", "RM100"});

  auto result = del({"RM100"});
  EXPECT_THAT(result, HasSubstr("still referenced"));
  // The refusal names the referrer so the user can act on it.
  EXPECT_THAT(result, HasSubstr("neighbor 10.0.0.2 ingress"));
  EXPECT_THAT(result, HasSubstr("re-point or remove those references first"));
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
  EXPECT_EQ(*policies()[0].description(), "in-use");
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTestFixture,
    deleteReferencedPolicyNamesEveryReferrer) {
  configure({"RM100"});
  configureNeighbor({"10.0.0.2", "egress-policy", "RM100"});
  configurePeerGroup({"SPINE", "ingress-policy", "RM100"});
  configurePeerGroup({"SPINE", "egress-policy", "RM100"});
  configureGlobal({"network6", "add", "2001:db8::/64", "policy", "RM100"});

  auto result = del({"RM100"});
  EXPECT_THAT(result, HasSubstr("neighbor 10.0.0.2 egress"));
  EXPECT_THAT(result, HasSubstr("peer-group SPINE ingress"));
  EXPECT_THAT(result, HasSubstr("peer-group SPINE egress"));
  EXPECT_THAT(result, HasSubstr("network6 2001:db8::/64"));
  ASSERT_EQ(policies().size(), 1);
}

TEST_F(
    CmdDeleteBgpPolicyRoutingPolicyTestFixture,
    referenceToOtherPolicyDoesNotBlockDelete) {
  configure({"RM100"});
  configure({"RM200"});
  // Referrers of RM100 must not block deleting RM200.
  configureNeighbor({"10.0.0.2", "ingress-policy", "RM100"});
  configurePeerGroup({"SPINE", "egress-policy", "RM100"});
  configureGlobal({"network6", "add", "2001:db8::/64", "policy", "RM100"});

  auto result = del({"RM200"});
  EXPECT_THAT(
      result, HasSubstr("Successfully deleted BGP routing-policy RM200"));
  ASSERT_EQ(policies().size(), 1);
  EXPECT_EQ(*policies()[0].name(), "RM100");
}

} // namespace facebook::fboss
