// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy as-path-list
 * <name> [<attribute> <value> ...]`.
 *
 * Scope: the whole AS-path list, including its flat `regex` patterns (the
 * as_paths field bgpd matches on). Every test stages the change AND
 * commits it, then asserts the value landed at the correct thrift field path
 * inside the matching .policies.aspath_lists[] entry of bgpd's running config
 * (via getRunningConfig RPC) — which also confirms bgpd accepts and adopts a
 * `.policies` blob at all. Stage-only behavior (attribute parsing,
 * validation, rejection) is covered by the unit tests; an integration test
 * that never commits exercises no daemon.
 *
 * Requirements:
 *   - The fboss2-dev binary under test (config subcommand tree).
 *   - HOME is set (the session file lives under $HOME/.fboss2).
 *   - bgpd is installed/active (commit restarts it).
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

#include "fboss/cli/fboss2/test/integration_test/ConfigBgpTestBase.h"
#include "folly/json/dynamic.h"
#include "gmock/gmock.h"

using namespace facebook::fboss;
using ::testing::HasSubstr;
using ::testing::Not;

namespace {
// Test-only AS-path-list names, unlikely to collide with a real list in the
// device's running BGP config.
const std::string kList = "FBOSS2-TEST-ASPATH";
} // namespace

class ConfigBgpPolicyAsPathListTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy as-path-list <tokens...>` WITHOUT
  // clearing the staged session, so attributes can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageAsPathList(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config", "protocol", "bgp", "policy", "as-path-list"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .policies.aspath_lists[] entry named `name`, or nullptr.
  static const folly::dynamic* findList(
      const folly::dynamic& config,
      const std::string& name) {
    if (config.count("policies") == 0 ||
        config["policies"].count("aspath_lists") == 0) {
      return nullptr;
    }
    for (const auto& list : config["policies"]["aspath_lists"]) {
      if (list.count("name") && list["name"].asString() == name) {
        return &list;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyAsPathListTest, SetListDescriptionAndCommit) {
  discardSession();
  clearBgpSession();
  stageAsPathList({kList, "description", "test", "spine", "as-paths"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted the .policies blob.
  auto config = readSystemBgpConfig();
  const auto* list = findList(config, kList);
  ASSERT_NE(list, nullptr) << "committed config has no as-path-list " << kList;
  EXPECT_EQ((*list)["description"].asString(), "test spine as-paths");

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningList = findList(running, kList);
  ASSERT_NE(runningList, nullptr)
      << "bgpd's running config has no as-path-list " << kList;
  EXPECT_EQ((*runningList)["description"].asString(), "test spine as-paths");
}

TEST_F(ConfigBgpPolicyAsPathListTest, SetRegexAndOperatorAndCommit) {
  discardSession();
  clearBgpSession();
  stageAsPathList({kList, "regex", "^65000_"});
  stageAsPathList({kList, "regex", "_65001$"});
  stageAsPathList({kList, "boolean-operator", "AND"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // bgpd matches on as_paths + boolean_operator (not the entry list); the
  // running config proves it parsed and adopted both.
  auto running = readRunningBgpConfigViaRpc();
  const auto* list = findList(running, kList);
  ASSERT_NE(list, nullptr) << "bgpd's running config has no as-path-list "
                           << kList;
  ASSERT_TRUE(list->count("as_paths"));
  std::vector<std::string> paths;
  for (const auto& p : (*list)["as_paths"]) {
    paths.push_back(p.asString());
  }
  EXPECT_EQ(paths, std::vector<std::string>({"^65000_", "_65001$"}));
  ASSERT_TRUE(list->count("boolean_operator"));
  // routing_policy.BooleanOperator.AND == 1
  EXPECT_EQ((*list)["boolean_operator"].asInt(), 1);
}

TEST_F(ConfigBgpPolicyAsPathListTest, MalformedRegexRejected) {
  clearBgpSession();
  auto result = runCli(
      {"config",
       "protocol",
       "bgp",
       "policy",
       "as-path-list",
       kList,
       "regex",
       "^65000_("});
  EXPECT_THAT(result.stdout, HasSubstr("Malformed regex"));
  EXPECT_FALSE(std::filesystem::exists(bgpSessionPath()))
      << "session file should not exist after rejected input";
}

TEST_F(ConfigBgpPolicyAsPathListTest, DeleteListAndCommit) {
  // Land an as-path-list in the system config, then delete it through a second
  // commit and verify it is gone from bgpd's running config.
  discardSession();
  clearBgpSession();
  stageAsPathList({kList, "description", "delete-me"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "setup commit did not land the as-path-list in bgpd's running config";

  clearBgpSession();
  auto result =
      runCli({"delete", "protocol", "bgp", "policy", "as-path-list", kList});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "deleted as-path-list still present in bgpd's running config";
}
