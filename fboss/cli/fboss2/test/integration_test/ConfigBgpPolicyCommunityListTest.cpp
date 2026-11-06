// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy community-list
 * <name> [<attribute> <value> ...]`.
 *
 * Scope: the whole community-list, including its flat `community` values (the
 * communities field bgpd matches on). Every test
 * stages the change AND commits it, then asserts the value landed at the
 * correct thrift field path inside the matching .policies.community_lists[]
 * entry of bgpd's running config (via getRunningConfig RPC) — which also
 * confirms bgpd accepts and adopts a `.policies` blob at all. Stage-only
 * behavior (attribute parsing, validation, rejection) is covered by the unit
 * tests; an integration test that never commits exercises no daemon.
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
// Test-only community-list name, unlikely to collide with a real list in the
// device's running BGP config.
const std::string kList = "FBOSS2-TEST-COMMUNITY";
} // namespace

class ConfigBgpPolicyCommunityListTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy community-list <tokens...>` WITHOUT
  // clearing the staged session, so attributes can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageCommunityList(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config", "protocol", "bgp", "policy", "community-list"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .policies.community_lists[] entry named `name`, or nullptr.
  static const folly::dynamic* findList(
      const folly::dynamic& config,
      const std::string& name) {
    if (config.count("policies") == 0 ||
        config["policies"].count("community_lists") == 0) {
      return nullptr;
    }
    for (const auto& list : config["policies"]["community_lists"]) {
      if (list.count("name") && list["name"].asString() == name) {
        return &list;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyCommunityListTest, SetListAttributesAndCommit) {
  discardSession();
  clearBgpSession();
  stageCommunityList({kList, "description", "test", "spine", "communities"});
  stageCommunityList({kList, "boolean-operator", "AND"});
  stageCommunityList({kList, "exact-match", "true"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted the .policies blob.
  auto config = readSystemBgpConfig();
  const auto* list = findList(config, kList);
  ASSERT_NE(list, nullptr) << "committed config has no community-list "
                           << kList;
  EXPECT_EQ((*list)["description"].asString(), "test spine communities");

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningList = findList(running, kList);
  ASSERT_NE(runningList, nullptr)
      << "bgpd's running config has no community-list " << kList;
  EXPECT_EQ((*runningList)["description"].asString(), "test spine communities");
  // Enums ride the SimpleJSON wire format as integers:
  // routing_policy.BooleanOperator.AND = 1.
  EXPECT_EQ((*runningList)["boolean_operator"].asInt(), 1);
  EXPECT_TRUE((*runningList)["exact_match"].asBool());
}

TEST_F(ConfigBgpPolicyCommunityListTest, SetCommunityAndOperatorAndCommit) {
  discardSession();
  clearBgpSession();
  stageCommunityList({kList, "community", "65000:100"});
  stageCommunityList({kList, "community", "65000:200"});
  stageCommunityList({kList, "boolean-operator", "AND"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // bgpd matches on the flat `communities` list + boolean_operator (never on
  // members[]); the running config proves it parsed and adopted both.
  auto running = readRunningBgpConfigViaRpc();
  const auto* list = findList(running, kList);
  ASSERT_NE(list, nullptr) << "bgpd's running config has no community-list "
                           << kList;
  ASSERT_TRUE(list->count("communities"));
  std::vector<std::string> communities;
  for (const auto& c : (*list)["communities"]) {
    communities.push_back(c.asString());
  }
  EXPECT_EQ(communities, std::vector<std::string>({"65000:100", "65000:200"}));
  ASSERT_TRUE(list->count("boolean_operator"));
  // routing_policy.BooleanOperator.AND == 1
  EXPECT_EQ((*list)["boolean_operator"].asInt(), 1);
}

TEST_F(ConfigBgpPolicyCommunityListTest, MalformedCommunityRejected) {
  clearBgpSession();
  auto result = runCli(
      {"config",
       "protocol",
       "bgp",
       "policy",
       "community-list",
       kList,
       "community",
       "65000:("});
  EXPECT_THAT(result.stdout, HasSubstr("Malformed community"));
  EXPECT_FALSE(std::filesystem::exists(bgpSessionPath()))
      << "session file should not exist after rejected input";
}

TEST_F(ConfigBgpPolicyCommunityListTest, DeleteListAndCommit) {
  // Land a community-list in the system config, then delete it through a
  // second commit and verify it is gone from bgpd's running config.
  discardSession();
  clearBgpSession();
  stageCommunityList({kList, "description", "delete-me"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "setup commit did not land the community-list in bgpd's running "
         "config";

  clearBgpSession();
  auto result =
      runCli({"delete", "protocol", "bgp", "policy", "community-list", kList});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findList(readRunningBgpConfigViaRpc(), kList), nullptr)
      << "deleted community-list still present in bgpd's running config";
}
