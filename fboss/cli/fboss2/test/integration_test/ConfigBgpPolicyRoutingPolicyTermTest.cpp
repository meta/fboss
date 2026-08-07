// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/**
 * End-to-end tests for `fboss2-dev config protocol bgp policy routing-policy
 * <name> term <seq-num> [<attribute> <value> ...]`.
 *
 * Scope: the term level (bgp_policy.BgpPolicyTerm in
 * .policies.bgp_policy_statements[].policy_entries[]). The term level is a
 * real CLI11 subcommand beneath `routing-policy` — unlike the sibling policy
 * families' token-parsed nesting — so these tests also exercise the
 * subcommand dispatch through the full CLI parse, not just the handler.
 * Every test stages the change AND commits it, then asserts the value landed
 * at the correct thrift field path inside bgpd's running config (via
 * getRunningConfig RPC).
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
// Test-only routing-policy name, unlikely to collide with a real policy in
// the device's running BGP config.
const std::string kPolicy = "FBOSS2-TEST-RP-TERM";
} // namespace

class ConfigBgpPolicyRoutingPolicyTermTest : public ConfigBgpTestBase {
 protected:
  // Stage `config protocol bgp policy routing-policy <kPolicy> term
  // <tokens...>` WITHOUT clearing the staged session, so attributes can
  // accumulate across invocations. Returns the staged session JSON.
  folly::dynamic stageTerm(const std::vector<std::string>& tokens) {
    std::vector<std::string> args = {
        "config",
        "protocol",
        "bgp",
        "policy",
        "routing-policy",
        kPolicy,
        "term"};
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

  // The .policy_entries[] term with sequence_number `seqNum`, or nullptr.
  static const folly::dynamic* findTerm(
      const folly::dynamic& policy,
      int64_t seqNum) {
    if (policy.count("policy_entries") == 0) {
      return nullptr;
    }
    for (const auto& term : policy["policy_entries"]) {
      if (term.count("sequence_number") &&
          term["sequence_number"].asInt() == seqNum) {
        return &term;
      }
    }
    return nullptr;
  }
};

TEST_F(ConfigBgpPolicyRoutingPolicyTermTest, SetTermAttributesAndCommit) {
  discardSession();
  clearBgpSession();
  stageTerm({"10", "description", "test", "term", "ten"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  // Two layers: the promoted file proves what the commit wrote; the daemon's
  // getRunningConfig RPC proves bgpd parsed and adopted the term inside the
  // .policies.bgp_policy_statements[].policy_entries[] blob.
  auto config = readSystemBgpConfig();
  const auto* policy = findPolicy(config, kPolicy);
  ASSERT_NE(policy, nullptr)
      << "committed config has no routing-policy " << kPolicy;
  const auto* term = findTerm(*policy, 10);
  ASSERT_NE(term, nullptr) << "committed config has no term 10";
  EXPECT_EQ((*term)["description"].asString(), "test term ten");

  auto running = readRunningBgpConfigViaRpc();
  const auto* runningPolicy = findPolicy(running, kPolicy);
  ASSERT_NE(runningPolicy, nullptr)
      << "bgpd's running config has no routing-policy " << kPolicy;
  const auto* runningTerm = findTerm(*runningPolicy, 10);
  ASSERT_NE(runningTerm, nullptr) << "bgpd's running config has no term 10";
  EXPECT_EQ((*runningTerm)["description"].asString(), "test term ten");
  EXPECT_EQ((*runningTerm)["sequence_number"].asInt(), 10);
}

TEST_F(ConfigBgpPolicyRoutingPolicyTermTest, DeleteTermAndCommit) {
  // Land a routing-policy with two terms in the system config, then delete
  // one term through a second commit and verify the policy and the other
  // term survive in bgpd's running config.
  discardSession();
  clearBgpSession();
  stageTerm({"10", "description", "delete-me"});
  stageTerm({"20", "description", "keep-me"});
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after commit; state="
      << bgpDaemonActiveState();
  {
    auto running = readRunningBgpConfigViaRpc();
    const auto* policy = findPolicy(running, kPolicy);
    ASSERT_NE(policy, nullptr)
        << "setup commit did not land the routing-policy in bgpd's running "
           "config";
    ASSERT_NE(findTerm(*policy, 10), nullptr);
    ASSERT_NE(findTerm(*policy, 20), nullptr);
  }

  clearBgpSession();
  auto result = runCli(
      {"delete",
       "protocol",
       "bgp",
       "policy",
       "routing-policy",
       kPolicy,
       "term",
       "10"});
  EXPECT_THAT(
      result.stdout,
      HasSubstr(
          fmt::format(
              "Successfully deleted BGP routing-policy {} term 10", kPolicy)));
  commitAndGetSha();
  ASSERT_TRUE(waitForBgpDaemonActive())
      << "bgpd did not return active after delete commit; state="
      << bgpDaemonActiveState();
  auto running = readRunningBgpConfigViaRpc();
  const auto* policy = findPolicy(running, kPolicy);
  ASSERT_NE(policy, nullptr)
      << "deleting one term must not delete the routing-policy";
  EXPECT_EQ(findTerm(*policy, 10), nullptr)
      << "deleted term still present in bgpd's running config";
  EXPECT_NE(findTerm(*policy, 20), nullptr)
      << "surviving term missing from bgpd's running config";
}
