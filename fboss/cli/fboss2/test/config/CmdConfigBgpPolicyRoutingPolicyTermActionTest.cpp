// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/CmdConfigProtocolBgpPolicyRoutingPolicyTerm.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/action/CmdConfigProtocolBgpPolicyRoutingPolicyTermAction.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// The action dispatcher only touches the BGP side of ConfigSession, which
// seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyRoutingPolicyTermTest).
class CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture
    : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture()
      : CmdConfigTestBase(
            "bgp_routing_policy_term_action_test_%%%%-%%%%-%%%%",
            "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  // Invoke the action leaves the way the framework does: the ancestors'
  // parsed args plus the leaf's own tokens.
  std::string runResult(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyConfig({"RM100"}),
        BgpRoutingPolicyTermConfig({"10"}),
        BgpRoutingPolicyTermActionResultConfig(tokens));
  }

  std::string runSet(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicyTermActionSet cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyConfig({"RM100"}),
        BgpRoutingPolicyTermConfig({"10"}),
        BgpRoutingPolicyTermActionSetConfig(tokens));
  }

  const std::vector<bgp::bgp_policy::BgpPolicyStatement>& policies() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .bgp_policy_statements();
  }

  const bgp::bgp_policy::BgpPolicyTerm& term() {
    return (*policies()[0].policy_entries())[0];
  }

  const std::vector<bgp::bgp_policy::BgpPolicyAction>& actions() {
    return *term().policy_action_entries();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// Arg validation (both action leaves)
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, argValidation) {
  // result.
  auto result = BgpRoutingPolicyTermActionResultConfig({"ACCEPT"});
  EXPECT_EQ(result.values(), std::vector<std::string>({"ACCEPT"}));

  // Single-token set attribute.
  auto localPref = BgpRoutingPolicyTermActionSetConfig({"local-pref", "200"});
  EXPECT_EQ(localPref.attr(), "local-pref");
  EXPECT_EQ(localPref.values(), std::vector<std::string>({"200"}));

  // Composed two-token set attribute.
  auto prepend = BgpRoutingPolicyTermActionSetConfig(
      {"as-path", "prepend", "65000", "65000"});
  EXPECT_EQ(prepend.attr(), "as-path prepend");
  EXPECT_EQ(prepend.values(), std::vector<std::string>({"65000", "65000"}));

  // Invalid: empty, unknown set attribute, `as-path` without `prepend`.
  EXPECT_THROW(
      BgpRoutingPolicyTermActionResultConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpRoutingPolicyTermActionSetConfig({}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermActionSetConfig({"no-such-attr", "1"}),
      std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermActionSetConfig({"as-path", "65000"}),
      std::invalid_argument);
}

// ==============================================================================
// result
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setResult) {
  // Each CLI value maps onto its FlowControlAction arm; the policy and term
  // are implicitly created.
  runResult({"ACCEPT"});
  EXPECT_EQ(*term().term_miss_action(), FlowControlAction::ACCEPT);

  runResult({"REJECT"});
  EXPECT_EQ(*term().term_miss_action(), FlowControlAction::DENY);

  auto result = runResult({"CONTINUE"});
  EXPECT_THAT(result, HasSubstr("Successfully set result to: CONTINUE"));
  EXPECT_THAT(result, HasSubstr("for routing-policy RM100 term 10"));
  EXPECT_EQ(*term().term_miss_action(), FlowControlAction::NEXT_TERM);
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, gotoTermRejected) {
  // GOTO-TERM is documented but deferred (no FlowControlAction arm; the
  // thrift models it per-action-entry). It must be rejected, not mismapped.
  auto result = runResult({"GOTO-TERM"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Invalid result value 'GOTO-TERM'; expected "
          "ACCEPT|REJECT|CONTINUE"));
  // The rejected value must not leave a phantom term or policy behind.
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

// ==============================================================================
// set <attribute> — each writes its own action entry
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setAsPathPrepend) {
  auto result = runSet({"as-path", "prepend", "65000", "65000", "65000"});
  EXPECT_THAT(result, HasSubstr("Successfully set as-path prepend"));
  ASSERT_EQ(actions().size(), 1);
  // bgpd dispatches on the (deprecated, still load-bearing) type field.
  EXPECT_EQ(*actions()[0].type(), BgpPolicyActionType::AS_PATH_PREPEND);
  const auto& prepend = *actions()[0].set_as_path_prepend();
  EXPECT_EQ(*prepend.asn(), 65000);
  EXPECT_EQ(*prepend.repeat_times(), 3);
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture,
    mixedAsnPrependRejected) {
  // SetAsPathPrepend models one ASN repeated N times; a mixed list has no
  // thrift representation and must be rejected.
  auto result = runSet({"as-path", "prepend", "65000", "65001"});
  EXPECT_THAT(result, HasSubstr("single ASN repeated"));
  EXPECT_TRUE(policies().empty());
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setCommunity) {
  auto result = runSet({"community", "65000:100"});
  EXPECT_THAT(result, HasSubstr("Successfully set community to: 65000:100"));
  ASSERT_EQ(actions().size(), 1);
  const auto& action = actions()[0];
  // bgpd dispatches on the (deprecated, still load-bearing) type field and
  // reads community_action.communities.
  EXPECT_EQ(*action.type(), BgpPolicyActionType::COMMUNITY_LIST);
  // Overwrite semantics without `additive`.
  EXPECT_EQ(
      *action.community_action()->action_type(), CommunityActionType::SET);
  EXPECT_EQ(
      *action.community_action()->communities(),
      std::vector<std::string>({"65000:100"}));

  // `additive` switches to append semantics on the same entry.
  runSet({"community", "65000:200", "additive"});
  ASSERT_EQ(actions().size(), 1);
  EXPECT_EQ(
      *actions()[0].community_action()->action_type(),
      CommunityActionType::ADD);
  EXPECT_EQ(
      *actions()[0].community_action()->communities(),
      std::vector<std::string>({"65000:200"}));
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setLocalPref) {
  runSet({"local-pref", "4294967295"});
  ASSERT_EQ(actions().size(), 1);
  EXPECT_EQ(*actions()[0].set_local_pref()->local_pref(), 4294967295);

  // Out-of-range is rejected without touching the stored value.
  auto result = runSet({"local-pref", "4294967296"});
  EXPECT_THAT(result, HasSubstr("local-pref requires <0-4294967295>"));
  EXPECT_EQ(*actions()[0].set_local_pref()->local_pref(), 4294967295);
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setMed) {
  auto result = runSet({"med", "500"});
  EXPECT_THAT(result, HasSubstr("Successfully set med to: 500"));
  ASSERT_EQ(actions().size(), 1);
  const auto& med = *actions()[0].med_action();
  EXPECT_EQ(*med.med_value(), 500);
  EXPECT_EQ(*med.med_action_type(), MedActionType::SET);
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setNextHop) {
  runSet({"next-hop", "2001:db8::1"});
  ASSERT_EQ(actions().size(), 1);
  const auto& nexthop = *actions()[0].set_nexthop();
  EXPECT_FALSE(*nexthop.set_self());
  EXPECT_EQ(*nexthop.next_hop()->next_hop_prefix(), "2001:db8::1");
  EXPECT_EQ(*nexthop.next_hop()->version(), 6);

  // bgpd's SetNexthop validator rejects set_self, so the CLI must reject
  // `self` up front instead of staging a config the daemon aborts on.
  auto selfResult = runSet({"next-hop", "self"});
  EXPECT_THAT(selfResult, HasSubstr("not supported by bgpd yet"));
  EXPECT_FALSE(*actions()[0].set_nexthop()->set_self());

  auto result = runSet({"next-hop", "not-an-ip"});
  EXPECT_THAT(result, HasSubstr("Invalid next-hop value 'not-an-ip'"));
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setOrigin) {
  auto result = runSet({"origin", "IGP"});
  EXPECT_THAT(result, HasSubstr("Successfully set origin to: IGP"));
  ASSERT_EQ(actions().size(), 1);
  EXPECT_EQ(*actions()[0].set_origin(), Origin::IGP);
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture, setWeight) {
  runSet({"weight", "65535"});
  ASSERT_EQ(actions().size(), 1);
  const auto& weight = *actions()[0].weight_action();
  EXPECT_EQ(*weight.weight_value(), 65535);
  EXPECT_EQ(*weight.weight_action_type(), WeightActionType::SET);

  auto result = runSet({"weight", "65536"});
  EXPECT_THAT(result, HasSubstr("weight requires <0-65535>"));
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture,
    actionKindsGetTheirOwnEntries) {
  // Each action kind lives in its own policy_action_entries[] entry;
  // re-issuing a kind updates its entry instead of appending.
  runSet({"local-pref", "100"});
  runSet({"med", "50"});
  runSet({"origin", "EGP"});
  ASSERT_EQ(actions().size(), 3);

  runSet({"local-pref", "200"});
  ASSERT_EQ(actions().size(), 3);
  size_t localPrefEntries = 0;
  for (const auto& action : actions()) {
    if (action.set_local_pref().has_value()) {
      ++localPrefEntries;
      EXPECT_EQ(*action.set_local_pref()->local_pref(), 200);
    }
  }
  EXPECT_EQ(localPrefEntries, 1);
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture,
    rejectedValueRollsBackPhantoms) {
  auto result = runSet({"local-pref", "not-a-number"});
  EXPECT_THAT(result, HasSubstr("local-pref requires"));
  // The rejected value must not leave a phantom term or policy behind.
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermActionTestFixture,
    ancestorAttributeAlongsideActionRejected) {
  // The CLI parse accepts `... term 10 description x action result ACCEPT`,
  // but only the leaf runs — the term attribute must be rejected, not
  // silently dropped.
  CmdConfigProtocolBgpPolicyRoutingPolicyTermActionResult cmd;
  HostInfo hostInfo("testhost");
  auto result = cmd.queryClient(
      hostInfo,
      BgpRoutingPolicyConfig({"RM100"}),
      BgpRoutingPolicyTermConfig({"10", "description", "x"}),
      BgpRoutingPolicyTermActionResultConfig({"ACCEPT"}));
  EXPECT_THAT(
      result, HasSubstr("Error: configure routing-policy/term attributes"));
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

} // namespace facebook::fboss
