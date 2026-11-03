// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/CmdConfigProtocolBgpPolicyPrefixList.h"
#include "fboss/cli/fboss2/commands/config/protocol/bgp/policy/prefix-list/entry/CmdConfigProtocolBgpPolicyPrefixListEntry.h"
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
using facebook::bgp::routing_policy::ComparisonOperator;
using facebook::bgp::routing_policy::MatchValueLogicOperator;

namespace facebook::fboss {

// The entry dispatcher only touches the BGP side of ConfigSession, which seeds
// from thrift schema defaults when neither a staged session nor a system
// bgpcpp.conf exists — so no seed agent config is needed (mirrors
// CmdConfigBgpPolicyPrefixListTest).
class CmdConfigBgpPolicyPrefixListEntryTestFixture : public CmdConfigTestBase {
 public:
  CmdConfigBgpPolicyPrefixListEntryTestFixture()
      : CmdConfigTestBase("bgp_prefix_list_entry_test_%%%%-%%%%-%%%%", "") {}

  void SetUp() override {
    CmdConfigTestBase::SetUp();
    setupTestableConfigSession();
  }

  // Invoke the entry handler the way the framework does: the parent's parsed
  // args plus the entry's own tokens.
  std::string runEntry(
      const std::vector<std::string>& listTokens,
      const std::vector<std::string>& entryTokens) {
    CmdConfigProtocolBgpPolicyPrefixListEntry cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(
        hostInfo,
        BgpPrefixListConfig(listTokens),
        BgpPrefixListEntryConfig(entryTokens));
  }

  std::string runList(const std::vector<std::string>& tokens) {
    CmdConfigProtocolBgpPolicyPrefixList cmd;
    HostInfo hostInfo("testhost");
    return cmd.queryClient(hostInfo, BgpPrefixListConfig(tokens));
  }

  const std::vector<bgp::routing_policy::PrefixList>& lists() {
    return *ConfigSession::getInstance()
                .getBgpConfig()
                .policies()
                .ensure()
                .prefix_lists();
  }

  // Entry `idx` of list `listIdx`.
  const bgp::routing_policy::PrefixListEntry& entry(
      size_t listIdx,
      size_t idx) {
    return lists()[listIdx].prefixes()->at(idx);
  }

  bool sessionFileExists() {
    return std::filesystem::exists(
        ConfigSession::getInstance().getBgpSessionConfigPath());
  }
};

// ==============================================================================
// BgpPrefixListEntryConfig (arg) validation
// ==============================================================================

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, argValidation) {
  // Bare create.
  auto bare = BgpPrefixListEntryConfig({"10.0.0.0/8"});
  EXPECT_EQ(bare.basePrefix(), "10.0.0.0/8");
  EXPECT_TRUE(bare.attr().empty());

  // Attribute with values.
  auto attr = BgpPrefixListEntryConfig({"10.0.0.0/8", "description", "a", "b"});
  EXPECT_EQ(attr.basePrefix(), "10.0.0.0/8");
  EXPECT_EQ(attr.attr(), "description");
  EXPECT_EQ(attr.values(), std::vector<std::string>({"a", "b"}));

  // Prefixes are stored in canonical form, including v6.
  EXPECT_EQ(
      BgpPrefixListEntryConfig({"2001:db8::1/32"}).basePrefix(),
      "2001:db8::/32");
  EXPECT_EQ(
      BgpPrefixListEntryConfig({"10.255.1.2/8"}).basePrefix(), "10.0.0.0/8");

  // Invalid: empty; a prefix without an explicit /len (folly would default
  // the mask), a non-address, an out-of-range mask, a second slash; unknown
  // attribute.
  EXPECT_THROW(BgpPrefixListEntryConfig({}), std::invalid_argument);
  EXPECT_THROW(BgpPrefixListEntryConfig({"10.0.0.0"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"not-a-prefix"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"10.0.0.0/99"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"10.0.0.0/8/8"}), std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"10.0.0.0/8", "no-such-attr", "1"}),
      std::invalid_argument);
  // base-prefix is the key, not an attribute; ip-version is a list attribute.
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"10.0.0.0/8", "base-prefix", "10.0.0.0/8"}),
      std::invalid_argument);
  EXPECT_THROW(
      BgpPrefixListEntryConfig({"10.0.0.0/8", "ip-version", "v4"}),
      std::invalid_argument);
}

// ==============================================================================
// Entry-level handlers, and the reject paths that must persist nothing
// ==============================================================================

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, bareCreateEntry) {
  auto result = runEntry({"PL100"}, {"10.0.0.0/8"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Successfully created BGP prefix-list PL100 prefix 10.0.0.0/8"));
  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  // The key is the entry's base_prefix; nothing writes seq_num, which bgpd
  // rejects.
  EXPECT_EQ(*entry(0, 0).base_prefix(), "10.0.0.0/8");
  EXPECT_FALSE(entry(0, 0).seq_num().has_value());
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    normalizesPrefixBeforeStoringAndLookup) {
  auto result = runEntry({"PL100"}, {"10.255.1.2/8"});
  EXPECT_THAT(
      result,
      HasSubstr(
          "Successfully created BGP prefix-list PL100 prefix 10.0.0.0/8"));
  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_EQ(*entry(0, 0).base_prefix(), "10.0.0.0/8");

  runEntry({"PL100"}, {"10.0.0.0/8", "description", "same-network"});
  EXPECT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_EQ(*entry(0, 0).description(), "same-network");
}

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, setAttributes) {
  runEntry({"PL100"}, {"10.0.0.0/8", "description", "spine", "block"});
  runEntry({"PL100"}, {"10.0.0.0/8", "match-logic", "EQUAL"});
  runEntry({"PL100"}, {"10.0.0.0/8", "max-allowed-subnet-count", "64"});
  runEntry({"PL100"}, {"10.0.0.0/8", "regex", "^10\\..*"});

  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  const auto& e = entry(0, 0);
  EXPECT_FALSE(e.seq_num().has_value());
  EXPECT_EQ(*e.base_prefix(), "10.0.0.0/8");
  EXPECT_EQ(*e.description(), "spine block");
  EXPECT_EQ(*e.match_logic(), MatchValueLogicOperator::EQUAL);
  EXPECT_EQ(*e.max_allowed_golden_prefix_subnet_count(), 64);
  EXPECT_EQ(*e.regex(), "^10\\..*");
}

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, setPrefixLenRange) {
  runEntry(
      {"PL100"}, {"10.0.0.0/8", "prefix-len-range", "compare-operator", "GE"});
  runEntry({"PL100"}, {"10.0.0.0/8", "prefix-len-range", "value", "24"});

  // Both sub-attributes land on the single prefix_len_ranges[0] element.
  ASSERT_EQ(entry(0, 0).prefix_len_ranges()->size(), 1);
  const auto& range = entry(0, 0).prefix_len_ranges()->front();
  EXPECT_EQ(*range.compare_operator(), ComparisonOperator::GE);
  EXPECT_EQ(*range.value(), 24);
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    valuesBgpdRejectsAreRefused) {
  runEntry(
      {"PL100"}, {"10.0.0.0/8", "prefix-len-range", "compare-operator", "GE"});
  // bgpd: toPolicyComparisonOperator() throws on RG.
  auto rg = runEntry(
      {"PL100"}, {"10.0.0.0/8", "prefix-len-range", "compare-operator", "RG"});
  EXPECT_THAT(rg, HasSubstr("expected EQ|GE|LE|NE|GT|LT"));
  EXPECT_EQ(
      *entry(0, 0).prefix_len_ranges()->front().compare_operator(),
      ComparisonOperator::GE);
  // bgpd: "Unsupported Prefix configuration: match_logic" unless EQUAL.
  auto ne = runEntry({"PL100"}, {"10.0.0.0/8", "match-logic", "NOT_EQUAL"});
  EXPECT_THAT(ne, HasSubstr("expected EQUAL"));
  EXPECT_EQ(*entry(0, 0).match_logic(), MatchValueLogicOperator::EQUAL);
}

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, communitiesAccumulate) {
  auto result = runEntry({"PL100"}, {"10.0.0.0/8", "communities", "65000:100"});
  EXPECT_THAT(result, HasSubstr("Successfully added 65000:100 to communities"));
  runEntry({"PL100"}, {"10.0.0.0/8", "communities", "65000:200"});

  ASSERT_TRUE(entry(0, 0).communities().has_value());
  EXPECT_THAT(
      *entry(0, 0).communities(),
      UnorderedElementsAre("65000:100", "65000:200"));

  // Re-adding an existing member reports it without duplicating.
  auto repeated =
      runEntry({"PL100"}, {"10.0.0.0/8", "communities", "65000:100"});
  EXPECT_THAT(repeated, HasSubstr("communities already contains 65000:100"));
  EXPECT_EQ(entry(0, 0).communities()->size(), 2);
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    entriesAccumulateAndAreKeyed) {
  runEntry({"PL100"}, {"10.0.0.0/8", "description", "a"});
  runEntry({"PL100"}, {"192.168.0.0/16", "description", "b"});
  ASSERT_EQ(lists().size(), 1);
  ASSERT_EQ(lists()[0].prefixes()->size(), 2);

  // Re-referencing an existing entry by prefix updates it, not appends.
  runEntry({"PL100"}, {"10.0.0.0/8", "match-logic", "EQUAL"});
  EXPECT_EQ(lists()[0].prefixes()->size(), 2);
  EXPECT_EQ(*entry(0, 0).base_prefix(), "10.0.0.0/8");
  EXPECT_EQ(*entry(0, 0).match_logic(), MatchValueLogicOperator::EQUAL);
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    invalidPrefixRejectedBeforeAnythingIsCreated) {
  // The prefix is validated at construction (see argValidation), so a bad
  // one never reaches queryClient and never creates a list or an entry.
  EXPECT_THROW(
      runEntry({"PL100"}, {"10.0.0.0", "description", "x"}),
      std::invalid_argument);
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(CmdConfigBgpPolicyPrefixListEntryTestFixture, prefixAcceptsV6) {
  auto result = runEntry({"PL100"}, {"2001:db8::1/32"});
  EXPECT_THAT(result, HasSubstr("Successfully created"));
  EXPECT_EQ(*entry(0, 0).base_prefix(), "2001:db8::/32");
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    invalidPrefixLenRangeRejected) {
  // Unknown sub-attribute.
  auto badSub =
      runEntry({"PL100"}, {"10.0.0.0/8", "prefix-len-range", "min", "8"});
  EXPECT_THAT(
      badSub,
      HasSubstr(
          "Error: prefix-len-range requires <compare-operator|value> <value>"));
  // Out-of-range length.
  auto badLen =
      runEntry({"PL100"}, {"10.0.0.0/8", "prefix-len-range", "value", "129"});
  EXPECT_THAT(
      badLen,
      HasSubstr("Invalid prefix-len-range value value '129'; expected 0-128"));
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists())
      << "session file should not exist after rejected input";
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    rejectedPrefixLenRangeKeepsNoPhantomRange) {
  // Land the entry first, then reject a range value on it: the entry survives
  // but no phantom prefix_len_ranges element may appear.
  runEntry({"PL100"}, {"10.0.0.0/8"});
  auto result =
      runEntry({"PL100"}, {"10.0.0.0/8", "prefix-len-range", "value", "300"});
  EXPECT_THAT(result, HasSubstr("Invalid"));
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_TRUE(entry(0, 0).prefix_len_ranges()->empty());
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    rejectedEntryOnExistingListKeepsList) {
  // Create the list first, then reject an entry value on it.
  runList({"PL100", "description", "keep-me"});
  ASSERT_EQ(lists().size(), 1);

  auto result = runEntry({"PL100"}, {"10.0.0.0/8", "match-logic", "MAYBE"});
  EXPECT_THAT(result, HasSubstr("Invalid"));
  // The pre-existing list survives; only the phantom entry is rolled back.
  ASSERT_EQ(lists().size(), 1);
  EXPECT_TRUE(lists()[0].prefixes()->empty());
  EXPECT_EQ(*lists()[0].description(), "keep-me");
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    rejectedEntryKeepsExistingEntries) {
  runEntry({"PL100"}, {"10.0.0.0/8"});
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);

  auto result = runEntry({"PL100"}, {"192.168.0.0/16", "match-logic", "MAYBE"});
  EXPECT_THAT(result, HasSubstr("Invalid"));
  // Only the phantom entry is rolled back; the existing one survives.
  ASSERT_EQ(lists()[0].prefixes()->size(), 1);
  EXPECT_EQ(*entry(0, 0).base_prefix(), "10.0.0.0/8");
}

TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    reReferenceReportsExisting) {
  runEntry({"PL100"}, {"10.0.0.0/8"});
  EXPECT_THAT(
      runEntry({"PL100"}, {"10.0.0.0/8"}),
      HasSubstr("prefix 10.0.0.0/8 already exists"));
  EXPECT_EQ(lists()[0].prefixes()->size(), 1);
}

// A list-level attribute alongside an entry must be rejected: only the leaf
// command runs, so the list attribute would be silently dropped.
TEST_F(
    CmdConfigBgpPolicyPrefixListEntryTestFixture,
    listAttributeMixedWithEntryRejected) {
  auto result = runEntry({"PL100", "description", "mixed"}, {"10.0.0.0/8"});
  EXPECT_THAT(result, HasSubstr("separate commands"));
  EXPECT_TRUE(lists().empty());
  EXPECT_FALSE(sessionFileExists());
}

} // namespace facebook::fboss
