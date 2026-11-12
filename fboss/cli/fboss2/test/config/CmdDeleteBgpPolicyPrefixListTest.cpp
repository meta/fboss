// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/BgpPrefixListCliUtils.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/CmdConfigProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/commands/delete/protocol/bgp/policy/prefix-list/CmdDeleteProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/session/ConfigSession.h"
#include "fboss/cli/fboss2/test/config/CmdConfigTestBase.h"
#include "fboss/cli/fboss2/utils/HostInfo.h"

#ifndef IS_OSS
#include <configerator/structs/neteng/bgp_policy/thrift/gen-cpp2/routing_policy_types.h>
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
class CmdDeleteBgpPolicyPrefixListTestFixture : public CmdConfigTestBase {
 public:
  CmdDeleteBgpPolicyPrefixListTestFixture()
      : CmdConfigTestBase("bgp_prefix_list_delete_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  std::string configure(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyPrefixList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPrefixListConfig(tokens));
  }

  // Seed an entry. The entry level is its own subcommand
  // (CmdConfigProtocolBgpPolicyPrefixListEntry), which lands above this
  // command in the stack — so stage it through the shared helpers rather than
  // depending on that handler.
  void configureEntry(
      const std::string& listName,
      const std::string& basePrefix) {
    auto& session = ConfigSession::getInstance();
    auto& list =
        bgpcli::findOrCreatePrefixList(session.getBgpConfig(), listName);
    bgpcli::findOrCreatePrefixListEntry(list, basePrefix);
    session.saveBgpConfig();
  }

  std::string del(const std::vector<std::string>& tokens) {
    CmdDeleteProtocolBgpPolicyPrefixList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPrefixListRef(tokens));
  }

  const std::vector<bgp::routing_policy::PrefixList>& lists() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .prefix_lists();
  }

  // Seed a routing-policy term whose PREFIX_LIST match names `listName` (the
  // term/match CLIs live in higher PRs), the way bgpd reads the reference:
  // prefix_filters.prefix_list_names.
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
    auto term =
        std::find_if(terms.begin(), terms.end(), [&](const auto& entry) {
          return entry.sequence_number().has_value() &&
              *entry.sequence_number() == seq;
        });
    if (term == terms.end()) {
      terms.emplace_back();
      terms.back().sequence_number() = seq;
      term = std::prev(terms.end());
    }
    auto& matches = *term->policy_match_entries().ensure().match_entries();
    matches.emplace_back();
    matches.back().type() =
        bgp::bgp_policy::BgpPolicyAtomicMatchType::PREFIX_LIST;
    matches.back().prefix_filters().ensure().prefix_list_names() = {listName};
    ConfigSession::getInstance().saveBgpConfig();
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpPrefixListRef (arg) validation
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, argValidation) {
  auto listOnly = BgpPrefixListRef({"PL100"});
  EXPECT_EQ(listOnly.listName(), "PL100");
  EXPECT_FALSE(listOnly.hasPrefix());

  auto withPrefix = BgpPrefixListRef({"PL100", "prefix", "10.255.1.2/8"});
  EXPECT_EQ(withPrefix.listName(), "PL100");
  EXPECT_TRUE(withPrefix.hasPrefix());
  EXPECT_EQ(withPrefix.basePrefix(), "10.0.0.0/8");
  EXPECT_FALSE(BgpPrefixListRef({"PL100"}).hasPrefix());

  // Invalid: empty, empty name, non-`prefix` second token, missing prefix,
  // a prefix without /len or malformed, extra tokens.
  EXPECT_THROW(BgpPrefixListRef({}), std::invalid_argument);
  EXPECT_THROW(BgpPrefixListRef({""}), std::invalid_argument);
  EXPECT_THROW(BgpPrefixListRef({"PL100", "PL200"}), std::invalid_argument);
  EXPECT_THROW(BgpPrefixListRef({"PL100", "prefix"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListRef({"PL100", "prefix", "10.0.0.0"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListRef({"PL100", "prefix", "ten"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListRef({"PL100", "prefix", "10.0.0.0/8", "extra"}),
      std::invalid_argument);
}

// ==============================================================================
// queryClient: whole-list deletion
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteExistingList) {
  configureEntry("PL100", "10.0.0.0/8");
  configure({"PL200", "description", "keep"});
  ASSERT_EQ(lists().size(), 2);

  auto result = del({"PL100"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP prefix-list PL100"));
  // The other list survives untouched.
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "PL200");
  EXPECT_EQ(*lists()[0].description(), "keep");
  EXPECT_TRUE(sessionFileExists());
}

// Delete mirrors add: an absent target is a success with a warning, never an
// error, so a replayed script stays idempotent.
TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteUnknownListWarns) {
  auto result = del({"NO-SUCH-LIST"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP prefix-list NO-SUCH-LIST does not exist; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  // Nothing changed, so nothing is staged.
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteTwiceIsIdempotent) {
  configure({"PL100", "description", "delete-me"});
  EXPECT_THAT(del({"PL100"}), HasSubstr("Successfully deleted"));
  EXPECT_TRUE(lists().empty());

  auto again = del({"PL100"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP prefix-list PL100 does not exist; nothing to delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  EXPECT_TRUE(lists().empty());
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    deleteUnknownLeavesOthersIntact) {
  configure({"PL100", "description", "one"});
  auto result = del({"PL200"});
  EXPECT_THAT(result, HasSubstr("does not exist"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "PL100");
}

// ==============================================================================
// queryClient: single entry (prefix) deletion
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteExistingEntry) {
  configureEntry("PL100", "10.0.0.0/8");
  configureEntry("PL100", "192.168.0.0/16");

  auto result = del({"PL100", "prefix", "10.255.1.2/8"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Successfully deleted BGP prefix-list PL100 prefix 10.0.0.0/8"));
  // The list and its other entry survive.
  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_EQ(*(*lists()[0].prefixes())[0].base_prefix(), "192.168.0.0/16");
  EXPECT_TRUE(sessionFileExists());
}

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteLastEntryKeepsList) {
  configureEntry("PL100", "10.0.0.0/8");

  auto result = del({"PL100", "prefix", "10.0.0.0/8"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted"));
  // The list stays, shaped like one that never had entries (prefixes is a
  // non-optional list, so empty — not unset — is that shape).
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "PL100");
  EXPECT_TRUE(lists()[0].prefixes()->empty());
}

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteUnknownEntryWarns) {
  configureEntry("PL100", "10.0.0.0/8");
  // Remove the session file created by configure so its absence afterwards
  // proves the no-op delete did not persist anything new.
  ASSERT_TRUE(sessionFileExists());
  std::filesystem::remove(
      ConfigSession::getInstance().getBgpSessionConfigPath());

  auto result = del({"PL100", "prefix", "172.16.0.0/12"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP prefix-list PL100 has no prefix 172.16.0.0/12; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  EXPECT_FALSE(sessionFileExists())
      << "no-op delete must not persist a session file";
  // The existing entry is untouched.
  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_EQ(*(*lists()[0].prefixes())[0].base_prefix(), "10.0.0.0/8");
}

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteEntryTwiceIsIdempotent) {
  configureEntry("PL100", "10.0.0.0/8");
  EXPECT_THAT(
      del({"PL100", "prefix", "10.0.0.0/8"}), HasSubstr("Successfully"));
  EXPECT_TRUE(lists()[0].prefixes()->empty());

  auto again = del({"PL100", "prefix", "10.0.0.0/8"});
  EXPECT_THAT(
      again,
      HasSubstr(
          "Warning: BGP prefix-list PL100 has no prefix 10.0.0.0/8; nothing to "
          "delete"));
  EXPECT_THAT(again, Not(HasSubstr("Error:")));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_TRUE(lists()[0].prefixes()->empty());
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    deleteEntryFromUnknownListWarns) {
  auto result = del({"NO-SUCH-LIST", "prefix", "10.0.0.0/8"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Warning: BGP prefix-list NO-SUCH-LIST does not exist; nothing to "
          "delete"));
  EXPECT_THAT(result, Not(HasSubstr("Error:")));
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after a no-op delete";
}

// ==============================================================================
// Reference guard — a list a routing-policy term still names is not deletable
// ==============================================================================

TEST_F(CmdDeleteBgpPolicyPrefixListTestFixture, deleteReferencedListRejected) {
  configure({"PL100", "description", "in-use"});
  addPolicyTermMatching("RM100", 10, "PL100");

  auto result = del({"PL100"});
  EXPECT_THAT(result, HasSubstr("still referenced"));
  // The refusal names the policy and term so the user can act on it.
  EXPECT_THAT(result, HasSubstr("policy RM100 term 10"));
  EXPECT_THAT(result, HasSubstr("remove those matches first"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "PL100");
  EXPECT_EQ(*lists()[0].description(), "in-use");
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    duplicateReferencesFromOneTermAreNamedOnce) {
  configure({"PL100"});
  addPolicyTermMatching("RM100", 10, "PL100");
  addPolicyTermMatching("RM100", 10, "PL100");

  const auto result = del({"PL100"});
  const std::string label = "policy RM100 term 10";
  const auto first = result.find(label);
  ASSERT_NE(first, std::string::npos);
  EXPECT_EQ(result.find(label, first + label.size()), std::string::npos);
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    deleteReferencedListNamesEveryReferrer) {
  configure({"PL100"});
  addPolicyTermMatching("RM100", 10, "PL100");
  addPolicyTermMatching("RM200", 20, "PL100");

  auto result = del({"PL100"});
  EXPECT_THAT(result, HasSubstr("policy RM100 term 10"));
  EXPECT_THAT(result, HasSubstr("policy RM200 term 20"));
  ASSERT_EQ(lists().size(), 1);
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    referenceToOtherListDoesNotBlockDelete) {
  configure({"PL100"});
  configure({"PL200"});
  // A term referencing PL100 must not block deleting PL200.
  addPolicyTermMatching("RM100", 10, "PL100");

  auto result = del({"PL200"});
  EXPECT_THAT(result, HasSubstr("Successfully deleted BGP prefix-list PL200"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(*lists()[0].name(), "PL100");
}

TEST_F(
    CmdDeleteBgpPolicyPrefixListTestFixture,
    entryDeleteOnReferencedListAllowed) {
  // Removing one entry keeps the name defined, so the reference stays valid.
  configureEntry("PL100", "10.0.0.0/8");
  configureEntry("PL100", "192.168.0.0/16");
  addPolicyTermMatching("RM100", 10, "PL100");

  auto result = del({"PL100", "prefix", "10.0.0.0/8"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Successfully deleted BGP prefix-list PL100 prefix 10.0.0.0/8"));
  ASSERT_EQ(lists().size(), 1);
  EXPECT_EQ(lists()[0].prefixes()->size(), 1);
}

} // namespace facebook::fboss
