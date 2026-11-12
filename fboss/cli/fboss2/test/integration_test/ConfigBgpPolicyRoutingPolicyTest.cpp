// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy routing-policy
 * <name> [<attribute> <value> ...]`.
 *
 * Scope: the routing-policy (bgp_policy.BgpPolicyStatement) and its
 * policy-level attributes. Every test stages the change AND commits it, then
 * asserts the value landed at the correct thrift field path inside the
 * matching .policies.bgp_policy_statements[] entry of bgpd's running config
 * (via getRunningConfig RPC) — which also confirms bgpd accepts and adopts a
 * policy statement with no terms. Stage-only behavior (attribute parsing,
 * validation, rejection) is covered by the unit tests; an integration test
 * that never commits exercises no daemon.
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
// Test-only routing-policy name, unlikely to collide with a real policy in
// the device's running BGP config.
const std::string kPolicy = "FBOSS2-TEST-ROUTING-POLICY";
} // namespace

class ConfigBgpPolicyRoutingPolicyTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy routing-policy <tokens...>` WITHOUT
  // clearing the staged session, so attributes can accumulate across
  // invocations. Returns the staged session JSON.
  folly::dynamic stageRoutingPolicy(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config", "protocol", "bgp", "policy", "routing-policy"};
    args.insert(args.end(), tokens.begin(), tokens.end());
    auto result = runCli(args);
    EXPECT_EQ(result.exitCode, 0)
        << "stdout=" << result.stdout << " stderr=" << result.stderr;
    EXPECT_THAT(result.stdout, Not(HasSubstr("Error:")));
    return readBgpSessionConfig();
  }

  // The .policies.bgp_policy_statements[] entry named `name`, or nullptr.
  static const folly::dynamic* findPolicy(
      const folly::dynamic& config,
      const std::string& name) {
    if (config.count("policies") == 0 ||
        config["policies"].count("bgp_policy_statements") == 0) {
      return nullptr;
    }
    for (const auto& policy : config["policies"]["bgp_policy_statements"]) {
      if (policy.count("name") && policy["name"].asString() == name) {
        return &policy;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyRoutingPolicyTest, SetDescriptionAndCommit) {
  discardSession();
  clearBgpSession();
  stageRoutingPolicy({kPolicy, "description", "test", "spine", "export"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted the
  // .policies.bgp_policy_statements blob.
  auto config = readSystemBgpConfig();
  const auto* policy = findPolicy(config, kPolicy);
  ASSERT_NE(policy, nullptr)
      << "committed config has no routing-policy " << kPolicy;
  EXPECT_EQ((*policy)["description"].asString(), "test spine export");

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningPolicy = findPolicy(running, kPolicy);
  ASSERT_NE(runningPolicy, nullptr)
      << "bgpd's running config has no routing-policy " << kPolicy;
  EXPECT_EQ((*runningPolicy)["description"].asString(), "test spine export");
}

TEST_F(ConfigBgpPolicyRoutingPolicyTest, DeletePolicyAndCommit) {
  // Land a routing-policy in the system config, then delete it through a
  // second commit and verify it is gone from bgpd's running config.
  discardSession();
  clearBgpSession();
  stageRoutingPolicy({kPolicy, "description", "delete-me"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  ASSERT_NE(findPolicy(readRunningBgpConfigViaRpc(), kPolicy), nullptr)
      << "setup commit did not land the routing-policy in bgpd's running "
         "config";

  clearBgpSession();
  auto result = runCli(
      {"delete", "protocol", "bgp", "policy", "routing-policy", kPolicy});
  EXPECT_THAT(result.stdout, HasSubstr("Successfully deleted"));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  EXPECT_EQ(findPolicy(readRunningBgpConfigViaRpc(), kPolicy), nullptr)
      << "deleted routing-policy still present in bgpd's running config";
}
