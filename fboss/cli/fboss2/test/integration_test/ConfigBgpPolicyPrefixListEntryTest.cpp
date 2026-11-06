// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy prefix-list
 * <name> prefix <prefix/len> [<attribute> <value> ...]`.
 *
 * Scope: the entry level. Every test stages the change AND commits it, then
 * asserts the value landed at the correct thrift field path inside the
 * matching .policies.prefix_lists[].prefixes[] entry of bgpd's running config
 * (via getRunningConfig RPC). Stage-only behavior (attribute parsing,
 * validation, rejection) is covered by the unit tests; an integration test
 * that never commits exercises no daemon.
 *
 * Requirements:
 *   - The fboss2-dev binary under test (config subcommand tree).
 *   - HOME is set (the session file lives under $HOME/.fboss2).
 *   - bgpd is installed/active (commit restarts it).
 */

#include <fmt/format.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/ConfigBgpTestBase.h"
#include "folly/json/dynamic.h"
#include "gmock/gmock.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;
using ::testing::Not;

namespace {
// Test-only prefix-list / entry keys, unlikely to collide with a real list in
// the device's running BGP config.
const std::string kList = "FBOSS2-TEST-PREFIXES-ENTRY";
const std::string kPrefix = "10.0.0.0/8";
const std::string kPrefix2 = "192.168.0.0/16";
} // namespace

class ConfigBgpPolicyPrefixListEntryTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy prefix-list <list> prefix <tokens...>`
  // WITHOUT clearing the staged session, so attributes can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageEntry(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config", "protocol", "bgp", "policy", "prefix-list", kList, "prefix"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .policies.prefix_lists[] entry named `name`, or nullptr.
  static const folly::dynamic* findList(
      const folly::dynamic& config,
      const std::string& name) {
    if (config.count("policies") == 0 ||
        config["policies"].count("prefix_lists") == 0) {
      return nullptr;
    }
    for (const auto& list : config["policies"]["prefix_lists"]) {
      if (list.count("name") && list["name"].asString() == name) {
        return &list;
      }
    }
    return nullptr;
  }

  // The .prefixes[] entry with base_prefix `prefix` inside `list`, or nullptr.
  static const folly::dynamic* findEntry(
      const folly::dynamic& list,
      const std::string& prefix) {
    if (list.count("prefixes") == 0) {
      return nullptr;
    }
    for (const auto& entry : list["prefixes"]) {
      if (entry.count("base_prefix") &&
          entry["base_prefix"].asString() == prefix) {
        return &entry;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyPrefixListEntryTest, SetAttributesAndCommit) {
  discardSession();
  clearBgpSession();
  stageEntry({kPrefix, "match-logic", "EQUAL"});
  stageEntry({kPrefix, "prefix-len-range", "compare-operator", "GE"});
  stageEntry({kPrefix, "prefix-len-range", "value", "24"});
  stageEntry({kPrefix, "communities", "65000:100"});
  stageEntry({kPrefix, "description", "test"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Verify the nested entry paths .prefixes[].{base_prefix, match_logic,
  // prefix_len_ranges[0], communities, description} through the daemon's own
  // view of its config.
  auto running = readRunningBgpConfigViaRpc();
  const auto* list = findList(running, kList);
  ASSERT_NE(list, nullptr) << "bgpd's running config has no prefix-list "
                           << kList;
  const auto* entry = findEntry(*list, kPrefix);
  ASSERT_NE(entry, nullptr) << "running config has no prefix " << kPrefix;
  EXPECT_EQ((*entry)["base_prefix"].asString(), kPrefix);
  // The key is the prefix; nothing writes seq_num, which bgpd rejects.
  EXPECT_EQ(entry->count("seq_num"), 0);
  // routing_policy.MatchValueLogicOperator.EQUAL = 0 (integer on the
  // SimpleJSON wire). bgpd rejects any other value once a policy references
  // the list.
  EXPECT_EQ((*entry)["match_logic"].asInt(), 0);
  ASSERT_EQ((*entry)["prefix_len_ranges"].size(), 1);
  // routing_policy.ComparisonOperator.GE = 2.
  EXPECT_EQ((*entry)["prefix_len_ranges"][0]["compare_operator"].asInt(), 2);
  EXPECT_EQ((*entry)["prefix_len_ranges"][0]["value"].asInt(), 24);
  ASSERT_EQ((*entry)["communities"].size(), 1);
  EXPECT_EQ((*entry)["communities"][0].asString(), "65000:100");
  EXPECT_EQ((*entry)["description"].asString(), "test");
}

TEST_F(ConfigBgpPolicyPrefixListEntryTest, DeleteEntryAndCommit) {
  // Land a prefix-list with two entries, delete one entry through a second
  // commit, and verify the list and the other entry survive in bgpd's running
  // config.
  discardSession();
  clearBgpSession();
  stageEntry({kPrefix});
  stageEntry({kPrefix2});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  {
    auto running = readRunningBgpConfigViaRpc();
    const auto* list = findList(running, kList);
    ASSERT_NE(list, nullptr)
        << "setup commit did not land the prefix-list in bgpd's running "
           "config";
    ASSERT_NE(findEntry(*list, kPrefix), nullptr);
    ASSERT_NE(findEntry(*list, kPrefix2), nullptr);
  }

  clearBgpSession();
  auto result = runCli(
      {"delete",
       "protocol",
       "bgp",
       "policy",
       "prefix-list",
       kList,
       "prefix",
       kPrefix});
  EXPECT_THAT(
      result.stdout,
      HasSubstr(
          fmt::format(
              "Successfully deleted BGP prefix-list {} prefix {}",
              kList,
              kPrefix)));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* list = findList(running, kList);
  ASSERT_NE(list, nullptr)
      << "deleting one entry must not delete the prefix-list";
  EXPECT_EQ(findEntry(*list, kPrefix), nullptr)
      << "deleted entry still present in bgpd's running config";
  EXPECT_NE(findEntry(*list, kPrefix2), nullptr)
      << "surviving entry missing from bgpd's running config";
}
