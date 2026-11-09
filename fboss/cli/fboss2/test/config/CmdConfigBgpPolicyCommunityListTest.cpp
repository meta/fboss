// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/community-list/CmdConfigProtocolBgpPolicyCommunityList.h"
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
using facebook::bgp::routing_policy::BooleanOperator;

namespace facebook::fboss {

// The community-list dispatcher only touches the BGP side of ConfigSession,
// which seeds from thrift schema defaults when neither a staged session nor a
// system bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyAsPathListTest).
class CmdConfigBgpPolicyCommunityListTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyCommunityListTestFixture()
      : CmdConfigTestBase("bgp_community_list_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string run(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyCommunityList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpCommunityListConfig(tokens));
  }

  const std::vector<bgp::bgp_policy::CommunityList>& lists() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .community_lists();
  }

  // Seed a routing-policy term whose COMMUNITY match names `listName`, the
  // way bgpd reads the reference. Read the inline filter back via filterOf():
  // the vectors may reallocate on the next seed.
  void addPolicyTermMatching(
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
    matches.back().type() =
        bgp::bgp_policy::BgpPolicyAtomicMatchType::COMMUNITY_LIST;
    matches.back().communities_filter().ensure().community_list_names() = {
        listName};
    ConfigSession::getInstance().saveBgpConfig();
  }

  const bgp::bgp_policy::CommunityList& filterOf(const std::string& policy) {
    auto& policies = *ConfigSession::getInstance()
                          .getBgpConfig()
                          .policies()
                          ->bgp_policy_statements();
    auto it = std::find_if(policies.begin(), policies.end(), [&](auto& p) {
      return *p.name() == policy;
    });
    return *(*(*it->policy_entries())[0]
                  .policy_match_entries()
                  ->match_entries())[0]
                .communities_filter();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpCommunityListConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, argValidation) {
  // Bare create list.
  auto bareList = BgpCommunityListConfig({"CL100"});
  EXPECT_EQ(bareList.listName(), "CL100");
  EXPECT_TRUE(bareList.attr().empty());

  // List-level attribute.
  auto listAttr =
      BgpCommunityListConfig({"CL100", "description", "my", "list"});
  EXPECT_EQ(listAttr.attr(), "description");
  EXPECT_EQ(listAttr.values(), std::vector<std::string>({"my", "list"}));

  // Invalid: empty, empty name, unknown attribute.
  EXPECT_THROW(BgpCommunityListConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpCommunityListConfig({""}), std::invalid_argument);
  EXPECT_THROW(
      BgpCommunityListConfig({"CL100", "no-such-attr", "1"}),
      std::invalid_argument);
  // value was the removed community subcommand's attribute; bgpd never read
  // the members it wrote, so it must not be accepted anywhere.
  EXPECT_THROW(
      BgpCommunityListConfig({"CL100", "value", "65000:100"}),
      std::invalid_argument);

  // List-level community and boolean-operator are recognized.
  EXPECT_EQ(
      BgpCommunityListConfig({"CL100", "community", "65000:100"}).attr(),
      "community");
  EXPECT_EQ(
      BgpCommunityListConfig({"CL100", "boolean-operator", "AND"}).attr(),
      "boolean-operator");
}

// ==============================================================================
// List-level handlers
// ==============================================================================

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, bareCreateList) {
  auto result = run({"CL100"});
  EXPECT_THAT(
      result, HasSubstr("Successfully created BGP community-list CL100"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "CL100");
  EXPECT_FALSE(lists()[0].communities().has_value());
  EXPECT_TRUE(sessionFileExists());

  run({"CL200"});
  ASSERT_EQ(lists().size(), 2);
  EXPECT_EQ(*lists()[1].name(), "CL200");
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, setListDescription) {
  auto result = run({"CL100", "description", "spine", "communities"});
  EXPECT_THAT(result, HasSubstr("Successfully set description"));
  ASSERT_EQ(lists().size(), 1);
  // Multi-token description is re-joined.
  EXPECT_EQ(*lists()[0].description(), "spine communities");
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, setBooleanOperator) {
  auto result = run({"CL100", "boolean-operator", "AND"});
  EXPECT_THAT(result, HasSubstr("Successfully set boolean-operator to: AND"));
  EXPECT_EQ(*lists()[0].boolean_operator(), BooleanOperator::AND);

  run({"CL100", "boolean-operator", "OR"});
  EXPECT_EQ(*lists()[0].boolean_operator(), BooleanOperator::OR);
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, booleanOperatorNotRejected) {
  auto result = run({"CL100", "boolean-operator", "NOT"});
  EXPECT_THAT(result, HasSubstr("Invalid boolean-operator value 'NOT'"));
  EXPECT_THAT(result, HasSubstr("AND|OR"));
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, booleanOperatorDefaultIsOr) {
  run({"CL100"});
  EXPECT_EQ(*lists()[0].boolean_operator(), BooleanOperator::OR);
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, setExactMatch) {
  auto result = run({"CL100", "exact-match", "true"});
  EXPECT_THAT(result, HasSubstr("Successfully enabled exact-match"));
  ASSERT_TRUE(lists()[0].exact_match().has_value());
  EXPECT_TRUE(*lists()[0].exact_match());

  auto disabled = run({"CL100", "exact-match", "false"});
  EXPECT_THAT(disabled, HasSubstr("Successfully disabled exact-match"));
  EXPECT_FALSE(*lists()[0].exact_match());
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, namedListsAreDistinct) {
  run({"CL100", "description", "one"});
  run({"CL200", "description", "two"});
  ASSERT_EQ(lists().size(), 2);
  // Re-referencing an existing list by name updates it, not appends.
  run({"CL100", "description", "one-updated"});
  ASSERT_EQ(lists().size(), 2);
  EXPECT_EQ(*lists()[0].description(), "one-updated");
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, reReferenceReportsExisting) {
  run({"CL100"});
  // A second bare reference must not claim to have created it again.
  EXPECT_THAT(run({"CL100"}), HasSubstr("already exists"));
  ASSERT_EQ(lists().size(), 1);
}

// ==============================================================================
// Reject paths — the error is surfaced and nothing is persisted
// ==============================================================================

TEST_F(
    CmdConfigBgpPolicyCommunityListTestFixture,
    invalidBooleanOperatorRejected) {
  auto result = run({"CL100", "boolean-operator", "XOR"});
  // The full message matters: the accepted-values suffix once printed garbage
  // (a string_view capture dangling over a fmt::format temporary).
  EXPECT_THAT(
      result,
      HasSubstr("Invalid boolean-operator value 'XOR'; expected AND|OR"));
  // The rejected value must not leave a phantom list behind.
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, invalidExactMatchRejected) {
  auto result = run({"CL100", "exact-match", "maybe"});
  EXPECT_THAT(result, HasSubstr("Invalid"));
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

// ==============================================================================
// community — communities, the field bgpd matches against
// ==============================================================================

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, communityAppends) {
  auto result = run({"CL100", "community", "65000:100"});
  EXPECT_THAT(result, HasSubstr("Successfully added community 65000:100"));
  EXPECT_THAT(result, HasSubstr("for community-list CL100"));
  run({"CL100", "community", "65000:200"});
  ASSERT_EQ(lists().size(), 1);
  // Regression guard for the inert-config bug: the value must land in the
  // flat `communities` list bgpd reads, never in the dead `members`.
  ASSERT_TRUE(lists()[0].communities().has_value());
  EXPECT_EQ(
      *lists()[0].communities(),
      std::vector<std::string>({"65000:100", "65000:200"}));
  EXPECT_FALSE(lists()[0].members().has_value());
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, communityAcceptsRegex) {
  // bgpd compiles a non-literal value as a regex, so one is accepted here.
  auto result = run({"CL100", "community", "^65000:.*$"});
  EXPECT_THAT(result, HasSubstr("Successfully added community ^65000:.*$"));
  EXPECT_EQ(
      *lists()[0].communities(), std::vector<std::string>({"^65000:.*$"}));
}

TEST_F(
    CmdConfigBgpPolicyCommunityListTestFixture,
    communityAddAgainIsIdempotent) {
  run({"CL100", "community", "65000:100"});
  auto result = run({"CL100", "community", "65000:100"});
  EXPECT_THAT(result, HasSubstr("already present"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(lists()[0].communities()->size(), 1);
}

TEST_F(CmdConfigBgpPolicyCommunityListTestFixture, malformedCommunityRejected) {
  // Neither a literal nor a compilable regex: bgpd would refuse it at load.
  auto result = run({"CL100", "community", "65000:("});
  EXPECT_THAT(result, HasSubstr("Error: Malformed community"));
  // A rejected first attribute leaves no phantom list and nothing on disk.
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyCommunityListTestFixture,
    communityWithWhitespaceRejected) {
  run({"CL100"});
  auto result = run({"CL100", "community", "65000:100 65000:200"});
  EXPECT_THAT(result, HasSubstr("must not contain whitespace"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_FALSE(lists()[0].communities().has_value());

  EXPECT_THAT(run({"CL100", "community"}), HasSubstr("requires <community>"));
  EXPECT_THAT(
      run({"CL100", "community", "65000:100", "65000:200"}),
      HasSubstr("requires <community>"));
}

// ==============================================================================
// Reference sync — bgpd reads the operator and exact_match from the term's
// inline copy of the list
// ==============================================================================

TEST_F(
    CmdConfigBgpPolicyCommunityListTestFixture,
    setBooleanOperatorSyncsReferencingFilters) {
  run({"CL100"});
  run({"CL200"});
  addPolicyTermMatching("RM100", 10, "CL100");
  addPolicyTermMatching("RM200", 20, "CL200");

  run({"CL100", "boolean-operator", "AND"});
  EXPECT_EQ(*filterOf("RM100").boolean_operator(), BooleanOperator::AND);
  // A filter naming another list is untouched.
  EXPECT_EQ(*filterOf("RM200").boolean_operator(), BooleanOperator::OR);

  run({"CL100", "boolean-operator", "OR"});
  EXPECT_EQ(*filterOf("RM100").boolean_operator(), BooleanOperator::OR);
}

TEST_F(
    CmdConfigBgpPolicyCommunityListTestFixture,
    setExactMatchSyncsReferencingFilters) {
  run({"CL100"});
  addPolicyTermMatching("RM100", 10, "CL100");
  EXPECT_FALSE(filterOf("RM100").exact_match().has_value());

  run({"CL100", "exact-match", "true"});
  EXPECT_EQ(*filterOf("RM100").boolean_operator(), BooleanOperator::OR);
  ASSERT_TRUE(filterOf("RM100").exact_match().has_value());
  EXPECT_TRUE(*filterOf("RM100").exact_match());

  run({"CL100", "exact-match", "false"});
  EXPECT_FALSE(*filterOf("RM100").exact_match());
}

} // namespace facebook::fboss
