// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/CmdConfigProtocolBgpPolicyAsPathList.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/bgp_policy_types.h>
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h>
#include <configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#else
#include <neteng/fboss/bgp/public_tld/configerator/structs/neteng/fboss/bgp/gen-cpp2/bgp_config_types.h>
#endif

using namespace ::testing;

namespace facebook::fboss {

// The as-path-list dispatcher only touches the BGP side of ConfigSession,
// which seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPeerGroupTest).
class CmdConfigBgpPolicyAsPathListTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyAsPathListTestFixture()
      : CmdConfigTestBase("bgp_aspath_list_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyAsPathList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpAsPathListConfig(tokens));
  }

  const std::vector<bgp::bgp_policy::AsPathList>& lists() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .aspath_lists();
  }

  // Seed a routing-policy term whose AS_PATH match names `listName` (the
  // term/match CLIs live in higher PRs), the way bgpd reads the reference:
  // as_path_filters.as_path_list_names. Returns the seeded atomic match.
  bgp::bgp_policy::BgpPolicyAtomicMatch& addPolicyTermMatching(
      const std::string& policy,
      int64_t seq,
      const std::string& listName) {
    auto& cfg = ConfigSession::getInstance().getBgpConfig();
    auto& policies = *cfg.policies().ensure().bgp_policy_statements();
    policies.emplace_back();
    policies.back().name() = policy;
    auto& terms = *policies.back().policy_entries();
    terms.emplace_back();
    terms.back().sequence_number() = seq;
    auto& matches =
        *terms.back().policy_match_entries().ensure().match_entries();
    matches.emplace_back();
    matches.back().type() = bgp::bgp_policy::BgpPolicyAtomicMatchType::AS_PATH;
    matches.back().as_path_filters().ensure().as_path_list_names().ensure() = {
        listName};
    ConfigSession::getInstance().saveBgpConfig();
    return matches.back();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpAsPathListConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, argValidation) {
  // Bare create list.
  auto bareList = BgpAsPathListConfig({"AS100"});
  EXPECT_EQ(bareList.listName(), "AS100");
  EXPECT_TRUE(bareList.attr().empty());

  // List-level attribute.
  auto listAttr = BgpAsPathListConfig({"AS100", "description", "my", "list"});
  EXPECT_EQ(listAttr.attr(), "description");
  EXPECT_EQ(listAttr.values(), std::vector<std::string>({"my", "list"}));

  // Invalid: empty, empty name, unknown attribute.
  EXPECT_THROW(BgpAsPathListConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpAsPathListConfig({""}), std::invalid_argument);
  EXPECT_THROW(
      BgpAsPathListConfig({"AS100", "no-such-attr", "1"}),
      std::invalid_argument);
  // asn-regexp was the removed entry subcommand's attribute; bgpd never read
  // the entries it wrote, so it must not be accepted anywhere.
  EXPECT_THROW(
      BgpAsPathListConfig({"AS100", "asn-regexp", "^65000_"}),
      std::invalid_argument);

  // List-level regex and boolean-operator are recognized.
  EXPECT_EQ(BgpAsPathListConfig({"AS100", "regex", "^65000_"}).attr(), "regex");
  EXPECT_EQ(
      BgpAsPathListConfig({"AS100", "boolean-operator", "AND"}).attr(),
      "boolean-operator");
}

// ==============================================================================
// List-level handlers
// ==============================================================================

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, bareCreateList) {
  auto result = run({"AS100"});
  EXPECT_THAT(result, HasSubstr("Successfully created BGP as-path-list AS100"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "AS100");
  EXPECT_TRUE(lists()[0].as_path_list()->empty());
  EXPECT_TRUE(sessionFileExists());

  run({"AS200"});
  ASSERT_EQ(lists().size(), 2);
  EXPECT_EQ(*lists()[1].name(), "AS200");
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, setListDescription) {
  auto result = run({"AS100", "description", "spine", "as-paths"});
  EXPECT_THAT(result, HasSubstr("Successfully set description"));
  ASSERT_EQ(lists().size(), 1);
  // Multi-token description is re-joined.
  EXPECT_EQ(*lists()[0].description(), "spine as-paths");
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, namedListsAreDistinct) {
  run({"AS100", "description", "one"});
  run({"AS200", "description", "two"});
  ASSERT_EQ(lists().size(), 2);
  // Re-referencing an existing list by name updates it, not appends.
  run({"AS100", "description", "one-updated"});
  ASSERT_EQ(lists().size(), 2);
  EXPECT_EQ(*lists()[0].description(), "one-updated");
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, reReferenceReportsExisting) {
  run({"AS100"});
  // A second bare reference must not claim to have created it again.
  EXPECT_THAT(run({"AS100"}), HasSubstr("already exists"));
  ASSERT_EQ(lists().size(), 1);
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, unknownAttributeRejected) {
  EXPECT_THROW(run({"AS100", "no-such-attr", "1"}), std::invalid_argument);
  // The rejected command must not leave a phantom list behind.
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

// ==============================================================================
// regex — as_paths, the field bgpd matches against
// ==============================================================================

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, regexAppends) {
  auto result = run({"AS100", "regex", "^65000_"});
  EXPECT_THAT(result, HasSubstr("Successfully added regex ^65000_"));
  EXPECT_THAT(result, HasSubstr("for as-path-list AS100"));
  run({"AS100", "regex", "_65001$"});
  ASSERT_EQ(lists().size(), 1);
  ASSERT_TRUE(lists()[0].as_paths().has_value());
  EXPECT_EQ(
      *lists()[0].as_paths(), std::vector<std::string>({"^65000_", "_65001$"}));
  // The dead entry list is left alone.
  EXPECT_TRUE(lists()[0].as_path_list()->empty());
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, regexAddAgainIsIdempotent) {
  run({"AS100", "regex", "^65000_"});
  auto result = run({"AS100", "regex", "^65000_"});
  EXPECT_THAT(result, HasSubstr("already present"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(lists()[0].as_paths()->size(), 1);
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, malformedRegexRejected) {
  auto result = run({"AS100", "regex", "^65000_("});
  EXPECT_THAT(result, HasSubstr("Error: Malformed regex"));
  // A rejected first attribute leaves no phantom list and nothing on disk.
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, regexWithWhitespaceRejected) {
  run({"AS100"});
  auto result = run({"AS100", "regex", "65000 65001"});
  EXPECT_THAT(result, HasSubstr("must not contain whitespace"));
  EXPECT_THAT(result, HasSubstr("_"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_FALSE(lists()[0].as_paths().has_value());

  EXPECT_THAT(run({"AS100", "regex"}), HasSubstr("requires <regex>"));
  EXPECT_THAT(
      run({"AS100", "regex", "^65000_", "_65001$"}),
      HasSubstr("requires <regex>"));
}

// ==============================================================================
// boolean-operator — AND|OR, written through to referencing terms
// ==============================================================================

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, booleanOperatorAccepted) {
  auto result = run({"AS100", "boolean-operator", "AND"});
  EXPECT_THAT(result, HasSubstr("Successfully set boolean-operator to: AND"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(
      *lists()[0].boolean_operator(),
      bgp::routing_policy::BooleanOperator::AND);
  run({"AS100", "boolean-operator", "OR"});
  EXPECT_EQ(
      *lists()[0].boolean_operator(), bgp::routing_policy::BooleanOperator::OR);
}

TEST_F(CmdConfigBgpPolicyAsPathListTestFixture, booleanOperatorNotRejected) {
  auto result = run({"AS100", "boolean-operator", "NOT"});
  EXPECT_THAT(result, HasSubstr("Invalid boolean-operator value 'NOT'"));
  EXPECT_THAT(result, HasSubstr("AND|OR"));
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists());
}

TEST_F(
    CmdConfigBgpPolicyAsPathListTestFixture,
    booleanOperatorPropagatesToReferencingTerms) {
  run({"AS100"});
  run({"AS200"});
  const auto& match = addPolicyTermMatching("RM100", 10, "AS100");
  const auto& other = addPolicyTermMatching("RM100", 20, "AS200");
  EXPECT_EQ(
      *match.as_path_filters()->boolean_operator(),
      bgp::routing_policy::BooleanOperator::OR);

  run({"AS100", "boolean-operator", "AND"});
  // bgpd requires the term's inline copy to carry the list's operator.
  EXPECT_EQ(
      *match.as_path_filters()->boolean_operator(),
      bgp::routing_policy::BooleanOperator::AND);
  // A term referencing a different list is untouched.
  EXPECT_EQ(
      *other.as_path_filters()->boolean_operator(),
      bgp::routing_policy::BooleanOperator::OR);
}

} // namespace facebook::fboss
