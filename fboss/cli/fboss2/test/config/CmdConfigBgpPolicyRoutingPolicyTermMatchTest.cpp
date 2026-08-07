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
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/routing-policy/term/match/CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

using namespace ::testing;

namespace facebook::fboss {

// The match dispatcher only touches the BGP side of ConfigSession, which
// seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyRoutingPolicyTermActionTest).
class CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture
    : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture()
      : CmdConfigTestBase(
            "bgp_routing_policy_term_match_test_%%%%-%%%%-%%%%",
            "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  // Invoke the match handler the way the framework does: the ancestors'
  // parsed args plus the match's own tokens.
  std::string runMatch(const std::vector<std::string>& matchTokens) {
    CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpRoutingPolicyConfig({"RM100"}),
        BgpRoutingPolicyTermConfig({"10"}),
        BgpRoutingPolicyTermMatchConfig(matchTokens));
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

  // The atomic match entries of the term's match object. bgpd only reads the
  // (thrift-deprecated) policy_match_entries container, so that is where the
  // CLI must write and what these tests assert on.
  const std::vector<bgp::bgp_policy::BgpPolicyAtomicMatch>& atomics() {
    return *term().policy_match_entries()->match_entries();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpRoutingPolicyTermMatchConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture, argValidation) {
  auto prefixList =
      BgpRoutingPolicyTermMatchConfig({"from", "prefix-list", "PL1"});
  EXPECT_EQ(prefixList.attr(), "from prefix-list");
  EXPECT_EQ(prefixList.values(), std::vector<std::string>({"PL1"}));

  // Invalid: empty, missing `from`, bare `from`, unknown attribute.
  EXPECT_THROW(BgpRoutingPolicyTermMatchConfig({}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermMatchConfig({"prefix-list", "PL1"}),
      std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermMatchConfig({"from"}), std::invalid_argument);
  EXPECT_THROW(
      BgpRoutingPolicyTermMatchConfig({"from", "no-such-attr", "1"}),
      std::invalid_argument);
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture,
    unsupportedMatchesRejected) {
  // These four are documented in the sheet but bgpd cannot apply them:
  // local-pref/med/next-hop have no match case at all, and community-list's
  // match validates its inline communities before anything resolves the
  // by-name reference. A term carrying any of them aborts the daemon, so they
  // must be rejected at parse time rather than staged.
  for (const auto& attr : {"community-list", "local-pref", "med", "next-hop"}) {
    EXPECT_THROW(
        BgpRoutingPolicyTermMatchConfig({"from", attr, "1"}),
        std::invalid_argument)
        << "expected `from " << attr << "` to be rejected";
  }
}

// ==============================================================================
// from <attribute> — atomic matches keyed by type
// ==============================================================================

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture, fromAsPathList) {
  auto result = runMatch({"from", "as-path-list", "ASPL1"});
  EXPECT_THAT(result, HasSubstr("Successfully set as-path-list to: ASPL1"));
  EXPECT_THAT(result, HasSubstr("for routing-policy RM100 term 10 match"));
  ASSERT_EQ(atomics().size(), 1);
  EXPECT_EQ(*atomics()[0].type(), BgpPolicyAtomicMatchType::AS_PATH);
  // The reference lives in as_path_list_names, NOT in `name` (a label bgpd
  // never resolves): AsPathMatch keeps these names and
  // PolicyManager::PopulateReferences merges in asPathListMap_'s entry.
  EXPECT_EQ(
      *atomics()[0].as_path_filters()->as_path_list_names(),
      std::vector<std::string>({"ASPL1"}));
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture, fromOrigin) {
  auto result = runMatch({"from", "origin", "EGP"});
  EXPECT_THAT(result, HasSubstr("Successfully set origin to: EGP"));
  ASSERT_EQ(atomics().size(), 1);
  EXPECT_EQ(*atomics()[0].type(), BgpPolicyAtomicMatchType::ORIGIN);
  EXPECT_EQ(*atomics()[0].origin(), Origin::EGP);

  auto invalid = runMatch({"from", "origin", "BGP"});
  EXPECT_THAT(
      invalid,
      HasSubstr("Invalid origin value 'BGP'; expected IGP|EGP|INCOMPLETE"));
  EXPECT_EQ(*atomics()[0].origin(), Origin::EGP);
}

TEST_F(CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture, fromPrefixList) {
  runMatch({"from", "prefix-list", "PL1"});
  ASSERT_EQ(atomics().size(), 1);
  EXPECT_EQ(*atomics()[0].type(), BgpPolicyAtomicMatchType::PREFIX_LIST);
  EXPECT_EQ(
      *atomics()[0].prefix_filters()->prefix_list_names(),
      std::vector<std::string>({"PL1"}));
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture,
    matchKindsAreKeyedByType) {
  // Each match kind gets one atomic entry; re-issuing a kind updates it. The
  // entries compose under the match object's default AND, which is the only
  // operator bgpd accepts for more than one entry.
  runMatch({"from", "prefix-list", "PL1"});
  runMatch({"from", "as-path-list", "ASPL1"});
  runMatch({"from", "origin", "IGP"});
  ASSERT_EQ(atomics().size(), 3);
  EXPECT_EQ(
      *term().policy_match_entries()->match_logic_type(),
      bgp::routing_policy::BooleanOperator::AND);

  runMatch({"from", "prefix-list", "PL2"});
  ASSERT_EQ(atomics().size(), 3);
  size_t prefixListEntries = 0;
  for (const auto& atomic : atomics()) {
    if (*atomic.type() == BgpPolicyAtomicMatchType::PREFIX_LIST) {
      ++prefixListEntries;
      EXPECT_EQ(
          *atomic.prefix_filters()->prefix_list_names(),
          std::vector<std::string>({"PL2"}));
    }
  }
  EXPECT_EQ(prefixListEntries, 1);
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture,
    rejectedValueRollsBackPhantoms) {
  auto result = runMatch({"from", "prefix-list"});
  EXPECT_THAT(result, HasSubstr("Error: prefix-list requires <name>"));
  // The rejected value must not leave a phantom term or policy behind.
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyRoutingPolicyTermMatchTestFixture,
    ancestorAttributeAlongsideMatchRejected) {
  // The CLI parse accepts `... term 10 description x match from origin IGP`,
  // but only the leaf runs — the term attribute must be rejected, not
  // silently dropped.
  CmdConfigProtocolBgpPolicyRoutingPolicyTermMatch cmd;
  HostInfo hostInfo("testhost");
  auto result = cmd.queryClient(
      hostInfo,
      BgpRoutingPolicyConfig({"RM100"}),
      BgpRoutingPolicyTermConfig({"10", "description", "x"}),
      BgpRoutingPolicyTermMatchConfig({"from", "origin", "IGP"}));
  EXPECT_THAT(
      result, HasSubstr("Error: configure routing-policy/term attributes"));
  EXPECT_TRUE(policies().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

} // namespace facebook::fboss
