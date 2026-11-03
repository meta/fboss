// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy prefix-list
 * <name> [<attribute> <value> ...]`.
 *
 * Scope: the prefix-list level (its seq-num-keyed entries are covered by
 * ConfigBgpPolicyPrefixListEntryTest). Every test stages the
 * change AND commits it, then asserts the value landed at the correct thrift
 * field path inside the matching .policies.prefix_lists[] entry of bgpd's
 * running config (via getRunningConfig RPC) — which also confirms bgpd
 * accepts and adopts a `.policies` blob at all. Stage-only behavior
 * (attribute parsing, validation, rejection) is covered by the unit tests; an
 * integration test that never commits exercises no daemon.
 *
 * Requirements:
 *   - The fboss2-dev binary under test (config subcommand tree).
 *   - HOME is set (the session file lives under $HOME/.fboss2).
 *   - bgpd is installed/active (commit restarts it).
 */

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
// Test-only prefix-list name, unlikely to collide with a real list in the
// device's running BGP config.
const std::string kList = "FBOSS2-TEST-PREFIXES";
} // namespace

class ConfigBgpPolicyPrefixListTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy prefix-list <tokens...>` WITHOUT
  // clearing the staged session, so attributes can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stagePrefixList(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config", "protocol", "bgp", "policy", "prefix-list"};
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
};

TEST_F(ConfigBgpPolicyPrefixListTest, SetListAttributesAndCommit) {
  discardSession();
  clearBgpSession();
  stagePrefixList({kList, "description", "test", "spine", "prefixes"});
  stagePrefixList({kList, "boolean-operator", "OR"});
  stagePrefixList({kList, "ip-version", "v4"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted the .policies blob.
  auto config = readSystemBgpConfig();
  const auto* list = findList(config, kList);
  ASSERT_NE(list, nullptr) << "committed config has no prefix-list " << kList;
  EXPECT_EQ((*list)["description"].asString(), "test spine prefixes");

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningList = findList(running, kList);
  ASSERT_NE(runningList, nullptr)
      << "bgpd's running config has no prefix-list " << kList;
  EXPECT_EQ((*runningList)["description"].asString(), "test spine prefixes");
  // Enums ride the SimpleJSON wire format as integers:
  // routing_policy.BooleanOperator.OR = 2. bgpd only validates list
  // operators from PrefixTreeMatch::PopulateReferences, i.e. once a
  // routing-policy term references the list, so an end-to-end rejection
  // test belongs with the term match command.
  EXPECT_EQ((*runningList)["boolean_operator"].asInt(), 2);
  EXPECT_EQ((*runningList)["version"].asInt(), 4);
}

TEST_F(ConfigBgpPolicyPrefixListTest, DeleteListAndCommit) {
  // Land a prefix-list in the system config, then delete it through a second
  // commit and verify it is gone from bgpd's running config.
  discardSession();
  clearBgpSession();
  stagePrefixList({kList, "description", "delete-me"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "setup commit did not land the prefix-list in bgpd's running config";

  clearBgpSession();
  auto result =
      runCli({"delete", "protocol", "bgp", "policy", "prefix-list", kList});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "deleted prefix-list still present in bgpd's running config";
}
