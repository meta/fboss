// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/as-path-list/CmdConfigProtocolBgpPolicyAsPathList.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/as-path-list/CmdDeleteProtocolBgpPolicyAsPathList.h"
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
// CmdConfigBgpPeerGroupTest).
class CmdDeleteBgpPolicyAsPathListTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteBgpPolicyAsPathListTestFixture()
      : CmdConfigTestBase("bgp_aspath_list_delete_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configure(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyAsPathList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpAsPathListConfig(tokens));
  }

  std::string del(const std::vector<std::string>& tokens) {
    CmdDeleteProtocolBgpPolicyAsPathList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpAsPathListRef(tokens));
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
  // as_path_filters.as_path_list_names.
  void addPolicyTermMatching(
      const std::string& policy,
      int64_t seq,
      const std::string& listName) {
    auto& cfg = ConfigSession::getInstance().getBgpConfig();
    auto& policies = *cfg.policies().ensure().bgp_policy_statements();
    auto it = std::find_if(policies.begin(), policies.end(), [&](auto& p) {
      return *p.name() == policy;
    });
    if (it == policies.end()) {
      policies.emplace_back();
      policies.back().name() = policy;
      it = std::prev(policies.end());
    }
    auto& terms = *it->policy_entries();
    terms.emplace_back();
    terms.back().sequence_number() = seq;
    auto& matches =
        *terms.back().policy_match_entries().ensure().match_entries();
    matches.emplace_back();
    matches.back().type() = bgp::bgp_policy::BgpPolicyAtomicMatchType::AS_PATH;
    matches.back().as_path_filters().ensure().as_path_list_names().ensure() = {
        listName};
    ConfigSession::getInstance().saveBgpConfig();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpAsPathListRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, argValidation) {
  EXPECT_EQ(BgpAsPathListRef({"AS100"}).listName(), "AS100");
  EXPECT_FALSE(BgpAsPathListRef({"AS100"}).hasRegex());

  // Optional `regex <regex>` selector.
  auto withRegex = BgpAsPathListRef({"AS100", "regex", "^65000_"});
  EXPECT_EQ(withRegex.listName(), "AS100");
  EXPECT_TRUE(withRegex.hasRegex());
  EXPECT_EQ(withRegex.regex(), "^65000_");

  // Invalid: empty, empty name, extra tokens, incomplete or trailing selector.
  EXPECT_THROW(BgpAsPathListRef({}), std::invalid_argument);
  EXPECT_THROW(BgpAsPathListRef({""}), std::invalid_argument);
  EXPECT_THROW(BgpAsPathListRef({"AS100", "AS200"}), std::invalid_argument);
  EXPECT_THROW(BgpAsPathListRef({"AS100", "regex"}), std::invalid_argument);
  EXPECT_THROW(BgpAsPathListRef({"AS100", "regex", ""}), std::invalid_argument);
  EXPECT_THROW(
      BgpAsPathListRef({"AS100", "regex", "^65000_", "extra"}),
      std::invalid_argument);
}

// ==============================================================================
// queryClient
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteExistingList) {
  configure({"AS100", "description", "delete-me"});
  configure({"AS200", "description", "keep"});
  ASSERT_EQ(lists().size(), 2);

  auto result = del({"AS100"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP as-path-list AS100"));
  // The other list survives untouched.
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "AS200");
  EXPECT_EQ(*lists()[0].description(), "keep");
  EXPECT_TRUE(sessionFileExists());
}

// Delete mirrors add: an absent target is a success with a warning, never an
// error, so a replayed script stays idempotent.
TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteUnknownListWarns) {
  auto result = del({"NO-SUCH-LIST"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP as-path-list NO-SUCH-LIST does not exist; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  // Nothing changed, so nothing is staged.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteTwiceIsIdempotent) {
  configure({"AS100", "description", "delete-me"});
  EXPECT_THAT(del({"AS100"}), HasSubstr("Successfully deleted"));
  EXPECT_TRUE(lists().empty());

  auto again = del({"AS100"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP as-path-list AS100 does not exist; nothing to delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  EXPECT_TRUE(lists().empty());
}

TEST_F(
    CmdDeleteBgpPolicyAsPathListTestFixture,
    deleteUnknownLeavesOthersIntact) {
  configure({"AS100", "description", "one"});
  auto result = del({"AS200"});
  EXPECT_THAT(result, HasSubstr("does not exist"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "AS100");
}

// ==============================================================================
// Reference guard — a list a routing-policy term still names is not deletable
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteReferencedListRejected) {
  configure({"AS100", "description", "in-use"});
  addPolicyTermMatching("RM100", 10, "AS100");

  auto result = del({"AS100"});
  EXPECT_THAT(result, HasSubstr("still referenced"));
  // The refusal names the policy and term so the user can act on it.
  EXPECT_THAT(result, HasSubstr("policy RM100 term 10"));
  EXPECT_THAT(result, HasSubstr("remove those matches first"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "AS100");
  EXPECT_EQ(*lists()[0].description(), "in-use");
}

TEST_F(
    CmdDeleteBgpPolicyAsPathListTestFixture,
    deleteReferencedListNamesEveryReferrer) {
  configure({"AS100"});
  addPolicyTermMatching("RM100", 10, "AS100");
  addPolicyTermMatching("RM200", 20, "AS100");

  auto result = del({"AS100"});
  EXPECT_THAT(result, HasSubstr("policy RM100 term 10"));
  EXPECT_THAT(result, HasSubstr("policy RM200 term 20"));
  ASSERT_EQ(lists().size(), 1);
}

TEST_F(
    CmdDeleteBgpPolicyAsPathListTestFixture,
    referenceToOtherListDoesNotBlockDelete) {
  configure({"AS100"});
  configure({"AS200"});
  // A term referencing AS100 must not block deleting AS200.
  addPolicyTermMatching("RM100", 10, "AS100");

  auto result = del({"AS200"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP as-path-list AS200"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "AS100");
}

TEST_F(
    CmdDeleteBgpPolicyAsPathListTestFixture,
    rejectedDeleteDoesNotStageWhenNothingStaged) {
  // Seed the reference without going through the session file, then confirm
  // the refused delete writes nothing.
  auto& cfg = ConfigSession::getInstance().getBgpConfig();
  auto& list = cfg.policies().ensure().aspath_lists()->emplace_back();
  list.name() = "AS100";
  auto& policies = *cfg.policies()->bgp_policy_statements();
  policies.emplace_back();
  policies.back().name() = "RM100";
  auto& term = policies.back().policy_entries()->emplace_back();
  term.sequence_number() = 10;
  auto& match =
      term.policy_match_entries().ensure().match_entries()->emplace_back();
  match.type() = bgp::bgp_policy::BgpPolicyAtomicMatchType::AS_PATH;
  match.as_path_filters().ensure().as_path_list_names().ensure() = {"AS100"};
  ASSERT_FALSE(sessionFileExists());

  auto result = del({"AS100"});
  EXPECT_THAT(result, HasSubstr("still referenced"));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected delete";
}

// ==============================================================================
// regex selector — remove one pattern, keep the list
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteOneRegex) {
  configure({"AS100", "regex", "^65000_"});
  configure({"AS100", "regex", "_65001$"});
  // Removing a pattern from a referenced list is fine: the name stays defined.
  addPolicyTermMatching("RM100", 10, "AS100");

  auto result = del({"AS100", "regex", "^65000_"});
  EXPECT_THAT(
      result,
      HasSubstr("Successfully deleted BGP as-path-list AS100 regex ^65000_"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].as_paths(), std::vector<std::string>({"_65001$"}));

  // Deleting the last pattern clears as_paths but keeps the list.
  del({"AS100", "regex", "_65001$"});
  ASSERT_EQ(lists().size(), 1);
  EXPECT_FALSE(lists()[0].as_paths().has_value());
}

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteUnknownRegexWarns) {
  configure({"AS100", "regex", "^65000_"});
  // Remove the session file created by configure so its absence afterwards
  // proves the no-op delete did not persist anything new.
  ASSERT_TRUE(sessionFileExists());
  std::filesystem::remove(
      ConfigSession::getInstance().getBgpSessionConfigPath());

  auto result = del({"AS100", "regex", "^65002_"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP as-path-list AS100 has no regex ^65002_; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  EXPECT_FALSE(sessionFileExists())
      << "no-op delete must not persist a session file";
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(lists()[0].as_paths()->size(), 1);

  auto unknownList = del({"NO-SUCH-LIST", "regex", "^65000_"});
  EXPECT_THAT(
      unknownList,
      HasSubstr(
          "Warning: BGP as-path-list NO-SUCH-LIST does not exist; nothing to "
          "delete"));
  EXPECT_THAT(unknownList, Not(HasSubstr("Error:")));
}

TEST_F(CmdDeleteBgpPolicyAsPathListTestFixture, deleteRegexTwiceIsIdempotent) {
  configure({"AS100", "regex", "^65000_"});
  EXPECT_THAT(del({"AS100", "regex", "^65000_"}), HasSubstr("Successfully"));
  EXPECT_FALSE(lists()[0].as_paths().has_value());

  auto again = del({"AS100", "regex", "^65000_"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP as-path-list AS100 has no regex ^65000_; nothing to "
          "delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_FALSE(lists()[0].as_paths().has_value());
}

} // namespace facebook::fboss
